#!/usr/bin/env bash
# ESP32-OS -> прошивка Arduino UNO R3 (окремий проєкт arduino/uno_r3).
# Порт за замовчуванням COM7 (CH340). Приклад: ./flash_uno.sh COM7
set -e
PIO="$HOME/.platformio/penv/Scripts/platformio.exe"
PORT="${1:-COM7}"
cd "$(dirname "$0")/arduino/uno_r3"
echo "== [UNO] build + upload -> $PORT =="
"$PIO" run -e uno -t upload --upload-port "$PORT"
echo "== [UNO] DONE. Монітор:  \"$PIO\" device monitor -p $PORT -b 9600 =="
