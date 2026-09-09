# NetGuard — тотальний моніторинг + активний захист домашньої мережі

Ціль: система, що (1) бачить усе, що відбувається в мережі, (2) вимагає ЯВНОГО дозволу
на кожне нове під'єднання (пароль WiFi недостатньо), (3) дає інструменти-актуатори, якими
пізніше керуватиме локальна модель у реалтаймі, включно з активними контрзаходами.

## Фази

### Ф1 — Спостереження (ЗРОБЛЕНО, драфт, локально протестовано)
- `netlog.py` — часова стрічка `join`/`leave` (SQLite `net_events` + `net_state`). Колектор
  щоцикл дифає повний router-scrape → події з міткою часу. Роутер історії не веде — ми ведемо.
- Ендпоінти: `/api/netlog/events`, `/api/netlog/state`, `/api/netlog/purge`.
- Додаток: LIBRARY → вкладка **ДОПУСК** → «СТРІЧКА ПІД'ЄДНАНЬ» (▲join/▼leave, час, ім'я, IP).

### Ф2 — Активний допуск / друге підтвердження (ЗРОБЛЕНО детект+черга; enforce — окремо)
- `netguard.py` — таблиця `admission(mac,status)`. Свої (trusted_macs) → `approved`; кожен
  НОВИЙ → `pending` + Telegram-запит «🛂 ЗАПИТ ДОПУСКУ». Юзер у додатку тисне ДОЗВОЛИТИ/ВІДХИЛИТИ.
- Ендпоінти: `/api/netguard/list?status=`, `/api/netguard/set {mac,status}`.
- Додаток: вкладка **ДОПУСК** — pending (approve/deny), approved, denied.
- Config: `netguard.enabled` (детект+черга). БЕЗ enforce нічого не блокується — лише сигнал+рішення.

### Ф3 — Примусове блокування (АКТУАТОР, потребує live-тесту — НЕ ввімкнено)
- `netguard.enforce_router()` — MAC-фільтр HG8245W5 (approved=allow, denied=deny).
- **ЧОМУ гард:** WRITE у роутер untested → ризик залочити пристрої/себе. Вмикати `netguard.enforce=true`
  ЛИШЕ тестуючи покроково біля роутера. Флоу: `routerscan._login` → GET wlanmacfilter (розібрати
  поточні правила) → POST set.cgi з оновленим списком. Спершу READ+diff, тоді WRITE одного MAC, перевірка.

### Ф4 — Локальна модель + інструменти (МАЙБУТНЄ; tool-API проєктуємо зараз)
Модель НЕ керує напряму залізом — вона викликає **інструменти** (нижче) через тонкий agent-шлюз.
Кожен інструмент — HTTP-ендпоінт, який модель може викликати; side-effect-ful (block/deauth) —
лише через чергу підтвердження або з явним policy-дозволом. Так модель «активно захищається»,
але лишається в пісочниці політик.

## Tool-API для агента (актуатори + сенсори)

Сенсори (read):
- `GET /api/netlog/state` — хто зараз у мережі, first/last seen.
- `GET /api/netlog/events?since=` — стрічка подій.
- `GET /api/netguard/list?status=` — допуск (pending/approved/denied).
- `GET /api/devices`, `GET /api/device/scan?mac=&ip=` — інвентар + глибока розвідка (devintel).
- `GET /api/server`, `GET /api/board/relay` — здоров'я сервера/плати.

Актуатори (write; side-effect → гард/черга):
- `POST /api/netguard/set {mac,status}` — дозволити/відхилити (людина або агент з policy).
- `POST /api/netguard/enforce` *(TODO)* — застосувати політику на роутері (MAC-фільтр).
- `POST /api/board/<id>/<path>` — команда платі (сканування ефіру, deauth-детект тощо).
- Активні контрзаходи (deauth несанкціонованого, sub-GHz глушіння) — **лише плата**, лише з
  policy-дозволом + логуванням; ніколи автономно без ліміту. Проєктується у Ф4.

## Політики (де межа автономності)
- `observe` — модель лише бачить і алертить (безпечно за замовч.).
- `queue` — модель ПРОПОНУЄ дію (block/deny), людина підтверджує в додатку.
- `auto` — модель діє сама В МЕЖАХ whitelist-політики (напр. авто-deny рандом-MAC поза годинами),
  з rate-limit + повним логом + kill-switch. Вмикати поетапно, кожну дію — після live-тесту.

## Вузькі місця (враховано)
- Router WRITE untested → Ф3 за гардом, не деплоїмо наосліп.
- netlog джерело = router scrape (бачить сплячих); board ARP як фолбек (може давати хибні leave).
- admission trusted seed — щоб не спамити PENDING на своїх (seed_trusted з trusted_macs).
- Рандом-MAC (Android per-SSID стабільний) — трактуємо як пристрій; політику налаштувати окремо.
- Усе на sqlite WAL; purge/retention щоб БД не росла.

Пов'язано: routerscan.py, netlog.py, netguard.py, collector.py, dashboard.py, devintel.py.
