"""
dashboard — веб-фронт СЕРВЕРА (APEX): жива панель плати + графіки історії з SQLite.
Доступний у tailnet (bind на Tailscale IP) з телефона/ПК-браузера. Розвантажує плату:
історію тримає сервер, важкий рендер — у браузері.

Ендпоінти:
  GET /                  -> dashboard/index.html
  GET /api/history?h=6   -> телеметрія за останні h годин (для графіків)
  GET /api/live          -> поточні sysinfo+status (проксі до плати)
systemd-сервіс espos-dashboard. Конфіг — server/config.json.
"""
from __future__ import annotations
import json, os, sqlite3, time, hashlib, gzip
from flask import Flask, jsonify, request, send_from_directory
import requests
import insights
import srvstats
import netwatch
import devintel

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
DB = CFG.get("db_path", os.path.join(HERE, "telemetry.db"))
BOARD = CFG["board_url"]

app = Flask(__name__, static_folder=os.path.join(HERE, "dashboard"))

# Admin / Library / board-routing / phone-export / health API (tasks 3,5,6,7,9)
try:
    import db as _db
    from admin_api import admin as _admin_bp
    _db.init()
    app.register_blueprint(_admin_bp)
except Exception as _e:
    print("[dashboard] admin_api not mounted:", _e)

# Honeytokens: decoy-ендпоінти + honey-cred детект (лише детект, нічого не блокує).
# Спрацювання -> Telegram (дедуп) + tamper-evident audit-ланцюг (завжди).
try:
    if CFG.get("honeytokens", {}).get("enabled", False):
        import honeytokens, audit as _audit
        _adb = CFG.get("db_path", os.path.join(HERE, "telemetry.db"))
        app.register_blueprint(honeytokens.make_blueprint(
            CFG, audit_fn=lambda kind, data: _audit.append(_adb, kind, data)))
        print("[dashboard] honeytokens armed:", honeytokens.configured_paths(CFG))
except Exception as _e:
    print("[dashboard] honeytokens not mounted:", _e)

@app.route("/")
def index():
    return send_from_directory(app.static_folder, "index.html")

@app.route("/app.apk")
def app_apk():
    """OTA: віддає останній залитий APK (scp у apk_path). Телефон у tailnet качає й ставить одним тапом."""
    p = CFG.get("apk_path", "/opt/espos/ota/app.apk")
    if not os.path.exists(p):
        return "APK ще не завантажено (scp -> apk_path)", 404
    d, f = os.path.split(p)
    return send_from_directory(d, f, as_attachment=True, download_name="espos-app.apk",
                               mimetype="application/vnd.android.package-archive")

def _ota_dir():
    return os.path.dirname(CFG.get("apk_path", "/opt/espos/ota/app.apk"))

@app.route("/api/manifest")
def manifest():
    """Маніфест оновлення: {versionCode, versionName, sha256, size, notes}. Додаток звіряє
    versionCode і ПЕРЕД встановленням перевіряє sha256 завантаженого APK."""
    mp = os.path.join(_ota_dir(), "manifest.json")
    if not os.path.exists(mp):
        return jsonify({}), 404
    try:
        return jsonify(json.load(open(mp)))
    except Exception:
        return jsonify({}), 500

# ── Прошивка ПЛАТИ (USB-флеш із телефона) ────────────────────────────────────
def _fw_path():
    return CFG.get("firmware_path", "/opt/espos/ota/firmware.bin")

@app.route("/firmware.bin")
def firmware_bin():
    """Сирий app-образ ESP32 (те, що я пушу). Телефон качає й пише його по USB-OTG
    напряму в app-партицію (без dual-OTA). offset за замовч. 0x10000."""
    p = _fw_path()
    if not os.path.exists(p):
        return "firmware.bin ще не завантажено (scp -> firmware_path)", 404
    d, f = os.path.split(p)
    return send_from_directory(d, f, as_attachment=True, download_name="firmware.bin",
                               mimetype="application/octet-stream")

@app.route("/api/firmware")
def firmware_manifest():
    """Маніфест прошивки плати: {versionName, sha256, size, notes, offset}. Якщо поруч
    є firmware.json — беремо version/notes звідти; sha256+size рахуємо з файлу (істина)."""
    p = _fw_path()
    if not os.path.exists(p):
        return jsonify({}), 404
    meta = {}
    mj = os.path.join(os.path.dirname(p), "firmware.json")
    if os.path.exists(mj):
        try: meta = json.load(open(mj))
        except Exception: meta = {}
    return jsonify({
        "versionName": meta.get("versionName", time.strftime("%Y%m%d", time.localtime(os.path.getmtime(p)))),
        "sha256": _fw_sha256(p),
        "size": os.path.getsize(p),
        "notes": meta.get("notes", ""),
        "offset": int(meta.get("offset", 0x10000)),
        "tests": meta.get("tests", []),   # тести оновлення (видаляються після успіху)
    })

# Кеш sha256 firmware за (шлях, mtime, розмір) — не перечитуємо 2.2МБ на кожен /api/firmware.
_FW_SHA_CACHE = {}
def _fw_sha256(p):
    key = (p, os.path.getmtime(p), os.path.getsize(p))
    if _FW_SHA_CACHE.get("key") == key:
        return _FW_SHA_CACHE["sha"]
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    _FW_SHA_CACHE["key"], _FW_SHA_CACHE["sha"] = key, h.hexdigest()
    return _FW_SHA_CACHE["sha"]

# ── Часова стрічка мережі (netlog): хто/коли зайшов/вийшов ───────────────────
def _netlog_db():
    return CFG.get("db_path", os.path.join(HERE, "telemetry.db"))

@app.route("/api/netlog/events")
def netlog_events():
    """Події join/leave: ?since=unixts&limit=N (новіші перші)."""
    try:
        import netlog
        since = int(request.args.get("since", 0))
        limit = min(int(request.args.get("limit", 500)), 2000)
        return jsonify(netlog.events(_netlog_db(), since, limit))
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/netlog/state")
def netlog_state():
    """Поточний стан пристроїв: online + first_seen/last_seen/last_change."""
    try:
        import netlog
        return jsonify(netlog.state(_netlog_db()))
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/netlog/purge", methods=["POST"])
def netlog_purge():
    try:
        import netlog
        d = request.get_json(force=True, silent=True) or {}
        n = netlog.purge(_netlog_db(), int(d.get("days", 0)))
        return jsonify({"ok": True, "removed": n})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route("/api/netlog/rebuild", methods=["POST"])
def netlog_rebuild():
    """RECOVERABILITY: перебудувати read-модель net_state з довіреного журналу подій net_events."""
    try:
        import netlog
        return jsonify({"ok": True, **netlog.rebuild_state(_netlog_db())})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

# ── Tamper-evident audit (hash-chain безпекових подій) ───────────────────────
@app.route("/api/audit/verify")
def audit_verify():
    """Перевірити цілісність ланцюга: {ok, count, broken_at?, reason?}."""
    try:
        import audit
        return jsonify(audit.verify(_netlog_db()))
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route("/api/audit/tail")
def audit_tail():
    """Останні події ланцюга: ?limit=N&kind=honeytoken (newest-first)."""
    try:
        import audit
        limit = min(int(request.args.get("limit", 200)), 2000)
        return jsonify(audit.tail(_netlog_db(), limit, request.args.get("kind") or None))
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ── Admission control (netguard): активний дозвіл на під'єднання ─────────────
@app.route("/api/netguard/list")
def netguard_list():
    """Список допуску: ?status=pending|approved|denied (або всі)."""
    try:
        import netguard
        return jsonify(netguard.listing(_netlog_db(), request.args.get("status")))
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/netguard/set", methods=["POST"])
def netguard_set():
    """Схвалити/відхилити пристрій: {mac, status:approved|denied|pending}. Друге підтвердження юзера."""
    try:
        import netguard
        d = request.get_json(force=True, silent=True) or {}
        ok = netguard.set_status(_netlog_db(), d.get("mac", ""), d.get("status", ""), d.get("note", ""))
        return jsonify({"ok": ok})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route("/api/notify", methods=["POST"])
def notify_bot():
    """Надіслати довільний звіт/дані у Telegram-бота: {text, title?}. Короткий текст -> повідомлення,
    великий -> .txt-документ (обхід ліміту 4096). Використовує токен із CFG (сервер-сайд). Тіло до ~200КБ."""
    try:
        import alerts
        d = request.get_json(force=True, silent=True) or {}
        text = str(d.get("text", "")).strip()
        title = str(d.get("title", "ESP32-OS")).strip() or "ESP32-OS"
        if not text:
            return jsonify({"ok": False, "error": "empty"}), 400
        if len(text) > 200000:
            text = text[:200000] + "\n…(обрізано)"
        ok = alerts.notify(CFG, text, title)
        return jsonify({"ok": ok})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route("/api/board/ingest", methods=["POST"])
def board_ingest():
    """Реле плата→сервер через телефон: додаток на USB тягне телеметрію плати й шле сюди.
    Так сервер бачить плату БЕЗ спільної мережі. Пишемо останній знімок у board_relay.json."""
    try:
        data = request.get_json(force=True, silent=True) or {}
    except Exception:
        data = {}
    data["_ts"] = int(time.time())
    data["_ip"] = request.remote_addr
    try:
        json.dump(data, open(os.path.join(HERE, "board_relay.json"), "w"))
    except Exception:
        pass
    return jsonify({"ok": True})

# ── Сесійні логи плати (прошивка/тести/сесії) — окрема тека, посесійно ──────────
def _logs_dir():
    d = os.path.join(HERE, "logs", "sessions")
    os.makedirs(d, exist_ok=True)
    return d

def _safe_sid(sid):
    # лише [A-Za-z0-9_-] — щоб не вийти з теки
    return "".join(c for c in str(sid) if c.isalnum() or c in "_-")[:64] or "session"

def _sess_paths(sid):
    """(json_path, gz_path) для сесії."""
    base = os.path.join(_logs_dir(), _safe_sid(sid) + ".json")
    return base, base + ".gz"

def _read_session(sid):
    """Прочитати сесію з .json АБО .json.gz. None якщо нема/битий."""
    p, gz = _sess_paths(sid)
    try:
        if os.path.exists(p):
            return json.load(open(p))
        if os.path.exists(gz):
            with gzip.open(gz, "rt", encoding="utf-8") as f:
                return json.load(f)
    except Exception:
        pass
    return None

def _sid_from_file(f):
    if f.endswith(".json.gz"): return f[:-8]
    if f.endswith(".json"):    return f[:-5]
    return None

def _logs_autoclean():
    """Retention: старі НЕзакріплені сесії СТИСКАЄМО в .json.gz (не видаляємо!) — економія
    місця зі збереженням історії. Закріплені (pinned) не чіпаємо. 0 днів = вимкнено."""
    days = int(CFG.get("log_retention_days", 0) or 0)
    if days <= 0:
        return 0
    cutoff = time.time() - days * 86400
    n = 0
    for f in os.listdir(_logs_dir()):
        if not f.endswith(".json"):      # тільки сирі .json (вже .gz — пропускаємо)
            continue
        p = os.path.join(_logs_dir(), f)
        try:
            if os.path.getmtime(p) >= cutoff:
                continue
            data = json.load(open(p))
            if data.get("pinned"):        # закріплені — виняток
                continue
            mt = os.path.getmtime(p)
            with gzip.open(p + ".gz", "wt", encoding="utf-8") as g:
                json.dump(data, g)
            os.utime(p + ".gz", (mt, mt))  # зберегти дату сесії
            os.remove(p)
            n += 1
        except Exception:
            pass
    return n

@app.route("/api/logs/session", methods=["POST"])
def logs_session_ingest():
    """Прийом сесійного логу з телефона: {id?, title, lines:[..], meta}. Дописує у файл
    сесії. Так сервер тримає історію прошивок/тестів/сесій по даті-часу."""
    _logs_autoclean()
    try:
        d = request.get_json(force=True, silent=True) or {}
    except Exception:
        d = {}
    sid = _safe_sid(d.get("id") or time.strftime("%Y%m%d-%H%M%S"))
    p, gz = _sess_paths(sid)
    sess = _read_session(sid) or {"id": sid, "title": d.get("title", ""), "ts": int(time.time()), "lines": []}
    # ЗАМІНА (не append): телефон шле ПОВНУ кумулятивну сесію щоразу (LogStore — джерело
    # істини). append дублював би рядки експоненційно.
    new_lines = d.get("lines")
    if isinstance(new_lines, list):
        sess["lines"] = [str(x) for x in new_lines][-2000:]
    if d.get("title"):
        sess["title"] = d["title"]
    if "pinned" in d:
        sess["pinned"] = bool(d["pinned"])
    sess["ts"] = int(time.time())
    try:
        json.dump(sess, open(p, "w"))
        if os.path.exists(gz): os.remove(gz)   # свіжий .json заступає стиснений
    except Exception: pass
    return jsonify({"ok": True, "id": sid, "lines": len(sess["lines"])})

@app.route("/api/logs/sessions")
def logs_sessions_list():
    """Список сесій: [{id, title, ts, lines, bytes, pinned, compressed}], новіші перші."""
    _logs_autoclean()
    out = []
    seen = set()
    for f in os.listdir(_logs_dir()):
        sid = _sid_from_file(f)
        if not sid or sid in seen:
            continue
        seen.add(sid)
        s = _read_session(sid)
        if not s:
            continue
        p = os.path.join(_logs_dir(), f)
        out.append({"id": s.get("id", sid), "title": s.get("title", ""),
                    "ts": s.get("ts", int(os.path.getmtime(p))),
                    "lines": len(s.get("lines", [])), "bytes": os.path.getsize(p),
                    "pinned": bool(s.get("pinned")), "compressed": f.endswith(".gz")})
    out.sort(key=lambda x: x["ts"], reverse=True)
    return jsonify(out)

@app.route("/api/logs/session/<sid>")
def logs_session_get(sid):
    s = _read_session(sid)
    if s is None:
        return jsonify({}), 404
    return jsonify(s)

@app.route("/api/logs/session/<sid>/pin", methods=["POST"])
def logs_session_pin(sid):
    """Закріпити/відкріпити сесію: {pinned:true|false}. Закріплена НЕ стискається й НЕ
    видаляється авточисткою. Якщо була стиснена — розпаковуємо назад у .json."""
    d = request.get_json(force=True, silent=True) or {}
    pinned = bool(d.get("pinned", True))
    s = _read_session(sid)
    if s is None:
        return jsonify({"ok": False, "error": "not found"}), 404
    s["pinned"] = pinned
    p, gz = _sess_paths(sid)
    try:
        json.dump(s, open(p, "w"))          # завжди тримаємо закріплену читабельною (.json)
        if os.path.exists(gz): os.remove(gz)
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    return jsonify({"ok": True, "pinned": pinned})

@app.route("/api/logs/session/<sid>/delete", methods=["POST"])
def logs_session_delete(sid):
    p, gz = _sess_paths(sid)
    ok = False
    for f in (p, gz):
        try:
            if os.path.exists(f): os.remove(f); ok = True
        except Exception:
            pass
    return jsonify({"ok": ok})

@app.route("/api/logs/clear", methods=["POST"])
def logs_clear_all():
    """Очистити ВСЕ, крім закріплених (pinned лишаються)."""
    n = 0
    for f in os.listdir(_logs_dir()):
        sid = _sid_from_file(f)
        if not sid:
            continue
        s = _read_session(sid)
        if s and s.get("pinned"):
            continue
        try: os.remove(os.path.join(_logs_dir(), f)); n += 1
        except Exception: pass
    return jsonify({"ok": True, "removed": n})

@app.route("/api/logs/retention", methods=["GET", "POST"])
def logs_retention():
    """GET -> {days}. POST {days} -> зберегти у config (0 = ніколи не чистити)."""
    if request.method == "POST":
        try:
            d = request.get_json(force=True, silent=True) or {}
            days = int(d.get("days", 0))
            CFG["log_retention_days"] = max(0, days)
            cfg_path = os.path.join(HERE, "config.json")   # АБСОЛЮТНИЙ шлях (урок: не відносний!)
            json.dump(CFG, open(cfg_path, "w"), indent=2)
        except Exception as e:
            return jsonify({"ok": False, "error": str(e)}), 400
        return jsonify({"ok": True, "days": CFG.get("log_retention_days", 0)})
    return jsonify({"days": int(CFG.get("log_retention_days", 0) or 0)})

@app.route("/api/board/relay")
def board_relay():
    """Останній знімок телеметрії плати, отриманий через телефон-реле (для дашборда/додатка)."""
    p = os.path.join(HERE, "board_relay.json")
    if not os.path.exists(p):
        return jsonify({}), 404
    try:
        d = json.load(open(p))
        d["_age_s"] = int(time.time()) - int(d.get("_ts", 0))
        return jsonify(d)
    except Exception:
        return jsonify({}), 500

@app.route("/api/firmware/tests/clear", methods=["POST"])
def firmware_tests_clear():
    """Після успішних тестів оновлення — прибрати 'tests' із firmware.json (економія місця)."""
    mj = os.path.join(os.path.dirname(_fw_path()), "firmware.json")
    if not os.path.exists(mj):
        return jsonify({"ok": True, "note": "no firmware.json"})
    try:
        m = json.load(open(mj))
        m.pop("tests", None)
        json.dump(m, open(mj, "w"), indent=2)
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    return jsonify({"ok": True})

@app.route("/api/health", methods=["GET", "POST"])
def health():
    """Health-beacon: додаток при успішному старті шле ?vc=<versionCode> -> фіксуємо
    'остання жива версія'. Якщо білд встановлено, але beacon не прийшов -> він битий."""
    hp = os.path.join(HERE, "health.json")   # /opt/espos (пише espos), не ota-дира
    data = {}
    if os.path.exists(hp):
        try: data = json.load(open(hp))
        except Exception: data = {}
    vc = request.args.get("vc")
    if vc and vc.isdigit():
        data["last_good_vc"] = int(vc)
        data["last_seen"] = int(time.time())
        try: json.dump(data, open(hp, "w"))
        except Exception: pass
    return jsonify(data)

@app.route("/ota")
def ota():
    """Проста сторінка встановлення (телефон відкриває через Tailscale)."""
    p = CFG.get("apk_path", "/opt/espos/ota/app.apk")
    ok = os.path.exists(p)
    mt = time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(p))) if ok else "—"
    return (f"<html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
            f"<title>ESP32·OS OTA</title><style>body{{background:#0B0D0F;color:#E7EEF0;font-family:monospace;"
            f"text-align:center;padding:40px}}a{{display:inline-block;margin-top:20px;padding:14px 28px;"
            f"background:#00FFAB;color:#0B0D0F;text-decoration:none;border-radius:10px;font-weight:600}}"
            f".m{{color:#7D8B8C;font-size:13px}}</style></head><body><h2>ESP32·OS · OTA</h2>"
            f"<p class=m>останній APK: {mt}</p>"
            + (f"<a href='/app.apk'>⬇ Встановити застосунок</a>" if ok else "<p>APK ще не залито</p>")
            + "<p class=m>після завантаження — тапни файл, дозволь встановлення</p></body></html>")

@app.route("/api/history")
def history():
    hours = float(request.args.get("h", 6))
    since = int(time.time() - hours * 3600)
    conn = sqlite3.connect(DB)
    rows = conn.execute(
        "SELECT ts,temp_c,power_ma,batt_mv,rssi FROM telemetry WHERE ts>=? ORDER BY ts", (since,)
    ).fetchall()
    conn.close()
    return jsonify([
        {"ts": r[0], "temp": r[1], "power": r[2], "mv": r[3], "rssi": r[4]} for r in rows
    ])

def _has_name_col(conn):
    try: conn.execute("ALTER TABLE devices ADD COLUMN name TEXT")   # ідемпотентна міграція
    except sqlite3.OperationalError: pass

@app.route("/api/devices")
def devices():
    """Інвентар мережі (netwatch): пристрої, вік, ім'я, online-статус, прапорець нового."""
    conn = sqlite3.connect(DB)
    rows = []
    try:
        netwatch.ensure_table(conn)
        rows = conn.execute("SELECT mac, ip, vendor, first_seen, last_seen, is_gw, name, "
                            "hostname, category, intel_at FROM devices ORDER BY last_seen DESC").fetchall()
    except sqlite3.OperationalError:
        rows = []
    conn.close()
    now = int(time.time())
    return jsonify([{"mac": r[0], "ip": r[1], "vendor": r[2], "first_seen": r[3],
                     "last_seen": r[4], "gw": bool(r[5]), "name": r[6] or "",
                     "hostname": r[7] or "", "category": r[8] or "", "intel_at": r[9] or 0,
                     "age_s": now - r[4], "online": (now - r[4]) < 900,
                     "new": (now - r[3]) < 86400} for r in rows])

@app.route("/api/device")
def device_detail():
    """Повна розвідка одного пристрою (кеш із БД). ?mac=.."""
    mac = (request.args.get("mac") or "").upper()
    conn = sqlite3.connect(DB)
    netwatch.ensure_table(conn)
    row = conn.execute("SELECT mac, ip, vendor, first_seen, last_seen, is_gw, name, hostname, "
                       "category, intel_at FROM devices WHERE mac=?", (mac,)).fetchone()
    intel = netwatch.get_intel(conn, mac)
    conn.close()
    if not row:
        return jsonify({"error": "not found"}), 404
    return jsonify({"mac": row[0], "ip": row[1], "vendor": row[2], "first_seen": row[3],
                    "last_seen": row[4], "gw": bool(row[5]), "name": row[6] or "",
                    "hostname": row[7] or "", "category": row[8] or "", "intel_at": row[9] or 0,
                    "intel": intel})

@app.route("/api/device/scan", methods=["POST", "GET"])
def device_scan():
    """Запустити глибоку розвідку зараз: ?mac=..&ip=.. -> devintel.intel(), збереження, повернення."""
    mac = (request.args.get("mac") or "").upper()
    ip = request.args.get("ip") or ""
    if not ip:
        conn = sqlite3.connect(DB)
        r = conn.execute("SELECT ip, vendor FROM devices WHERE mac=?", (mac,)).fetchone()
        conn.close()
        if r:
            ip = r[0]
    if not ip:
        return jsonify({"error": "no ip"}), 400
    info = devintel.intel(ip, mac)
    conn = sqlite3.connect(DB)
    netwatch.ensure_table(conn)
    netwatch.store_intel(conn, mac, info)
    conn.close()
    return jsonify(info)

@app.route("/api/device/name", methods=["POST", "GET"])
def device_name():
    """Назвати пристрій: ?mac=..&name=.. -> зберігає у devices.name."""
    mac = (request.args.get("mac") or "").upper()
    if not mac:
        return jsonify({"ok": False}), 400
    conn = sqlite3.connect(DB)
    _has_name_col(conn)
    conn.execute("UPDATE devices SET name=? WHERE mac=?", (request.args.get("name") or "", mac))
    conn.commit(); conn.close()
    return jsonify({"ok": True})

@app.route("/api/server")
def server_stats():
    """Ресурси stolos: CPU-load, RAM, диск, темп, uptime, стан сервісів."""
    return jsonify(srvstats.stats())

_ALERT_KEYS = {"temp_crit_c", "batt_low_mv", "rssi_low", "heap_low", "offline_s", "cooldown_s",
               "alert_random_mac", "enabled"}
_TOP_KEYS = {"netscan_interval_s", "collector_interval_s"}

@app.route("/api/settings", methods=["GET", "POST"])
def settings_route():
    """Перегляд/редагування налаштувань вартового (пороги/інтервали). Bot-token НЕ віддаємо
    й не редагуємо через web. Зміни підхоплює collector гаряче (без рестарту)."""
    cpath = os.path.join(HERE, "config.json")
    cfg = json.load(open(cpath))
    if request.method == "POST":
        a = cfg.setdefault("alerts", {})
        for k, v in request.args.items():
            if k in _TOP_KEYS:
                try: cfg[k] = int(v)
                except ValueError: pass
            elif k in _ALERT_KEYS:
                if k in ("alert_random_mac", "enabled"): a[k] = v in ("1", "true", "True")
                else:
                    try: a[k] = int(v)
                    except ValueError: pass
        json.dump(cfg, open(cpath, "w"), indent=2)   # решта ключів (token/pin/url) збережено як є
    a = cfg.get("alerts", {})
    tok = str(a.get("telegram_token", ""))
    return jsonify({
        "netscan_interval_s": cfg.get("netscan_interval_s"),
        "collector_interval_s": cfg.get("collector_interval_s"),
        "alerts": {k: a.get(k) for k in _ALERT_KEYS},
        "telegram_configured": bool(tok) and "PUT-" not in tok,   # булеве, БЕЗ самого токена
    })

@app.route("/api/insights")
def insights_route():
    """Тренд-аналітика телеметрії за h годин: батарея/розряд/ETA, темп, споживання."""
    hours = float(request.args.get("h", 24))
    since = int(time.time() - hours * 3600)
    conn = sqlite3.connect(DB)
    rows = conn.execute("SELECT ts,temp_c,power_ma,batt_mv,rssi FROM telemetry WHERE ts>=? ORDER BY ts",
                        (since,)).fetchall()
    conn.close()
    data = [{"ts": r[0], "temp": r[1], "power": r[2], "mv": r[3], "rssi": r[4]} for r in rows]
    return jsonify(insights.insights(data))

_authed = False
def login():
    global _authed
    try:
        r = requests.post(f"{BOARD}/api/login", data=json.dumps({"pin": CFG['board_pin']}),
                          headers={"Content-Type": "text/plain"}, timeout=6)
        _authed = bool(r.json().get("ok"))
    except Exception:
        _authed = False

@app.route("/api/modules")
def modules():
    """Автодетект модулів обох плат (проксі до board /api/modules; логінимось при 401)."""
    try:
        r = requests.get(f"{BOARD}/api/modules", timeout=12)
        if r.status_code == 401:
            login(); r = requests.get(f"{BOARD}/api/modules", timeout=12)
        return jsonify(r.json())
    except Exception as e:
        return jsonify({"error": str(e)}), 502

@app.route("/api/live")
def live():
    try:
        si = requests.get(f"{BOARD}/api/sysinfo", timeout=6).json()
        st = requests.get(f"{BOARD}/api/status", timeout=6).json()
    except Exception as e:
        return jsonify({"error": str(e)}), 502
    return jsonify({"sysinfo": si, "status": st})

if __name__ == "__main__":
    login()
    app.run(host=CFG.get("bind_host", "127.0.0.1"), port=int(CFG.get("dashboard_port", 8080)))
