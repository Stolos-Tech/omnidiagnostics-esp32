#!/usr/bin/env bash
# publish_apk.sh — опублікувати APK для OTA-самооновлення додатка. ЦЕ фіксить
# «додаток не оновлюється вище 0.25»: сервер має віддавати НОВИЙ app.apk + manifest.json
# з versionCode > встановленого. Без цього /api/manifest лишається на старій версії.
#
# Використання:
#   ./publish_apk.sh <app.apk> <versionCode> <versionName> ["нотатки"]
# напр.:  ./publish_apk.sh espos-app-v0.26.apk 26 0.26 "USB-керування + реле"
set -euo pipefail

SRC="${1:?вкажи шлях до APK}"
VC="${2:?вкажи versionCode (ціле, напр. 26)}"
VN="${3:?вкажи versionName (напр. 0.26)}"
NOTES="${4:-}"

OTA_DIR="$(python3 - <<'PY'
import json,os
c=json.load(open("/opt/espos/config.json"))
print(os.path.dirname(c.get("apk_path","/opt/espos/ota/app.apk")))
PY
)"
DEST="$OTA_DIR/app.apk"

mkdir -p "$OTA_DIR"
cp -f "$SRC" "$DEST"
SHA="$(sha256sum "$DEST" | awk '{print $1}')"
SIZE="$(stat -c%s "$DEST")"

cat > "$OTA_DIR/manifest.json" <<JSON
{
  "versionCode": $VC,
  "versionName": "$VN",
  "sha256": "$SHA",
  "size": $SIZE,
  "notes": "$NOTES"
}
JSON

echo "опубліковано APK: $DEST"
echo "  versionCode=$VC versionName=$VN size=$SIZE"
echo "  sha256=$SHA"
echo "перевір: curl -s http://localhost:8080/api/manifest"
echo "у додатку: HELP -> ОНОВЛЕННЯ ДОДАТКА -> ПЕРЕВІРИТИ"
