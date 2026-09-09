"""
ESP32-OS secure gateway — міст «телефон (звідусіль) -> плата (домашня LAN)».

Архітектура (рішення юзера): Tailscale/WireGuard mesh + цей шлюз на домашньому Linux-девайсі.
  phone  --[Tailscale WireGuard, шифровано]-->  gateway (цей сервіс)  --[LAN]-->  board :80

БЕЗПЕКА (кілька шарів, «запаритись з обох сторін»):
  1. ТРАНСПОРТ: Tailscale (WireGuard) шифрує канал і пускає ЛИШЕ пристрої твого tailnet.
     ESP32 крипто НЕ робить (mbedTLS йому важкий) — усе шифрування тут/у mesh.
  2. ТОКЕН: кожен запит мусить нести X-Gateway-Token (довгий випадковий секрет) —
     захист навіть якщо чужий пристрій потрапив у tailnet. Порівняння constant-time.
  3. PIN ПЛАТИ тримається ТУТ (у config), у мережу до телефона НЕ виходить — шлюз сам
     автентифікується до плати й тримає сесію (ре-логін на 401).
  4. BIND лише на Tailscale-інтерфейс (100.x.y.z) -> недосяжний з відкритого інтернету/LAN.
  5. Rate-limit простий (захист від brute).

Плата лишає свій PIN-бар'єр як 2-й фактор; веб-хости (index.html) можна вимкнути на платі,
коли керуєш лише з телефона — REST/WS лишаються для шлюзу й ПК-браузера.

Запуск на Linux-сервері:
  pip install flask requests
  cp config.example.json config.json  # заповнити board_url/board_pin/gateway_token/bind_host
  python gateway.py
"""
from __future__ import annotations
import hmac, json, os, time, threading
from flask import Flask, request, Response

CFG_PATH = os.path.join(os.path.dirname(__file__), "config.json")
CFG = json.load(open(CFG_PATH))

import requests  # noqa: E402

app = Flask(__name__)
_authed = False
_lock = threading.Lock()

# --- простий rate-limit (вікно) ---
_hits: dict[str, list[float]] = {}
def rate_ok(ip: str, limit: int = 120, window: float = 60.0) -> bool:
    now = time.time()
    lst = [t for t in _hits.get(ip, []) if now - t < window]
    lst.append(now); _hits[ip] = lst
    return len(lst) <= limit

def board_login() -> bool:
    global _authed
    try:
        r = requests.post(f"{CFG['board_url']}/api/login",
                          data=json.dumps({"pin": CFG["board_pin"]}),
                          headers={"Content-Type": "text/plain"}, timeout=6)
        _authed = bool(r.json().get("ok"))
    except Exception:
        _authed = False
    return _authed

def token_ok() -> bool:
    return hmac.compare_digest(request.headers.get("X-Gateway-Token", ""), CFG["gateway_token"])

@app.route("/", defaults={"path": ""}, methods=["GET", "POST"])
@app.route("/<path:path>", methods=["GET", "POST"])
def proxy(path):
    ip = request.remote_addr or "?"
    if not rate_ok(ip):
        return Response("rate limited", 429)
    if not token_ok():
        return Response("unauthorized", 401)

    url = f"{CFG['board_url']}/{path}"
    if request.query_string:
        url += "?" + request.query_string.decode()

    with _lock:
        for _ in range(2):
            try:
                if request.method == "POST":
                    r = requests.post(url, data=request.get_data(),
                                      headers={"Content-Type": "text/plain"}, timeout=15)
                else:
                    r = requests.get(url, timeout=15)
            except Exception as e:
                return Response(f"board unreachable: {e}", 502)
            if r.status_code == 401:            # сесія плати впала -> ре-логін і повтор
                if not board_login():
                    return Response("board auth failed", 502)
                continue
            break
    return Response(r.content, r.status_code,
                    content_type=r.headers.get("Content-Type", "application/json"))

@app.route("/_gw/health", methods=["GET"])
def health():
    # healthcheck без токена — лише факт, що шлюз живий і чи авторизований на платі
    return {"gateway": "ok", "board_authed": _authed}

if __name__ == "__main__":
    board_login()
    app.run(host=CFG.get("bind_host", "127.0.0.1"), port=int(CFG.get("bind_port", 8443)))
