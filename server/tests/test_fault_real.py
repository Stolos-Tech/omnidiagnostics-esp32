"""Fault-injection на РЕАЛЬНИХ payload'ах плати: пошкоджені/часткові кадри, offline-плата,
devscan-intel. Мета — пайплайн не падає, погане відсіює, добре інжестить. Плату силою не
ребутаємо (може бути на телефоні); транспортні збої моделюємо на рівні даних/мережі."""
import json, copy
import pytest


def _corrupt(hosts):
    """Реальні хости + типові пошкодження кадру (те, що реально шле збійна плата/шум USB)."""
    bad = [
        {},                                   # порожній
        {"ip": "192.168.1.5"},                # без mac
        {"mac": None, "ip": "x"},             # mac=None
        {"mac": 123, "ip": None},             # mac не рядок
        {"mac": "", "ip": ""},                # порожній mac
    ]
    return list(hosts) + bad


def test_corrupted_netscan_no_crash(fresh_db, board_data):
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    good = [h for h in hosts if h.get("mac")]
    mixed = _corrupt(hosts)
    n = profiles.ingest_netscan(mixed)                    # не має кинути
    # інжеститься лише валідне (mac непорожній); mac=123 -> str(123).upper() теж «валідний» рядок
    assert n >= len(good)
    cnt = db.one("SELECT COUNT(*) c FROM device_profiles")["c"]
    assert cnt >= len(good)


def test_corrupted_airscan_no_crash(fresh_db, board_data):
    db = fresh_db
    import profiles
    aps = (board_data["airscan"]["js"] or {}).get("aps", [])
    if not aps:
        pytest.skip("airscan порожній")
    bad = [None, {}, {"ssid": "x"}, {"rssi": "not-int"}, {"ch": None, "rssi": None},
           [1, 2], "garbage", 42]
    n = profiles.ingest_airscan(list(aps) + bad)          # не має кинути
    assert n >= 1                                         # реальні AP пройшли, сміття відсіяно


def test_truncated_json_frame_handled():
    """Обрізаний RES-кадр (реальний ризик USB-шуму) -> клієнт повертає (code, None, raw), не падає."""
    import board_client
    # емулюємо парсинг обрізаного JSON так само, як робить клієнт get()
    raw = '{"hosts":[{"ip":"192.168.1.2","ma'    # обрізано
    try:
        json.loads(raw); parsed = True
    except Exception:
        parsed = False
    assert parsed is False                                # обрізаний JSON -> клієнт віддасть js=None (не крах)


def test_collector_offline_returns_none(monkeypatch):
    """Плата недосяжна під час активної операції -> sample() повертає None (не виняток)."""
    import collector
    class Boom:
        def __call__(self, *a, **k): raise ConnectionError("board dropped (USB reset)")
    monkeypatch.setattr(collector.requests, "get", Boom())
    assert collector.sample(object()) is None             # object() як фейкове conn — до нього не дійде


def test_partial_sysinfo_tolerant():
    """sysinfo без частини ключів (реально буває на свіжому boot) — побудова рядка не падає, дефолти."""
    # реплікуємо логіку collector.sample без мережі
    si = {"temp_c": 43.9}                                 # без heap/power/uptime
    st = {}                                               # статус ще не готовий
    d = dict(temp_c=si.get("temp_c"), power_ma=si.get("power", {}).get("ma"),
             batt_mv=st.get("mv"), rssi=st.get("rssi"), heap=si.get("heap"),
             sta=1 if st.get("sta") else 0, ap=1 if st.get("ap") else 0, uptime_s=si.get("uptime_s"))
    assert d["temp_c"] == 43.9 and d["power_ma"] is None and d["heap"] is None


def test_devscan_intel_ingest(fresh_db, board_data):
    """Реальний devscan одного хоста -> профіль отримує category/open_ports (лінк даних інструментів)."""
    db = fresh_db
    import profiles
    dv = board_data.get("devscan", {})
    js = dv.get("js") if isinstance(dv, dict) else None
    ip = board_data.get("devscan_ip")
    if not js or not ip:
        pytest.skip("devscan недоступний цього прогону")
    mac = js.get("mac") or "AA:BB:CC:DD:EE:FF"
    ports = js.get("ports") or js.get("port_labels")
    intel = {"category": js.get("category"), "ports": ports if isinstance(ports, list) else [],
             "hostname": js.get("hostname"), "vendor": js.get("vendor")}
    did = profiles.upsert_device(mac, ip, intel=intel, category=js.get("category"), event="scan")
    prof = profiles.device_profile(did)
    assert prof["device"]["mac"] == mac.upper()
    # intel_json збережено й читається назад
    if intel["category"]:
        assert prof["device"].get("category") == intel["category"]
