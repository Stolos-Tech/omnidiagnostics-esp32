# EspOsApp — Android-клієнт ESP32·OS

Нативний Android-додаток (Jetpack Compose) для керування платою по WiFi (REST:80).
Архітектура за **Flipper Android** (4-таб shell + `bridge`-клієнт).

## Відкрити / зібрати

1. **Android Studio** (Hedgehog+): `File → Open` → ця тека `mobile/android/`.
   IDE сама згенерує gradle-wrapper і синхронізує залежності.
2. Або CLI: `gradle wrapper` (раз), потім `./gradlew :app:assembleDebug`.
3. Запуск на телефоні (в одній WiFi з платою або на її точці `OmniDiag-Setup`):
   ввести Host (напр. `192.168.50.53`) + PIN (`036163`) → **Connect**.

> ⚠️ Не компілювалось у цій сесії (тут нема Android SDK) — це робочий скелет,
> перевіряй/доводь у Android Studio. Клієнтський шар (`EspOsClient`) — порт із
> `mobile/espos_client.py`, який ПЕРЕВІРЕНО наживо проти плати.

## Структура (за Flipper `bridge`/4-tab)

```
app/src/main/java/one/espos/
  client/EspOsClient.kt   — транспорт+сесія+типізовані ендпоінти (OkHttp REST)
  client/WsStream.kt      — стрім екрана через WS:81 (screen streaming, як Flipper)
  client/Discovery.kt     — mDNS-дискавері плати (Android NsdManager)
  app/MainActivity.kt     — 4-таб shell + ViewModel + екрани
```

| Таб | Екран | API |
|---|---|---|
| **Device** | live-мірор екрана + D-pad; **LIVE/poll** тумблер (WS:81 vs полінг); тап пункту = запуск | `/api/mirror` `/api/cmd` · ws:81 |
| **Archive** | збережене за категоріями (як Flipper FlipperKeyType); **тап = перегляд вмісту** (network/channel логи) | `/api/archive` `/reports/get` |
| **Apps** | Berry-скрипти (= FAP-аналог) | `/fs/list` |
| **Tools** | SD-статус, sub-GHz радіо, перемикач тем | `/api/sd` `/theme` `/api/subghz/spectrum` |

Connect-бар має **Find** (mDNS) — знаходить плату без вводу IP.

## Готово в цій ітерації
- ✅ 4-таб shell (Device/Archive/Apps/Tools) + ViewModel + REST-клієнт (порт з перевіреного Python).
- ✅ **WS-стрім** екрана (`WsStream`, OkHttp WebSocket) з фолбеком на полінг.
- ✅ **mDNS-дискавері** (`Discovery`, NsdManager) — авто-пошук плати.
- ✅ Перегляд вмісту звіту (діалог).

## Наступне (roadmap)
- Нативні графіки спектра (subghz/nrf) на Canvas.
- SD-менеджер (`/api/sd/list` `/api/sd/get`), редактор скриптів (`/fs/save`).
- BLE-транспорт за тим самим інтерфейсом (без WiFi) — див. mobile_arch артефакт.
- ПРИМІТКА: `FilterChip` у M3 може вимагати `@OptIn(ExperimentalMaterial3Api::class)` — Android Studio підкаже.

Референс-архітектура: https://claude.ai/code/artifact/98bc450c-eb02-43b6-8b05-14a75504bb64
