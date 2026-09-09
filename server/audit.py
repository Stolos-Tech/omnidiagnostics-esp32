"""
audit — tamper-evident журнал безпекових подій (hash-chain). Append-only сам по собі
НЕ є доказовим: процес із доступом до БД може переписати рядок. Тому кожен запис
зчеплений хешем із попереднім (hash = sha256(prev_hash|ts|kind|data)). Будь-яка
підміна/видалення/переставлення старого запису рве ланцюг -> verify() це ловить.

Окрема низькооб'ємна таблиця (audit_chain), НЕ net_events: сюди йдуть лише безпекові
події (honeytoken, netguard-допуск, auth-lockout, OTA-публікація, новий пристрій),
а append серіалізується write-локом -> коректний ланцюг попри кілька процесів.

Golden thread для розслідування інцидентів. Без зовнішніх залежностей (stdlib).
Пов'язано: honeytokens, netguard, netlog (recoverability read-моделі там).
"""
from __future__ import annotations
import sqlite3, time, json, hashlib

GENESIS = "0" * 64

_DDL = """
CREATE TABLE IF NOT EXISTS audit_chain(
  seq       INTEGER PRIMARY KEY AUTOINCREMENT,
  ts        INTEGER NOT NULL,
  kind      TEXT NOT NULL,
  data      TEXT NOT NULL,       -- канонічний JSON (sort_keys, компактний)
  prev_hash TEXT NOT NULL,
  hash      TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_audit_ts ON audit_chain(ts);
CREATE INDEX IF NOT EXISTS ix_audit_kind ON audit_chain(kind);
"""


def _conn(db_path: str) -> sqlite3.Connection:
    c = sqlite3.connect(db_path, timeout=10)
    c.execute("PRAGMA journal_mode=WAL")
    c.executescript(_DDL)
    return c


def _canon(data) -> str:
    """Детермінований JSON: однаковий вхід -> однаковий рядок (щоб хеш відтворювався)."""
    return json.dumps(data, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, default=str)


def _digest(prev_hash: str, ts: int, kind: str, data_json: str) -> str:
    h = hashlib.sha256()
    h.update(f"{prev_hash}|{ts}|{kind}|{data_json}".encode("utf-8"))
    return h.hexdigest()


def append(db_path: str, kind: str, data: dict | None = None, ts: int | None = None) -> dict:
    """Дописати зчеплену подію. Серіалізовано write-локом (BEGIN IMMEDIATE), тож
    prev_hash завжди читається консистентно навіть із кількох процесів."""
    ts = int(time.time()) if ts is None else int(ts)
    dj = _canon(data if data is not None else {})
    c = _conn(db_path)
    try:
        c.isolation_level = None            # ручний контроль транзакції
        c.execute("BEGIN IMMEDIATE")        # write-лок -> нема гонки за хвіст ланцюга
        row = c.execute("SELECT hash FROM audit_chain ORDER BY seq DESC LIMIT 1").fetchone()
        prev = row[0] if row else GENESIS
        hh = _digest(prev, ts, str(kind), dj)
        cur = c.execute("INSERT INTO audit_chain(ts,kind,data,prev_hash,hash) VALUES(?,?,?,?,?)",
                        (ts, str(kind), dj, prev, hh))
        seq = cur.lastrowid
        c.execute("COMMIT")
    except Exception:
        try: c.execute("ROLLBACK")
        except Exception: pass
        raise
    finally:
        c.close()
    return {"seq": seq, "ts": ts, "kind": str(kind), "prev_hash": prev, "hash": hh}


def verify(db_path: str) -> dict:
    """Пройти ланцюг: перерахувати хеші й звірити зчеплення. -> {ok, count, broken_at?, reason?}."""
    c = _conn(db_path)
    try:
        rows = c.execute("SELECT seq,ts,kind,data,prev_hash,hash FROM audit_chain ORDER BY seq ASC").fetchall()
    finally:
        c.close()
    prev = GENESIS
    for i, (seq, ts, kind, data, prev_hash, hh) in enumerate(rows):
        if prev_hash != prev:
            return {"ok": False, "count": len(rows), "broken_at": seq, "reason": "prev_hash mismatch"}
        if _digest(prev_hash, ts, kind, data) != hh:
            return {"ok": False, "count": len(rows), "broken_at": seq, "reason": "hash mismatch"}
        prev = hh
    return {"ok": True, "count": len(rows), "head": prev}


def tail(db_path: str, limit: int = 200, kind: str | None = None) -> list[dict]:
    c = _conn(db_path)
    try:
        if kind:
            rows = c.execute("SELECT seq,ts,kind,data,hash FROM audit_chain WHERE kind=? "
                             "ORDER BY seq DESC LIMIT ?", (kind, limit)).fetchall()
        else:
            rows = c.execute("SELECT seq,ts,kind,data,hash FROM audit_chain "
                             "ORDER BY seq DESC LIMIT ?", (limit,)).fetchall()
    finally:
        c.close()
    out = []
    for seq, ts, kind, data, hh in rows:
        try: d = json.loads(data)
        except Exception: d = {"_raw": data}
        out.append({"seq": seq, "ts": ts, "kind": kind, "data": d, "hash": hh})
    return out


if __name__ == "__main__":
    import sys
    db = sys.argv[1] if len(sys.argv) > 1 else "audit_test.db"
    print(verify(db))
    for e in tail(db, 50):
        print(time.strftime("%m-%d %H:%M", time.localtime(e["ts"])), e["kind"], e["data"])
