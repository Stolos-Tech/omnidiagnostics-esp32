#include "script_path.h"
#include <string.h>
#include <stdio.h>

// Категорії = модулі ОС (порядок = порядок у списку меню/редакторі).
const char* const SCRIPT_CATEGORIES[SCRIPT_CAT_COUNT] = {
  "system",   // стан плати: heap, температура, uptime, батарея
  "net",      // мережа: WiFi/RSSI/IP
  "gpio",     // прямий GPIO/I2C
  "uno",      // копроцесор UNO R3: датчики/RFID/мотори через uno_link
  "misc"      // усе інше + стара пласка розкладка /apps/*.be
};

bool script_category_valid(const char* cat) {
  if (!cat) return false;
  for (int i = 0; i < SCRIPT_CAT_COUNT; i++)
    if (strcmp(cat, SCRIPT_CATEGORIES[i]) == 0) return true;
  return false;
}

bool script_name_from_path(const char* path, char* out, size_t out_size) {
  if (!path || !out || out_size == 0) return false;

  // базове ім'я після останнього '/' або '\'
  const char* base = path;
  for (const char* p = path; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;

  size_t len = strlen(base);

  // має закінчуватись на ".be"
  const size_t ext = 3;
  if (len <= ext) return false;
  if (strcmp(base + len - ext, ".be") != 0) return false;

  size_t name_len = len - ext;  // без ".be"
  if (name_len == 0) return false;

  if (name_len >= out_size) name_len = out_size - 1;
  memcpy(out, base, name_len);
  out[name_len] = '\0';
  return true;
}

bool script_category_from_path(const char* path, char* out, size_t out_size) {
  if (!path || !out || out_size == 0) return false;
  char name[8];  // перевірка, що це взагалі .be (значення не потрібне)
  if (!script_name_from_path(path, name, sizeof(name))) return false;

  // Тека безпосередньо перед файлом: ".../<dir>/<file>.be"
  const char* base = path;
  for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
  const char* dir_end = (base > path) ? base - 1 : path;   // вказує на роздільник
  const char* dir_start = path;
  for (const char* p = path; p < dir_end; p++) if (*p == '/' || *p == '\\') dir_start = p + 1;

  size_t dir_len = (size_t)(dir_end - dir_start);
  char dir[16] = "";
  if (dir_len > 0 && dir_len < sizeof(dir)) {
    memcpy(dir, dir_start, dir_len);
    dir[dir_len] = '\0';
  }

  const char* cat = script_category_valid(dir) ? dir : "misc";
  snprintf(out, out_size, "%s", cat);
  return true;
}

bool script_build_path(const char* cat, const char* name, char* out, size_t out_size) {
  if (!cat || !name || !out || out_size == 0) return false;
  if (!script_category_valid(cat)) return false;
  // ім'я має бути голим файлом *.be без шляху
  for (const char* p = name; *p; p++) if (*p == '/' || *p == '\\') return false;
  char tmp[8];
  if (!script_name_from_path(name, tmp, sizeof(tmp))) return false;
  int n = snprintf(out, out_size, "/apps/%s/%s", cat, name);
  return n > 0 && (size_t)n < out_size;
}
