"""Server health diagnostics & shutdown root-cause (task 9). Samples CPU/RAM/
thermal/voltage/uptime into server_health; on each boot detects whether the last
shutdown was clean or sudden and classifies the suspected cause (thermal/power/
oom/kernel) from the last samples + kernel logs. Runs as espos-healthmon."""
from __future__ import annotations
import time, json, os, subprocess, glob, re
import db

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "config.json")))
SAMPLE = int(CFG.get("health_sample_s", 15))

try:
    import psutil
except Exception:
    psutil = None
try:
    import alerts
except Exception:
    alerts = None


def _read_first(paths) -> str | None:
    for p in paths:
        for f in glob.glob(p):
            try:
                return open(f).read().strip()
            except Exception:
                continue
    return None


def cpu_temp() -> float | None:
    if psutil and hasattr(psutil, "sensors_temperatures"):
        try:
            t = psutil.sensors_temperatures()
            for arr in t.values():
                if arr:
                    return round(arr[0].current, 1)
        except Exception:
            pass
    v = _read_first(["/sys/class/thermal/thermal_zone*/temp"])
    if v and v.isdigit():
        return round(int(v) / 1000.0, 1)
    return None


def voltages() -> dict:
    """Best-effort voltage/undervoltage. Raspberry-Pi vcgencmd, hwmon rails, or none."""
    out = {}
    try:
        r = subprocess.run(["vcgencmd", "measure_volts"], capture_output=True, text=True, timeout=4)
        m = re.search(r"([\d.]+)V", r.stdout)
        if m:
            out["core_v"] = float(m.group(1))
        t = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True, text=True, timeout=4)
        m = re.search(r"throttled=(0x[0-9a-fA-F]+)", t.stdout)
        if m:
            out["throttled"] = m.group(1)
            out["undervolt"] = bool(int(m.group(1), 16) & 0x1)
    except Exception:
        pass
    for hw in glob.glob("/sys/class/hwmon/hwmon*/in*_input"):
        try:
            out[os.path.basename(hw)] = int(open(hw).read().strip()) / 1000.0
        except Exception:
            pass
    return out


def boot_id() -> str | None:
    return _read_first(["/proc/sys/kernel/random/boot_id"])


def sample() -> dict:
    load1 = os.getloadavg()[0] if hasattr(os, "getloadavg") else 0.0
    cores = (psutil.cpu_count() if psutil else os.cpu_count()) or 1
    # mem/disk/uptime: psutil якщо є, ІНАКШЕ stdlib (/proc, statvfs через srvstats).
    # psutil немає у venv сервера -> раніше все було 0%. Тепер не залежимо від нього.
    if psutil:
        vm = psutil.virtual_memory(); du = psutil.disk_usage("/")
        mem_used_pct = int(vm.percent); mem_avail_mb = int(vm.available / 1e6)
        disk_used_pct = int(du.percent)
        uptime_s = int(time.time() - psutil.boot_time())
    else:
        mem_used_pct = mem_avail_mb = disk_used_pct = uptime_s = 0
        try:
            import srvstats
            m = srvstats.parse_meminfo(srvstats._read("/proc/meminfo"))
            mem_used_pct = int(m.get("used_pct", 0)); mem_avail_mb = int(m.get("avail_mb", 0))
            uptime_s = srvstats.parse_uptime(srvstats._read("/proc/uptime"))
        except Exception:
            pass
        try:
            st = os.statvfs("/"); tot = st.f_blocks * st.f_frsize; free = st.f_bavail * st.f_frsize
            disk_used_pct = round((tot - free) * 100 / tot) if tot else 0
        except Exception:
            pass
    volts = voltages()
    row = {
        "ts": db.now(),
        "load1": round(load1, 2),
        "load_pct": int(min(100, load1 / cores * 100)),
        "mem_used_pct": mem_used_pct,
        "mem_avail_mb": mem_avail_mb,
        "disk_used_pct": disk_used_pct,
        "cpu_temp_c": cpu_temp(),
        "volts_json": json.dumps(volts),
        "uptime_s": uptime_s,
        "undervolt": 1 if volts.get("undervolt") else 0,
        "services_json": json.dumps(_services()),
    }
    db.run("""INSERT OR REPLACE INTO server_health(ts,load1,load_pct,mem_used_pct,mem_avail_mb,
              disk_used_pct,cpu_temp_c,volts_json,uptime_s,undervolt,services_json)
              VALUES(:ts,:load1,:load_pct,:mem_used_pct,:mem_avail_mb,:disk_used_pct,
              :cpu_temp_c,:volts_json,:uptime_s,:undervolt,:services_json)""", row)
    return row


def _services() -> dict:
    svc = {}
    for name in CFG.get("watch_services", ["espos-gateway", "espos-collector", "espos-dashboard"]):
        try:
            r = subprocess.run(["systemctl", "is-active", name], capture_output=True, text=True, timeout=4)
            svc[name] = r.stdout.strip()
        except Exception:
            svc[name] = "unknown"
    return svc


def detect_shutdown():
    """On startup: was last shutdown clean? Classify sudden ones."""
    bid = boot_id()
    prev = db.one("SELECT boot_id FROM shutdown_events ORDER BY id DESC LIMIT 1")
    if prev and prev["boot_id"] == bid:
        return                                  # same boot, already logged
    last = db.one("SELECT * FROM server_health ORDER BY ts DESC LIMIT 1")
    clean = _was_clean_shutdown()
    suspected, evidence = _classify(last, clean)
    db.run("""INSERT INTO shutdown_events(detected_at,boot_id,prev_uptime_s,clean,last_temp_c,
              last_load,suspected,evidence) VALUES(?,?,?,?,?,?,?,?)""",
           (db.now(), bid, last["uptime_s"] if last else None, 1 if clean else 0,
            last["cpu_temp_c"] if last else None, last["load1"] if last else None,
            suspected, evidence))
    if not clean and alerts:
        try:
            alerts.send_telegram(CFG, f"⚠️ Server rebooted UNCLEAN. Suspected: {suspected}. {evidence}")
        except Exception:
            pass


def _was_clean_shutdown() -> bool:
    try:
        r = subprocess.run(["last", "-x", "-n", "5", "shutdown", "reboot"],
                           capture_output=True, text=True, timeout=6)
        return "shutdown" in r.stdout.splitlines()[0] if r.stdout.strip() else False
    except Exception:
        return False


def _classify(last, clean) -> tuple[str, str]:
    if clean:
        return "clean", "graceful shutdown/reboot"
    ev = []
    # kernel log evidence from previous boot
    for pat, cause in (("temperature above threshold", "thermal"),
                       ("thermal", "thermal"),
                       ("Under-voltage", "power"),
                       ("under-voltage", "power"),
                       ("Out of memory", "oom"),
                       ("oom-kill", "oom"),
                       ("Kernel panic", "kernel"),
                       ("hardware error", "kernel")):
        try:
            r = subprocess.run(["journalctl", "-k", "-b", "-1", "--no-pager"],
                               capture_output=True, text=True, timeout=10)
            if pat.lower() in r.stdout.lower():
                ev.append(cause)
        except Exception:
            break
    if last:
        if last["cpu_temp_c"] and last["cpu_temp_c"] > 80:
            ev.append("thermal")
        if last["undervolt"]:
            ev.append("power")
        if last["mem_used_pct"] and last["mem_used_pct"] > 96:
            ev.append("oom")
    if ev:
        top = max(set(ev), key=ev.count)
        return top, "signals: " + ",".join(dict.fromkeys(ev))
    return "unknown", "sudden power loss with no logged cause (suspect PSU/wiring/brownout)"


def stats() -> dict:
    """Live snapshot for the app/dashboard admin screen."""
    r = sample()
    r["volts"] = json.loads(r.pop("volts_json"))
    r["services"] = json.loads(r.pop("services_json"))
    recent = db.rows_to_dicts(db.q(
        "SELECT ts,cpu_temp_c,load_pct,mem_used_pct,undervolt FROM server_health ORDER BY ts DESC LIMIT 120"))
    r["history"] = recent
    r["shutdowns"] = db.rows_to_dicts(db.q(
        "SELECT detected_at,clean,suspected,evidence,last_temp_c FROM shutdown_events ORDER BY id DESC LIMIT 10"))
    return r


def loop():
    db.init()
    detect_shutdown()
    print(f"[healthmon] sample={SAMPLE}s")
    while True:
        try:
            sample()
        except Exception as e:
            print("[healthmon] err", e)
        time.sleep(SAMPLE)


if __name__ == "__main__":
    loop()
