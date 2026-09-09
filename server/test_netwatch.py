"""Тести netwatch (чиста логіка + SQLite in-memory). Запуск: python test_netwatch.py"""
import sqlite3
import netwatch


def test_is_local_mac():
    assert netwatch.is_local_mac("02:11:22:33:44:55") is True    # локальний біт
    assert netwatch.is_local_mac("F8:FE:5E:8A:3C:DE") is False   # глобальний (реальний вендор)
    assert netwatch.is_local_mac("DA:00:00:00:00:01") is True    # DA -> біт1 set
    assert netwatch.is_local_mac("zz:zz") is False   # не-hex -> false
    assert netwatch.is_local_mac("") is False


def test_new_devices_basic():
    # A8 -> глобальний (locally-administered біт=0), тож не skip як рандомний
    hosts = [{"mac": "F8:FE:5E:00:00:01", "ip": "192.168.1.2", "vendor": "Intel"},
             {"mac": "A8:BB:CC:00:00:02", "ip": "192.168.1.3", "vendor": "TP-Link"}]
    known = {"F8:FE:5E:00:00:01"}
    fresh = netwatch.new_devices(hosts, known)
    assert [h["mac"] for h in fresh] == ["A8:BB:CC:00:00:02"]


def test_new_devices_skips_random():
    hosts = [{"mac": "02:AA:BB:CC:DD:EE", "ip": "192.168.1.9", "vendor": "?"}]
    assert netwatch.new_devices(hosts, set(), skip_random=True) == []
    assert len(netwatch.new_devices(hosts, set(), skip_random=False)) == 1


def test_new_devices_case_insensitive():
    hosts = [{"mac": "a8:bb:cc:00:00:02", "ip": "192.168.1.3", "vendor": "x"}]
    assert netwatch.new_devices(hosts, {"A8:BB:CC:00:00:02"}) == []   # known зберігаємо upper


def test_record_and_load():
    conn = sqlite3.connect(":memory:")
    netwatch.ensure_table(conn)
    assert netwatch.load_known(conn) == set()
    netwatch.record(conn, [{"mac": "AA:BB:CC:00:00:02", "ip": "10.0.0.5", "vendor": "TP-Link"}], now=1000)
    known = netwatch.load_known(conn)
    assert "AA:BB:CC:00:00:02" in known
    # повторний запис оновлює last_seen, не дублює
    netwatch.record(conn, [{"mac": "AA:BB:CC:00:00:02", "ip": "10.0.0.6", "vendor": "TP-Link"}], now=2000)
    rows = list(conn.execute("SELECT mac, ip, first_seen, last_seen FROM devices"))
    assert len(rows) == 1
    assert rows[0][1] == "10.0.0.6" and rows[0][2] == 1000 and rows[0][3] == 2000


if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
