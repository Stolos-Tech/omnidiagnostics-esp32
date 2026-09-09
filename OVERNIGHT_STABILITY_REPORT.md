# OVERNIGHT STABILITY REPORT — ESP32-OS

**Прогін:** 2026-09-07 · автономний QA-луп (Test → Fault → Refactor → Re-test → Benchmark).
**Периметр (свідомий, безпечний вибір):** стрес/fault-injection проти **локального коду + тимчасових БД**;
вхідні дані — **РЕАЛЬНІ з плати** (COM5 USB або WiFi HTTP 192.168.100.41), без вигаданих payload'ів.
Живий сервер `stolos` і роутер **не чіпалися** (ризик залочення). Плату силою не ребутали.

---

## 1. Загальна стабільність і pass-rate

| Набір | Тести | Pass | Fail |
|---|---|---|---|
| Пайплайн на реальних даних (`test_pipeline_real`) | 7 | 7 | 0 |
| Стрес БД/конкуренція (`test_db_stress`) | 4 | 4 | 0 |
| Fault-injection (`test_fault_real`) | 6 | 6 | 0 |
| devintel — глибокий скан живого хоста (`test_devintel_real`) | 5 | 5 | 0 |
| App API — Бібліотека `/lib/*` (`test_admin_api_real`) | 6 | 6 | 0 |
| Легасі-юніти (`test_alerts/insights/netwatch/srvstats`) | 21 | 21 | 0 |
| **РАЗОМ** | **49** | **49** | **0** |

**Pass-rate: 100%.** Дані джерела — жива плата (`board_available: true`), 5 реальних LAN-хостів +
28 реальних WiFi AP + реальний devscan + живий глибокий скан плати (.41: :80 open, класифіковано).

## 2. Знайдені й авто-виправлені баги

| # | Симптом (виявлено реальними даними) | Корінь | Фікс |
|---|---|---|---|
| **1** | `ingest_airscan` інжестив **0 з 28** реальних AP | плата шле `aps` як **dict** `{ssid,bssid,rssi,ch,enc}`, код чекав **list** `ap[0..]` → усе в `except` | приймає обидва формати (dict+list) |
| **2** | Конкурентні `upsert` → `UNIQUE constraint failed` (device_profiles.mac / networks.bssid,ssid) | SELECT-then-INSERT **не атомарний**; глобальний lock не рятує крос-процесно (collector/dashboard/admin — окремі процеси) | атомарний `INSERT … ON CONFLICT DO UPDATE` для мереж, пристроїв і лінку device↔network |
| **3** | Пошкоджений кадр із non-str MAC (`int`) валив **увесь** ingest і craш-лупив collector | `(mac or "").upper()` на не-рядку → `AttributeError` | захисне `str(...).strip().upper()` + валідація `isinstance(str)` в `ingest_netscan` і `netlog.record_scan` |

Усі три відтворені тестом, виправлені у джерелі, ре-тест зелений.

## 3. Бенчмарки (реальні payload'и)

- **Пропускна здатність (bulk replay):** 200 раундів × (5 host + 28 ap) = **6200 записів за 55.4с → 111.9 оп/с**.
  Обмежувач — connect+commit (WAL fsync) на кожну операцію; для фактичного навантаження
  (collector — раз/15с, netscan — раз/300с) із запасом достатньо. Не вузьке місце в бою.
- **Пам'ять:** пік tracemalloc у bulk-циклі = **1.6 КБ → витоку немає** (лінійна історія, профілі унікальні).
- **Конкуренція:** 8 потоків × 80 інжестів = **0 помилок БД, 0 lockup** (після фіксу #2); цілісність збережена
  (рівно N унікальних пристроїв попри гонку).
- **Індексація:** `EXPLAIN QUERY PLAN` підтвердив — гарячі запити б'{device by ip}' та '{history by device_id,ts}'
  йдуть по індексах `ix_dev_ip` / `ix_devhist_dev` (не full-scan).

## 4. Перевірені (без багів) підсистеми

- **Boot-loop prevention** (`boot_state.cpp`): heavy-апки (Net Scan/Analyzer/Sniffer/…) **ніколи** не авто-відновлюються;
  `pending` армується на старті й чиститься у `mark_stable` (>15с uptime); `crash≥2 → SAFE MODE (лаунчер)` + скид.
  Механізм коректний — «wipes dirty job states upon reboot» виконується.
- **Idempotency пайплайну:** повторний netscan не плодить дублі, `times_seen` росте; netlog join/leave ідемпотентний.
- **Admission (netguard):** trusted → approved (не спамить), новий → pending. На реальних MAC — коректно.
- **Offline-стійкість:** `collector.sample` при недосяжній платі повертає `None` (не виняток); частковий sysinfo не падає.
- **devintel (глибокий скан):** живий скан плати .41 — порти/HTTP/класифікація/summary без винятків; недосяжний IP → dict з порожніми портами (graceful).
- **App API `/lib/*`:** Бібліотека віддає реальні пристрої/мережі/профілі; auth-guard 401 без токена, rate-limit 429 після 10 невдач/60с; таргет-перемикач лишає рівно 1 активний.

## 5. Журнал змін коду

**`server/profiles.py`**
- `upsert_network` → атомарний upsert (ON CONFLICT bssid,ssid) з CASE-логікою min/max RSSI.
- `upsert_device` → атомарний upsert (ON CONFLICT mac) + атомарний лінк `device_network`; захист MAC.
- `ingest_airscan` → підтримка реального dict-формату (та легасі list).
- `ingest_netscan` → валідація кадру (isinstance dict/str), відсів сміття.

**`server/netlog.py`** — `record_scan`: захист від non-str/None MAC.

**Новий тест-харнес `server/tests/`**
- `board_client.py` — реальний клієнт плати: USB COM5 (REQ/RES) + WiFi HTTP фолбек.
- `conftest.py` — ізоляція БД (temp), session-збір реальних даних плати, хук метрик → `STABILITY_TEST_RUNS.json`.
- `test_pipeline_real.py` (7), `test_db_stress.py` (4), `test_fault_real.py` (6),
  `test_devintel_real.py` (5, живий скан хоста), `test_admin_api_real.py` (6, Flask test-client Бібліотеки).

**Метрики:** `STABILITY_TEST_RUNS.json` (машиночитний журнал прогонів). Калібрування таймінгів окремо — `docs/TIMINGS.md`.

## 6. Не покрито (чесно) / далі

- **Прошивка (C++) і парсери додатка (Kotlin)** не юніт-тестуються цим Python-харнесом — перевірено **статично**
  (boot-loop) + наскрізно через реальні відповіді плати. Повний firmware-харнес — окрема інфраструктура (Unity/ceedling).
- **Перф db.py:** connection-per-op коректний, але не high-throughput. За потреби масового інжесту — батч-транзакція
  (одне з'єднання на пачку). Зараз НЕ вузьке місце → не чіпав (дисципліна обсягу, ризик регресу).
- **Live-стрес сервера/роутера** свідомо не робився (ризик залочення). Пайплайн-код сервера покрито через локальні копії.
