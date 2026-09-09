"""
honeytokens — дешева детекція компрометації: приманки, до яких легітимний код НІКОЛИ
не звертається. Будь-яке звернення = сигнал розвідки/зламу з near-zero false-positive.

Два типи приманок:
  1. Decoy-ендпоінти — спокусливі шляхи (/api/backup, /.env, /wp-login.php…), яких
     немає в реальному API. Хіт = хтось сканує сервер. Віддаємо звичайний 404 (не
     видаємо пастку), але логуємо + алертимо.
  2. Honey-credential — фейковий токен-приманка. Якщо він з'явився в аргументах чи
     auth-заголовках БУДЬ-ЯКОГО запиту — хтось його знайшов і пробує → алерт.

Чиста логіка (evaluate_request) тестується без Flask/мережі. Алерти йдуть через
наявний alerts.notify (AlertGate-дедуп на key+IP). Нічого не блокуємо — лише детект.
Вимикач/налаштування — config["honeytokens"]. Пов'язано: alerts, netguard.
"""
from __future__ import annotations
import json, os, time

import alerts

_SENSITIVE_HEADERS = ("Authorization", "X-Auth-Token", "X-Gateway-Token",
                      "X-Api-Key", "X-Pin", "Cookie")
_MIN_TOKEN_LEN = 8   # коротший «токен» дав би фолс-позитиви на випадкових збігах


def default_paths() -> list[str]:
    """Приманки-шляхи за замовчуванням (жоден не перетинається з реальними роутами)."""
    return ["/api/backup", "/api/export", "/api/keys", "/api/secrets",
            "/api/config/dump", "/api/users", "/.env", "/.git/config",
            "/wp-login.php", "/phpmyadmin"]


def configured_paths(cfg: dict) -> list[str]:
    h = cfg.get("honeytokens", {}) or {}
    p = h.get("paths")
    return [str(x) for x in p] if p else default_paths()


def _norm(path: str) -> str:
    return ("/" + str(path or "").strip().lstrip("/")).rstrip("/").lower() or "/"


def evaluate_request(path: str, args, headers, cfg: dict) -> tuple[str, str] | None:
    """Чиста функція: чи є запит спрацюванням приманки. -> (key, detail) або None.
    args/headers — будь-що з .values()/.get() (dict, werkzeug MultiDict/Headers)."""
    h = cfg.get("honeytokens", {}) or {}
    if not h.get("enabled", False):
        return None

    # 1) honey-credential у значеннях аргументів або чутливих заголовках
    token = str(h.get("token", "")).strip()
    if len(token) >= _MIN_TOKEN_LEN:
        hay = []
        try:
            hay.extend(str(v) for v in args.values())
        except Exception:
            pass
        for k in _SENSITIVE_HEADERS:
            try:
                v = headers.get(k, "")
            except Exception:
                v = ""
            if v:
                hay.append(str(v))
        if any(token in v for v in hay):
            return ("honey_cred", "приманка-креденшел пред'явлено")

    # 2) decoy-шлях
    p = _norm(path)
    if p in (_norm(x) for x in configured_paths(cfg)):
        return ("honey_path", f"decoy-шлях {path}")

    return None


def _log_path(cfg: dict) -> str:
    h = cfg.get("honeytokens", {}) or {}
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs", "honeytokens.log")
    return h.get("log_path", default)


def record(cfg: dict, ip: str, key: str, detail: str, method: str = "", path: str = "", ua: str = "") -> None:
    """Дописати спрацювання у власний лог (JSONL). Тихо ігнорує помилки I/O."""
    try:
        lp = _log_path(cfg)
        os.makedirs(os.path.dirname(lp), exist_ok=True)
        rec = {"ts": int(time.time()), "ip": ip, "key": key, "detail": detail,
               "method": method, "path": path, "ua": ua[:200]}
        with open(lp, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    except Exception:
        pass


def make_blueprint(cfg: dict, notify_fn=None, gate: "alerts.AlertGate | None" = None, audit_fn=None):
    """Flask-блупринт: реєструє decoy-роути + глобальний скан honey-cred. notify_fn/audit_fn
    інжектуються в тестах (за замовчуванням — alerts.notify у Telegram; audit_fn — no-op)."""
    from flask import Blueprint, request

    h = cfg.get("honeytokens", {}) or {}
    g = gate or alerts.AlertGate(int(h.get("cooldown_s", 300)))
    notify = notify_fn or (lambda text: alerts.notify(cfg, text, "HONEYTOKEN"))
    audit = audit_fn or (lambda kind, data: None)
    bp = Blueprint("honey", __name__)

    def _trip(hit: tuple[str, str]) -> None:
        key, detail = hit
        ip = (request.remote_addr or "?")
        ua = request.headers.get("User-Agent", "")
        # tamper-evident запис — ЗАВЖДИ (не залежить від дедупу нотифікацій)
        try:
            audit("honeytoken", {"key": key, "detail": detail, "ip": ip,
                                 "method": request.method, "path": request.path, "ua": ua[:200]})
        except Exception:
            pass
        if not g.ready(f"{key}:{ip}"):
            return
        msg = (f"🍯 HONEYTOKEN: {detail}\n"
               f"IP {ip} · {request.method} {request.path}\n"
               f"UA: {ua[:120]}\n{time.strftime('%Y-%m-%d %H:%M:%S')}")
        try:
            notify(msg)
        except Exception:
            pass
        record(cfg, ip, key, detail, request.method, request.path, ua)

    @bp.before_app_request
    def _scan_cred():
        # honey-cred детект на КОЖНОМУ запиті (дешево: лише args + чутливі заголовки).
        # Спрацьовує тільки коли токен реально пред'явлено -> нуль латентності на легіт-трафік.
        try:
            hit = evaluate_request(request.path, request.args, request.headers, cfg)
            if hit and hit[0] == "honey_cred":
                _trip(hit)
        except Exception:
            pass
        return None  # ніколи не блокуємо реальний запит

    def _decoy_handler(**_kw):
        from flask import abort
        try:
            _trip(("honey_path", f"decoy-шлях {request.path}"))
        except Exception:
            pass
        abort(404)  # блендимось під «шляху немає», щоб не видати пастку

    for i, pth in enumerate(configured_paths(cfg)):
        bp.add_url_rule(pth, endpoint=f"honey_decoy_{i}", view_func=_decoy_handler,
                        methods=["GET", "POST", "PUT", "DELETE", "HEAD"])
    return bp
