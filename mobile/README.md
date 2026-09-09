# ESP32·OS — мобільний клієнт

Клієнтський шар для майбутнього мобільного додатка (iOS/Android). Плата вже — повний
backend (REST:80 + WS:81); тут — типізований клієнт цього API, за архітектурою
**Flipper Android** (`bridge:connection`): `Transport → Session → feature-методи`.

## Файли

| Файл | Що це |
|---|---|
| `espos_client.py` | **Референс-клієнт (Python), ПЕРЕВІРЕНИЙ наживо** — executable-специфікація API. |
| `demo.py` | Наскрізний прогін усіх 4 табів проти живої плати (`python demo.py [host] [pin]`). |
| `kotlin/EspOsClient.kt` | **Порт на Kotlin (1:1)** — стартова точка Android-додатка (OkHttp + kotlinx.serialization). |

## 4-таб модель (за Flipper `BottomBarTabEnum`)

| Таб | Методи клієнта | Board-ендпоінти |
|---|---|---|
| **DEVICE** | `version` `status` `mirror` `cmd` | `/api/version` `/api/status` `/api/mirror` `/api/cmd` |
| **ARCHIVE** | `archive` `report` | `/api/archive` (типізовано за категоріями) `/reports/get` |
| **APPS** | `scripts` | `/fs/list` (Berry .be = FAP-аналог) |
| **TOOLS** | `subghzSpectrum` `nrfSpectrum` `sd*` | `/api/subghz/spectrum` `/api/nrf/spectrum` `/api/sd` |
| Settings | `themes` `setTheme` | `/theme` |

## Ключові правила (з живих тестів)

- **Discovery:** mDNS `esp32os.local`, або пряма IP. Або точка плати `OmniDiag-Setup` (без домашньої мережі).
- **Auth:** `POST /api/login {"pin":".."}` — глобальна сесія; один логін відкриває всі PIN-gated виклики.
- **POST-тіло = `text/plain`** (плата читає `arg("plain")`); form-urlencoded НЕ пройде — критично для порту.
- **Стрім екрана:** поллити `mirror()` (~350мс) АБО відкрити WS:81. Мобілка може почати з полінгу (простіше).
- **Навігація = дані:** меню приходить у `mirror().items`; запуск пункту = `cmd(idx=N)`, назад = `cmd(back=true)`.

## Наступні кроки (Android)

1. Створити Android-проєкт (Compose), додати `EspOsClient.kt` як `bridge`-модуль (api/impl рознести).
2. **4-таб shell** (`BottomBar`): Device / Archive / Apps / Tools — рендерити зі стану клієнта.
3. **DEVICE**: live-mirror (рендер `items` як список) + D-pad контролери (`cmd(btn=..)`), status-бар.
4. **ARCHIVE**: групувати `archive()` за `cat` (subghz/rfid/wifi/network/detect/system) — як Flipper за FlipperKeyType.
5. **TOOLS**: нативні графіки спектра (subghz/nrf), SD-менеджер.
6. Пізніше: **BLE-транспорт** за тим самим інтерфейсом `Transport` (коли треба керування без WiFi).

Референс-архітектура (діаграми, транспорт-порівняння, мапінг Flipper 1.8.1):
https://claude.ai/code/artifact/98bc450c-eb02-43b6-8b05-14a75504bb64
