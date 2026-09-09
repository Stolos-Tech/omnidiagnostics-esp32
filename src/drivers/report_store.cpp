#include "report_store.h"
#include "sd_store.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

// Монотонний лічильник звітів у NVS (не скидається на ребут). ІМʼЯ звіту починається з
// нього -> сортування за іменем = ХРОНОЛОГІЧНЕ (uptime-seq цього не давав: скидався на
// ребут, а slug-first сорт був алфавітним). Це чинить і prune (видаляє реально найстаріші),
// і порядок відправки в бота.
static unsigned long report_next_seq() {
  Preferences p; unsigned long n = 0;
  if (p.begin("os", false)) { n = p.getULong("repseq", 0); p.putULong("repseq", n + 1); p.end(); }
  return n;
}

#define REPORT_DIR "/reports"

static void sanitize(const char* in, char* out, size_t osz) {
  size_t j = 0;
  for (const char* p = in; *p && j < osz - 1; p++) {
    char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') out[j++] = c;
  }
  if (j == 0) out[j++] = 'r';
  out[j] = 0;
}

// true, якщо базове імʼя безпечне (без обходу шляху).
static bool safe_name(const char* n) {
  return n && n[0] && !strchr(n, '/') && !strchr(n, '\\') && !strstr(n, "..");
}

bool report_store_init() {
  if (!LittleFS.exists(REPORT_DIR)) LittleFS.mkdir(REPORT_DIR);
  return true;
}

int report_list(char names[][REPORT_NAME_MAX], int max) {
  int n = 0;
  File dir = LittleFS.open(REPORT_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* nm = f.name();
    const char* base = nm;
    for (const char* p = nm; *p; p++) if (*p == '/') base = p + 1;
    snprintf(names[n], REPORT_NAME_MAX, "%s", base);
    n++;
  }
  dir.close();
  // сортування за іменем (n малий) — seq монотонний, тож найстаріші перші
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (strcmp(names[j], names[i]) < 0) {
        char t[REPORT_NAME_MAX]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t);
      }
  return n;
}

static void prune() {
  char names[REPORT_MAX + 8][REPORT_NAME_MAX];
  int n = report_list(names, REPORT_MAX + 8);
  int i = 0;
  while (n - i > REPORT_MAX) {
    char path[80]; snprintf(path, sizeof(path), REPORT_DIR "/%s", names[i]);
    LittleFS.remove(path);
    i++;
  }
}

bool report_save(const char* label, const char* content) {
  report_store_init();
  char safe[REPORT_NAME_MAX]; sanitize(label ? label : "rep", safe, sizeof(safe));
  time_t now = time(nullptr);
  char stamp[28]; unsigned long seq;
  if (now > 1600000000) {
    seq = (unsigned long)now;
    struct tm* tmv = localtime(&now);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tmv);
  } else {
    seq = (unsigned long)(millis() / 1000);
    snprintf(stamp, sizeof(stamp), "uptime %lus", seq);
  }
  char base[64];
  // ІМʼЯ: <монотонний-лічильник>_<slug>.txt -> хронологічний сорт + коректний prune.
  snprintf(base, sizeof(base), "%06lu_%s.txt", report_next_seq(), safe);
  char path[96];
  snprintf(path, sizeof(path), REPORT_DIR "/%s", base);
  char header[80];
  snprintf(header, sizeof(header), "# %s  @ %s\n", safe, stamp);
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(header);
  if (content) f.print(content);
  f.close();
  prune();
  // Центральне дзеркалення на SD: усі звіти (Self Test / RF Audit / saved views)
  // авто-персистять на карту тим самим іменем (no-op якщо карти нема).
  sd_mirror_report(base, header, content);
  return true;
}

bool report_read(const char* name, String& out) {
  if (!safe_name(name)) return false;
  char path[96]; snprintf(path, sizeof(path), REPORT_DIR "/%s", name);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

bool report_delete(const char* name) {
  if (!safe_name(name)) return false;
  char path[96]; snprintf(path, sizeof(path), REPORT_DIR "/%s", name);
  return LittleFS.remove(path);
}
