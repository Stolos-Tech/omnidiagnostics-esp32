"""netguard — активний дозвіл на під'єднання (admission control). Пароль WiFi — НЕ
достатньо: кожен НОВИЙ пристрій отримує статус PENDING і чекає ЯВНОГО схвалення юзера
(друге підтвердження) у додатку/Telegram. Схвалені -> allow, відхилені -> deny.

Механізм детекту: netlog.state дає всіх, хто мав lease. Кого нема в таблиці admission —
той PENDING. Юзер approve/deny у додатку -> статус фіксується.

ПРИМУСОВЕ блокування на роутері (MAC-filter HG8245W5) — окремий актуатор enforce_router().
За замовчуванням ВИМКНЕНО (netguard.enforce=false у config): спершу тестуємо WRITE наживо
разом (ризик залочити пристрої/себе). Доти netguard = детект + черга + сповіщення.

Таблиця admission(mac, status, name, ts, note). Без зовн. залежностей (sqlite3).
"""
from __future__ import annotations
import sqlite3, time

_DDL = """
CREATE TABLE IF NOT EXISTS admission(
  mac TEXT PRIMARY KEY,
  status TEXT NOT NULL CHECK(status IN ('pending','approved','denied')),
  name TEXT, ts INTEGER, note TEXT);
"""


def _conn(db_path: str) -> sqlite3.Connection:
    c = sqlite3.connect(db_path, timeout=5)
    c.execute("PRAGMA journal_mode=WAL")
    c.executescript(_DDL)
    return c


def seed_trusted(db_path: str, macs: list[str]):
    """Позначити відомі MAC як approved (щоб не спамити PENDING на своїх). Ідемпотентно."""
    now = int(time.time())
    c = _conn(db_path)
    try:
        for m in macs:
            m = m.upper()
            c.execute("INSERT INTO admission(mac,status,name,ts,note) VALUES(?,'approved','',?,'trusted')"
                      " ON CONFLICT(mac) DO NOTHING", (m, now))
        c.commit()
    finally:
        c.close()


def sync_pending(db_path: str, devices: list[dict]) -> list[str]:
    """Кого з поточних пристроїв ще нема в admission -> додати як pending. Повертає нові pending."""
    now = int(time.time())
    new = []
    c = _conn(db_path)
    try:
        have = {r[0] for r in c.execute("SELECT mac FROM admission")}
        for d in devices:
            mac = (d.get("mac") or "").upper()
            if not mac or mac in have:
                continue
            c.execute("INSERT INTO admission(mac,status,name,ts,note) VALUES(?,'pending',?,?,'')",
                      (mac, d.get("name", ""), now))
            new.append(mac)
        c.commit()
    finally:
        c.close()
    return new


def set_status(db_path: str, mac: str, status: str, note: str = "") -> bool:
    if status not in ("pending", "approved", "denied"):
        return False
    mac = mac.upper()
    now = int(time.time())
    c = _conn(db_path)
    try:
        c.execute("INSERT INTO admission(mac,status,name,ts,note) VALUES(?,?,?,?,?)"
                  " ON CONFLICT(mac) DO UPDATE SET status=excluded.status, ts=excluded.ts, note=excluded.note",
                  (mac, status, "", now, note))
        c.commit()
    finally:
        c.close()
    return True


def listing(db_path: str, status: str | None = None) -> list[dict]:
    c = _conn(db_path)
    try:
        if status:
            rows = c.execute("SELECT mac,status,name,ts,note FROM admission WHERE status=? ORDER BY ts DESC",
                             (status,)).fetchall()
        else:
            rows = c.execute("SELECT mac,status,name,ts,note FROM admission ORDER BY ts DESC").fetchall()
    finally:
        c.close()
    return [{"mac": r[0], "status": r[1], "name": r[2], "ts": r[3], "note": r[4]} for r in rows]


# ── Актуатор на роутері (HG8245W5 WLAN MAC-filter). ГАРД: лише коли enforce=true. ──
# УВАГА: WRITE у роутер НЕ протестовано наживо — вмикати лише разом із юзером (ризик
# залочити доступ). Наразі повертає (False, 'disabled') доки enforce не увімкнено.
def enforce_router(db_path: str, cfg: dict) -> tuple[bool, str]:
    if not cfg.get("netguard", {}).get("enforce"):
        return (False, "enforce disabled (safe mode: detect+queue only)")
    # TODO(live-test): реалізувати SetWlanMacFilter на HG8245W5 (allow=approved, deny=denied).
    # Флоу: login (routerscan._login) -> GET wlanmacfilter page (розібрати поточні правила) ->
    # POST set.cgi з оновленим списком. Тестувати ПОКРОКОВО з юзером біля роутера.
    return (False, "router actuator not yet enabled (needs live test)")
