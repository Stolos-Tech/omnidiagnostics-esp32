"""
insights — тренд-аналітика телеметрії (чиста логіка, тестується локально). Рахує з історії
SQLite: здоров'я/розряд батареї, температурні тренди, середнє споживання, оцінку часу до розряду.
Викликається dashboard /api/insights.
"""
from __future__ import annotations


def _slope_per_h(pts: list[tuple[int, float]]) -> float | None:
    """Нахил y за ts (у годинах) методом найменших квадратів. pts=[(ts_s, y)]. None якщо мало точок."""
    pts = [(t, y) for t, y in pts if y is not None]
    n = len(pts)
    if n < 3:
        return None
    t0 = pts[0][0]
    xs = [(t - t0) / 3600.0 for t, _ in pts]           # години від старту
    ys = [y for _, y in pts]
    mx = sum(xs) / n; my = sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    if den == 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den


def insights(rows: list[dict]) -> dict:
    """rows: [{ts,temp,power,mv,rssi}] за вікно. Повертає зведення трендів."""
    if not rows:
        return {"samples": 0}
    ts = [r["ts"] for r in rows]
    mv = [r.get("mv") for r in rows if r.get("mv")]
    tc = [r.get("temp") for r in rows if r.get("temp") is not None]
    pw = [r.get("power") for r in rows if r.get("power") is not None]
    span_h = round((ts[-1] - ts[0]) / 3600.0, 2)

    out: dict = {"samples": len(rows), "span_h": span_h}

    if mv:
        drain = _slope_per_h([(r["ts"], r["mv"]) for r in rows if r.get("mv")])   # mV/год (від'ємне = розряд)
        last = mv[-1]
        eta_h = None
        if drain is not None and drain < -1:                # реально розряджається
            eta_h = round((last - 3300) / (-drain), 1)       # до 3300 mV (майже пуста)
            if eta_h < 0:
                eta_h = 0.0
        out["battery"] = {
            "last_mv": last, "min_mv": min(mv), "max_mv": max(mv),
            "drain_mv_h": round(drain, 1) if drain is not None else None,
            "eta_h": eta_h,
            "pct": _li_pct(last),
        }
    if tc:
        out["temp"] = {"last_c": round(tc[-1], 1), "avg_c": round(sum(tc) / len(tc), 1), "max_c": round(max(tc), 1)}
    if pw:
        out["power"] = {"avg_ma": round(sum(pw) / len(pw)), "max_ma": max(pw)}
    return out


def _li_pct(mv: int) -> int:
    """Груба SoC Li-ion за напругою спокою (4200=100%, 3300=0%). Не точна під навантаженням."""
    return max(0, min(100, round((mv - 3300) * 100 / (4200 - 3300))))
