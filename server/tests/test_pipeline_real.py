"""Пайплайн на РЕАЛЬНИХ даних плати: netscan/airscan -> Library (profiles), netlog, netguard,
netwatch. Перевіряємо авто-створення профілів, лінкування device<->network, індексацію, ідемпотентність."""
import json
import pytest


def test_real_netscan_ingest_creates_device_profiles(fresh_db, board_data):
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    assert hosts, "плата не повернула жодного хоста netscan"
    n = profiles.ingest_netscan(hosts)
    macs = [h["mac"] for h in hosts if h.get("mac")]
    assert n == len(macs)
    got = {r["mac"] for r in db.q("SELECT mac FROM device_profiles")}
    assert got == {m.upper() for m in macs}
    # history-рядок на кожен пристрій
    for m in macs:
        d = profiles.list_devices()
        assert any(x["mac"] == m.upper() for x in d)


def test_real_netscan_idempotent(fresh_db, board_data):
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    profiles.ingest_netscan(hosts)
    before = db.one("SELECT COUNT(*) c FROM device_profiles")["c"]
    profiles.ingest_netscan(hosts)                       # повторний інжест тих самих
    after = db.one("SELECT COUNT(*) c FROM device_profiles")["c"]
    assert after == before, "повторний netscan НЕ має плодити дублі профілів"
    # times_seen має зрости
    for h in hosts:
        if h.get("mac"):
            r = db.one("SELECT times_seen FROM device_profiles WHERE mac=?", (h["mac"].upper(),))
            assert r["times_seen"] >= 2


def test_real_airscan_ingest_creates_networks(fresh_db, board_data):
    db = fresh_db
    import profiles
    js = board_data["airscan"]["js"] or {}
    aps = js.get("aps", [])
    if not aps:
        pytest.skip("airscan порожній цього прогону (throttle/радіо)")
    n = profiles.ingest_airscan(aps)
    assert n == len(aps), f"інжест airscan: {n} з {len(aps)} AP (реальний формат dict!)"
    nets = profiles.list_networks()
    ssids = {a.get("ssid", "") for a in aps}
    got = {x["ssid"] for x in nets}
    assert ssids.issubset(got)


def test_real_airscan_rssi_bounds(fresh_db, board_data):
    db = fresh_db
    import profiles
    js = board_data["airscan"]["js"] or {}
    aps = js.get("aps", [])
    if not aps:
        pytest.skip("airscan порожній")
    profiles.ingest_airscan(aps)
    profiles.ingest_airscan(aps)                          # другий прохід -> min/max мають лишитись валідні
    for row in db.q("SELECT ssid,max_rssi,min_rssi FROM networks"):
        assert row["max_rssi"] is None or row["min_rssi"] is None or row["max_rssi"] >= row["min_rssi"], \
            f"{row['ssid']}: max_rssi<min_rssi ({row['max_rssi']}<{row['min_rssi']})"


def test_real_device_network_linking(fresh_db, board_data):
    """on_connect має злінкувати реальний пристрій із мережею (device_network)."""
    db = fresh_db
    import profiles
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    js = board_data["airscan"]["js"] or {}
    aps = js.get("aps", [])
    if not hosts or not aps:
        pytest.skip("немає реальних host+ap для лінку")
    ssid = aps[0].get("ssid"); bssid = aps[0].get("bssid")
    h = hosts[0]
    res = profiles.on_connect(h["mac"], h["ip"], vendor=h.get("vendor", ""), ssid=ssid, bssid=bssid)
    assert res["device_id"] and res["network_id"]
    link = db.one("SELECT 1 FROM device_network WHERE device_id=? AND network_id=?",
                  (res["device_id"], res["network_id"]))
    assert link, "device_network лінк не створено"
    prof = profiles.device_profile(res["device_id"])
    assert any(nw["id"] == res["network_id"] for nw in prof["networks"])


def test_real_netlog_join(fresh_db, board_data):
    """netlog.record_scan на реальних хостах -> подія join першого разу."""
    db = fresh_db
    import netlog
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    src = [{"mac": h["mac"], "ip": h.get("ip", ""), "name": h.get("vendor", ""), "online": True}
           for h in hosts if h.get("mac")]
    if not src:
        pytest.skip("немає MAC")
    ev = netlog.record_scan(db.DB_PATH, src)
    assert set(m.upper() for m in ev["join"]) == {s["mac"].upper() for s in src}
    ev2 = netlog.record_scan(db.DB_PATH, src)             # без змін
    assert ev2["join"] == [] and ev2["leave"] == []


def test_real_netguard_pending(fresh_db, board_data):
    """Новий реальний пристрій -> pending; trusted -> approved (не pending)."""
    db = fresh_db
    import netguard
    hosts = (board_data["netscan"]["js"] or {}).get("hosts", [])
    src = [{"mac": h["mac"], "ip": h.get("ip", ""), "online": True} for h in hosts if h.get("mac")]
    if len(src) < 2:
        pytest.skip("<2 реальних пристроїв")
    trusted = [src[0]["mac"]]
    netguard.seed_trusted(db.DB_PATH, trusted)
    pending = netguard.sync_pending(db.DB_PATH, src)
    assert src[0]["mac"].upper() not in {m.upper() for m in pending}   # trusted не pending
    assert src[1]["mac"].upper() in {m.upper() for m in pending}       # новий -> pending
