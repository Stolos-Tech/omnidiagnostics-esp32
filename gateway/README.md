# ESP32-OS Secure Gateway

Міст «телефон звідусіль → плата в домашній мережі» — як смарт-хоум/камери, але під наш контроль.
Крипто НЕ на ESP32 (йому важко) — усе шифрування бере **Tailscale (WireGuard)**, а цей сервіс
проксить до плати й додає токен-шар. Запускається на домашньому Linux-девайсі (старий ноут = ідеально).

## Архітектура

```
 phone (будь-де)                     home Linux box                     board (LAN)
 ┌─────────────┐   Tailscale/WG    ┌───────────────┐      LAN :80     ┌──────────┐
 │  Android    │══════════════════▶│  gateway.py   │─────────────────▶│ ESP32-OS │
 │ +Tailscale  │  (шифровано,      │ +Tailscale    │  (PIN тут)       │  REST/WS │
 └─────────────┘   лише твій       │  token-шар    │                  └──────────┘
                   tailnet)        └───────────────┘
```

## Шари безпеки (обидва боки)

1. **Транспорт — Tailscale (WireGuard):** канал шифрований, у нього пускає ЛИШЕ пристрої твого
   tailnet (device-auth за ключами). Немає прокидання портів, немає відкритого endpoint в інтернеті.
2. **Токен:** кожен запит несе `X-Gateway-Token` (довгий випадковий секрет) — 2-й бар'єр, якщо
   чужий девайс якось у tailnet. Порівняння constant-time.
3. **PIN плати — тільки в `config.json` на шлюзі**, у мережу до телефона НЕ виходить. Шлюз сам
   логіниться на плату й тримає сесію.
4. **Bind лише на Tailscale-IP** (`100.x.y.z`) → недосяжний із відкритого інтернету й навіть із LAN.
5. **Rate-limit** (120 req/хв на IP за замовч.) — проти brute.

## Налаштування (на Linux-боксі)

```bash
# 1. Tailscale на боксі І на телефоні (застосунок Tailscale) — один акаунт/tailnet
curl -fsSL https://tailscale.com/install.sh | sh && sudo tailscale up
tailscale ip -4          # -> твій 100.x.y.z (впиши в config bind_host)

# 2. Залежності
pip install flask requests

# 3. Конфіг
cp config.example.json config.json
#   board_url    = http://<IP плати в LAN>
#   board_pin    = PIN плати
#   gateway_token= python -c "import secrets;print(secrets.token_urlsafe(48))"
#   bind_host    = 100.x.y.z (Tailscale IP боксу)

# 4. Запуск (як сервіс — systemd unit нижче)
python gateway.py
```

У додатку: **Host = `100.x.y.z` (Tailscale IP шлюзу), Port = 8443**, + токен у налаштуваннях безпеки.
Локальний тест (без Tailscale): `bind_host=127.0.0.1`, і `curl -H "X-Gateway-Token: <tok>" http://127.0.0.1:8443/api/status`.

## systemd (авто-старт на сервері)

```ini
# /etc/systemd/system/espos-gateway.service
[Unit]
Description=ESP32-OS gateway
After=network-online.target tailscaled.service
[Service]
ExecStart=/usr/bin/python3 /opt/espos/gateway/gateway.py
Restart=always
User=espos
[Install]
WantedBy=multi-user.target
```
`sudo systemctl enable --now espos-gateway`

## Плата: веб-хости

Коли керуєш ЛИШЕ з телефона — HTML-веб (index.html) на платі можна вимкнути (звільнити flash/RAM),
REST/WS лишити (їх юзає шлюз). Для ПК-браузера веб потрібен — тож зробимо **toggle** у прошивці
(serial/endpoint) «web UI on/off». REST/WS API незмінні для обох шляхів.

## Перевірено
Локально (127.0.0.1) проти живої плати: health ok, token-gate (401 без/з невірним токеном), проксі
`/api/status` `/api/sysinfo` (temp 43.6°C, power 181mA), PIN не виходить у клієнта. Tailscale-рівень —
конфіг (bind на 100.x.y.z + застосунок Tailscale на телефоні).
