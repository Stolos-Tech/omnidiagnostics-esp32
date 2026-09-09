#!/usr/bin/env bash
# ESP32-OS home server installer (Ubuntu/Debian). Ставить Tailscale + Python-стек
# (gateway/collector/dashboard) як systemd-сервіси у /opt/espos. Ідемпотентний.
# Запуск:  sudo bash setup.sh
set -euo pipefail

DEST=/opt/espos
SRC="$(cd "$(dirname "$0")" && pwd)"
echo "== ESP32-OS server setup =="
echo "джерело: $SRC  ->  $DEST"

if [ "$(id -u)" -ne 0 ]; then echo "!! запусти через sudo"; exit 1; fi

echo "[1/6] системні пакети…"
apt-get update -qq
apt-get install -y -qq python3 python3-venv python3-pip curl

echo "[2/6] Tailscale…"
if ! command -v tailscale >/dev/null; then
  curl -fsSL https://tailscale.com/install.sh | sh
fi
echo "   -> далі ВРУЧНУ один раз: sudo tailscale up   (авторизуй у браузері)"
echo "   -> свій Tailscale IP:    tailscale ip -4     (впиши в config.json bind_host)"

echo "[3/6] файли у $DEST…"
mkdir -p "$DEST"
cp -f "$SRC"/gateway.py "$SRC"/collector.py "$SRC"/dashboard.py "$DEST"/
mkdir -p "$DEST/dashboard"; cp -f "$SRC"/dashboard/index.html "$DEST/dashboard/"
if [ ! -f "$DEST/config.json" ]; then
  cp "$SRC/config.example.json" "$DEST/config.json"
  echo "   -> СТВОРЕНО $DEST/config.json — ВІДРЕДАГУЙ (board_url/board_pin/gateway_token/bind_host)!"
  echo "   -> токен:  python3 -c \"import secrets;print(secrets.token_urlsafe(48))\""
fi

echo "[4/6] python venv + залежності…"
python3 -m venv "$DEST/venv"
"$DEST/venv/bin/pip" install --quiet --upgrade pip
"$DEST/venv/bin/pip" install --quiet flask requests

echo "[5/6] systemd-сервіси…"
for svc in gateway collector dashboard; do
  sed "s|__DEST__|$DEST|g" "$SRC/systemd/espos-$svc.service" > "/etc/systemd/system/espos-$svc.service"
done
# окремий користувач без привілеїв
id espos >/dev/null 2>&1 || useradd -r -s /usr/sbin/nologin espos
chown -R espos:espos "$DEST"
systemctl daemon-reload

echo "[6/6] готово. ПІСЛЯ редагування config.json та 'tailscale up' запусти:"
echo "   sudo systemctl enable --now espos-gateway espos-collector espos-dashboard"
echo "   перевірка:  systemctl status espos-*   |   journalctl -u espos-gateway -f"
echo "   дашборд:    http://<твій-tailscale-ip>:8080"
