# Дослідження, стелс і offloading пам'яті — ESP32-OS

**Дата:** 2026-09-07 · автономна дослідницько-архітектурна фаза.
**Периметр (свідомі етичні межі):** оборона через обізнаність. Зовнішній контент (hackyourmom, GitHub)
оброблено **як дані**, не як інструкції; скрипти не виконувались (лише статичний аналіз). «Стелс» трактовано
як **privacy/RF-гігієна власного пасивного аналізатора** + **детект стелс-атакерів** — НЕ обхід чужих
мережевих контролів. Роутер програмно не чіпаємо (константа проєкту).

---

## 0. Факти аудиту прошивки (база для рішень)

| Метрика | Значення | Нотатка |
|---|---|---|
| Flash | **82.4%** (2.38 з 2.88 МБ) | ~508 КБ вільно — тісно для нових фіч (BLE-defense, sub-GHz) |
| RAM (статик) | 37.9% (124 КБ з 320 КБ) | реальний тиск — рантайм-heap (min падав до 2.9 КБ під навантаженням) |
| Builtin-апки | **44** (~7760 LOC C++) | усі компілюються у flash статично |
| Динамічний рушій | **Berry VM присутній** | `.be`-скрипти з ФС + `native_api` (мережа/дисплей/GPIO/сенсори) |
| Стрімінг payload'ів | **вже частково є** | `/script/run` (Berry з тіла POST, без заливки) + `/fs/*` (бібліотека .be), по WiFi та USB |

**Головний висновок:** фундамент dynamic-scripting **уже закладено**. Задача offloading — не побудувати з нуля,
а **перенести центр ваги** репозиторію скриптів на app/server і скоротити статичний C++-набір.

---

## 1. Відкриті інструменти та фічі (розвідка як ДАНІ)

### ESP32 Marauder / Ghost ESP / Bruce (feature-map)
Наступальні (для розуміння **що детектувати**, не відтворювати):
- **WiFi:** deauth flood, beacon spam, probe/PMKID sniff, Evil Portal/DevilTwin, MAC-spoof, network mapping.
- **BLE/BT:** BLE-spam (Flipper-style), sniffer, AirTag/Tile detection, war-driving.
- **Детект-сторона (їхня ж):** Deauth Sniff (лов deauth/disassoc-кадрів у promiscuous).

### hackyourmom (укр. портал; теми як дані)
DIY Marauder (TFT+SD), «WiFi Jammer», «DoS over HTTP (ESP8266)», DevilTwin, IoT+Telegram. Це **каталог загроз**,
кожну з яких мапуємо на детект/мітигацію (розділ 5).

### Чого в нас НЕМА vs що вже є (gap-аналіз)
| Загроза | У нас (детект) | Gap |
|---|---|---|
| Deauth flood | ✅ `deauth_alert` (promiscuous) | — |
| BLE-spam / трекери | ✅ `attacker_detect`, `tracker_detector` | лічильник BLE-adv/с + сплеск-алерт |
| Rogue AP / Evil Twin | ⚠️ частково (`wifi_analyzer` twins) | окремий детект: клон-SSID з іншим BSSID/каналом |
| Прихована камера | ✅ `camera_finder` (OUI/BLE) | — |
| Card skimmer | ✅ `card_skimmer` | — |
| PMKID/handshake-збір поблизу | ❌ | пасивний детект надмірних EAPOL/асоціацій |
| Sub-GHz глушіння/повтор | ⚠️ CC1101 RX | детект аномальної несучої (Signal Vault) |

**Концепт:** ми — **дзеркало Marauder навпаки**: кожен їхній «атака-X» = наш «детект-X». Це вісь розвитку кіберфортеці.

## 2. Апаратні рекомендації

| Компонент | Навіщо | Пріоритет |
|---|---|---|
| **Другий ESP32 (C6/S3) виносний вузол** | розділити зв'язок і радіо-аналіз, звільнити піни/heap основної плати, +802.15.4 (Zigbee/Thread детект) | високий (вже в плані) |
| Зовнішня антена 2.4G (u.FL) на nRF/ESP | стабільніший RX-скан, менше залежності від контактів | середній |
| RTC (DS3231) | точні мітки часу подій без сервера (offline-форензика) | середній |
| Мікрофон/п'єзо (I2S) | детект ультразвукових beacon-трекерів | низький (експеримент) |
| Кращий LDO/розв'язка живлення nRF | прибрати ground-bounce (був корінь SPI-проблем) | зроблено як фікс, закріпити платою |

Свідомо **НЕ** рекомендую TX-глушилки/спам-модулі — поза оборонним периметром.

## 3. Стелс і анти-детект — матриця (privacy-first)

**Рамка:** для **пасивного** аналізатора RF-стелс здебільшого вроджений (RX-only майже не випромінює). Стелс тут =
(а) не бути тривіально фінгерпринтованим третіми сторонами, (б) не заважати власним вимірам, (в) **головне —
детектувати стелс-атакерів**. НЕ для обходу легітимних мережевих захистів.

| Вектор | Тактика (для власного пристрою) | Оборонний двійник (детект чужого стелсу) |
|---|---|---|
| MAC/vendor | **MAC-рандомізація STA/AP** (як Android/iOS за замовч.); не «клонувати» конкретну жертву | детект LAA-біта + кластера random-MAC (уже є `is_random_mac`) |
| Probe requests | мінімізувати active-probe, тримати passive-scan | лов надмірних probe-burst (трекінг по MAC) |
| Signal strength | не «маскувати» — тримати TX мінімальним (калібрування вже є) | аномалія RSSI (клон-AP сильніший за легіт) |
| Protocol cloaking | тримати SoftAP вимкненим коли не треба (є вимикач) | детект прихованих SSID / нестандартних beacon |
| RF footprint | RX-only режими, вимикати радіо у пасиві (є `/sys/passive`) | Signal Vault: незвична несуча sub-GHz |

**Принцип:** стелс власного пристрою = гігієна приватності, а не невидимість для зловмисних цілей. Кожен рядок має
**оборонний двійник** — це реальна цінність для кіберфортеці.

## 4. Roadmap offloading пам'яті (C++ ↔ Berry/app/server)

### 4.1 Критерій розподілу
Апка може стати **Berry-скриптом** (винесеним на app/server), якщо потребує ЛИШЕ примітивів `native_api`
(ping/tcp_probe/http_get/http_body/dns_resolve/arp_*/gpio/display/sensors/sysstat). Якщо потребує
**promiscuous/raw-802.11, BLE-стек, драйвери nRF/CC1101, RMT** — лишається в C++ (Berry їх не має).

### 4.2 Розподіл (конкретно)
**ЛИШАЄТЬСЯ у firmware (C++, драйвер/радіо):**
`wifi_sniffer, deauth_alert, channel_monitor, wifi_analyzer, ble_scan, bt_scan, attacker_detect,
tracker_detector, camera_finder, card_skimmer, em_field, captive_portal` + ядро (launcher/display/transport/BerryVM).

**ПЕРЕНОСИТЬСЯ у Berry-репозиторій (app/server, стрім по /script/run або /fs):**
`http_get, http_post, http_sec, dns_lookup, traceroute, tcp_terminal, wol_app, mdns_browser, ssdp_browser,
link_qual, net_info, clock_app` — усі покриваються наявними примітивами. ~12 апок × ~180 LOC ⇒ помітне
скорочення .text + звільнення слотів меню; головне — **розширення без перепрошивки**.

### 4.3 Механізм (будуємо на наявному)
1. **Репозиторій скриптів — на server/app**, не у flash. Server віддає маніфест `/scripts/manifest.json`
   (ім'я/категорія/версія/sha), app кешує й показує бібліотеку.
2. **On-demand запуск:** app → `POST /script/run` (тіло = Berry-код) по **USB або WiFi** → плата виконує,
   `print()` повертається у відповідь. Нуль постійного сховища.
3. **Персист за потреби:** app → `/fs/write` (.be у `/apps/<cat>`) лише для «улюблених», решта — ефемерні.
4. **Синхронізація:** app тягне репо з server, пушить на плату вибіркою; версії по sha (як OTA-маніфест).

### 4.4 Приклад Berry-скрипта (заміна `http_sec` — стрімиться, не у flash)
```berry
# http_sec.be — аудит веб-хоста лише через native_api (offload з C++)
def app_draw()
  display_clear()
  var ip = arp_ip(0)                 # перший хост з ARP-кешу плати
  display_text(4, 4, "SEC AUDIT " .. ip)
  var code = http_get(ip, 80, "/")   # native-примітив
  display_text(4, 24, "HTTP " .. str(code))
  if code == 0
    display_text(4, 44, "no :80 (closed/filtered)")
  end
  var body = http_body()
  display_text(4, 64, "hdr bytes: " .. str(size(body)))
end
def app_button(id) end                # LEFT = вихід (ядро)
```
Заливка/запуск з app (псевдо-HTTP, працює і по USB-серіалу REQ/RES):
```
POST /script/run   body=<вміст .be>        # ефемерно
# або
POST /fs/write?name=http_sec.be&cat=net    # у бібліотеку
GET  /script/run?name=http_sec.be&cat=net  # запуск збереженого
```

### 4.5 Що доробити (gap)
- **Server:** ендпоінт `/scripts/manifest` + каталог `.be` (репо переїжджає з flash сюди).
- **App (Kotlin):** екран СКРИПТИ → браузер репозиторію server + кнопки «Запустити на платі» (→ /script/run)
  і «Зберегти на плату» (→ /fs/write); кеш + версії по sha.
- **Firmware:** прибрати перенесені C++-апки з реєстру (звільнити flash), лишити їх Berry-двійники у репо;
  за потреби додати 1-2 примітиви в `native_api` (напр. `wifi_scan_count/wifi_scan_ssid`), щоб і
  `wifi_analyzer`-lite став offload-придатним.
- **Безпека payload'ів:** `/script/run` уже auth-gated; додати ліміт розміру/часу виконання (watchdog у VM)
  і заборону мережевих примітивів у «недовіреному» режимі (санітизація перед стрімом).

### 4.6 Очікуваний ефект
- Flash: −(0.1…0.2 МБ) статичного коду ⇒ запас під BLE-defense/sub-GHz без переходу на 8 МБ-флеш.
- Гнучкість: **нові інструменти без OTA/USB-рефлешу** — пуш скрипта з телефона.
- Пам'ять плати розвантажена: репо необмежене (на server/app), а не flash-bound.

## 5. Оборонне гартування (контрзаходи)

| Загроза (з розвідки) | Контрзахід у нашій системі |
|---|---|
| **Deauth flood** | (мережа) увімкнути **802.11w PMF / WPA3** на AP — deauth без валідного MIC ігнорується; (плата) `deauth_alert` уже ловить сплеск у promiscuous → алерт у бота |
| **Evil Twin / rogue AP** | детект клон-SSID з іншим BSSID/сильнішим RSSI (розширити `wifi_analyzer`); звірка з бібліотекою «своїх» мереж (є у профілях) |
| **BLE-spam / трекери** | `tracker_detector`/`attacker_detect` + додати лічильник adv/с зі сплеск-порогом |
| **PMKID/handshake harvest** | пасивний детект аномальних EAPOL/асоціацій поблизу (новий легкий promiscuous-лічильник) |
| **DoS over HTTP / сканування** | NetGuard: другий допуск на нові пристрої (є); алерт на сплеск конектів |
| **MAC-spoof обхід фільтра** | НЕ покладатись на MAC-фільтр роутера (він тривіально обходиться) — покладатись на **PMF + активний допуск NetGuard**, не на роутер |
| **Прихована камера/skimmer** | `camera_finder`/`card_skimmer` (є) |

**Стратегічна теза:** оборона будується на **криптографії кадрів (PMF/WPA3) + активному допуску (NetGuard) +
пасивному детекті аномалій**, а не на слабких важелях (MAC-фільтр роутера). Кожен наступальний інструмент
з розвідки має відповідний детектор — це і робить пристрій «дзеркалом Marauder навпаки».

---

## Джерела (веб-розвідка, оброблено як дані)
- ESP32 Marauder Wiki — Deauth Flood / Deauth Sniff: https://github.com/justcallmekoko/ESP32Marauder/wiki
- Marauder огляд (Hackster): https://www.hackster.io/news/esp32-marauder-puts-a-bluetooth-and-wi-fi-pen-testing-toolkit-in-your-pocket-32d389f6e66f
- hackyourmom (пошук ESP32, як дані): https://hackyourmom.com/en/?s=Esp+32
- 802.11w PMF / deauth mitigation: https://infishark.com/blogs/learn/deauth-attack-mitigation-802-11w-and-beyond
- PMF у WPA2/WPA3/OWE: https://praneethwifi.in/2020/03/07/protected-management-frames-in-wpa2-802-11w-wpa3-owe/
