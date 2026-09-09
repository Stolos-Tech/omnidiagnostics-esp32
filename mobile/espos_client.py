"""
espos_client — референс-клієнт board-API ESP32·OS (red-team).

Це executable-специфікація для майбутнього мобільного додатка (iOS/Android). Структура
свідомо дзеркалить архітектуру Flipper Android `bridge:connection`:

    Transport (HTTP/WS)  ->  Session (auth)  ->  Feature methods (typed endpoints)

4-таб модель Flipper мапиться на методи:
    DEVICE   -> version() / status() / mirror() / cmd()      (стрім екрана + керування)
    ARCHIVE  -> archive() / report()                          (збережене за категоріями)
    APPS     -> scripts()                                     (Berry .be = FAP-аналог)
    TOOLS    -> subghz_spectrum() / nrf_spectrum() / sd_*()   (аналізатори/сховище)

Порт на Kotlin = 1:1: Transport -> OkHttp, Session -> той самий PIN-flow, методи -> suspend fun.
Транспорт тут HTTP:80 (REST). WS:81 (стрім) — опційно; мобілка може так само поллити mirror.
"""
from __future__ import annotations
import json
import time
import urllib.request
import urllib.error
from dataclasses import dataclass
from typing import Any, Optional


# ---- Transport: тонкий шар над HTTP (Kotlin -> OkHttp/Ktor) --------------------
class Transport:
    def __init__(self, host: str, timeout: float = 8.0):
        self.base = f"http://{host}"
        self.timeout = timeout

    def get(self, path: str) -> Any:
        return self._req("GET", path)

    def post(self, path: str, body: Optional[dict] = None) -> Any:
        # ВАЖЛИВО: тіло йде як text/plain (плата читає arg("plain")); form-urlencoded не пройде.
        data = json.dumps(body).encode() if body is not None else b""
        return self._req("POST", path, data)

    def _req(self, method: str, path: str, data: bytes = b"") -> Any:
        req = urllib.request.Request(self.base + path, data=data if method == "POST" else None,
                                     method=method)
        if method == "POST":
            req.add_header("Content-Type", "text/plain")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                raw = r.read().decode("utf-8", "replace")
                status = r.status
        except urllib.error.HTTPError as e:
            return {"_http": e.code, "_error": e.reason}
        except Exception as e:  # noqa: BLE001
            return {"_error": str(e)}
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return {"_text": raw, "_http": status}


# ---- Data models (Kotlin -> data class) ---------------------------------------
@dataclass
class ArchiveItem:
    name: str
    cat: str


# ---- Client: Session + feature methods ----------------------------------------
class EspOsClient:
    def __init__(self, host: str = "192.168.50.53"):
        self.t = Transport(host)
        self._authed = False

    # --- Session ---
    def login(self, pin: str) -> bool:
        r = self.t.post("/api/login", {"pin": pin})
        self._authed = bool(isinstance(r, dict) and r.get("ok"))
        return self._authed

    @property
    def authed(self) -> bool:
        return self._authed

    # --- DEVICE tab ---
    def version(self) -> dict:
        return self.t.get("/api/version")

    def status(self) -> dict:
        return self.t.get("/api/status")

    def stats(self) -> dict:
        return self.t.get("/api/stats")

    def mirror(self) -> dict:
        """[state, status] -> {'page':..,'items':[..],'status':{..}} злитий."""
        arr = self.t.get("/api/mirror")
        out: dict = {}
        if isinstance(arr, list):
            for x in arr:
                if isinstance(x, dict) and "items" in x:
                    out.update(x)
                elif isinstance(x, dict) and "status" in x:
                    out["status"] = x["status"]
        return out

    def cmd(self, *, btn: str = None, idx: int = None, back: bool = None, text: str = None,
            value: str = None) -> dict:
        body: dict = {}
        if btn is not None:  body["btn"] = btn
        if idx is not None:  body["idx"] = idx
        if back:             body["back"] = True
        if text is not None: body["text"] = text; body["value"] = value or ""
        return self.t.post("/api/cmd", body)

    # --- ARCHIVE tab (typed, per Flipper FlipperKeyType) ---
    def archive(self) -> list[ArchiveItem]:
        r = self.t.get("/api/archive")
        items = r.get("items", []) if isinstance(r, dict) else []
        return [ArchiveItem(i.get("name", ""), i.get("cat", "misc")) for i in items]

    def report(self, name: str) -> str:
        r = self.t.get(f"/reports/get?f={name}")
        return r.get("_text", "") if isinstance(r, dict) else str(r)

    def save_report(self, name: str, content: str) -> Any:
        # /reports/save?name= з text/plain тілом = вміст (PIN-gated).
        req = urllib.request.Request(self.t.base + f"/reports/save?name={name}",
                                     data=content.encode(), method="POST")
        req.add_header("Content-Type", "text/plain")
        try:
            with urllib.request.urlopen(req, timeout=self.t.timeout) as r:
                return r.read().decode("utf-8", "replace")
        except Exception as e:  # noqa: BLE001
            return f"error: {e}"

    # --- TOOLS tab ---
    def subghz_spectrum(self) -> dict:
        return self.t.get("/api/subghz/spectrum")

    def nrf_spectrum(self) -> dict:
        return self.t.get("/api/nrf/spectrum")

    def sd_status(self) -> dict:
        return self.t.get("/api/sd")

    def sd_mount(self) -> dict:
        return self.t.post("/sd/mount")

    # --- APPS tab (Berry scripts = FAP) ---
    def scripts(self) -> list[dict]:
        r = self.t.get("/fs/list")
        return r.get("files", []) if isinstance(r, dict) else []

    # --- Settings ---
    def themes(self) -> dict:
        return self.t.get("/theme")

    def set_theme(self, i: int) -> dict:
        return self.t.post(f"/theme?i={i}")
