"""
srvstats — ресурси сервера (stolos) для моніторингу з дашборда/додатка. Без зовнішніх
залежностей: читає /proc, os.statvfs, systemctl. Парсери — чисті (тестуються локально),
IO-обгортки — окремо.
"""
from __future__ import annotations
import os, subprocess, time


def parse_meminfo(text: str) -> dict:
    """MemTotal/MemAvailable з /proc/meminfo -> total_mb, avail_mb, used_pct."""
    kv = {}
    for line in text.splitlines():
        p = line.split(":")
        if len(p) == 2:
            kv[p[0].strip()] = p[1].strip()
    def kb(k): return int(kv.get(k, "0").split()[0]) if kv.get(k) else 0
    total = kb("MemTotal"); avail = kb("MemAvailable")
    used_pct = round((total - avail) * 100 / total) if total else 0
    return {"total_mb": total // 1024, "avail_mb": avail // 1024, "used_pct": used_pct}


def parse_loadavg(text: str) -> dict:
    """/proc/loadavg -> load 1/5/15 хв."""
    p = text.split()
    return {"l1": float(p[0]), "l5": float(p[1]), "l15": float(p[2])} if len(p) >= 3 else {}


def parse_uptime(text: str) -> int:
    """/proc/uptime -> секунди."""
    try:
        return int(float(text.split()[0]))
    except Exception:
        return 0


def _read(path: str) -> str:
    try:
        with open(path) as f: return f.read()
    except Exception:
        return ""


def read_cpu_temp() -> float | None:
    """Найгарячіша thermal-зона (°C), якщо доступна (старий ноут може не мати)."""
    best = None
    base = "/sys/class/thermal"
    try:
        for z in os.listdir(base):
            if z.startswith("thermal_zone"):
                v = _read(os.path.join(base, z, "temp")).strip()
                if v.isdigit():
                    c = int(v) / 1000.0
                    if best is None or c > best: best = c
    except Exception:
        pass
    return round(best, 1) if best else None


def service_state(names: list[str]) -> dict:
    out = {}
    for n in names:
        try:
            r = subprocess.run(["systemctl", "is-active", n], capture_output=True, text=True, timeout=5)
            out[n] = r.stdout.strip() or "unknown"
        except Exception:
            out[n] = "unknown"
    return out


def stats() -> dict:
    """Повне зведення ресурсів сервера."""
    mem = parse_meminfo(_read("/proc/meminfo"))
    load = parse_loadavg(_read("/proc/loadavg"))
    up = parse_uptime(_read("/proc/uptime"))
    cores = os.cpu_count() or 1
    try:
        st = os.statvfs("/")
        disk_total = st.f_blocks * st.f_frsize
        disk_free = st.f_bavail * st.f_frsize
        disk = {"total_gb": round(disk_total / 1e9, 1), "free_gb": round(disk_free / 1e9, 1),
                "used_pct": round((disk_total - disk_free) * 100 / disk_total) if disk_total else 0}
    except Exception:
        disk = {}
    return {
        "ts": int(time.time()), "cores": cores, "uptime_s": up,
        "load": load, "load_pct": round(load.get("l1", 0) * 100 / cores) if load else 0,
        "mem": mem, "disk": disk, "cpu_temp_c": read_cpu_temp(),
        "services": service_state(["espos-gateway", "espos-collector", "espos-dashboard"]),
    }
