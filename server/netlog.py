"""netlog — часовий лог подій домашньої мережі (хто/коли зайшов/вийшов).

Роутер HG8245W5 не веде історію; ми будуємо її самі: колектор кожен цикл дає поточний
список пристроїв (routerscan.scrape), а netlog діфає його з попереднім станом і пише
події join/leave з міткою часу у SQLite. Так з'являється справжня стрічка під'єднань.

Таблиці:
  net_events(ts, mac, ip, name, event)   -- подія join|leave
  net_state(mac, ip, name, online, first_seen, last_seen, last_change)  -- поточний стан
Без зовнішніх залежностей (лише stdlib sqlite3). Усе best-effort, помилки не валять колектор.
"""
from __future__ import annotations
import sqlite3, time, os

_DDL = """
CREATE TABLE IF NOT EXISTS net_events(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL, mac TEXT NOT NULL, ip TEXT, name TEXT,
  event TEXT NOT NULL CHECK(event IN ('join','leave')));
CREATE INDEX IF NOT EXISTS ix_events_ts ON net_events(ts);
CREATE INDEX IF NOT EXISTS ix_events_mac ON net_events(mac);
CREATE TABLE IF NOT EXISTS net_state(
  mac TEXT PRIMARY KEY, ip TEXT, name TEXT, online INTEGER,
  first_seen INTEGER, last_seen INTEGER, last_change INTEGER);
"""


def _conn(db_path: str) -> sqlite3.Connection:
    c = sqlite3.connect(db_path, timeout=5)
    c.execute("PRAGMA journal_mode=WAL")
    c.executescript(_DDL)
    return c


def record_scan(db_path: str, devices: list[dict]) -> dict:
    """Дифнути поточний скан [{mac,ip,name,online}] з попереднім станом -> події join/leave.
    Повертає {'join':[macs], 'leave':[macs]}. Ідемпотентно щодо незмінних станів."""
    now = int(time.time())
    # str() -> захист від non-str mac у пошкодженому кадрі (щоб не завалити колектор)
    cur = {str(d["mac"]).strip().upper(): d
           for d in devices if isinstance(d, dict) and d.get("mac")}
    joined, left = [], []
    c = _conn(db_path)
    try:
        prev = {r[0]: r[1] for r in c.execute("SELECT mac, online FROM net_state")}
        # нові/оновлені онлайн
        for mac, d in cur.items():
            online = 1 if d.get("online") else 0
            was = prev.get(mac)
            if was is None:
                # перша поява взагалі
                c.execute("INSERT INTO net_state(mac,ip,name,online,first_seen,last_seen,last_change)"
                          " VALUES(?,?,?,?,?,?,?)",
                          (mac, d.get("ip", ""), d.get("name", ""), online, now, now, now))
                if online: joined.append(mac); c.execute(
                    "INSERT INTO net_events(ts,mac,ip,name,event) VALUES(?,?,?,?,'join')",
                    (now, mac, d.get("ip", ""), d.get("name", "")))
            else:
                if online and was == 0:
                    joined.append(mac)
                    c.execute("INSERT INTO net_events(ts,mac,ip,name,event) VALUES(?,?,?,?,'join')",
                              (now, mac, d.get("ip", ""), d.get("name", "")))
                    c.execute("UPDATE net_state SET last_change=? WHERE mac=?", (now, mac))
                c.execute("UPDATE net_state SET ip=?,name=?,online=?,last_seen=? WHERE mac=?",
                          (d.get("ip", ""), d.get("name", ""), online, now, mac))
        # ті, кого нема в скані АЛЕ були online -> leave
        for mac, was in prev.items():
            if mac not in cur and was == 1:
                left.append(mac)
                row = c.execute("SELECT ip,name FROM net_state WHERE mac=?", (mac,)).fetchone()
                ip, name = (row or ("", ""))
                c.execute("INSERT INTO net_events(ts,mac,ip,name,event) VALUES(?,?,?,?,'leave')",
                          (now, mac, ip, name))
                c.execute("UPDATE net_state SET online=0,last_change=? WHERE mac=?", (now, mac))
            elif mac in cur:
                d = cur[mac]
                if not d.get("online") and was == 1:
                    left.append(mac)
                    c.execute("INSERT INTO net_events(ts,mac,ip,name,event) VALUES(?,?,?,?,'leave')",
                              (now, mac, d.get("ip", ""), d.get("name", "")))
                    c.execute("UPDATE net_state SET last_change=? WHERE mac=?", (now, mac))
        c.commit()
    finally:
        c.close()
    return {"join": joined, "leave": left}


def events(db_path: str, since: int = 0, limit: int = 2000) -> list[dict]:
    # since=0 -> уся стрічка (newest-first для показу). since>0 -> лише НОВІ події (ts>since,
    # ексклюзивно) для інкрементальної синхронізації без дублювання межової події.
    op = ">" if since > 0 else ">="
    c = _conn(db_path)
    try:
        rows = c.execute(f"SELECT ts,mac,ip,name,event FROM net_events WHERE ts{op}? "
                         "ORDER BY ts DESC LIMIT ?", (since, limit)).fetchall()
    finally:
        c.close()
    return [{"ts": r[0], "mac": r[1], "ip": r[2], "name": r[3], "event": r[4]} for r in rows]


def state(db_path: str) -> list[dict]:
    c = _conn(db_path)
    try:
        rows = c.execute("SELECT mac,ip,name,online,first_seen,last_seen,last_change "
                         "FROM net_state ORDER BY online DESC, last_change DESC").fetchall()
    finally:
        c.close()
    return [{"mac": r[0], "ip": r[1], "name": r[2], "online": bool(r[3]),
             "first_seen": r[4], "last_seen": r[5], "last_change": r[6]} for r in rows]


def rebuild_state(db_path: str) -> dict:
    """RECOVERABILITY: перебудувати read-модель net_state ЛИШЕ з довіреного журналу подій
    net_events (реплей join/leave у хронопорядку). Застосувати, якщо net_state пошкоджено
    чи скомпрометовано — відновлення у відомий добрий стан із джерела істини.
    Кожен net_event = перехід стану (netlog пише подію лише на зміні), тож реплей точний."""
    c = _conn(db_path)
    try:
        evs = c.execute("SELECT ts,mac,ip,name,event FROM net_events ORDER BY ts ASC, id ASC").fetchall()
        agg: dict[str, dict] = {}
        for ts, mac, ip, name, event in evs:
            a = agg.get(mac)
            if a is None:
                a = {"ip": "", "name": "", "online": 0,
                     "first_seen": ts, "last_seen": ts, "last_change": ts}
                agg[mac] = a
            a["first_seen"] = min(a["first_seen"], ts)
            a["last_seen"] = max(a["last_seen"], ts)
            a["last_change"] = ts
            if ip: a["ip"] = ip
            if name: a["name"] = name
            a["online"] = 1 if event == "join" else 0
        c.execute("BEGIN")
        c.execute("DELETE FROM net_state")
        for mac, a in agg.items():
            c.execute("INSERT INTO net_state(mac,ip,name,online,first_seen,last_seen,last_change)"
                      " VALUES(?,?,?,?,?,?,?)",
                      (mac, a["ip"], a["name"], a["online"], a["first_seen"], a["last_seen"], a["last_change"]))
        c.commit()
    finally:
        c.close()
    return {"macs": len(agg), "online": sum(1 for a in agg.values() if a["online"])}


def purge(db_path: str, older_than_days: int) -> int:
    if older_than_days <= 0:
        return 0
    cutoff = int(time.time()) - older_than_days * 86400
    c = _conn(db_path)
    try:
        n = c.execute("DELETE FROM net_events WHERE ts<?", (cutoff,)).rowcount
        c.commit()
    finally:
        c.close()
    return n


if __name__ == "__main__":
    import sys
    db = sys.argv[1] if len(sys.argv) > 1 else "netlog_test.db"
    for e in events(db):
        print(time.strftime("%m-%d %H:%M", time.localtime(e["ts"])), e["event"].ljust(5),
              e["mac"], e["ip"], e["name"])
