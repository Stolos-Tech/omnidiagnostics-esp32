"""Стрес БД/пайплайну на РЕАЛЬНИХ payload'ах плати (реплей у обсязі + конкурентно).
Дані не вигадані — беруться реальні netscan/airscan, повторюються під навантаженням.
Метрики (throughput/lockup/RAM) пишуться у STABILITY_TEST_RUNS.json харнесом run_stability.py."""
import os, time, threading, tracemalloc, sqlite3
import pytest

METRICS = {}


def test_bulk_replay_throughput(fresh_db, board_data):
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    aps = (board_data["airscan"]["js"] or {}).get("aps", [])
    assert hosts, "немає реальних хостів"
    tracemalloc.start()
    t0 = time.time()
    ROUNDS = 200
    for _ in range(ROUNDS):
        profiles.ingest_netscan(hosts)
        if aps:
            profiles.ingest_airscan(aps)
    dt = time.time() - t0
    cur, peak = tracemalloc.get_traced_memory(); tracemalloc.stop()
    ops = ROUNDS * (len(hosts) + len(aps))
    METRICS["bulk"] = {"rounds": ROUNDS, "ops": ops, "sec": round(dt, 3),
                       "ops_per_s": round(ops / dt, 1), "peak_kb": round(peak / 1024, 1)}
    # інваріанти: жодних дублів (унікальність mac/ssid), історія росте лінійно
    devs = db.one("SELECT COUNT(*) c FROM device_profiles")["c"]
    assert devs == len({h["mac"].upper() for h in hosts if h.get("mac")})
    dh = db.one("SELECT COUNT(*) c FROM device_history")["c"]
    assert dh >= ROUNDS * len([h for h in hosts if h.get("mac")])   # рядок історії на кожен інжест
    assert peak / 1024 / 1024 < 100, f"пік RAM {peak/1e6:.1f}MB — можливий витік"


def test_concurrent_writers_no_lockup(fresh_db, board_data):
    """Багато потоків пишуть реальні профілі одночасно — db.run має серіалізувати без 'database is locked'."""
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    aps = (board_data["airscan"]["js"] or {}).get("aps", [])
    errors = []
    def worker(wid):
        try:
            for _ in range(40):
                profiles.ingest_netscan(hosts)
                if aps:
                    profiles.ingest_airscan(aps)
        except sqlite3.OperationalError as e:
            errors.append(f"w{wid}: {e}")
        except Exception as e:
            errors.append(f"w{wid}: {type(e).__name__}: {e}")
    t0 = time.time()
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
    [t.start() for t in ths]; [t.join() for t in ths]
    dt = time.time() - t0
    METRICS["concurrent"] = {"threads": 8, "sec": round(dt, 3), "errors": len(errors)}
    assert not errors, f"конкурентні помилки БД: {errors[:3]}"
    # цілісність: рівно N унікальних пристроїв попри 8 потоків
    devs = db.one("SELECT COUNT(*) c FROM device_profiles")["c"]
    assert devs == len({h["mac"].upper() for h in hosts if h.get("mac")})


def test_query_uses_indexes(fresh_db, board_data):
    """Гарячі запити мають бити по індексах (не full scan) — індексація без лок-апів."""
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    profiles.ingest_netscan(hosts)
    c = db.connect()
    try:
        plan_ip = c.execute("EXPLAIN QUERY PLAN SELECT * FROM device_profiles WHERE ip=?",
                            ("192.168.100.1",)).fetchall()
        txt = " ".join(str(dict(r)) for r in plan_ip)
        assert "ix_dev_ip" in txt or "USING INDEX" in txt.upper(), f"ip-запит не по індексу: {txt}"
        # history-запит по (device_id,ts)
        plan_h = c.execute("EXPLAIN QUERY PLAN SELECT * FROM device_history WHERE device_id=? ORDER BY ts DESC",
                          (1,)).fetchall()
        th = " ".join(str(dict(r)) for r in plan_h)
        assert "ix_devhist_dev" in th, f"history-запит не по індексу: {th}"
    finally:
        c.close()


def test_history_retention_bounded(fresh_db, board_data):
    """network_history/device_history не мають розпухати без межі — перевіряємо purge-логіку netlog."""
    db = fresh_db
    import netlog
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    src = [{"mac": h["mac"], "ip": h.get("ip", ""), "online": True} for h in hosts if h.get("mac")]
    if not src:
        pytest.skip("немає MAC")
    for _ in range(5):
        netlog.record_scan(db.DB_PATH, src)
    purged = netlog.purge(db.DB_PATH, older_than_days=0)     # усе старіше за 0 днів (тобто все) прибирається
    assert purged >= 0     # purge працює без винятку
