"""Тести insights (чиста логіка). Запуск: python test_insights.py"""
import insights


def test_empty():
    assert insights.insights([]) == {"samples": 0}


def test_battery_drain_and_eta():
    # 4 години, mv падає 4100 -> 3700 (100 mV/год розряд)
    rows = [{"ts": i * 3600, "mv": 4100 - i * 100, "temp": 40 + i, "power": 180} for i in range(5)]
    r = insights.insights(rows)
    assert r["samples"] == 5 and r["span_h"] == 4.0
    b = r["battery"]
    assert b["last_mv"] == 3700 and b["min_mv"] == 3700 and b["max_mv"] == 4100
    assert abs(b["drain_mv_h"] + 100) < 0.1          # ~ -100 mV/год
    assert b["eta_h"] == round((3700 - 3300) / 100, 1)  # 4.0 год до 3300
    assert 0 <= b["pct"] <= 100


def test_temp_power_stats():
    rows = [{"ts": i * 60, "mv": 4000, "temp": 40.0 + i, "power": 100 + i * 10} for i in range(5)]
    r = insights.insights(rows)
    assert r["temp"]["max_c"] == 44.0 and r["temp"]["last_c"] == 44.0
    assert r["power"]["max_ma"] == 140


def test_no_eta_when_charging():
    rows = [{"ts": i * 3600, "mv": 3700 + i * 100, "temp": 40, "power": 180} for i in range(4)]  # росте
    b = insights.insights(rows)["battery"]
    assert b["eta_h"] is None                         # заряджається -> без ETA


def test_pct_bounds():
    assert insights._li_pct(4300) == 100
    assert insights._li_pct(3200) == 0
    assert 40 <= insights._li_pct(3700) <= 50


if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
