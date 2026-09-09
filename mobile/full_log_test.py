"""Повний тест із МЕРЕЖЕВИМИ+КАНАЛЬНИМИ даними у звіти (далі -> бот).
1) WiFi Analyzer: сусідні мережі + гістограма каналів (кілька сторінок).
2) Self Test: 11 мережевих інструментів.
Запуск: python full_log_test.py [host] [pin]"""
import sys, time
from espos_client import EspOsClient

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.53"
pin = sys.argv[2] if len(sys.argv) > 2 else "036163"
c = EspOsClient(host)

def page_items():
    m = c.mirror()
    return m.get("page", "?"), m.get("items", [])

def to_launcher():
    for _ in range(5):
        if c.mirror().get("page") == "launcher":
            return
        c.cmd(btn="S2"); time.sleep(0.5)

assert c.login(pin), "login failed"
to_launcher()
print("=== capturing WiFi networks + channels ===")

# WiFi domain (idx 2) -> WiFi Analyzer (member idx 1). Дає сканування ~3с.
c.cmd(idx=2); time.sleep(0.6)
c.cmd(idx=1); time.sleep(4.5)          # чекаємо синхронний скан
rep = ["# WiFi ANALYZER  networks + channels", f"# captured {time.strftime('%Y-%m-%d %H:%M:%S')}", ""]
seen = set()
for step in range(4):                   # LIST / CHANNELS / RSSI / (цикл)
    page, items = page_items()
    key = "|".join(items)
    if key and key not in seen:
        seen.add(key)
        rep.append(f"-- {page} --")
        rep += [f"  {it}" for it in items]
        rep.append("")
    c.cmd(btn="S2"); time.sleep(1.3)     # наступна сторінка аналайзера
wifi_body = "\n".join(rep)
print(wifi_body)
print("save:", c.save_report("wifi_scan", wifi_body).strip()[:40])

# вихід з WiFi Analyzer -> WiFi selector -> launcher
c.cmd(back=True); time.sleep(0.5)
c.cmd(back=True); time.sleep(0.5)
to_launcher()

print("\n=== running Self Test (11 network modules, ~100s) ===")
c.cmd(idx=9); time.sleep(0.6)           # System group
c.cmd(idx=1); time.sleep(0.6)           # Self Test member
c.cmd(btn="S5"); time.sleep(2)          # start run
done = False
for i in range(28):
    time.sleep(5)
    page, items = page_items()
    first = items[0] if items else ""
    print(f"  +{(i+1)*5}s: {first}")
    if "Done" in first:
        done = True; break
print("Self Test done:", done)

# повернути в лаунчер, показати підсумок звітів
c.cmd(back=True); time.sleep(0.5); to_launcher()
arc = c.archive()
print(f"\n=== reports on board: {len(arc)} ===")
by = {}
for it in arc:
    by.setdefault(it.cat, []).append(it.name)
for cat, names in sorted(by.items()):
    print(f"  {cat}: {len(names)}")
