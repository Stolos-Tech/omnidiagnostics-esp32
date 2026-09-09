"""Shared SQLite access for the central store. One WAL DB (telemetry.db) holds
telemetry + the relational network/device Library + board registry + health."""
from __future__ import annotations
import os, sqlite3, threading, time, json

HERE = os.path.dirname(os.path.abspath(__file__))
_CFG = json.load(open(os.path.join(HERE, "config.json")))
DB_PATH = _CFG.get("db_path", os.path.join(HERE, "telemetry.db"))
_SCHEMA = os.path.join(HERE, "db_schema.sql")
_lock = threading.Lock()
_inited = False


def connect() -> sqlite3.Connection:
    c = sqlite3.connect(DB_PATH, timeout=10)
    c.execute("PRAGMA foreign_keys=ON")
    c.row_factory = sqlite3.Row
    return c


def init() -> None:
    global _inited
    if _inited:
        return
    with _lock:
        if _inited:
            return
        c = connect()
        try:
            with open(_SCHEMA, encoding="utf-8") as f:
                c.executescript(f.read())
            c.commit()
        finally:
            c.close()
        _inited = True


def now() -> int:
    return int(time.time())


def q(sql: str, args: tuple = ()) -> list[sqlite3.Row]:
    init()
    c = connect()
    try:
        return c.execute(sql, args).fetchall()
    finally:
        c.close()


def one(sql: str, args: tuple = ()):
    r = q(sql, args)
    return r[0] if r else None


def run(sql: str, args: tuple = ()) -> int:
    """Execute a write; returns lastrowid."""
    init()
    with _lock:
        c = connect()
        try:
            cur = c.execute(sql, args)
            c.commit()
            return cur.lastrowid
        finally:
            c.close()


def rows_to_dicts(rows) -> list[dict]:
    return [dict(r) for r in rows]


if __name__ == "__main__":
    init()
    print("db ready:", DB_PATH)
