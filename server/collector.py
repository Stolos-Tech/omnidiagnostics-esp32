"""
collector — фоновий збирач телеметрії плати в SQLite (історія на СЕРВЕРІ, не на платі:
плата розвантажена, не тримає ринг-буфери). Опитує /api/sysinfo + /api/status кожні
N секунд і пише рядок. Дашборд/додаток малюють графіки з цієї БД.

systemd-сервіс espos-collector. Конфіг — server/config.json.
"""
from __future__ import annotations
import json, os, sqlite3, time
import requests
import alerts
import netwatch

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
DB = CFG.get("db_path", os.path.join(HERE, "telemetry.db"))
INTERVAL = int(CFG.get("collector_interval_s", 15))
BOARD = CFG["board_url"]

def db():
    c = sqlite3.connect(DB)
    c.execute("""CREATE TABLE IF NOT EXISTS telemetry(
        ts INTEGER PRIMARY KEY, temp_c REAL, power_ma INTEGER, batt_mv INTEGER,
        rssi INTEGER, heap INTEGER, sta INTEGER, ap INTEGER, uptime_s INTEGER)""")
    return c

_authed = False
def login():
    global _authed
    try:
        r = requests.post(f"{BOARD}/api/login", data=json.dumps({"pin": CFG['board_pin']}),
                          headers={"Content-Type": "text/plain"}, timeout=6)
        _authed = bool(r.json().get("ok"))
    except Exception:
        _authed = False
    return _authed

def sample(conn):
    """Опитує плату, пише рядок у БД. Повертає dict зразка або None (плата недосяжна)."""
    try:
        si = requests.get(f"{BOARD}/api/sysinfo", timeout=6).json()
        st = requests.get(f"{BOARD}/api/status", timeout=6).json()
    except Exception:
        return None
    d = dict(ts=int(time.time()), temp_c=si.get("temp_c"), power_ma=si.get("power", {}).get("ma"),
             batt_mv=st.get("mv"), rssi=st.get("rssi"), heap=si.get("heap"),
             sta=1 if st.get("sta") else 0, ap=1 if st.get("ap") else 0, uptime_s=si.get("uptime_s"))
    conn.execute("INSERT OR REPLACE INTO telemetry VALUES(?,?,?,?,?,?,?,?,?)",
                 (d["ts"], d["temp_c"], d["power_ma"], d["batt_mv"], d["rssi"],
                  d["heap"], d["sta"], d["ap"], d["uptime_s"]))
    conn.commit()
    return d

def netscan():
    """GET /api/netscan (потребує auth) з ре-логіном на 401. Повертає dict або None."""
    try:
        r = requests.get(f"{BOARD}/api/netscan", timeout=20)
        if r.status_code == 401:
            login(); r = requests.get(f"{BOARD}/api/netscan", timeout=20)
        return r.json()
    except Exception:
        return None

def main():
    conn = db()
    login()
    acfg = CFG.get("alerts", {})
    gate = alerts.AlertGate(int(acfg.get("cooldown_s", 1800)))
    on = bool(acfg.get("enabled"))
    offline_after = int(acfg.get("offline_s", 120))
    last_ok = time.time()
    offline = False
    # netwatch: інвентар пристроїв + детект нового MAC
    netwatch.ensure_table(conn)
    baseline = len(netwatch.load_known(conn)) == 0     # перший запуск -> тихий baseline
    net_interval = int(CFG.get("netscan_interval_s", 300))
    last_net = 0.0
    cfg_path = os.path.join(HERE, "config.json")
    print(f"[collector] board={BOARD} db={DB} every {INTERVAL}s alerts={'on' if on else 'off'} netscan={net_interval}s")
    while True:
        # Гаряче перечитування конфіга -> зміни порогів/інтервалів з дашборда застосовуються без рестарту.
        try:
            CFG.update(json.load(open(cfg_path)))
            acfg = CFG.get("alerts", {})
            on = bool(acfg.get("enabled"))
            offline_after = int(acfg.get("offline_s", 120))
            net_interval = int(CFG.get("netscan_interval_s", 300))
        except Exception:
            pass
        row = sample(conn)
        now = time.time()
        if row is not None:
            last_ok = now
            if offline:                                   # відновилось після офлайну
                offline = False; gate.clear("offline")
                if on: alerts.send_telegram(CFG, "✅ Плата знову онлайн")
            if on:
                fired = alerts.evaluate(row, CFG)
                keys = {k for k, _ in fired}
                for k in ("overheat", "batt_low", "rssi_low", "heap_low"):
                    if k not in keys: gate.clear(k)        # стан ок -> дозволити майбутній алерт
                for k, msg in fired:
                    if gate.ready(k): alerts.send_telegram(CFG, msg)
        elif on and not offline and now - last_ok > offline_after:
            offline = True
            if gate.ready("offline"):
                alerts.send_telegram(CFG, f"⚠️ Плата НЕ відповідає >{offline_after}s (можливо перезавантаження/розряд)")

        # Періодичний netscan: інвентар + алерт нового пристрою.
        if net_interval > 0 and now - last_net >= net_interval:
            last_net = now
            js = netscan()
            router_hosts = None
            router_full = None
            if CFG.get("router", {}).get("pass"):      # роутер бачить сплячі IoT, яких ARP плати не ловить
                try:
                    import routerscan
                    router_full = routerscan.scrape()   # повний список зі статусом online/offline
                    rh = [{"ip": d["ip"], "mac": d["mac"], "vendor": ""} for d in router_full if d.get("online")]
                    if rh:
                        router_hosts = rh
                except Exception:
                    router_hosts = None
            # netlog: часова стрічка під'єднань (join/leave) — джерело істини = повний router scrape,
            # інакше board netscan (усе, що бачить ARP, вважаємо online).
            joined_macs = set()   # MAC-и, що ЩОЙНО під'єдналися -> тригер глибокого скану
            try:
                import netlog
                nl_src = router_full if router_full else (
                    [{"mac": h.get("mac", ""), "ip": h.get("ip", ""), "name": h.get("vendor", ""), "online": True}
                     for h in (js.get("hosts", []) if js else [])])
                if nl_src:
                    ev = netlog.record_scan(DB, nl_src)
                    joined_macs = set((m or "").upper() for m in ev.get("join", []))
                    for mac in ev["join"]:
                        alerts.send_telegram(CFG, f"🔌 Під'єднався: {mac}") if acfg.get("alert_join") else None
                    # admission control: свої (trusted) -> approved; нові -> pending + запит підтвердження
                    if CFG.get("netguard", {}).get("enabled"):
                        import netguard
                        netguard.seed_trusted(DB, acfg.get("trusted_macs", []))
                        for mac in netguard.sync_pending(DB, nl_src):
                            alerts.send_telegram(
                                CFG, f"🛂 ЗАПИТ ДОПУСКУ\nНовий пристрій хоче в мережу: {mac}\n"
                                     f"Підтвердь у додатку (ДОПУСК): дозволити чи відхилити.")
            except Exception:
                pass
            if (js and "hosts" in js) or router_hosts:
                hosts = router_hosts if router_hosts else js["hosts"]   # роутер має пріоритет (повніший)
                if acfg.get("alert_untrusted"):        # режим «нова квартира»: усе не-моє = чуже
                    trusted = netwatch.load_trusted(conn, acfg.get("trusted_macs", []))
                    known = netwatch.load_known(conn)
                    foreign = netwatch.foreign_devices(hosts, trusted, skip_random=not acfg.get("alert_random_mac", False))
                    fresh = [h for h in foreign if (h.get("mac", "").upper() not in known)]  # чуже І нове -> алерт раз
                    baseline = False                    # не мовчимо на першому скані
                else:
                    known = netwatch.load_known(conn)
                    fresh = netwatch.new_devices(hosts, known, skip_random=not acfg.get("alert_random_mac", False))
                netwatch.record(conn, hosts)
                try:                              # task 5: mirror into relational Library
                    import profiles
                    profiles.ingest_netscan(hosts)
                except Exception as _e:
                    pass
                # Глибокий скан + алерт для всього, що НЕ в білому списку (trusted).
                # Тригер — подія join (новий АБО відомий, що повернувся онлайн). Білі
                # пристрої: скануємо лише РАЗОВО (для інвентаря), без алертів. Не-білі:
                # скан щоразу + алерт (з тротлом AlertGate, щоб флап не спамив).
                if not baseline:
                    trusted = {m.upper() for m in netwatch.load_trusted(conn, acfg.get("trusted_macs", []))}
                    has_intel = {(r[0] or "").upper() for r in
                                 conn.execute("SELECT mac FROM devices WHERE intel_at IS NOT NULL AND intel_at>0")}
                    fresh_macs = {(h.get("mac", "") or "").upper() for h in fresh}
                    hostmap = {(h.get("mac", "") or "").upper(): h for h in hosts}
                    for mac in (joined_macs | fresh_macs):
                        h = hostmap.get(mac)
                        if not h or not h.get("ip"):
                            continue
                        untrusted = mac not in trusted
                        if not (untrusted or mac not in has_intel):
                            continue                              # білий і вже з intel -> не чіпаємо (нуль холостого)
                        try:
                            import devintel
                            info = devintel.intel(h.get("ip", ""), h.get("mac", ""), h.get("vendor", ""))
                            netwatch.store_intel(conn, h.get("mac", ""), info)
                            line = devintel.summary_line(info)
                        except Exception as e:
                            line = f"{h.get('vendor','?')} (розвідка не вдалась: {e})"
                        if on and untrusted and gate.ready(f"untrusted:{mac}"):
                            tag = "🆕 новий" if mac in fresh_macs else "↩️ повернувся"
                            alerts.send_telegram(
                                CFG, f"⚠️ Пристрій НЕ в білому списку ({tag})\n"
                                     f"IP: {h.get('ip','?')}  ·  MAC: {h['mac']}\n🔎 {line}")
                baseline = False
        time.sleep(INTERVAL)

if __name__ == "__main__":
    main()
