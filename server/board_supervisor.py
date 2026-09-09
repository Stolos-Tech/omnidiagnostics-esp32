"""Board watchdog daemon (task 1). Periodically probes every registered board;
if a USB board stops responding it escalates a force-reboot (soft->dtr->esptool)
and logs the event. Runs as systemd service espos-supervisor."""
from __future__ import annotations
import time, json, os
import db, usbctl, boardproxy

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
POLL = int(CFG.get("supervisor_poll_s", 20))
MISS_LIMIT = int(CFG.get("supervisor_miss_limit", 3))
_miss: dict[int, int] = {}

try:
    import alerts
except Exception:
    alerts = None


def probe(board) -> bool:
    if board["transport"] == "usb" and board["serial_port"]:
        # prefer proxy ping (keeps the persistent link warm), else raw serial ping
        link = boardproxy.link_for(board["id"])
        if link:
            r = link.rpc("GET", "/api/ping", timeout=3)
            if r.get("_status") == 200 or r.get("json"):
                return True
        return usbctl.ping_board(board["serial_port"], board["baud"] or 115200)
    # wifi board
    if board["url"]:
        try:
            import requests
            return requests.get(board["url"] + "/api/ping", timeout=4).ok
        except Exception:
            return False
    return False


def handle_frozen(board):
    bid = board["id"]
    db.run("UPDATE boards SET state='frozen' WHERE id=?", (bid,))
    if board["transport"] == "usb" and board["serial_port"]:
        db.run("UPDATE boards SET state='rebooting' WHERE id=?", (bid,))
        res = usbctl.force_reboot(board["serial_port"], board["baud"] or 115200, board["kind"] or "esp32")
        msg = f"♻️ Board '{board['name']}' unresponsive → force-reboot {res}"
        if alerts:
            try:
                alerts.send_telegram(CFG, msg)
            except Exception:
                pass
        db.run("UPDATE boards SET state=? WHERE id=?", ("online" if res["ok"] else "offline", bid))
        _miss[bid] = 0
    else:
        db.run("UPDATE boards SET state='offline' WHERE id=?", (bid,))


def loop():
    db.init()
    boardproxy.discover_and_register()
    print(f"[supervisor] poll={POLL}s miss_limit={MISS_LIMIT}")
    while True:
        for board in db.rows_to_dicts(db.q("SELECT * FROM boards")):
            bid = board["id"]
            ok = probe(board)
            if ok:
                _miss[bid] = 0
                db.run("UPDATE boards SET last_seen=?, state='online' WHERE id=? AND state!='rebooting'",
                       (db.now(), bid))
            else:
                _miss[bid] = _miss.get(bid, 0) + 1
                if _miss[bid] >= MISS_LIMIT:
                    handle_frozen(board)
        time.sleep(POLL)


if __name__ == "__main__":
    loop()
