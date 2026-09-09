"""Тести парсерів srvstats (чисті). Запуск: python test_srvstats.py"""
import srvstats

MEMINFO = """MemTotal:       16307412 kB
MemFree:         9500000 kB
MemAvailable:   12000000 kB
Buffers:          200000 kB
"""

def test_meminfo():
    m = srvstats.parse_meminfo(MEMINFO)
    assert m["total_mb"] == 16307412 // 1024
    assert m["avail_mb"] == 12000000 // 1024
    assert m["used_pct"] == round((16307412 - 12000000) * 100 / 16307412)

def test_meminfo_empty():
    m = srvstats.parse_meminfo("")
    assert m["total_mb"] == 0 and m["used_pct"] == 0

def test_loadavg():
    l = srvstats.parse_loadavg("0.42 0.35 0.30 1/234 5678")
    assert l == {"l1": 0.42, "l5": 0.35, "l15": 0.30}
    assert srvstats.parse_loadavg("") == {}

def test_uptime():
    assert srvstats.parse_uptime("123456.78 90000.0") == 123456
    assert srvstats.parse_uptime("bad") == 0

def test_stats_smoke():
    # IO-виклик: не падає, повертає очікувані ключі (значення залежать від ОС)
    s = srvstats.stats()
    for k in ("cores", "uptime_s", "mem", "disk", "services"):
        assert k in s

if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
