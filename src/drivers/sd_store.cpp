#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sd_store.h"
#include "../kernel/hw_safe.h"
#include "../kernel/shared_store.h"
#include "../kernel/log_ring.h"

static SPIClass s_sdSPI(HSPI);
static bool     s_mounted = false;
static uint32_t s_log_cursor = 0;   // скільки рядків log_ring уже скинуто на SD
static char     s_status[64] = "// no SD. Where do you expect me to store this trash?";

static void ensure_dir(const char* d) { if (!SD.exists(d)) SD.mkdir(d); }

// Переприв'язати HSPI-MISO на пін SD (21) перед доступом до картки — бо nRF24 ділить SCK/MOSI
// на HSPI, але має ОКРЕМИЙ MISO=38 і міг перемкнути маршрут на себе. Викликати на початку
// кожної SD-операції (nRF24 і SD не працюють одночасно -> лишається на 21 до кінця операції).
static inline void sd_claim_bus() { spiAttachMISO(s_sdSPI.bus(), SD_PIN_MISO); }

// Публічна обгортка (для доступу до SD.* поза цим модулем — веб-статика тощо).
void sd_bus_select() { if (s_mounted) sd_claim_bus(); }

// Самотест: пише відомий рядок у /selftest.txt і читає назад. Повертає к-ть прочитаних
// байтів (має дорівнювати записаним) або <0 при помилці. Ізолює SD-запис/читання на
// спільній шині від веб-стеку -> діагностика partial-read бага.
int sd_selftest() {
  if (!s_mounted) return -1;
  const char* msg = "SD selftest line 1\nSD selftest line 2\n";
  sd_claim_bus();
  File w = SD.open("/selftest.txt", FILE_WRITE);
  if (!w) return -2;
  int wrote = (int)w.print(msg);
  w.flush(); w.close();
  sd_claim_bus();
  File r = SD.open("/selftest.txt", FILE_READ);
  if (!r) return -3;
  int fsize = (int)r.size();
  char buf[64] = {0};
  int n = (int)r.read((uint8_t*)buf, sizeof(buf) - 1);
  r.close();
  Serial.printf("[SD] selftest: wrote=%d fsize=%d read=%d\n", wrote, fsize, n);
  Serial.printf("[SD] content: %s", buf);
  return n;
}

bool sd_mount() {
  if (g_hw_safe) return false;   // safe-режим: не жени CS/CE/SPI (розводка не звірена)
  if (s_mounted) return true;
  // ВАЖЛИВО для веб-монтування: до цього могла працювати радіо-апка (2.4G/Sub-GHz
  // Analyzer), що ділить цю ж HSPI. Примусово ДЕСЕЛЕКТУЄМО всі інші пристрої шини,
  // щоб жоден не тримав MISO/не «висів» на SPI під час SD.begin (класична причина
  // «SD mount failed» саме через веб, коли по серійці — одразу після boot — усе ок):
  //   nRF24 CSN=25 -> HIGH,  CC1101 CS=27 -> HIGH,  nRF24 CE=12 -> LOW.
  pinMode(25, OUTPUT); digitalWrite(25, HIGH);
  pinMode(27, OUTPUT); digitalWrite(27, HIGH);
  pinMode(12, OUTPUT); digitalWrite(12, LOW);
  // Спільна HSPI-шина (SCK/MISO/MOSI) + виділений CS. begin повертає false, якщо
  // карти нема / не відповідає -> graceful, робоча плата не падає.
  s_sdSPI.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
  sd_claim_bus();   // MISO=21 (begin міг бути no-op при ре-mount; nRF24 міг тримати 38)
  // Одна повторна спроба: перший SD.begin на спільній шині інколи не синхронізується
  // одразу після радіо-активності; коротка пауза + retry надійніше за одиничний виклик.
  bool ok = SD.begin(SD_PIN_CS, s_sdSPI);
  if (!ok) {
    SD.end();
    delay(20);
    sd_claim_bus();
    ok = SD.begin(SD_PIN_CS, s_sdSPI);
  }
  if (!ok) {
    snprintf(s_status, sizeof(s_status), "// SD mount failed. Card AWOL or miswired.");
    return false;
  }
  uint8_t t = SD.cardType();
  if (t == CARD_NONE) { SD.end(); snprintf(s_status, sizeof(s_status), "// SD slot empty. Feed me a card."); return false; }
  ensure_dir("/hosts"); ensure_dir("/networks"); ensure_dir("/channels"); ensure_dir("/logs");
  s_mounted = true;
  uint64_t mb = SD.cardSize() / (1024ULL * 1024ULL);
  snprintf(s_status, sizeof(s_status), "// SD mounted · %lluMB · sorting loot by type", (unsigned long long)mb);
  return true;
}

void sd_unmount() {
  if (!s_mounted) return;
  SD.end();
  s_mounted = false;
  snprintf(s_status, sizeof(s_status), "// SD ejected. Secrets safe... probably.");
}

bool        sd_mounted()     { return s_mounted; }
const char* sd_status_line() { return s_status; }
uint64_t    sd_card_bytes()  { return s_mounted ? SD.cardSize() : 0; }

// Мінімальне екранування для JSON-рядка (лапки/бекслеш -> безпечні).
static void json_str(File& f, const char* s) {
  f.write('"');
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') f.write('\'');       // спрощення: не ламаємо JSON
    else if (c >= 32) f.write((uint8_t)c);
  }
  f.write('"');
}

int sd_persist_shared() {
  if (!s_mounted) return -1;
  sd_claim_bus();   // повернути MISO на 21 (nRF24 міг перехопити на 38)
  int lines = 0;
  // Хости -> /hosts/data.jsonl (перезапис поточним знімком store).
  File fh = SD.open("/hosts/data.jsonl", FILE_WRITE);
  if (!fh) {
    // Запис не відкрився -> карту могли висмикнути / глюк живлення. Позначаємо unmounted,
    // щоб наступний sd_mount() перечитав карту заново (не лишаємось у «завислому» стані).
    s_mounted = false;
    snprintf(s_status, sizeof(s_status), "// SD write failed. Card yanked? -> re-mount.");
    return -1;
  }
  {
    for (int i = 0; i < shared_host_count(); i++) {
      const SharedHost* h = shared_host(i);
      fh.print("{\"ip\":"); json_str(fh, h->ip);
      fh.print(",\"note\":"); json_str(fh, h->note); fh.println("}");
      lines++;
    }
    fh.close();
  }
  // Мережі -> /networks/data.jsonl.
  File fn = SD.open("/networks/data.jsonl", FILE_WRITE);
  if (fn) {
    for (int i = 0; i < shared_net_count(); i++) {
      const SharedNet* n = shared_net(i);
      fn.print("{\"ssid\":"); json_str(fn, n->ssid);
      fn.printf(",\"ch\":%d,\"rssi\":%d,\"enc\":", n->ch, n->rssi); json_str(fn, n->enc);
      fn.println("}");
      lines++;
    }
    fn.close();
  }
  // Канали -> /channels/data.csv (похідне: скільки AP на кожен канал 1..14).
  File fc = SD.open("/channels/data.csv", FILE_WRITE);
  if (fc) {
    int hist[15] = {0};
    for (int i = 0; i < shared_net_count(); i++) { int ch = shared_net(i)->ch; if (ch >= 1 && ch <= 14) hist[ch]++; }
    fc.println("channel,ap_count");
    for (int ch = 1; ch <= 14; ch++) if (hist[ch]) { fc.printf("%d,%d\n", ch, hist[ch]); lines++; }
    fc.close();
  }
  // Лог активності -> /logs/scans.log (append рядка-підсумку кожного дампу).
  File fl = SD.open("/logs/scans.log", FILE_APPEND);
  if (fl) {
    fl.printf("[%lus] hosts=%d nets=%d\n", (unsigned long)(millis() / 1000), shared_host_count(), shared_net_count());
    fl.close();
  }
  return lines;
}

// Неблокуюче логування: дописує нові рядки з RAM-log_ring на SD обмеженим батчем.
// Викликається періодично з циклу -> запис розмазаний у часі, main loop не морозиться.
int sd_flush_log(int max_lines) {
  if (!s_mounted) return 0;
  uint32_t total = log_ring().total();
  if (s_log_cursor >= total) return 0;            // нема нових рядків
  if (max_lines > 16) max_lines = 16;             // жорсткий стель на батч (обмежений час запису)
  static char batch[16][LogRing::LINE];
  int got = log_ring().since(s_log_cursor, batch, max_lines);
  if (got <= 0) { s_log_cursor = total; return 0; }
  sd_claim_bus();
  File f = SD.open("/logs/session.log", FILE_APPEND);
  if (!f) { s_mounted = false; return 0; }        // збій -> re-mount наступного разу
  for (int i = 0; i < got; i++) f.println(batch[i]);
  f.flush(); f.close();                            // flush перед close -> цілісність при пропажі живлення
  s_log_cursor += got;
  if (s_log_cursor > total) s_log_cursor = total;
  return got;
}

// Дзеркалить збережений звіт на SD /reports/<filename> (той самий header+content, що й
// у LittleFS). No-op якщо карти нема -> робота без SD не ламається. Центральна точка:
// викликається з report_save -> усі Self Test / RF Audit / saved-view звіти авто-персистять.
void sd_mirror_report(const char* filename, const char* header, const char* content) {
  if (!s_mounted || !filename) return;
  sd_claim_bus();
  ensure_dir("/reports");
  char path[80];
  snprintf(path, sizeof(path), "/reports/%s", filename);
  File f = SD.open(path, FILE_WRITE);
  if (!f) { s_mounted = false; snprintf(s_status, sizeof(s_status), "// SD write failed. Card yanked? -> re-mount."); return; }
  if (header)  f.print(header);
  if (content) f.print(content);
  f.flush(); f.close();
}

// Дописує рядок результату аналіз-апки в SD /logs/results.log (з міткою uptime). No-op
// без карти. Викликається ядром на виході з аналіз-апки -> живі аналізатори (WiFi/BT/
// Radio/EMF), що не зберігають звіт самі, теж лишають слід на SD.
void sd_log_result(const char* tag, const char* body) {
  if (!s_mounted || !tag) return;
  sd_claim_bus();
  ensure_dir("/logs");
  File f = SD.open("/logs/results.log", FILE_APPEND);
  if (!f) { s_mounted = false; return; }
  f.printf("[%lus] === %s ===\n", (unsigned long)(millis() / 1000), tag);
  if (body && body[0]) { f.print(body); if (body[strlen(body) - 1] != '\n') f.write('\n'); }
  f.flush(); f.close();
}

int sd_list(const char* dir, char out[][40], int max_n) {
  if (!s_mounted) return -1;
  sd_claim_bus();   // MISO на 21 перед читанням директорії
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) { if (d) d.close(); return 0; }
  int n = 0;
  for (File f = d.openNextFile(); f && n < max_n; f = d.openNextFile()) {
    const char* nm = f.name();
    snprintf(out[n], 40, "%s", nm ? nm : "?");
    n++; f.close();
  }
  d.close();
  return n;
}
