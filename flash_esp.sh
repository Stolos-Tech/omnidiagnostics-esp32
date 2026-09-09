#!/usr/bin/env bash
# ESP32-OS -> прошивка T-Display (firmware + filesystem). Запуск із кореня проєкту.
# Порт за замовчуванням COM5 (CH9102). Приклад: ./flash_esp.sh COM5
set -e
PIO="$HOME/.platformio/penv/Scripts/platformio.exe"
PORT="${1:-COM5}"
cd "$(dirname "$0")"
echo "== [ESP32] build + upload FIRMWARE -> $PORT =="
"$PIO" run -e ttgo-t-display -t upload --upload-port "$PORT"
echo "== [ESP32] upload FILESYSTEM (web/scripts, LittleFS) -> $PORT =="
"$PIO" run -e ttgo-t-display -t uploadfs --upload-port "$PORT"
echo "== [ESP32] DONE. Монітор:  \"$PIO\" device monitor -p $PORT -b 115200 =="
