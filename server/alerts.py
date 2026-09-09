"""
alerts — рушій правил-вартового для телеметрії плати. Чиста логіка (evaluate) +
дедуп (AlertGate) + Telegram-нотифаєр. Викликається з collector на кожному зразку.

Токен бота береться ЛИШЕ з config.json (сервер-сайд, gitignored) — у код не хардкодимо.
Пороги — у config["alerts"]. Тестується локально (test_alerts.py), без мережі/плати.
"""
from __future__ import annotations
import time
import requests


def evaluate(row: dict, cfg: dict) -> list[tuple[str, str]]:
    """Повертає [(key, message)] для порушених порогів на ОДНОМУ зразку. Чиста функція."""
    a = cfg.get("alerts", {})
    out: list[tuple[str, str]] = []

    temp = row.get("temp_c")
    if temp is not None and temp >= a.get("temp_crit_c", 70):
        out.append(("overheat", f"🔥 Перегрів плати: {temp:.1f}°C (поріг {a.get('temp_crit_c', 70)}°C)"))

    mv = row.get("batt_mv")
    if mv is not None and 0 < mv <= a.get("batt_low_mv", 3400):
        out.append(("batt_low", f"🔋 Батарея низька: {mv} mV (поріг {a.get('batt_low_mv', 3400)} mV)"))

    rssi = row.get("rssi")
    if rssi is not None and rssi != 0 and rssi <= a.get("rssi_low", -85):
        out.append(("rssi_low", f"📶 Слабкий Wi-Fi лінк: RSSI {rssi} dBm (поріг {a.get('rssi_low', -85)})"))

    heap = row.get("heap")
    if heap is not None and 0 < heap <= a.get("heap_low", 8000):
        out.append(("heap_low", f"🧠 Мало вільної RAM: heap {heap} B (поріг {a.get('heap_low', 8000)})"))

    return out


class AlertGate:
    """Дедуп: не слати той самий алерт частіше ніж cooldown_s. Коли стан відновився —
    clear() дозволяє наступний алерт одразу (щоб не «проковтнути» повторне порушення)."""
    def __init__(self, cooldown_s: int = 1800):
        self.cooldown = cooldown_s
        self._last: dict[str, float] = {}

    def ready(self, key: str, now: float | None = None) -> bool:
        now = time.time() if now is None else now
        if now - self._last.get(key, 0.0) >= self.cooldown:
            self._last[key] = now
            return True
        return False

    def clear(self, key: str) -> None:
        self._last.pop(key, None)


def send_telegram(cfg: dict, text: str) -> bool:
    """POST у Telegram Bot API. Токен/чат — з config["alerts"]. Тихо false, якщо не налаштовано."""
    a = cfg.get("alerts", {})
    token, chat = a.get("telegram_token", ""), a.get("telegram_chat", "")
    if not token or not chat:
        return False
    try:
        r = requests.post(f"https://api.telegram.org/bot{token}/sendMessage",
                          json={"chat_id": chat, "text": text}, timeout=8)
        return bool(r.ok)
    except Exception:
        return False


def send_telegram_document(cfg: dict, filename: str, content: str, caption: str = "") -> bool:
    """Надіслати повний звіт як .txt-документ (обходить ліміт 4096 символів sendMessage).
    Для великих наборів даних (усі мережі/хости/лог). Тихо false, якщо не налаштовано."""
    a = cfg.get("alerts", {})
    token, chat = a.get("telegram_token", ""), a.get("telegram_chat", "")
    if not token or not chat:
        return False
    try:
        data = {"chat_id": chat}
        if caption:
            data["caption"] = caption[:1024]
        r = requests.post(f"https://api.telegram.org/bot{token}/sendDocument",
                          data=data, files={"document": (filename, content.encode("utf-8"))}, timeout=20)
        return bool(r.ok)
    except Exception:
        return False


def notify(cfg: dict, text: str, title: str = "ESP32-OS") -> bool:
    """Універсальна відправка: короткий текст -> повідомлення; великий -> .txt-документ."""
    if not text:
        return False
    if len(text) <= 3500:
        return send_telegram(cfg, text)
    import time as _t
    fn = f"{title.replace(' ', '_')}_{_t.strftime('%Y%m%d_%H%M%S')}.txt"
    return send_telegram_document(cfg, fn, text, caption=title)
