"""App -> Server -> Board routing (task 6). When a board is attached by USB, all
app/dashboard traffic to it is bridged over serial by the server instead of the
board's WiFi. This keeps monitoring alive while the board's WiFi module is busy,
restarting, or running a tool that drops the AP.

Serial JSON-RPC line protocol (mirrors firmware src/remote/serial_command):
    ->  REQ <id> <METHOD> <path> [json-body]
    <-  RES <id> <status> <json>
Board registry state is kept in the boards table; a background supervisor keeps a
persistent SerialBoard per USB board and multiplexes requests by id."""
from __future__ import annotations
import json, threading, time, itertools
import db, usbctl

_boards: dict[str, "ProxyLink"] = {}          # serial_port -> link
_idgen = itertools.count(1)
_glock = threading.Lock()


class ProxyLink:
    def __init__(self, port: str, baud: int = 115200, kind: str = "esp32"):
        self.port = port
        self.baud = baud
        self.kind = kind
        self.sb = usbctl.SerialBoard(port, baud)
        self.lock = threading.Lock()
        self.alive = False
        self.last_ok = 0

    def ensure(self) -> bool:
        if self.sb.is_open():
            return True
        return self.sb.open()

    def rpc(self, method: str, path: str, body: dict | None = None, timeout: float = 6.0) -> dict:
        """One request/response over serial. Serialized per board."""
        rid = next(_idgen)
        payload = json.dumps(body, ensure_ascii=False) if body else ""
        line = f"REQ {rid} {method} {path} {payload}".rstrip()
        with self.lock:
            if not self.ensure():
                return {"_proxy": "serial-open-failed", "status": 502}
            self.sb.write_line(line)
            end = time.time() + timeout
            while time.time() < end:
                for ln in self.sb.read_lines(1.0):
                    if ln.startswith(f"RES {rid} "):
                        parts = ln.split(" ", 3)
                        status = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 200
                        data = parts[3] if len(parts) > 3 else "{}"
                        self.alive = True
                        self.last_ok = db.now()
                        try:
                            return {"_status": status, "json": json.loads(data)}
                        except Exception:
                            return {"_status": status, "text": data}
            self.alive = False
            return {"_proxy": "timeout", "status": 504}


def register_usb_board(port: str, kind: str = "esp32", baud: int = 115200, name: str = "") -> int:
    n = db.now()
    row = db.one("SELECT id FROM boards WHERE serial_port=?", (port,))
    if row:
        db.run("UPDATE boards SET transport='usb',kind=?,baud=?,last_seen=?,state='online' WHERE id=?",
               (kind, baud, n, row["id"]))
        bid = row["id"]
    else:
        bid = db.run("""INSERT INTO boards(name,kind,transport,serial_port,baud,last_seen,state,created_at)
                        VALUES(?,?,?,?,?,?,?,?)""",
                     (name or f"{kind}@{port}", kind, "usb", port, baud, n, "online", n))
    with _glock:
        _boards[port] = ProxyLink(port, baud, kind)
    return bid


def discover_and_register() -> list[dict]:
    """Auto-detect USB boards and register them for proxying."""
    found = []
    for p in usbctl.list_serial():
        if p["kind"] in ("esp32", "uno"):
            bid = register_usb_board(p["port"], p["kind"])
            found.append({"board_id": bid, **p})
    return found


def link_for(board_id: int) -> ProxyLink | None:
    b = db.one("SELECT serial_port,kind,baud,transport FROM boards WHERE id=?", (board_id,))
    if not b or b["transport"] != "usb" or not b["serial_port"]:
        return None
    port = b["serial_port"]
    with _glock:
        if port not in _boards:
            _boards[port] = ProxyLink(port, b["baud"] or 115200, b["kind"] or "esp32")
        return _boards[port]


def proxy_request(board_id: int, method: str, path: str, body: dict | None = None) -> dict:
    """Public entry: route an app/dashboard request to a USB board over serial.
    Falls back to nothing here — the HTTP layer decides wifi-vs-usb by transport."""
    link = link_for(board_id)
    if not link:
        return {"error": "board not on usb", "status": 409}
    return link.rpc(method, path, body)


def is_usb(board_id: int) -> bool:
    b = db.one("SELECT transport FROM boards WHERE id=?", (board_id,))
    return bool(b and b["transport"] == "usb")
