"""Реальний клієнт плати ESP32-OS по USB-серіалу (COM5) — протокол REQ/RES.
БЕЗ вигаданих даних: усі payload'и беруться з живої плати. Використовується тест-харнесом
та стрес-лупом. Порт відкривається -> DTR ресетить ESP32 (реальний reboot — самі використовуємо
це як fault-injection). Тримаємо одне з'єднання й реюзаємо.
"""
from __future__ import annotations
import json, time, os
try:
    import serial
except Exception:
    serial = None

PORT = os.environ.get("ESPOS_PORT", "COM5")
BAUD = 115200
PIN = os.environ.get("ESPOS_PIN", "")


class BoardUnavailable(Exception):
    pass


class BoardClient:
    def __init__(self, port=PORT, baud=BAUD, settle=3.0):
        if serial is None:
            raise BoardUnavailable("pyserial не встановлено")
        try:
            self.s = serial.Serial(port, baud, timeout=2)
        except Exception as e:
            raise BoardUnavailable(f"не відкрити {port}: {e}")
        self.port = port
        self._id = 0
        time.sleep(settle)                # DTR-reset ESP32 -> чекаємо boot
        self.s.reset_input_buffer()
        self.authed = False

    def _req(self, line: str, wait: float = 20.0) -> str:
        self.s.write((line + "\n").encode())
        self.s.flush()
        t0 = time.time()
        while time.time() - t0 < wait:
            l = self.s.readline().decode(errors="replace").strip()
            if l.startswith("RES "):
                return l
        return "(no RES)"

    def login(self, pin=PIN, tries=3) -> bool:
        for _ in range(tries):
            r = self._req(f'REQ 1 POST /api/login {{"pin":"{pin}"}}', 10)
            if "200" in r:
                self.authed = True
                return True
            time.sleep(1.5)
        return False

    def get(self, path: str, wait: float = 20.0):
        """GET -> (status_code, json|None, raw). Реальна відповідь плати."""
        self._id += 1
        r = self._req(f"REQ {self._id} GET {path}", wait)
        p = r.split(" ", 3)
        if len(p) == 4 and p[2].isdigit():
            code = int(p[2])
            try:
                return code, json.loads(p[3]), p[3]
            except Exception:
                return code, None, p[3]
        return 0, None, r

    def post(self, path: str, body: str = "", wait: float = 20.0):
        self._id += 1
        r = self._req(f"REQ {self._id} POST {path} {body}", wait)
        p = r.split(" ", 3)
        if len(p) >= 3 and p[2].isdigit():
            code = int(p[2])
            payload = p[3] if len(p) == 4 else ""
            try:
                return code, json.loads(payload) if payload else None, payload
            except Exception:
                return code, None, payload
        return 0, None, r

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


class HttpBoardClient:
    """Реальна плата по WiFi HTTP (той самий шлях, що й додаток у WIFI-режимі). Використовується,
    коли USB зайнятий телефоном. Дані так само РЕАЛЬНІ — не вигадані."""
    def __init__(self, base):
        import requests
        self.base = base.rstrip("/")
        self.s = requests.Session()
        self.authed = False
        # ping
        r = self.s.get(f"{self.base}/api/status", timeout=6)
        r.raise_for_status()

    def login(self, pin=PIN, tries=3):
        for _ in range(tries):
            try:
                r = self.s.post(f"{self.base}/api/login", data=json.dumps({"pin": pin}),
                                headers={"Content-Type": "text/plain"}, timeout=8)
                if r.status_code == 200 and r.json().get("ok"):
                    self.authed = True
                    return True
            except Exception:
                pass
            time.sleep(1.0)
        return False

    def get(self, path, wait=25):
        try:
            r = self.s.get(f"{self.base}{path}", timeout=wait)
            try:
                return r.status_code, r.json(), r.text
            except Exception:
                return r.status_code, None, r.text
        except Exception as e:
            return 0, None, str(e)

    def post(self, path, body="", wait=25):
        try:
            r = self.s.post(f"{self.base}{path}", data=body,
                            headers={"Content-Type": "text/plain"}, timeout=wait)
            try:
                return r.status_code, (r.json() if r.text else None), r.text
            except Exception:
                return r.status_code, None, r.text
        except Exception as e:
            return 0, None, str(e)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


BOARD_HTTP = os.environ.get("ESPOS_BOARD_URL", "http://192.168.100.41")


def try_board(**kw):
    """Реальна плата: спершу USB COM5 (реалтайм), інакше WiFi HTTP (плата на телефоні).
    None — жоден транспорт недоступний."""
    try:
        return BoardClient(**kw)
    except BoardUnavailable:
        pass
    try:
        return HttpBoardClient(BOARD_HTTP)
    except Exception:
        return None
