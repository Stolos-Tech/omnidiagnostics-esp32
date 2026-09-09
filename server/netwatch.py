"""
netwatch — інвентар пристроїв мережі + детект НОВОГО MAC (кіберфортеця). Сервер періодично
смикає /api/netscan плати, веде таблицю devices у SQLite і шле Telegram-алерт на новий пристрій.
Перший скан = baseline (мовчки записуємо все, без спаму). Чиста логіка (new_devices, is_local_mac)
тестується локально.
"""
from __future__ import annotations
import time, json


def is_local_mac(mac: str) -> bool:
    """Локально-адміністрований (рандомізований) MAC — біт 1 у першому байті (02/06/0A/0E…)."""
    try:
        return bool(int(mac[:2], 16) & 0b10)
    except Exception:
        return False


def new_devices(hosts: list[dict], known: set[str], *, skip_random: bool = True) -> list[dict]:
    """Хости, чий MAC відсутній у known. Опційно ігнорує рандомні (приватні) MAC телефонів."""
    out = []
    for h in hosts:
        mac = (h.get("mac") or "").upper()
        if not mac or mac in known:
            continue
        if skip_random and is_local_mac(mac):
            continue
        out.append(h)
    return out


def foreign_devices(hosts: list[dict], trusted: set[str], *, skip_random: bool = False) -> list[dict]:
    """Сценарій «нова квартира»: алерт на БУДЬ-ЯКИЙ пристрій, MAC якого НЕ в білому
    списку (trusted = мої пристрої). На відміну від new_devices (лише невідомі проти
    baseline), тут baseline не «приймається тихо» — усе не-моє = чуже. skip_random
    ігнорує рандомні MAC (щоб власний телефон з рандомізацією не смітив)."""
    out = []
    for h in hosts:
        mac = (h.get("mac") or "").upper()
        if not mac or mac in trusted:
            continue
        if skip_random and is_local_mac(mac):
            continue
        out.append(h)
    return out


def load_trusted(conn, extra: list[str] | None = None) -> set[str]:
    """Білий список: пристрої з trust='trusted' у device_profiles + явні MAC з конфіга."""
    s = {m.upper() for m in (extra or [])}
    try:
        for r in conn.execute("SELECT mac FROM device_profiles WHERE trust='trusted'"):
            s.add((r[0] or "").upper())
    except Exception:
        pass
    return s


def ensure_table(conn) -> None:
    conn.execute("""CREATE TABLE IF NOT EXISTS devices(
        mac TEXT PRIMARY KEY, ip TEXT, vendor TEXT,
        first_seen INTEGER, last_seen INTEGER, is_gw INTEGER DEFAULT 0)""")
    # ідемпотентні міграції під розвідку (devintel)
    for col, ddl in (("name", "TEXT"), ("hostname", "TEXT"), ("category", "TEXT"),
                     ("ports", "TEXT"), ("intel", "TEXT"), ("intel_at", "INTEGER")):
        try:
            conn.execute(f"ALTER TABLE devices ADD COLUMN {col} {ddl}")
        except Exception:
            pass
    conn.commit()


def store_intel(conn, mac: str, info: dict) -> None:
    """Зберегти результат розвідки devintel.intel() у рядок пристрою."""
    mac = (mac or "").upper()
    if not mac:
        return
    conn.execute(
        "UPDATE devices SET hostname=?, category=?, ports=?, intel=?, intel_at=?, "
        "vendor=CASE WHEN (vendor IS NULL OR vendor='' OR vendor='?') AND ?<>'' THEN ? ELSE vendor END "
        "WHERE mac=?",
        (info.get("hostname", ""), info.get("category", ""),
         ",".join(str(p) for p in info.get("ports", [])),
         json.dumps(info, ensure_ascii=False), int(info.get("at", time.time())),
         info.get("vendor", ""), info.get("vendor", ""), mac))
    conn.commit()


def get_intel(conn, mac: str) -> dict | None:
    r = conn.execute("SELECT intel FROM devices WHERE mac=?", ((mac or "").upper(),)).fetchone()
    if r and r[0]:
        try:
            return json.loads(r[0])
        except Exception:
            return None
    return None


def load_known(conn) -> set[str]:
    return {r[0] for r in conn.execute("SELECT mac FROM devices")}


def record(conn, hosts: list[dict], now: int | None = None) -> None:
    """Upsert: новим — first_seen, всім — last_seen/ip/vendor."""
    now = int(time.time()) if now is None else now
    for h in hosts:
        mac = (h.get("mac") or "").upper()
        if not mac:
            continue
        conn.execute("""INSERT INTO devices(mac, ip, vendor, first_seen, last_seen, is_gw)
            VALUES(?,?,?,?,?,?)
            ON CONFLICT(mac) DO UPDATE SET ip=excluded.ip, vendor=excluded.vendor, last_seen=excluded.last_seen""",
            (mac, h.get("ip", ""), h.get("vendor", "?"), now, now, 1 if h.get("gw") else 0))
    conn.commit()
