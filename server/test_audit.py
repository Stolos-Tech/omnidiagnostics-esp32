"""Локальні тести audit (hash-chain) + netlog.rebuild_state (recoverability).
Запуск: python test_audit.py"""
import os, sqlite3, tempfile, time
import audit, netlog


def _tmpdb():
    fd, p = tempfile.mkstemp(suffix=".db"); os.close(fd); os.remove(p)
    return p


def _rm(p):
    for ext in ("", "-wal", "-shm"):
        try: os.remove(p + ext)
        except OSError: pass


# ── audit hash-chain ────────────────────────────────────────────────────────
def test_empty_chain_ok():
    db = _tmpdb()
    try:
        assert audit.verify(db) == {"ok": True, "count": 0, "head": audit.GENESIS}
    finally: _rm(db)


def test_append_links_and_verifies():
    db = _tmpdb()
    try:
        a = audit.append(db, "honeytoken", {"ip": "10.0.0.9", "path": "/api/backup"})
        b = audit.append(db, "auth_lockout", {"ip": "10.0.0.9"})
        assert a["prev_hash"] == audit.GENESIS
        assert b["prev_hash"] == a["hash"]           # зчеплення
        v = audit.verify(db)
        assert v["ok"] is True and v["count"] == 2 and v["head"] == b["hash"]
    finally: _rm(db)


def test_tamper_data_detected():
    db = _tmpdb()
    try:
        audit.append(db, "k", {"x": 1}); audit.append(db, "k", {"x": 2}); audit.append(db, "k", {"x": 3})
        c = sqlite3.connect(db); c.execute("UPDATE audit_chain SET data=? WHERE seq=2", ('{"x":999}',)); c.commit(); c.close()
        v = audit.verify(db)
        assert v["ok"] is False and v["broken_at"] == 2 and v["reason"] == "hash mismatch"
    finally: _rm(db)


def test_tamper_prevhash_detected():
    db = _tmpdb()
    try:
        audit.append(db, "k", {"x": 1}); audit.append(db, "k", {"x": 2})
        c = sqlite3.connect(db); c.execute("UPDATE audit_chain SET prev_hash=? WHERE seq=2", ("f" * 64,)); c.commit(); c.close()
        v = audit.verify(db)
        assert v["ok"] is False and v["broken_at"] == 2 and v["reason"] == "prev_hash mismatch"
    finally: _rm(db)


def test_deleted_row_breaks_chain():
    db = _tmpdb()
    try:
        audit.append(db, "k", {"x": 1}); audit.append(db, "k", {"x": 2}); audit.append(db, "k", {"x": 3})
        c = sqlite3.connect(db); c.execute("DELETE FROM audit_chain WHERE seq=2"); c.commit(); c.close()
        v = audit.verify(db)                          # seq=3 тепер має prev_hash від видаленого 2
        assert v["ok"] is False and v["broken_at"] == 3
    finally: _rm(db)


def test_canon_deterministic():
    assert audit._canon({"b": 1, "a": 2}) == audit._canon({"a": 2, "b": 1})   # порядок ключів не впливає


def test_tail_newest_first_and_filter():
    db = _tmpdb()
    try:
        audit.append(db, "honeytoken", {"n": 1}); audit.append(db, "ota", {"n": 2}); audit.append(db, "honeytoken", {"n": 3})
        t = audit.tail(db, 10)
        assert [e["data"]["n"] for e in t] == [3, 2, 1]                       # newest-first
        assert [e["data"]["n"] for e in audit.tail(db, 10, kind="honeytoken")] == [3, 1]
    finally: _rm(db)


# ── netlog recoverability ───────────────────────────────────────────────────
def test_rebuild_state_from_events():
    db = _tmpdb()
    try:
        # два скани: A+B заходять, потім B виходить -> A online, B offline
        netlog.record_scan(db, [{"mac": "AA:AA", "ip": "1.1.1.1", "name": "a", "online": True},
                                {"mac": "BB:BB", "ip": "2.2.2.2", "name": "b", "online": True}])
        time.sleep(0.01)
        netlog.record_scan(db, [{"mac": "AA:AA", "ip": "1.1.1.1", "name": "a", "online": True}])  # B зник -> leave
        # зіпсуємо read-модель: збрешемо, що B online
        c = sqlite3.connect(db); c.execute("UPDATE net_state SET online=1 WHERE mac='BB:BB'"); c.commit(); c.close()
        r = netlog.rebuild_state(db)                  # відновлення з довіреного журналу подій
        assert r["macs"] == 2 and r["online"] == 1
        st = {s["mac"]: s for s in netlog.state(db)}
        assert st["AA:AA"]["online"] is True
        assert st["BB:BB"]["online"] is False         # виправлено реплеєм подій
    finally: _rm(db)


if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
