#include "filesystem.h"
#include "../kernel/script_path.h"
#include <LittleFS.h>

#define APPS_DIR "/apps"

static bool s_mounted = false;

bool fs_init() {
  // Спершу монтуємо БЕЗ форматування — щоб не стерти щойно залитий uploadfs образ,
  // якщо монтування раптом не вдалось (напр. образ був SPIFFS, а не LittleFS).
  s_mounted = LittleFS.begin(false);
  if (s_mounted) {
    Serial.println("[FS] mounted (no format)");
  } else {
    Serial.println("[FS] mount FAILED -> formatting (empty partition)");
    s_mounted = LittleFS.begin(true);  // форматувати й змонтувати порожній
    Serial.printf("[FS] after format: mounted=%d\n", s_mounted);
  }
  if (s_mounted && !LittleFS.exists(APPS_DIR)) {
    LittleFS.mkdir(APPS_DIR);
  }
  return s_mounted;
}

// Додає всі *.be з однієї теки. prefix — шлях теки ("/apps" або "/apps/<cat>").
static int scan_dir(const char* prefix, char paths[][FS_MAX_PATH], int max, int count) {
  File dir = LittleFS.open(prefix);
  if (!dir || !dir.isDirectory()) return count;
  char name_buf[FS_MAX_PATH];
  for (File f = dir.openNextFile(); f && count < max; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* full = f.name();
    if (!script_name_from_path(full, name_buf, sizeof(name_buf))) continue;
    const char* base = full;   // f.name() може віддавати як голе ім'я, так і повний шлях
    for (const char* p = full; *p; p++) if (*p == '/') base = p + 1;
    snprintf(paths[count], FS_MAX_PATH, "%s/%s", prefix, base);
    count++;
  }
  return count;
}

int fs_list_scripts(char paths[][FS_MAX_PATH], int max) {
  if (!s_mounted) return 0;
  // Спершу категорії (бібліотека), потім пласкі файли в /apps (стара розкладка).
  int count = 0;
  char sub[FS_MAX_PATH];
  for (int i = 0; i < SCRIPT_CAT_COUNT && count < max; i++) {
    snprintf(sub, sizeof(sub), APPS_DIR "/%s", SCRIPT_CATEGORIES[i]);
    count = scan_dir(sub, paths, max, count);
  }
  count = scan_dir(APPS_DIR, paths, max, count);
  return count;
}

bool fs_ensure_script_dirs() {
  if (!s_mounted) return false;
  char sub[FS_MAX_PATH];
  bool ok = true;
  for (int i = 0; i < SCRIPT_CAT_COUNT; i++) {
    snprintf(sub, sizeof(sub), APPS_DIR "/%s", SCRIPT_CATEGORIES[i]);
    if (!LittleFS.exists(sub) && !LittleFS.mkdir(sub)) ok = false;
  }
  return ok;
}

bool fs_read_file(const char* path, String& out) {
  if (!s_mounted) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

bool fs_write_file(const char* path, const char* content) {
  if (!s_mounted) return false;
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  if (content) f.print(content);
  f.close();
  return true;
}

bool fs_delete_file(const char* path) {
  if (!s_mounted) return false;
  return LittleFS.remove(path);
}
