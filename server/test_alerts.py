"""Локальні тести рушія алертів (без мережі/плати). Запуск: python test_alerts.py"""
import alerts

CFG = {"alerts": {"temp_crit_c": 70, "batt_low_mv": 3400, "rssi_low": -85, "heap_low": 8000, "cooldown_s": 100}}


def keys(row):
    return {k for k, _ in alerts.evaluate(row, CFG)}


def test_normal_no_alerts():
    assert keys(dict(temp_c=43.0, batt_mv=4100, rssi=-52, heap=40000)) == set()


def test_each_threshold():
    assert keys(dict(temp_c=72.0, batt_mv=4100, rssi=-50, heap=40000)) == {"overheat"}
    assert keys(dict(temp_c=40.0, batt_mv=3300, rssi=-50, heap=40000)) == {"batt_low"}
    assert keys(dict(temp_c=40.0, batt_mv=4100, rssi=-90, heap=40000)) == {"rssi_low"}
    assert keys(dict(temp_c=40.0, batt_mv=4100, rssi=-50, heap=5000)) == {"heap_low"}


def test_multiple_and_boundary():
    assert keys(dict(temp_c=70.0, batt_mv=3400, rssi=-85, heap=8000)) == {"overheat", "batt_low", "rssi_low", "heap_low"}


def test_ignores_missing_and_zero():
    # відсутні поля й нульові плейсхолдери (rssi=0, mv=0) не тригерять
    assert keys(dict(temp_c=None, batt_mv=0, rssi=0, heap=0)) == set()
    assert keys(dict()) == set()


def test_gate_cooldown():
    g = alerts.AlertGate(cooldown_s=100)
    assert g.ready("overheat", now=1000) is True     # перший — можна
    assert g.ready("overheat", now=1050) is False    # у межах cooldown — ні
    assert g.ready("overheat", now=1101) is True     # після cooldown — знову можна
    g.clear("overheat")
    assert g.ready("overheat", now=1120) is True      # після clear — одразу можна


def test_send_telegram_noconfig():
    assert alerts.send_telegram({"alerts": {}}, "x") is False  # без токена — тихо false


if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
