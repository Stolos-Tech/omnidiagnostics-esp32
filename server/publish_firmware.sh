#!/usr/bin/env bash
# publish_firmware.sh — покласти новий app-образ ESP32 у OTA-диру, щоб додаток міг
# зачитати його по /firmware.bin і прошити плату через USB-OTG з телефона.
#
# Використання:
#   ./publish_firmware.sh <firmware.bin> [versionName] ["нотатки"] [offset_hex]
# offset за замовч. 0x10000 (стандартна app-партиція; bootloader/parttable не чіпаємо).
#
# sha256/size додаток рахує сам із файлу через /api/firmware — тут firmware.json лише
# для versionName/notes/offset (необов'язковий).
set -euo pipefail

SRC="${1:?вкажи шлях до firmware.bin}"
VER="${2:-$(date +%Y%m%d-%H%M)}"
NOTES="${3:-}"
OFFSET="${4:-0x10000}"

OTA_DIR="$(python3 - <<'PY'
import json,os
c=json.load(open("/opt/espos/config.json"))
print(os.path.dirname(c.get("firmware_path","/opt/espos/ota/firmware.bin")))
PY
)"
DEST="$OTA_DIR/firmware.bin"

mkdir -p "$OTA_DIR"
cp -f "$SRC" "$DEST"
SHA="$(sha256sum "$DEST" | awk '{print $1}')"
SIZE="$(stat -c%s "$DEST")"

cat > "$OTA_DIR/firmware.json" <<JSON
{
  "versionName": "$VER",
  "notes": "$NOTES",
  "offset": $((OFFSET)),
  "sha256": "$SHA",
  "size": $SIZE
}
JSON

echo "опубліковано: $DEST"
echo "  version=$VER  size=$SIZE  offset=$OFFSET"
echo "  sha256=$SHA"
echo "додаток: HELP -> ПРОШИВКА ПЛАТИ -> СКАЧАТИ/ЗАЛИТИ/ТЕСТИ"
echo ""
echo 'ТЕСТИ оновлення (кнопка ТЕСТИ): додай у firmware.json масив "tests" —'
echo '  кожен тест {name, path (ендпоінт плати), expect (підрядок у відповіді)}:'
echo '  "tests":[{"name":"USB-контроль","path":"/api/status","expect":"sta"},'
echo '           {"name":"fw-статус","path":"/fw/status","expect":"factory"}]'
echo '  Після успіху додаток просить сервер видалити tests (економія місця).'
