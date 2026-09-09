"""
devintel — глибока розвідка пристрою в LAN БЕЗ зовнішніх бінарників (чистий Python, self-contained).
Дано IP (+MAC/vendor з netscan плати) — збирає максимум, щоб зрозуміти «що це за пристрій»:
  • reverse-DNS hostname
  • threaded TCP port-scan (курований список) + банери
  • HTTP/HTTPS фінгерпринт (Server-заголовок, <title>)
  • NetBIOS ім'я (UDP 137 NBSTAT) — típово Windows/NAS
  • SSDP/UPnP (unicast M-SEARCH -> LOCATION -> friendlyName/manufacturer/model)
  • евристична класифікація категорії пристрою + причини

Немає стану/залежностей від Flask — чисті функції, легко тестувати. Увесь I/O під try/except,
загальний бюджет часу обмежений, щоб не блокувати колектор.
"""
from __future__ import annotations
import socket, struct, ssl, threading, re, time, os, urllib.request, urllib.error

# --- OUI (виробник мережевого адаптера за MAC) ---
_OUI_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "oui.csv")
_OUI: dict[str, str] | None = None


def _load_oui() -> dict[str, str]:
    global _OUI
    if _OUI is not None:
        return _OUI
    d: dict[str, str] = {}
    try:
        with open(_OUI_PATH, encoding="latin-1") as f:
            for ln in f:
                # формат IEEE: MA-L,68ECC5,Intel Corporate,"address"
                parts = ln.split(",", 3)
                if len(parts) >= 3 and len(parts[1]) == 6:
                    d[parts[1].upper()] = parts[2].strip().strip('"')
    except Exception:
        pass
    _OUI = d
    return d


def oui_vendor(mac: str) -> str:
    if not mac:
        return ""
    p = re.sub(r"[^0-9A-Fa-f]", "", mac).upper()[:6]
    if len(p) < 6:
        return ""
    return _load_oui().get(p, "")


def is_random_mac(mac: str) -> bool:
    """Локально-адміністрований (приватний/рандомний) MAC — біт 1 першого байта."""
    try:
        return bool(int(mac.replace(":", "").replace("-", "")[:2], 16) & 0b10)
    except Exception:
        return False

# порт -> (мітка, вага-підказка для класифікації)
COMMON_PORTS = {
    21: "ftp", 22: "ssh", 23: "telnet", 25: "smtp", 53: "dns", 80: "http",
    110: "pop3", 139: "netbios", 143: "imap", 443: "https", 445: "smb",
    515: "printer-lpd", 554: "rtsp", 587: "smtp", 631: "ipp", 993: "imaps",
    1723: "pptp", 1883: "mqtt", 3306: "mysql", 3389: "rdp", 5000: "upnp/http",
    5060: "sip", 5353: "mdns", 5900: "vnc", 8009: "chromecast", 8080: "http-alt",
    8443: "https-alt", 8554: "rtsp-alt", 8883: "mqtts", 9000: "http-alt",
    9100: "printer-raw", 32400: "plex", 49152: "upnp", 62078: "apple-sync",
}
SCAN_PORTS = list(COMMON_PORTS.keys())


def reverse_dns(ip: str) -> str:
    try:
        return socket.gethostbyaddr(ip)[0]
    except Exception:
        return ""


def _scan_one(ip: str, port: int, timeout: float, out: list):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        if s.connect_ex((ip, port)) == 0:
            out.append(port)
        s.close()
    except Exception:
        pass


def scan_ports(ip: str, ports=SCAN_PORTS, timeout: float = 0.7) -> list[int]:
    out: list[int] = []
    threads = [threading.Thread(target=_scan_one, args=(ip, p, timeout, out)) for p in ports]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return sorted(out)


def grab_banner(ip: str, port: int, timeout: float = 1.0) -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((ip, port))
        data = s.recv(160)
        s.close()
        return data.decode("latin-1", "replace").strip()
    except Exception:
        return ""


def http_probe(ip: str, port: int = 80, https: bool = False, timeout: float = 2.0) -> dict:
    scheme = "https" if https else "http"
    url = f"{scheme}://{ip}:{port}/"
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "espos-intel/1"})
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
            server = r.headers.get("Server", "")
            body = r.read(4096).decode("latin-1", "replace")
    except urllib.error.HTTPError as e:  # сторінка є, але код помилки — заголовки все одно корисні
        server = e.headers.get("Server", "") if e.headers else ""
        body = ""
    except Exception:
        return {}
    title = ""
    m = re.search(r"<title[^>]*>(.*?)</title>", body, re.I | re.S)
    if m:
        title = re.sub(r"\s+", " ", m.group(1)).strip()[:80]
    res = {}
    if server:
        res["server"] = server[:80]
    if title:
        res["title"] = title
    return res


def netbios_name(ip: str, timeout: float = 1.2) -> str:
    """NBSTAT-запит (UDP 137). Повертає перше не-групове ім'я (Windows/NAS)."""
    try:
        # заголовок: trn_id, flags=0, qd=1
        pkt = struct.pack(">HHHHHH", 0x4573, 0x0000, 1, 0, 0, 0)
        pkt += b"\x20" + b"CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" + b"\x00"  # закодоване '*'
        pkt += struct.pack(">HH", 0x0021, 0x0001)  # NBSTAT, IN
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(timeout)
        s.sendto(pkt, (ip, 137))
        data, _ = s.recvfrom(1024)
        s.close()
        # знайти numnames: після заголовка(12)+name(34)+type(2)+class(2)+ttl(4)+rdlen(2)=56
        i = 56
        num = data[i]
        i += 1
        for _ in range(num):
            nm = data[i:i + 15].decode("latin-1", "replace").rstrip(" \x00")
            flags = struct.unpack(">H", data[i + 16:i + 18])[0]
            group = bool(flags & 0x8000)
            i += 18
            if nm and not group and nm != "__MSBROWSE__":
                return nm
    except Exception:
        return ""
    return ""


def ssdp_probe(ip: str, timeout: float = 1.5) -> dict:
    """Unicast M-SEARCH -> LOCATION -> опис пристрою (UPnP)."""
    msg = ("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
           'MAN: "ssdp:discover"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n').encode()
    loc = ""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(timeout)
        s.sendto(msg, (ip, 1900))
        data, _ = s.recvfrom(2048)
        s.close()
        m = re.search(r"LOCATION:\s*(\S+)", data.decode("latin-1", "replace"), re.I)
        if m:
            loc = m.group(1).strip()
    except Exception:
        return {}
    if not loc:
        return {}
    try:
        with urllib.request.urlopen(loc, timeout=timeout) as r:
            xml = r.read(8192).decode("latin-1", "replace")
    except Exception:
        return {"location": loc}
    out = {"location": loc}
    for tag in ("friendlyName", "manufacturer", "modelName", "modelDescription", "deviceType"):
        m = re.search(rf"<{tag}>(.*?)</{tag}>", xml, re.I | re.S)
        if m:
            out[tag] = re.sub(r"\s+", " ", m.group(1)).strip()[:80]
    return out


def classify(ports: list[int], http: dict, vendor: str, nb: str, ssdp: dict) -> tuple[str, list[str]]:
    """Евристична категорія + список причин (українською)."""
    P = set(ports)
    reasons: list[str] = []
    cat = "невідомий пристрій"
    srv = (http.get("server", "") + " " + http.get("title", "")).lower()
    vlow = (vendor or "").lower()

    if 9100 in P or 515 in P or 631 in P:
        cat = "мережевий принтер"; reasons.append("порти друку (9100/515/631)")
    elif 554 in P or 8554 in P or "ipcam" in srv or "camera" in srv or "hikvision" in vlow or "dahua" in vlow:
        cat = "IP-камера / відеоспостереження"; reasons.append("RTSP/камерні ознаки")
    elif 32400 in P:
        cat = "медіасервер Plex"; reasons.append("порт 32400")
    elif 8009 in P or "cast" in srv or "chromecast" in ssdp.get("modelName", "").lower():
        cat = "Chromecast / Google Cast"; reasons.append("порт 8009/UPnP-модель")
    elif 62078 in P or "apple" in vlow:
        cat = "Apple-пристрій (iPhone/iPad/Mac)"; reasons.append("apple-sync/OUI Apple")
    elif 445 in P or 139 in P or nb:
        cat = "Windows-ПК або NAS (SMB)"; reasons.append("SMB/NetBIOS")
    elif 3389 in P:
        cat = "Windows-ПК (RDP)"; reasons.append("порт 3389")
    elif 1883 in P or 8883 in P:
        cat = "IoT/розумний пристрій (MQTT)"; reasons.append("порт MQTT")
    elif 22 in P and (2000 in P or "raspbian" in srv or "openwrt" in srv or "ubuntu" in srv or "debian" in srv):
        cat = "Linux-хост / SBC (напр. Raspberry Pi)"; reasons.append("SSH + Linux-банер")
    elif 53 in P and (80 in P or 443 in P):
        cat = "роутер / шлюз / точка доступу"; reasons.append("DNS+веб-адмін")
    elif any(x in srv for x in ("router", "gateway", "openwrt", "dd-wrt", "mikrotik", "asus", "tp-link", "keenetic")):
        cat = "роутер / точка доступу"; reasons.append(f"веб-банер: {http.get('server') or http.get('title')}")
    elif 80 in P or 443 in P or 8080 in P:
        cat = "пристрій з веб-інтерфейсом"; reasons.append("відкритий HTTP")
    elif 22 in P:
        cat = "Linux/Unix-хост (SSH)"; reasons.append("порт 22")

    if ssdp.get("friendlyName"):
        reasons.append(f"UPnP: {ssdp['friendlyName']}")
    if nb:
        reasons.append(f"NetBIOS: {nb}")
    if vendor and vendor not in ("?", ""):
        reasons.append(f"виробник адаптера: {vendor}")
    return cat, reasons


def intel(ip: str, mac: str = "", vendor: str = "", budget: float = 8.0) -> dict:
    """Оркестратор. Повертає повний словник розвідки (усе best-effort у межах бюджету)."""
    t0 = time.time()
    res: dict = {"ip": ip, "mac": (mac or "").upper(), "at": int(t0)}
    # виробник: спершу з netscan, інакше — локальний OUI; позначаємо рандомний MAC
    ven = vendor if vendor and vendor not in ("?", "") else oui_vendor(mac)
    if not ven and is_random_mac(mac):
        ven = "приватний (рандомний) MAC"
    res["vendor"] = ven or ""
    vendor = ven or ""
    res["random_mac"] = is_random_mac(mac)
    res["hostname"] = reverse_dns(ip)
    ports = scan_ports(ip)
    res["ports"] = ports
    res["port_labels"] = {str(p): COMMON_PORTS.get(p, "?") for p in ports}
    http: dict = {}
    if 80 in ports or 8080 in ports or 5000 in ports:
        http = http_probe(ip, 80 if 80 in ports else (8080 if 8080 in ports else 5000))
    if not http and (443 in ports or 8443 in ports):
        http = http_probe(ip, 443 if 443 in ports else 8443, https=True)
    if http:
        res["http"] = http
    banners = {}
    for p in (22, 21, 23, 25):
        if p in ports and time.time() - t0 < budget:
            b = grab_banner(ip, p)
            if b:
                banners[str(p)] = b[:120]
    if banners:
        res["banners"] = banners
    res["netbios"] = netbios_name(ip) if time.time() - t0 < budget else ""
    res["ssdp"] = ssdp_probe(ip) if time.time() - t0 < budget else {}
    cat, reasons = classify(ports, http, vendor, res["netbios"], res["ssdp"])
    # уточнення коли нема портів/сервісів — за виробником адаптера / типом MAC
    if cat == "невідомий пристрій":
        vl = vendor.lower()
        if res["random_mac"]:
            cat = "мобільний/приватний пристрій (рандомний MAC — телефон?)"
            reasons.append("рандомізований MAC — типово смартфон")
        elif any(x in vl for x in ("intel", "realtek", "liteon", "dell", "lenovo", "hewlett", "hp ", "asus", "micro-star", "gigabyte")):
            cat = f"комп'ютер/ноутбук (адаптер: {vendor})"
            reasons.append("OUI відомих виробників мереж. карт ПК")
        elif any(x in vl for x in ("espressif", "raspberry", "arduino")):
            cat = f"IoT/DIY-плата (адаптер: {vendor})"
        elif any(x in vl for x in ("samsung", "xiaomi", "huawei", "oneplus", "apple", "google")):
            cat = f"смартфон/планшет (виробник: {vendor})"
        elif vendor:
            cat = f"пристрій {vendor}"
    res["category"] = cat
    res["reasons"] = reasons
    res["scan_ms"] = int((time.time() - t0) * 1000)
    return res


def summary_line(d: dict) -> str:
    """Короткий рядок для Telegram-алерту."""
    bits = [d.get("category", "?")]
    if d.get("hostname"):
        bits.append(d["hostname"])
    elif d.get("netbios"):
        bits.append(d["netbios"])
    if d.get("ports"):
        bits.append("порти " + ",".join(str(p) for p in d["ports"][:8]))
    return " · ".join(bits)


if __name__ == "__main__":
    import sys, json
    print(json.dumps(intel(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""),
                     ensure_ascii=False, indent=2))
