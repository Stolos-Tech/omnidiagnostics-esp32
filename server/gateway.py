"""
gateway — захищений проксі «телефон (tailnet) -> плата (LAN)». Частина server-стеку
(спільний config.json). Шари: Tailscale транспорт + X-Gateway-Token + PIN плати тільки
тут + bind на tailnet-IP + rate-limit. Крипто НЕ на ESP32. systemd espos-gateway.
"""
from __future__ import annotations
import hmac, json, os, threading, time
from flask import Flask, request, Response
import requests

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
app = Flask(__name__)
_authed = False
_lock = threading.Lock()
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

@app.route("/_gw/health")
def health():
    return {"gateway": "ok", "board_authed": _authed}

@app.route("/", defaults={"path": ""}, methods=["GET", "POST"])
@app.route("/<path:path>", methods=["GET", "POST"])
def proxy(path):
    if not rate_ok(request.remote_addr or "?"):
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
            if r.status_code == 401:
                if not board_login():
                    return Response("board auth failed", 502)
                continue
            break
    return Response(r.content, r.status_code,
                    content_type=r.headers.get("Content-Type", "application/json"))

if __name__ == "__main__":
    board_login()
    app.run(host=CFG.get("bind_host", "127.0.0.1"), port=int(CFG.get("gateway_port", 8443)))
