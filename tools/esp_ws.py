"""Мінімальний WebSocket-клієнт для esp32-os (без зовнішніх залежностей)."""
import socket, base64, os, struct, json, time

class WS:
    def __init__(self, host, port=81, timeout=6):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\n\r\n")
        self.s.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(1)
        assert b"101" in buf.split(b"\r\n")[0], buf[:120]
        self.buf = b""

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(data)
        hdr = b"\x81"
        if n < 126:   hdr += bytes([0x80 | n])
        elif n < 65536: hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else: hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _fill(self, n):
        while len(self.buf) < n:
            c = self.s.recv(4096)
            if not c: raise ConnectionError("closed")
            self.buf += c

    def recv(self):
        self._fill(2)
        b1, b2 = self.buf[0], self.buf[1]
        ln = b2 & 0x7F; off = 2
        if ln == 126:
            self._fill(4); ln = struct.unpack(">H", self.buf[2:4])[0]; off = 4
        elif ln == 127:
            self._fill(10); ln = struct.unpack(">Q", self.buf[2:10])[0]; off = 10
        self._fill(off + ln)
        payload = self.buf[off:off+ln]
        self.buf = self.buf[off+ln:]
        return payload.decode("utf-8", "replace")

    def recv_json(self, want=None, tries=40):
        for _ in range(tries):
            try: m = json.loads(self.recv())
            except Exception: continue
            if want is None or want(m): return m
        return None
    def close(self):
        try: self.s.close()
        except Exception: pass
