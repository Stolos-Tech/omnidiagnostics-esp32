"""Тест-харнес ESP32-OS: РЕАЛЬНІ дані з плати (COM5) -> пайплайн сервера проти ізольованої
тимчасової БД (живий stolos не чіпаємо). Board-дані збираються ОДИН раз на сесію (щадимо
плату й тримаємось 7с throttle airscan). Плата недоступна -> board-тести skip, не падають.
"""
from __future__ import annotations
import os, sys, time, tempfile, json
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SRV = os.path.dirname(HERE)
sys.path.insert(0, SRV)
sys.path.insert(0, HERE)

import board_client


@pytest.fixture()
def fresh_db(tmp_path, monkeypatch):
    """Ізольована БД: db.DB_PATH -> temp, схема з db_schema.sql, _inited скинуто."""
    import db
    p = str(tmp_path / "telemetry.db")
    monkeypatch.setattr(db, "DB_PATH", p)
    monkeypatch.setattr(db, "_inited", False)
    db.init()
    yield db
    # WAL cleanup
    try:
        c = db.connect(); c.execute("PRAGMA wal_checkpoint(TRUNCATE)"); c.close()
    except Exception:
        pass


# ---- РЕАЛЬНІ дані плати (session-scoped, збираються раз) ----
_BOARD_CACHE = {"tried": False, "data": None}


def _collect_board():
    if _BOARD_CACHE["tried"]:
        return _BOARD_CACHE["data"]
    _BOARD_CACHE["tried"] = True
    b = board_client.try_board()
    if not b:
        return None
    data = {"ok": b.login()}
    def grab(path, wait=25, gap=1.0):
        code, js, raw = b.get(path, wait); time.sleep(gap); return {"code": code, "js": js, "bytes": len(raw)}
    data["sysinfo"] = grab("/api/sysinfo")
    data["status"] = grab("/api/status")
    data["battery"] = grab("/api/battery")
    data["modules"] = grab("/api/modules")
    data["netscan"] = grab("/api/netscan")
    time.sleep(8)                                   # airscan 7с throttle
    data["airscan"] = grab("/api/airscan")
    # devscan одного реального хоста (не шлюз)
    hosts = (data["netscan"]["js"] or {}).get("hosts", [])
    tgt = next((h["ip"] for h in hosts if not h.get("gw")), hosts[0]["ip"] if hosts else None)
    data["devscan_ip"] = tgt
    if tgt:
        data["devscan"] = grab(f"/api/devscan?ip={tgt}", wait=28)
    b.close()
    _BOARD_CACHE["data"] = data
    # дамп для інспекції/повторного прогону
    try:
        json.dump(data, open(os.path.join(HERE, "_board_capture.json"), "w"), ensure_ascii=False, indent=1)
    except Exception:
        pass
    return data


@pytest.fixture(scope="session")
def board_data():
    d = _collect_board()
    if d is None or not d.get("ok"):
        pytest.skip("плата COM5 недоступна — пропускаю тести на реальних даних")
    return d


def _transport_used():
    d = _BOARD_CACHE.get("data")
    if not d:
        return "none"
    # HttpBoardClient не має self.s.port; BoardClient має .port
    return "usb" if os.path.exists("\\\\.\\" + os.environ.get("ESPOS_PORT", "COM5")) else "wifi"


_SESSION_START = [0.0]


def pytest_sessionstart(session):
    import time as _t
    _SESSION_START[0] = _t.time()


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    """Записати метрики прогону в STABILITY_TEST_RUNS.json (пайплайн стабільності)."""
    import time as _t, sys, json as _j
    tr = terminalreporter
    stats = tr.stats
    passed = len(stats.get("passed", []))
    failed = len(stats.get("failed", []))
    skipped = len(stats.get("skipped", []))
    errors = len(stats.get("error", []))
    total = passed + failed + skipped + errors
    metrics = {}
    mod = sys.modules.get("test_db_stress")
    if mod and getattr(mod, "METRICS", None):
        metrics = mod.METRICS
    rec = {
        "ts": int(_t.time()),
        "when": _t.strftime("%Y-%m-%d %H:%M:%S"),
        "tests": {"total": total, "passed": passed, "failed": failed,
                  "skipped": skipped, "errors": errors},
        "pass_rate": round(passed / total, 4) if total else None,
        "duration_s": round(_t.time() - _SESSION_START[0], 2) if _SESSION_START[0] else 0,
        "board_available": bool(_BOARD_CACHE.get("data") and _BOARD_CACHE["data"].get("ok")),
        "stress": metrics,
    }
    path = os.path.join(SRV, "..", "STABILITY_TEST_RUNS.json")
    path = os.path.abspath(path)
    try:
        runs = _j.load(open(path)) if os.path.exists(path) else []
    except Exception:
        runs = []
    runs.append(rec)
    try:
        _j.dump(runs, open(path, "w"), ensure_ascii=False, indent=1)
    except Exception:
        pass


@pytest.fixture()
def live_board():
    """Живе з'єднання для fault-injection (кожен тест сам відкриває/закриває)."""
    b = board_client.try_board()
    if not b:
        pytest.skip("плата COM5 недоступна")
    yield b
    b.close()
