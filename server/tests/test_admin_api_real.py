"""App API (admin_api Blueprint /lib/*) — інтерфейс Бібліотеки для мобільного додатка.
Flask test-client проти temp БД, засіяної РЕАЛЬНИМИ даними плати. Плюс auth/rate-limit guard."""
import pytest


@pytest.fixture()
def api(fresh_db, monkeypatch):
    """Flask-застосунок з admin-блупринтом, auth вимкнено (TOKEN=''), БД — ізольована temp."""
    from flask import Flask
    import admin_api
    monkeypatch.setattr(admin_api, "TOKEN", "")           # без токена -> _auth() True
    monkeypatch.setattr(admin_api, "_fails", {})
    app = Flask(__name__)
    app.register_blueprint(admin_api.admin)
    app.config["TESTING"] = True
    return app.test_client()


def _seed_real(board_data):
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    aps = (board_data["airscan"]["js"] or {}).get("aps", [])
    profiles.ingest_netscan(hosts)
    if aps:
        profiles.ingest_airscan(aps)
    return hosts, aps


def test_lib_devices_real(api, board_data):
    hosts, _ = _seed_real(board_data)
    r = api.get("/lib/devices")
    assert r.status_code == 200
    data = r.get_json()
    macs = {d["mac"] for d in data}
    for h in hosts:
        if h.get("mac"):
            assert h["mac"].upper() in macs


def test_lib_networks_real(api, board_data):
    _, aps = _seed_real(board_data)
    if not aps:
        pytest.skip("airscan порожній")
    r = api.get("/lib/networks")
    assert r.status_code == 200
    ssids = {n["ssid"] for n in r.get_json()}
    assert {a.get("ssid", "") for a in aps}.issubset(ssids)


def test_lib_device_profile_real(api, board_data):
    hosts, _ = _seed_real(board_data)
    did = api.get("/lib/devices").get_json()[0]["id"]
    r = api.get(f"/lib/devices/{did}")
    assert r.status_code == 200
    prof = r.get_json()
    assert "device" in prof and "history" in prof and "networks" in prof


def test_lib_target_switch(api, board_data):
    _seed_real(board_data)
    did = api.get("/lib/devices").get_json()[0]["id"]
    r1 = api.post(f"/lib/target?kind=device&id={did}&label=t1")
    assert r1.get_json()["ok"]
    r2 = api.post(f"/lib/target?kind=device&id={did}&label=t2")
    assert r2.get_json()["ok"]
    import db
    active = db.q("SELECT id FROM targets WHERE active=1")
    assert len(active) == 1                                # лише один активний таргет


def test_phone_wifi_import_crosslinks(api, board_data):
    body = {"device": "phone-test", "networks": [{"ssid": "MyHome", "psk": "x", "encryption": "WPA2"}]}
    r = api.post("/admin/phone/wifi/import", json=body)
    assert r.status_code == 200
    import db
    row = db.one("SELECT ssid,source_device FROM phone_wifi WHERE ssid=?", ("MyHome",))
    assert row and row["source_device"] == "phone-test"


def test_auth_guard_401_and_ratelimit(fresh_db, monkeypatch):
    from flask import Flask
    import admin_api
    monkeypatch.setattr(admin_api, "TOKEN", "secret-token")
    monkeypatch.setattr(admin_api, "_fails", {})
    app = Flask(__name__); app.register_blueprint(admin_api.admin); app.config["TESTING"] = True
    c = app.test_client()
    assert c.get("/lib/devices").status_code == 401          # без токена
    assert c.get("/lib/devices", headers={"X-Gateway-Token": "wrong"}).status_code == 401
    for _ in range(11):                                      # перевищити _FAIL_MAX=10
        c.get("/lib/devices")
    assert c.get("/lib/devices").status_code == 429          # rate-limited
    # правильний токен усе одно 429, доки вікно не мине (лок за IP) — очікувано
