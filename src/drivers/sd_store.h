// SD-сховище (SPI SD-модуль з AMS1117, живлення 5V). SCK/MOSI СПІЛЬНІ з nRF24 (HSPI),
// але MISO ОКРЕМИЙ: SD=GPIO21, nRF24=GPIO38 (буфер SD-модуля тримає свій MISO активним і
// не ділить лінію). HSPI-MISO перемикається spiAttachMISO перед доступом -> sd_bus_select().
//   SCK=GPIO2, MOSI=GPIO15, MISO=GPIO21, CS=GPIO33.
//
// БЕЗПЕКА: НЕ авто-монтується на boot (код ще не перевірений на залізі -> не чіпаємо
// робочу плату, доки юзер явно не змонтує з підключеною картою). Монтуй:
//   serial `sd mount` | веб POST /api/sd/mount. Без карти -> graceful, playful-статуси.
//
// СОРТУВАННЯ за типом даних у теки (JSONL — по одному об'єкту в рядок, append-friendly):
//   /hosts/data.jsonl  /networks/data.jsonl  /channels/data.jsonl  /logs/<name>.log
#pragma once
#include <stdint.h>

#define SD_PIN_SCK   2     // спільна HSPI-шина з nRF24
#define SD_PIN_MOSI  15
#define SD_PIN_MISO  21   // ОКРЕМИЙ від nRF24 (=38): буфер SD-модуля не відпускає MISO
#define SD_PIN_CS    33    // виділений CS (звільнений після видалення RGB)

bool        sd_mount();          // спроба SD.begin на спільній шині; true = карта є й змонтована
void        sd_unmount();
bool        sd_mounted();
const char* sd_status_line();    // людський/playful статус для UI
uint64_t    sd_card_bytes();     // розмір карти в байтах (0 якщо не змонтовано)

// Персистенція крос-модульного store у сортовані файли (виклик коли скан ЗАВЕРШЕНО).
// Пише лише якщо змонтовано; батч коротких рядків. Повертає к-ть записаних рядків,
// -1 якщо SD не змонтовано.
int  sd_persist_shared();

// Selective read: імена файлів у категорії-теці (напр. "/hosts"). Дозволяє модулю
// читати ЛИШЕ потрібну бібліотеку даних, не тягнучи все в RAM. -1 якщо не змонтовано.
int  sd_list(const char* dir, char out[][40], int max_n);

// Переприв'язати HSPI-MISO на пін SD (21). Викликати ПЕРЕД будь-яким прямим доступом до
// SD.* поза цим модулем (напр. streamFile веб-статики) — інакше nRF24 міг лишити MISO=38.
void sd_bus_select();

// Діагностика: пише+читає /selftest.txt на SD. Повертає к-ть прочитаних байтів (<0 = помилка).
// Ізолює SD-запис/читання на спільній шині від веб-стеку.
int  sd_selftest();

// Дзеркалення звіту на SD /reports/ (header+content) — центрально з report_save. No-op без карти.
void sd_mirror_report(const char* filename, const char* header, const char* content);
// Дописати результат аналіз-апки в SD /logs/results.log. No-op без карти.
void sd_log_result(const char* tag, const char* body);

// Неблокуюче логування: дописує НОВІ рядки з RAM log_ring у /logs/session.log (append),
// не більше max_lines за виклик (обмежений час) -> не морозить main loop. Викликати
// періодично з циклу. Повертає к-ть записаних рядків (0 якщо нема нових / не змонтовано).
int  sd_flush_log(int max_lines);
