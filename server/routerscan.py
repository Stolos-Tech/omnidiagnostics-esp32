"""routerscan — повний інвентар пристроїв із DHCP-таблиці роутера HUAWEI HG8245W5
(EchoLife GPON ONT). ARP-скан плати пропускає сплячі IoT; роутер бачить усіх, хто має
lease, зі статусом online/offline. Логін-флоу HG8245W5 (2022 fw):
  GET /asp/GetRandCount.asp -> token; cookie body:...:id=-1;
  POST /login.cgi {UserName, PassWord=base64(pw), Language, x.X_HW_Token} -> sid:id=1;
  POST /html/bbsp/common/GetLanUserDhcpInfo.asp + GetLanUserDevInfo.asp (Referer!), hex-escaped.
Креденшали — у config.json["router"]. Усе best-effort; помилка -> [].
"""
from __future__ import annotations
import os, json, re, base64, requests

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
RT = CFG.get("router", {})
BS = chr(92)


def _dec(x: str) -> str:
    return re.sub(BS + BS + "x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), x)


def _login(s: requests.Session, url: str, user: str, pw: str) -> bool:
    s.headers["User-Agent"] = "Mozilla/5.0"
    s.get(url + "/", timeout=8)
    tok = re.sub("[^0-9A-Za-z]", "", s.post(url + "/asp/GetRandCount.asp", timeout=8).text.strip())
    s.cookies.set("Cookie", "body:Language:english:id=-1")
    r = s.post(url + "/login.cgi", timeout=10, data={
        "UserName": user, "PassWord": base64.b64encode(pw.encode()).decode(),
        "Language": "english", "x.X_HW_Token": tok})
    # sid-значення буває з UPPERCASE-hex -> case-insensitive, інакше флаки-логін.
    m = re.search(r"(sid=[0-9a-fA-F]+:Language:[^;]*:id=1)", r.headers.get("Set-Cookie", ""))
    if not m:
        return False
    s.cookies.clear()
    s.cookies.set("Cookie", m.group(1))
    return True


def scrape() -> list[dict]:
    """[{mac, ip, name, online}] — усі пристрої з DHCP-таблиці роутера."""
    url = RT.get("url", "http://192.168.100.1")
    user = RT.get("user", "root")
    pw = RT.get("pass", "")
    if not pw:
        return []
    s = requests.Session()
    try:
        if not _login(s, url, user, pw):
            return []
        hdr = {"Referer": url + "/html/bbsp/userdevinfo/userdevinfo.asp"}
        dhcp = _dec(s.post(url + "/html/bbsp/common/GetLanUserDhcpInfo.asp", timeout=8, headers=hdr).text)
        devd = _dec(s.post(url + "/html/bbsp/common/GetLanUserDevInfo.asp", timeout=8, headers=hdr).text)
    except Exception:
        return []
    # статус online/offline із USERDevice(...)
    status = {}
    for ctor in re.findall(r"new USERDevice(?:New)?\(([^)]*)\)", devd):
        f = [x.strip().strip('"') for x in ctor.split(",")]
        macs = [x for x in f if re.fullmatch(r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", x)]
        if macs:
            st = next((x for x in f if x in ("Online", "Offline")), "")
            status[macs[0].upper()] = (st == "Online")
    out = []
    for m in re.findall(r"new DHCPInfo\(([^)]*)\)", dhcp):
        f = [x.strip().strip('"') for x in m.split(",")]
        if len(f) >= 5 and re.fullmatch(r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", f[3]):
            mac = f[3].upper()
            out.append({"mac": mac, "ip": f[2], "name": f[1] or "",
                        "online": status.get(mac, False)})
    return out


def online_hosts() -> list[dict]:
    """Формат, сумісний з netwatch (лише онлайн): {ip, mac, vendor}."""
    return [{"ip": d["ip"], "mac": d["mac"], "vendor": ""} for d in scrape() if d["online"]]


if __name__ == "__main__":
    for d in scrape():
        print(("ON " if d["online"] else "off"), d["mac"], d["ip"], d["name"])
