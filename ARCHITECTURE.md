# Архітектура esp32-os

Технічний опис рантайму для LilyGO TTGO T-Display (ESP32-WROOM-32).
README дає quick-start; цей файл пояснює, **як воно влаштоване всередині** —
модель застосунків, транспорти, веб-API, обмеження ресурсів.

---

## 1. Головний цикл (`src/main.cpp`)

Ядро — кооперативний однопотоковий цикл (жодних RTOS-задач для застосунків).
Кожен `loop()` виконує:

1. **Watchdog reset** — `esp_task_wdt_reset()` на початку тіку (таймаут 30с).
   Якщо будь-який застосунок зависне в блокуючій операції довше — плата
   перезавантажується сама.
2. **Черга подій** — `input_queue`: кнопки (`drivers/buttons`) і команди з
   телефона (WS) складаються в ЄДИНУ чергу; активний застосунок їх не розрізняє.
3. **`active_app->loop()`** — неблокуючий крок логіки застосунку.
4. **Малювання** (троттл ~30fps) — застосунок малює у повноекранний спрайт
   `TFT_eSprite` 240×135 16bpp; `display_push()` виштовхує його на екран за один
   SPI-трансфер. Пропускаємо кадри в пасивному режимі (екран off).
5. **Трансляція стану** у WS-клієнтів (див. §4): `remote_state`, `status`, `log`.
6. **Фонові служби**: RGB-статус, brownout-guard, mDNS-анонс, sleep-таймер.

Ключова інваріанта: **нічого не блокує цикл довше ~20мс**. Мережеві операції
(HTTP GET, свіп портів) — інкрементальні або з короткими таймаутами.

---

## 2. Модель застосунку (`kernel/app_interface.h`)

Кожен інструмент — клас, що успадковує `App`:

| Метод | Призначення |
|-------|-------------|
| `name()` | назва для меню |
| `init()` | запуск із лаунчера (виділити ресурси) |
| `loop()` | неблокуючий крок, щоцикл |
| `draw()` | малювання у спрайт |
| `button(id)` | подія з єдиної черги (кнопка АБО телефон) |
| `text(field, value)` | текстовий ввід із телефона (пароль, URL, діапазон…) |
| `select_index(idx)` | прямий вибір пункту списку (тап у веб) |
| `on_exit()` | звільнити ресурси при виході |
| `wants_exit()` | застосунок сам просить вихід у лаунчер |
| `remote_state()` | JSON-дзеркало стану для клієнта (список/статус) |

Той самий контракт мають **Berry-скрипти** (`app_draw`/`app_button`/…) —
`BerryScriptApp` адаптує їх під `App`.

### GroupApp (`apps_builtin/group_app`)

Композит: одна «апка», що містить кілька суб-апок і показує селектор режиму.
`configure(name, page, members, count)`; `mode_ = -1` → екран вибору, інакше
делегує всі виклики `members_[mode_].app`. Використано, щоб згорнути ~36 пунктів
меню у 5 груп (Discover / Web / Net Diag / Air / Bluetooth) без переписування
суб-апок — вони лишаються глобальними інстансами (на них також посилається
`self_test_targets`).

> **Правило:** при будь-якій зміні складу/порядку `builtin_apps[]` треба підняти
> `MENU_LAYOUT_VERSION`. Курсор і автозапуск зберігаються в NVS за ІНДЕКСОМ; без
> версійного гейта старий індекс вкаже на іншу апку (колись це стартувало
> Remote:BT під час setup → краш `vQueueDelete`).

---

## 3. Транспорти віддаленого керування (`src/remote/`)

- **`transport_wifi`** — SoftAP (`OmniDiag-Setup`) + STA одночасно (`WIFI_AP_STA`).
  `WebServer` :80 (сторінка + REST) і `WebSocketsServer` :81 (керування + стан).
- **`transport_bt`** — Bluetooth SPP-канал (альтернатива WiFi; вмикається окремо,
  бо BT і WiFi конкурують за радіо).
- **`protocol`** — будує/парсить JSON (`protocol_build_menu/status/log`,
  `remote_handle_incoming`). `session` тримає PIN-авторизацію.

AP-пароль **статичний** між рестартами (зберігається в NVS `remcred/appw`) — це
свідомий компроміс (зручність понад рандом-щоразу): колись пароль, що
регенерувався щобуту, викликав вічне «authenticating» на телефоні, що тримав старий.

### Авторизація

WS-клієнт надсилає `{"pin":"<PIN>"}` першим повідомленням. До успіху керування
недоступне. `/fs/*`, `/reports/*` (крім read-only), `/sys/*`, `/script/run`,
`/wifi/*` вимагають пройденої PIN-сесії (`require_auth`). `/api/*` — read-only,
доступні без PIN у межах LAN.

---

## 4. Веб-API (`transport_wifi.cpp`)

### Сторінка
- `GET /` — віддає **gzip-предстиснуту** `/web/index.html.gz` (~16КБ проти ~52КБ).
  `streamFile` САМ додає `Content-Encoding: gzip` для `.gz` — вручну заголовок НЕ
  додавати (подвійний ламає розпакування). Стиск робить `gzip_web.py` (extra_script)
  при збірці LittleFS.

### REST (read-only, без PIN)
| Маршрут | Віддає |
|---------|--------|
| `GET /api/status` | sta/ap/rssi/mv/usb — для іконок |
| `GET /api/battery` | mv/pct/usb |
| `GET /api/info` | chip/cpu/uptime/heap/ip |
| `GET /api/stats` | **здоров'я ресурсів**: heap (free/min-ever/largest-block/total), uptime, ws/ap-клієнти, к-сть звітів, log-lines, sketch-free, STA (ssid/ip/rssi) |

`heap_maxblk` (найбільший суцільний блок) проти `heap` дає **фрагментацію** —
веб-Stats рахує `1 − maxblk/heap`.

### Дії (потребують PIN)
| Маршрут | Дія |
|---------|-----|
| `/fs/list,get,save,del` | менеджер `.be`-скриптів (по категоріях) |
| `/reports/list,get,save,del` | знімки списків у LittleFS |
| `/reports/export` | **усі звіти одним завантаженням** (chunked через `sendContent` — без великого String у RAM) |
| `/reports/push` | плата сама POST-ить кожен звіт Telegram-боту (`settings_bot_url`) |
| `/script/run` | запуск Berry-коду з тіла POST (або збереженого `?name=`), захоплює `print()` |
| `/sys/reboot,passive` | софт-ребут (заміна зламаній RST) / екран off |
| `/wifi/saved,auto,forget` | вибір мереж для авто-підключення |

### WebSocket :81 (двонапрямний)
- **Вхід** (клієнт→плата): `{"pin"}`, `{"idx":N}`, `{"btn":"S2"}`, `{"back":true}`,
  `{"text":<field>,"value":<v>}`.
- **Вихід** (плата→клієнт, broadcast): `{"status":…}` (раз/2с), `{"log":[…]}`
  (раз/1с, нові рядки з `log_ring`), інакше `remote_state()` активного застосунку.

---

## 5. Ядро (`src/kernel/`)

| Модуль | Роль |
|--------|------|
| `launcher` / `launcher_ui` | меню, курсор, запуск апок |
| `input_queue` | єдина черга подій (кнопки + телефон) |
| `settings` | NVS-налаштування (яскравість, автозапуск, mDNS, bot-url, layout-version) |
| `log_ring` | кільцевий буфер логу (CAP=40), `since(cursor)` для веб-консолі |
| `power_state` / `power_guard` | класифікація живлення, brownout-захист |
| `format_util` / `net_util` / `url_util` | чиста логіка (тестується на хості) |
| `script_path` / `device_id` | шляхи `.be` по категоріях, ID пристрою |
| `sys_ctl` | `sys_request_reboot` / `sys_set_passive` (volatile-прапорці) |

`drivers/` — залізо: `display`, `buttons`, `battery_adc`, `filesystem`,
`wifi_sta`, `mdns_service`, `rgb_status`, `logger`, `report_store`.

---

## 6. Обмеження ресурсів

**Купа (heap) — головний вузол.** Найбільший споживач — спрайт TFT (240×135×2 =
~64КБ). Плюс `WiFi AP+STA`, `WebServer`, `WebSocketsServer` (~2.1КБ/клієнт),
Berry VM. Idle-heap ≈ **26.5КБ** (після оптимізацій: captive-буфер на купу за
потребою замість 4КБ статики; троттл малювання).

Наслідки в коді:
- Веб-сторінку віддаємо **gzip** — інакше при малій купі трансфер обривається.
- **mDNS OFF за замовчуванням** — респондер їсть ~5.6КБ; вмикається свідомо
  (`mdns on`), бо «знати адресу» вирішує показ LAN-IP на екрані Remote:WiFi.
- Одноразові великі буфери (парсинг captive-порталу 4КБ, експорт звітів) —
  `malloc`/chunked, **не статика**.

**Флеш:** кастомна таблиця розділів `partitions_esp32os.csv` (без OTA, великий
app-розділ — WiFi+BT+Berry не влазять у дефолт). Зайнято ~77%.

---

## 7. Збірка й тести

```sh
# env називається ttgo-t-display (НЕ lilygo-t-display — то board_id)
PYTHONIOENCODING=utf-8 pio run -e ttgo-t-display          # прошивка
pio run -e ttgo-t-display -t upload --upload-port COM5    # заливка по USB
pio run -e ttgo-t-display -t uploadfs                     # LittleFS (авто-gzip web)
pio test -e native                                        # 18 наборів юніт-тестів на хості
```

`extra_scripts = gzip_web.py` — `AddPreAction` на `littlefs.bin`: гзипить
`data/web/index.html` → `index.html.gz` перед пакуванням образу.

Native-тести (`[env:native]`) компілюють чисту логіку ядра під хост
(`test/native/test_*`) — покривають launcher, protocol, session, format/net/url-util,
log_ring, power_state/guard, backlight, button_edge, tap_hold, remote_dispatch,
script_path, device_id, berry_bridge, input_queue, line_assembler.
