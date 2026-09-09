"""Network & Device Library (task 5). Every network/device the system connects to
or observes gets an auto-generated, continuously updated relational profile with
full history. Feeds the app for targeting."""
from __future__ import annotations
import json
import db

try:
    import devintel
except Exception:
    devintel = None


# ---------------- NETWORKS ----------------
def upsert_network(ssid: str, bssid: str = "", *, channel=None, encryption=None,
                   band=None, vendor=None, rssi=None, hidden=0, twin=0,
                   saved=0, board_id=None) -> int:
    n = db.now()
    # Атомарний upsert (INSERT ... ON CONFLICT) — усуває гонку SELECT-then-INSERT при
    # конкурентних записах (collector/dashboard/admin — окремі процеси; глобальний lock їх не рятує).
    db.run("""INSERT INTO networks(bssid,ssid,band,channel,encryption,hidden,vendor,
              first_seen,last_seen,times_seen,max_rssi,min_rssi,is_twin,is_saved)
              VALUES(?,?,?,?,?,?,?,?,?,1,?,?,?,?)
              ON CONFLICT(bssid,ssid) DO UPDATE SET
                last_seen=excluded.last_seen,
                times_seen=networks.times_seen+1,
                channel=COALESCE(excluded.channel,networks.channel),
                encryption=COALESCE(excluded.encryption,networks.encryption),
                band=COALESCE(excluded.band,networks.band),
                vendor=COALESCE(excluded.vendor,networks.vendor),
                hidden=excluded.hidden, is_twin=excluded.is_twin,
                max_rssi=CASE WHEN excluded.max_rssi IS NULL THEN networks.max_rssi
                              WHEN networks.max_rssi IS NULL THEN excluded.max_rssi
                              ELSE MAX(networks.max_rssi,excluded.max_rssi) END,
                min_rssi=CASE WHEN excluded.min_rssi IS NULL THEN networks.min_rssi
                              WHEN networks.min_rssi IS NULL THEN excluded.min_rssi
                              ELSE MIN(networks.min_rssi,excluded.min_rssi) END""",
           (bssid, ssid, band, channel, encryption, hidden, vendor, n, n, rssi, rssi, twin, saved))
    nid = db.one("SELECT id FROM networks WHERE bssid=? AND ssid=?", (bssid, ssid))["id"]
    if rssi is not None:
        db.run("INSERT INTO network_history(network_id,ts,rssi,channel,source_board) VALUES(?,?,?,?,?)",
               (nid, n, rssi, channel, board_id))
    return nid


def network_profile(nid: int) -> dict:
    net = db.one("SELECT * FROM networks WHERE id=?", (nid,))
    if not net:
        return {"error": "not found"}
    hist = db.q("SELECT ts,rssi,channel,clients FROM network_history WHERE network_id=? ORDER BY ts DESC LIMIT 200", (nid,))
    devs = db.q("""SELECT d.id,d.mac,d.ip,d.hostname,d.category,dn.last_seen
                   FROM device_network dn JOIN device_profiles d ON d.id=dn.device_id
                   WHERE dn.network_id=? ORDER BY dn.last_seen DESC""", (nid,))
    return {"network": dict(net), "history": db.rows_to_dicts(hist),
            "devices": db.rows_to_dicts(devs)}


def list_networks() -> list[dict]:
    return db.rows_to_dicts(db.q(
        "SELECT id,ssid,bssid,channel,encryption,vendor,is_twin,is_saved,times_seen,"
        "max_rssi,last_seen FROM networks ORDER BY last_seen DESC"))


# ---------------- DEVICES ----------------
def upsert_device(mac: str, ip: str = "", *, vendor=None, hostname=None,
                  category=None, is_gw=0, random_mac=0, intel: dict = None,
                  network_id=None, board_id=None, event="seen") -> int:
    mac = str(mac or "").strip().upper()      # захист від non-str/None у пошкодженому кадрі
    if not mac:
        raise ValueError("upsert_device: порожній MAC")
    n = db.now()
    ports = ",".join(str(p) for p in (intel or {}).get("ports", [])) or None
    ij = json.dumps(intel, ensure_ascii=False) if intel else None
    # Атомарний upsert — race-free при конкурентних записах (див. upsert_network).
    db.run("""INSERT INTO device_profiles(mac,ip,hostname,vendor,category,is_random_mac,
              is_gateway,first_seen,last_seen,times_seen,open_ports,intel_json)
              VALUES(?,?,?,?,?,?,?,?,?,1,?,?)
              ON CONFLICT(mac) DO UPDATE SET
                ip=COALESCE(NULLIF(excluded.ip,''),device_profiles.ip),
                last_seen=excluded.last_seen,
                times_seen=device_profiles.times_seen+1,
                vendor=COALESCE(excluded.vendor,device_profiles.vendor),
                hostname=COALESCE(NULLIF(excluded.hostname,''),device_profiles.hostname),
                category=COALESCE(excluded.category,device_profiles.category),
                is_gateway=excluded.is_gateway, is_random_mac=excluded.is_random_mac,
                open_ports=COALESCE(excluded.open_ports,device_profiles.open_ports),
                intel_json=COALESCE(excluded.intel_json,device_profiles.intel_json)""",
           (mac, ip, hostname, vendor, category, random_mac, is_gw, n, n, ports, ij))
    did = db.one("SELECT id FROM device_profiles WHERE mac=?", (mac,))["id"]
    db.run("INSERT INTO device_history(device_id,ts,ip,event,detail,source_board) VALUES(?,?,?,?,?,?)",
           (did, n, ip, event, category or "", board_id))
    if network_id:
        # атомарний лінк device<->network (PK(device_id,network_id)) — без SELECT-then-INSERT гонки
        db.run("""INSERT INTO device_network(device_id,network_id,first_seen,last_seen) VALUES(?,?,?,?)
                  ON CONFLICT(device_id,network_id) DO UPDATE SET last_seen=excluded.last_seen""",
               (did, network_id, n, n))
    return did


def device_profile(did: int) -> dict:
    dev = db.one("SELECT * FROM device_profiles WHERE id=?", (did,))
    if not dev:
        return {"error": "not found"}
    hist = db.q("SELECT ts,ip,event,detail FROM device_history WHERE device_id=? ORDER BY ts DESC LIMIT 200", (did,))
    nets = db.q("""SELECT n.id,n.ssid,n.bssid,dn.last_seen FROM device_network dn
                   JOIN networks n ON n.id=dn.network_id WHERE dn.device_id=? ORDER BY dn.last_seen DESC""", (did,))
    d = dict(dev)
    if d.get("intel_json"):
        try:
            d["intel"] = json.loads(d.pop("intel_json"))
        except Exception:
            d["intel"] = None
    return {"device": d, "history": db.rows_to_dicts(hist), "networks": db.rows_to_dicts(nets)}


def list_devices() -> list[dict]:
    return db.rows_to_dicts(db.q(
        "SELECT id,mac,ip,hostname,vendor,category,trust,is_gateway,name,times_seen,last_seen "
        "FROM device_profiles ORDER BY last_seen DESC"))


def set_trust(did: int, trust: str) -> None:
    db.run("UPDATE device_profiles SET trust=? WHERE id=?", (trust, did))


def set_name(did: int, name: str) -> None:
    db.run("UPDATE device_profiles SET name=? WHERE id=?", (name, did))


# ---------------- TRIGGER: auto-profile on connect ----------------
def on_connect(mac: str, ip: str, *, vendor="", ssid="", bssid="", board_id=None) -> dict:
    """Called whenever the system connects to / first sees a device or network.
    Auto-creates the dedicated profile, runs deep intel, links device<->network."""
    nid = upsert_network(ssid, bssid, board_id=board_id, saved=1) if ssid else None
    intel = None
    if devintel and ip:
        try:
            intel = devintel.intel(ip, mac, vendor)
        except Exception:
            intel = None
    did = upsert_device(mac, ip, vendor=(intel or {}).get("vendor", vendor),
                        hostname=(intel or {}).get("hostname"),
                        category=(intel or {}).get("category"),
                        random_mac=1 if (intel or {}).get("random_mac") else 0,
                        intel=intel, network_id=nid, board_id=board_id, event="join")
    return {"device_id": did, "network_id": nid,
            "profile": device_profile(did)}


def ingest_netscan(hosts: list[dict], board_id=None) -> int:
    """Bulk ingest a board /api/netscan result into the Library."""
    n = 0
    for h in hosts:
        if not isinstance(h, dict):
            continue
        mac = h.get("mac")
        if not isinstance(mac, str) or not mac.strip():      # відсіюємо пошкоджені кадри
            continue
        upsert_device(mac, h.get("ip", "") or "", vendor=h.get("vendor"),
                      is_gw=1 if h.get("gw") else 0, board_id=board_id, event="scan")
        n += 1
    return n


def ingest_airscan(aps: list, board_id=None) -> int:
    """Bulk ingest a board /api/airscan aps.

    Приймає ОБИДВА формати:
      * dict (реальний формат плати): {ssid,bssid,rssi,ch,enc[,twin]}
      * list (легасі): [ssid,bssid,rssi,ch,enc,twin,risk]
    """
    n = 0
    for ap in aps:
        try:
            if isinstance(ap, dict):
                ssid = ap.get("ssid", "") or ""
                bssid = ap.get("bssid", "") or ""
                rssi = int(ap["rssi"]) if ap.get("rssi") is not None else None
                ch = int(ap["ch"]) if ap.get("ch") is not None else None
                enc = ap.get("enc")
                twin = 1 if ap.get("twin") else 0
            else:                                    # list/tuple легасі
                ssid, bssid = ap[0], ap[1]
                rssi = int(ap[2]); ch = int(ap[3]); enc = ap[4]
                twin = 1 if (len(ap) > 5 and ap[5] == "twin") else 0
        except Exception:
            continue
        upsert_network(ssid, bssid, channel=ch, encryption=enc, rssi=rssi,
                       hidden=1 if not ssid else 0, twin=twin, board_id=board_id)
        n += 1
    return n
