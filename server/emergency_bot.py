"""Emergency Telegram control bot (task 2). Backup control interface when the app
loses connection or the main API fails. Runs on the server (espos-bot), talks to
boards over USB/serial and manages server services directly — independent of the
dashboard/gateway Flask app so it survives their failure.

Auth: only chat ids in config.alerts.telegram_chat / admin_chats. Commands:
  /status  /boards  /health  /reboot_board <id>  /reboot_server
  /services  /svc <name> <start|stop|restart>  /kick <device_id>  /help
Requires python-telegram-bot (async)."""
from __future__ import annotations
import os, json, asyncio, subprocess

import db, usbctl, boardproxy

try:
    import healthmon
except Exception:
    healthmon = None
try:
    from telegram import Update
    from telegram.ext import Application, CommandHandler, ContextTypes
except Exception:
    Application = None

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
A = CFG.get("alerts", {})
TOKEN = A.get("telegram_token", "")
ADMINS = {str(x) for x in ([A.get("telegram_chat")] + CFG.get("admin_chats", [])) if x}
SERVICES = set(CFG.get("manageable_services",
                       ["espos-gateway", "espos-collector", "espos-dashboard",
                        "espos-supervisor", "espos-healthmon", "espos-bot"]))


def _ok(update) -> bool:
    return not ADMINS or str(update.effective_chat.id) in ADMINS


async def cmd_help(u, c):
    if not _ok(u):
        return
    await u.message.reply_text(
        "ESP32-OS emergency control\n"
        "/status /boards /health\n"
        "/reboot_board <id>\n/reboot_server\n"
        "/services /svc <name> <start|stop|restart>\n/kick <device_id>")


async def cmd_status(u, c):
    if not _ok(u):
        return
    boards = db.rows_to_dicts(db.q("SELECT id,name,transport,state,last_seen FROM boards"))
    lines = ["📟 Boards:"] + [f"  #{b['id']} {b['name']} [{b['transport']}] {b['state']}" for b in boards]
    await u.message.reply_text("\n".join(lines) or "no boards")


async def cmd_boards(u, c):
    if not _ok(u):
        return
    ser = usbctl.list_serial()
    await u.message.reply_text("USB serial:\n" + "\n".join(
        f"  {s['port']} {s['kind']} ({s['desc']})" for s in ser) or "none")


async def cmd_health(u, c):
    if not _ok(u):
        return
    if not healthmon:
        await u.message.reply_text("healthmon unavailable")
        return
    s = healthmon.stats()
    txt = (f"🖥 Server\ntemp {s.get('cpu_temp_c')}C  load {s.get('load_pct')}%  "
           f"ram {s.get('mem_used_pct')}%  disk {s.get('disk_used_pct')}%\n"
           f"undervolt: {'YES' if s.get('undervolt') else 'no'}\n"
           f"services: {s.get('services')}")
    sd = s.get("shutdowns", [])
    if sd:
        txt += f"\nlast shutdown: {'clean' if sd[0]['clean'] else 'SUDDEN'} · {sd[0]['suspected']}"
    await u.message.reply_text(txt)


async def cmd_reboot_board(u, c):
    if not _ok(u):
        return
    try:
        bid = int(c.args[0])
    except Exception:
        await u.message.reply_text("usage: /reboot_board <id>")
        return
    b = db.one("SELECT * FROM boards WHERE id=?", (bid,))
    if not b:
        await u.message.reply_text("no such board")
        return
    if b["transport"] == "usb":
        res = await asyncio.to_thread(usbctl.force_reboot, b["serial_port"], b["baud"] or 115200, b["kind"])
        await u.message.reply_text(f"reboot #{bid}: {res}")
    else:
        r = await asyncio.to_thread(boardproxy.proxy_request, bid, "POST", "/api/cmd", {"reboot": True})
        await u.message.reply_text(f"wifi reboot sent: {r}")


async def cmd_reboot_server(u, c):
    if not _ok(u):
        return
    await u.message.reply_text("♻️ rebooting server in 3s…")
    await asyncio.sleep(3)
    await asyncio.to_thread(subprocess.Popen, ["systemctl", "reboot"])


async def cmd_services(u, c):
    if not _ok(u):
        return
    out = []
    for s in SERVICES:
        r = await asyncio.to_thread(subprocess.run, ["systemctl", "is-active", s],
                                    capture_output=True, text=True)
        out.append(f"  {s}: {r.stdout.strip()}")
    await u.message.reply_text("Services:\n" + "\n".join(out))


async def cmd_svc(u, c):
    if not _ok(u):
        return
    if len(c.args) < 2 or c.args[0] not in SERVICES or c.args[1] not in ("start", "stop", "restart"):
        await u.message.reply_text("usage: /svc <name> <start|stop|restart>")
        return
    r = await asyncio.to_thread(subprocess.run, ["systemctl", c.args[1], c.args[0]],
                                capture_output=True, text=True)
    await u.message.reply_text(f"{c.args[1]} {c.args[0]}: {'ok' if r.returncode == 0 else r.stderr}")


async def cmd_kick(u, c):
    if not _ok(u):
        return
    try:
        did = int(c.args[0])
    except Exception:
        await u.message.reply_text("usage: /kick <device_id>")
        return
    dev = db.one("SELECT mac,ip FROM device_profiles WHERE id=?", (did,))
    if not dev:
        await u.message.reply_text("no device")
        return
    board = db.one("SELECT id FROM boards WHERE state='online' ORDER BY id LIMIT 1")
    if not board:
        await u.message.reply_text("no online board to issue kick")
        return
    r = await asyncio.to_thread(boardproxy.proxy_request, board["id"], "POST",
                                "/api/kick", {"mac": dev["mac"]})
    await u.message.reply_text(f"kick {dev['mac']}: {r}")


def main():
    if not Application or not TOKEN:
        print("[bot] telegram lib or token missing — bot disabled")
        return
    db.init()
    app = Application.builder().token(TOKEN).build()
    app.add_handler(CommandHandler(["start", "help"], cmd_help))
    app.add_handler(CommandHandler("status", cmd_status))
    app.add_handler(CommandHandler("boards", cmd_boards))
    app.add_handler(CommandHandler("health", cmd_health))
    app.add_handler(CommandHandler("reboot_board", cmd_reboot_board))
    app.add_handler(CommandHandler("reboot_server", cmd_reboot_server))
    app.add_handler(CommandHandler("services", cmd_services))
    app.add_handler(CommandHandler("svc", cmd_svc))
    app.add_handler(CommandHandler("kick", cmd_kick))
    print("[bot] emergency control bot up")
    app.run_polling(drop_pending_updates=True)


if __name__ == "__main__":
    main()
