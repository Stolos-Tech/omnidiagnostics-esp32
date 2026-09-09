"""USB board control & force-reboot (task 1). Manages serial-attached boards:
port discovery, line I/O, and layered reboot escalation for frozen boards:
  1) soft  : send REBOOT command over serial
  2) dtr   : pulse DTR/RTS (auto-reset line) — reboots ESP/UNO without reflash
  3) esptool: hard reset via esptool (ESP32) as last resort
Requires pyserial. All calls are best-effort and never raise to the caller."""
from __future__ import annotations
import time, threading, glob, sys

try:
    import serial
    from serial.tools import list_ports
except Exception:
    serial = None
    list_ports = None

_locks: dict[str, threading.Lock] = {}


def _lock(port: str) -> threading.Lock:
    return _locks.setdefault(port, threading.Lock())


def list_serial() -> list[dict]:
    """Enumerate serial ports with USB VID:PID (identify CH340/CH9102 = our boards)."""
    if not list_ports:
        return []
    out = []
    for p in list_ports.comports():
        vidpid = ""
        if p.vid is not None:
            vidpid = f"{p.vid:04X}:{p.pid:04X}"
        kind = "unknown"
        d = (p.description or "").lower()
        if "ch9102" in d or "55d4" in vidpid.lower():
            kind = "esp32"
        elif "ch340" in d or "7523" in vidpid.lower():
            kind = "uno"
        out.append({"port": p.device, "desc": p.description, "vidpid": vidpid, "kind": kind})
    return out


class SerialBoard:
    """Thin serial wrapper with a background reader queue (line protocol)."""

    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self._ser = None

    def open(self) -> bool:
        if not serial:
            return False
        try:
            with _lock(self.port):
                self._ser = serial.Serial(self.port, self.baud, timeout=1)
            return True
        except Exception:
            self._ser = None
            return False

    def close(self):
        try:
            if self._ser:
                self._ser.close()
        except Exception:
            pass
        self._ser = None

    def is_open(self) -> bool:
        return bool(self._ser and self._ser.is_open)

    def write_line(self, line: str) -> bool:
        if not self.is_open() and not self.open():
            return False
        try:
            with _lock(self.port):
                self._ser.write((line.rstrip("\n") + "\n").encode())
                self._ser.flush()
            return True
        except Exception:
            return False

    def read_lines(self, timeout: float = 1.5) -> list[str]:
        if not self.is_open() and not self.open():
            return []
        out = []
        end = time.time() + timeout
        try:
            with _lock(self.port):
                while time.time() < end:
                    raw = self._ser.readline()
                    if not raw:
                        break
                    out.append(raw.decode("utf-8", "replace").rstrip())
        except Exception:
            pass
        return out

    # ---- reboot escalation ----
    def reboot_soft(self) -> bool:
        return self.write_line("REBOOT")

    def reboot_dtr(self) -> bool:
        """Pulse the auto-reset line (DTR low->high). Works on CH340/CH9102 wiring."""
        if not serial:
            return False
        try:
            with _lock(self.port):
                s = self._ser or serial.Serial(self.port, self.baud, timeout=1)
                s.setDTR(False); s.setRTS(True); time.sleep(0.1)
                s.setDTR(True); s.setRTS(False); time.sleep(0.1)
                s.setDTR(False)
            return True
        except Exception:
            return False

    def reboot_esptool(self) -> bool:
        """Last resort for ESP32: esptool hard reset (no flashing)."""
        import subprocess
        self.close()
        for cmd in (["esptool.py", "--port", self.port, "--after", "hard_reset", "chip_id"],
                    [sys.executable, "-m", "esptool", "--port", self.port, "--after", "hard_reset", "chip_id"]):
            try:
                r = subprocess.run(cmd, capture_output=True, timeout=30)
                if r.returncode == 0:
                    return True
            except Exception:
                continue
        return False


def force_reboot(port: str, baud: int = 115200, kind: str = "esp32") -> dict:
    """Escalating reboot. Returns which stage succeeded."""
    b = SerialBoard(port, baud)
    stages = []
    if b.reboot_soft():
        stages.append("soft")
        time.sleep(2)
        if b.open() and b.read_lines(3):        # came back talking -> done
            b.close()
            return {"ok": True, "stage": "soft", "stages": stages}
    if b.reboot_dtr():
        stages.append("dtr")
        time.sleep(3)
        if b.open() and b.read_lines(3):
            b.close()
            return {"ok": True, "stage": "dtr", "stages": stages}
    if kind == "esp32" and b.reboot_esptool():
        stages.append("esptool")
        return {"ok": True, "stage": "esptool", "stages": stages}
    b.close()
    return {"ok": False, "stage": None, "stages": stages}


def ping_board(port: str, baud: int = 115200) -> bool:
    """Liveness probe: send PING, expect any reply within timeout."""
    b = SerialBoard(port, baud)
    if not b.open():
        return False
    b.write_line("PING")
    alive = any("PONG" in ln or ln for ln in b.read_lines(2))
    b.close()
    return alive
