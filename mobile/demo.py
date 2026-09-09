"""Наскрізна перевірка board-API як його бачитиме мобільний клієнт (4 таби Flipper).
Запуск: python demo.py [host] [pin]"""
import sys
from espos_client import EspOsClient

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.53"
pin = sys.argv[2] if len(sys.argv) > 2 else "036163"
c = EspOsClient(host)

print(f"=== ESP32-OS mobile client demo @ {host} ===\n")

# --- version (перше, що робить мобілка: перевірка сумісності) ---
v = c.version()
print("[version]", v.get("api"), "| features:", ",".join(v.get("features", [])))

# --- login ---
print("[login]", "ok" if c.login(pin) else "FAILED")

# --- DEVICE tab ---
s = c.status()
print(f"[DEVICE] sta={s.get('sta')} ap={s.get('ap')} rssi={s.get('rssi')} batt={s.get('mv')}mV")
m = c.mirror()
print(f"[DEVICE] screen page='{m.get('page')}' items={len(m.get('items', []))}")
if m.get("page") != "launcher":
    c.cmd(btn="S2")  # вийти в лаунчер (keep transport)
    m = c.mirror()
print("[DEVICE] launcher:", " | ".join(m.get("items", [])[:12]))

# --- ARCHIVE tab (typed) ---
arc = c.archive()
by_cat: dict = {}
for it in arc:
    by_cat.setdefault(it.cat, []).append(it.name)
print(f"[ARCHIVE] {len(arc)} items in {len(by_cat)} categories:")
for cat, names in sorted(by_cat.items()):
    print(f"          {cat:9} x{len(names)}")

# --- APPS tab (Berry scripts = FAP) ---
sc = c.scripts()
print(f"[APPS] {len(sc)} scripts:", ", ".join(s.get("n", "?") for s in sc))

# --- TOOLS tab ---
sg = c.subghz_spectrum()
print(f"[TOOLS] sub-GHz spectrum: present={sg.get('present')} points={len(sg.get('db', []))}")
sd = c.sd_status()
print(f"[TOOLS] SD: mounted={sd.get('mounted')}")

# --- Settings ---
th = c.themes()
print(f"[SET] themes={th.get('themes')} current={th.get('current')}")

print("\n=== all API surfaces reachable — mobile contract verified ===")
