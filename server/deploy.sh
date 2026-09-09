#!/usr/bin/env bash
# Оновлення коду ESP32-OS сервера (Ф1-2 вартовий+netwatch) + OTA-дира + безпечний
# апгрейд config.json. Запуск на stolos:  sudo bash ~/espos-server/deploy.sh
set -euo pipefail
DEST=/opt/espos
SRC="$(cd "$(dirname "$0")" && pwd)"
[ "$(id -u)" -eq 0 ] || { echo "!! запусти через sudo"; exit 1; }
OTA_USER="${SUDO_USER:-stolos}"

echo "[deploy] код -> $DEST"
cp -f "$SRC"/*.py "$DEST"/                                  # alerts/netwatch/collector/dashboard/gateway
mkdir -p "$DEST/dashboard"; cp -f "$SRC/dashboard/index.html" "$DEST/dashboard/"

echo "[deploy] OTA-дира (пише $OTA_USER, читає espos)"
mkdir -p "$DEST/ota"
chown "$OTA_USER":"$OTA_USER" "$DEST/ota"; chmod 755 "$DEST/ota"
[ -f "$SRC/app.apk" ] && { cp -f "$SRC/app.apk" "$DEST/ota/app.apk"; chown "$OTA_USER":"$OTA_USER" "$DEST/ota/app.apk"; echo "  APK покладено з теки"; } || true

echo "[deploy] апгрейд config.json (не чіпає board_url/pin/token; лише додає відсутнє)"
python3 - "$DEST/config.json" <<'PY'
import json, sys
p = sys.argv[1]; c = json.load(open(p))
c.setdefault("netscan_interval_s", 300)
c.setdefault("apk_path", "/opt/espos/ota/app.apk")
c.setdefault("firmware_path", "/opt/espos/ota/firmware.bin")   # USB/SD флеш плати
c.setdefault("log_retention_days", 0)                          # 0 = сесійні логи не чистяться авто
c.setdefault("netguard", {}).setdefault("enabled", False)      # активний допуск: детект+черга
c["netguard"].setdefault("enforce", False)                     # блокування на роутері (ТІЛЬКИ після live-тесту)
a = c.setdefault("alerts", {})
for k, v in {"enabled": True, "telegram_token": "PUT-BOT-TOKEN-HERE", "telegram_chat": "YOUR-TELEGRAM-CHAT-ID",
             "temp_crit_c": 70, "batt_low_mv": 3400, "rssi_low": -85, "heap_low": 8000,
             "offline_s": 120, "cooldown_s": 1800, "alert_random_mac": False, "alert_join": False}.items():
    a.setdefault(k, v)
json.dump(c, open(p, "w"), indent=2)
print("  config.json оновлено (firmware_path/netguard/log_retention/alert_join додано якщо бракувало)")
PY

# власник коду/конфігу — espos (сервіси під ним); OTA-дира лишається за OTA_USER
chown espos:espos "$DEST"/*.py "$DEST/config.json" "$DEST/dashboard/index.html"

echo "[deploy] рестарт сервісів"
systemctl restart espos-collector espos-dashboard espos-gateway
sleep 1; systemctl is-active espos-collector espos-dashboard espos-gateway || true

echo ""
echo "== ГОТОВО =="
echo "  Дашборд:  http://<tailscale-ip>:8080     (+ панель NETWORK·DEVICES)"
echo "  OTA:      http://<tailscale-ip>:8080/ota  (телефон -> Встановити)"
echo "  !! Впиши bot token у $DEST/config.json (alerts.telegram_token), тоді:"
echo "        sudo nano $DEST/config.json  &&  sudo systemctl restart espos-collector"
echo "     (chat_id YOUR-TELEGRAM-CHAT-ID уже стоїть; без токена алерти тихо вимкнені)"
