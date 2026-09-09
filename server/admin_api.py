"""Admin & Library REST (tasks 3,5,6,7,9). A Flask Blueprint mounted by
dashboard.py. Guarded by the same gateway token as the rest of the tailnet API.

  Server mgmt (3):  /admin/server/*  reboot, services, config
  Library (5):      /lib/networks, /lib/devices, /lib/*/&lt;id&gt; profiles
  Routing (6):      /board/&lt;id&gt;/&lt;path&gt;  -> proxied over USB when attached
  Phone export (7): /admin/phone/wifi/import
  Health (9):       /admin/health
"""
from __future__ import annotations
import os, json, subprocess, time, hmac
from flask import Blueprint, request, jsonify
import db, profiles, boardproxy, usbctl

try:
    import healthmon
except Exception:
    healthmon = None

HERE = os.path.dirname(os.path.abspath(__file__))
CFG_PATH = os.path.join(HERE, "config.json")
CFG = json.load(open(CFG_PATH))
TOKEN = CFG.get("gateway_token", "")
ALLOWED_SERVICES = set(CFG.get("manageable_services",
                               ["espos-gateway", "espos-collector", "espos-dashboard",
                                "espos-supervisor", "espos-healthmon", "espos-bot"]))

admin = Blueprint("admin", __name__)


def _auth() -> bool:
    if not TOKEN:
        return True
    got = request.headers.get("X-Gateway-Token") or ""
    return hmac.compare_digest(got, TOKEN)   # константо-час: без таймінг-ліку токена


# Лок невдалих auth на прямому :8080 (в обхід gateway rate_ok): >MAX помилок/вікно -> 429.
_fails: dict[str, list] = {}
_FAIL_MAX = 10
_FAIL_WINDOW = 60.0

def _locked(ip: str) -> bool:
    now = time.time()
    lst = [t for t in _fails.get(ip, ()) if now - t < _FAIL_WINDOW]
    _fails[ip] = lst
    return len(lst) >= _FAIL_MAX


@admin.before_request
def _guard():
    ip = request.remote_addr or "?"
    if _locked(ip):
        return jsonify({"error": "rate limited"}), 429
    if not _auth():
        _fails.setdefault(ip, []).append(time.time())   # рахувати лише невдалі
        return jsonify({"error": "unauthorized"}), 401
    _fails.pop(ip, None)                                 # успіх -> скинути лічильник


# ---------------- SERVER MANAGEMENT (task 3) ----------------
@admin.route("/admin/health")
def admin_health():
    return jsonify(healthmon.stats() if healthmon else {"error": "healthmon missing"})


@admin.route("/admin/server/services")
def services():
    out = {}
    for s in ALLOWED_SERVICES:
        try:
            r = subprocess.run(["systemctl", "is-active", s], capture_output=True, text=True, timeout=5)
            out[s] = r.stdout.strip()
        except Exception:
            out[s] = "unknown"
    return jsonify(out)


@admin.route("/admin/server/service", methods=["POST"])
def service_action():
    name = request.args.get("name", "")
    action = request.args.get("action", "")     # start|stop|restart|status
    if name not in ALLOWED_SERVICES or action not in ("start", "stop", "restart"):
        return jsonify({"error": "not permitted"}), 400
    try:
        r = subprocess.run(["systemctl", action, name], capture_output=True, text=True, timeout=30)
        return jsonify({"ok": r.returncode == 0, "out": r.stdout, "err": r.stderr})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@admin.route("/admin/server/reboot", methods=["POST"])
def server_reboot():
    if request.args.get("confirm") != "yes":
        return jsonify({"error": "confirm=yes required"}), 400
    mode = request.args.get("mode", "reboot")   # reboot|poweroff
    cmd = ["systemctl", "poweroff"] if mode == "poweroff" else ["systemctl", "reboot"]
    try:
        subprocess.Popen(cmd)
        return jsonify({"ok": True, "mode": mode})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@admin.route("/admin/server/config", methods=["GET", "POST"])
def server_config():
    if request.method == "GET":
        cfg = json.load(open(CFG_PATH))
        cfg.pop("gateway_token", None)
        cfg.get("alerts", {}).pop("telegram_token", None)
        return jsonify(cfg)
    # POST: shallow-merge whitelisted keys only
    body = request.get_json(force=True, silent=True) or {}
    allow = set(CFG.get("editable_config_keys",
                        ["health_sample_s", "supervisor_poll_s", "supervisor_miss_limit",
                         "netscan_interval_s", "watch_services"]))
    cfg = json.load(open(CFG_PATH))
    changed = {}
    for k, v in body.items():
        if k in allow:
            cfg[k] = v
            changed[k] = v
    json.dump(cfg, open(CFG_PATH, "w"), indent=2, ensure_ascii=False)
    return jsonify({"ok": True, "changed": changed})


# ---------------- BOARD REGISTRY + USB (tasks 1,6) ----------------
@admin.route("/admin/boards")
def boards():
    return jsonify(db.rows_to_dicts(db.q("SELECT * FROM boards ORDER BY id")))


@admin.route("/admin/boards/discover", methods=["POST"])
def boards_discover():
    return jsonify({"serial": usbctl.list_serial(), "registered": boardproxy.discover_and_register()})


@admin.route("/admin/boards/<int:bid>/reboot", methods=["POST"])
def board_reboot(bid):
    b = db.one("SELECT * FROM boards WHERE id=?", (bid,))
    if not b:
        return jsonify({"error": "no board"}), 404
    if b["transport"] == "usb" and b["serial_port"]:
        return jsonify(usbctl.force_reboot(b["serial_port"], b["baud"] or 115200, b["kind"] or "esp32"))
    # wifi board -> soft reboot via HTTP proxy path
    return jsonify(_wifi_cmd(b, "POST", "/api/cmd", {"reboot": True}))


@admin.route("/board/<int:bid>/", defaults={"path": ""}, methods=["GET", "POST"])
@admin.route("/board/<int:bid>/<path:path>", methods=["GET", "POST"])
def board_route(bid, path):
    """Task 6: App -> Server -> Board. USB boards bridged over serial; else HTTP."""
    b = db.one("SELECT * FROM boards WHERE id=?", (bid,))
    if not b:
        return jsonify({"error": "no board"}), 404
    body = request.get_json(silent=True) if request.method == "POST" else None
    full = "/" + path + (("?" + request.query_string.decode()) if request.query_string else "")
    if b["transport"] == "usb":
        r = boardproxy.proxy_request(bid, request.method, full, body)
        status = r.get("_status", r.get("status", 200))
        return jsonify(r.get("json", r)), status
    return jsonify(_wifi_cmd(b, request.method, full, body))


def _wifi_cmd(board, method, path, body):
    try:
        import requests
        url = (board["url"] or CFG["board_url"]) + path
        if method == "POST":
            r = requests.post(url, data=json.dumps(body or {}),
                              headers={"Content-Type": "text/plain"}, timeout=15)
        else:
            r = requests.get(url, timeout=15)
        try:
            return r.json()
        except Exception:
            return {"text": r.text, "status": r.status_code}
    except Exception as e:
        return {"error": str(e)}


# ---------------- NETWORK/DEVICE LIBRARY (task 5) ----------------
@admin.route("/lib/networks")
def lib_networks():
    return jsonify(profiles.list_networks())


@admin.route("/lib/networks/<int:nid>")
def lib_network(nid):
    return jsonify(profiles.network_profile(nid))


@admin.route("/lib/devices")
def lib_devices():
    return jsonify(profiles.list_devices())


@admin.route("/lib/devices/<int:did>")
def lib_device(did):
    return jsonify(profiles.device_profile(did))


@admin.route("/lib/devices/<int:did>/trust", methods=["POST"])
def lib_trust(did):
    profiles.set_trust(did, request.args.get("trust", "unknown"))
    return jsonify({"ok": True})


@admin.route("/lib/target", methods=["POST"])
def lib_target():
    kind = request.args.get("kind", "device")
    ref = int(request.args.get("id", "0"))
    label = request.args.get("label", "")
    db.run("UPDATE targets SET active=0 WHERE active=1")
    tid = db.run("INSERT INTO targets(kind,ref_id,label,created_at,active) VALUES(?,?,?,?,1)",
                 (kind, ref, label, db.now()))
    return jsonify({"ok": True, "target_id": tid})


# ---------------- PHONE WIFI EXPORT (task 7) ----------------
@admin.route("/admin/phone/wifi/import", methods=["POST"])
def phone_wifi_import():
    """Body: {"device":"phone-x","networks":[{"ssid","psk","encryption"}...]}"""
    body = request.get_json(force=True, silent=True) or {}
    nets = body.get("networks", [])
    src = body.get("device", "phone")
    n = 0
    for w in nets:
        ssid = (w.get("ssid") or "").strip()
        if not ssid:
            continue
        db.run("""INSERT INTO phone_wifi(ssid,psk,encryption,source_device,imported_at)
                  VALUES(?,?,?,?,?) ON CONFLICT(ssid) DO UPDATE SET psk=excluded.psk,
                  encryption=excluded.encryption, source_device=excluded.source_device,
                  imported_at=excluded.imported_at""",
               (ssid, w.get("psk", ""), w.get("encryption", ""), src, db.now()))
        # cross-link into the network Library as a saved/known net
        profiles.upsert_network(ssid, "", encryption=w.get("encryption"), saved=1)
        n += 1
    return jsonify({"ok": True, "imported": n})


@admin.route("/admin/phone/wifi")
def phone_wifi_list():
    rows = db.q("SELECT ssid,encryption,source_device,imported_at FROM phone_wifi ORDER BY imported_at DESC")
    return jsonify(db.rows_to_dicts(rows))    # psk withheld from listing
