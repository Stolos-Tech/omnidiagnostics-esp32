"""Наскрізний тест НОВИХ апок ПРЯМО через плату (REST), з перевіркою здоров'я
(без ребуту, стабільний heap) і збереженням підсумку у звіт -> далі в бота.
Запуск: python board_test.py [host] [pin]"""
import sys, time
from espos_client import EspOsClient

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.53"
pin = sys.argv[2] if len(sys.argv) > 2 else ""
c = EspOsClient(host)

def stat():
    s = c.stats()
    return s.get("uptime_s", -1), s.get("heap", -1)

def to_launcher():
    for _ in range(4):
        m = c.mirror()
        if m.get("page") == "launcher":
            return m
        c.cmd(btn="S2"); time.sleep(0.5)
    return c.mirror()

def open_path(steps):
    """steps = список idx-переходів; повертає (page, first_item) фінального екрана."""
    for idx in steps:
        c.cmd(idx=idx); time.sleep(0.6)
    m = c.mirror()
    items = m.get("items", [])
    return m.get("page", "?"), (items[0] if items else "")

def back_to_launcher(n):
    for _ in range(n):
        c.cmd(back=True); time.sleep(0.4)
    to_launcher()

print(f"=== NEW-APP board test @ {host} ===")
assert c.login(pin), "login failed"
v = c.version()
up0, hp0 = stat()
print(f"api={v.get('api')} uptime0={up0}s heap0={hp0}")
to_launcher()

# (мітка, кроки-idx, back-глибина, очікувана сторінка-підрядок)
TESTS = [
    ("Sub-GHz Analyzer", [5, 0], 2, "subghz_analyzer"),
    ("Sub-GHz Capture",  [5, 1], 2, "subghz_capture"),
    ("Sub-GHz Watch",    [7, 4], 2, "subghz_watch"),
    ("WiFi domain",      [2],    1, "wifi"),
    ("Network nested",   [3, 0], 2, "netdiag"),
    ("Hardware domain",  [8],    1, "hardware"),
    ("System domain",    [9],    1, "system"),
]

lines = []
ok_count = 0
for label, steps, depth, expect in TESTS:
    page, first = open_path(steps)
    ok = expect in page
    ok_count += ok
    mark = "OK " if ok else "!! "
    row = f"[{mark}] {label:18} -> page='{page}' first='{first[:26]}'"
    print(row); lines.append(row)
    back_to_launcher(depth)
    time.sleep(0.3)

up1, hp1 = stat()
no_reboot = up1 >= up0
heap_ok = hp1 > 8000
print(f"uptime1={up1}s (no_reboot={no_reboot})  heap1={hp1} (ok={heap_ok})")

summary = (
    "# NEW-APP STABILITY TEST (board-direct REST)\n"
    f"api {v.get('api')}  features {','.join(v.get('features', []))}\n"
    f"apps opened OK: {ok_count}/{len(TESTS)}\n"
    f"uptime {up0}s -> {up1}s  (no reboot: {no_reboot})\n"
    f"heap {hp0} -> {hp1}  (healthy: {heap_ok})\n\n"
    + "\n".join(lines) + "\n"
)
r = c.save_report("newapp_test", summary)
print("saved report:", r.strip()[:60])
print(f"\n=== {ok_count}/{len(TESTS)} apps OK, reboot={not no_reboot}, heap_ok={heap_ok} ===")
