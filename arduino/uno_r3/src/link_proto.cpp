#include "link_proto.h"
#include <stdio.h>
#include <stdlib.h>

static bool starts_with_tag(const char* line, const char* tag, const char** rest) {
  size_t n = 0;
  while (tag[n]) n++;
  for (size_t i = 0; i < n; i++) if (line[i] != tag[i]) return false;
  char d = line[n];
  if (d != ' ' && d != '\t' && d != '\0' && d != '\r' && d != '\n') return false;
  *rest = line + n;
  return true;
}

int link_build_sensors(int pot, bool reed, uint32_t ir, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  if (pot < 0) pot = 0;
  if (pot > 1023) pot = 1023;
  return snprintf(out, out_size, "S %d %d %lx\n", pot, reed ? 1 : 0, (unsigned long)ir);
}

int link_build_env(int temp_x10, int hum_x10, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "ENV %d %d\n", temp_x10, hum_x10);
}

int link_build_rfid(uint32_t uid, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "RFID %lx\n", (unsigned long)uid);
}

int link_build_joy(int x, int y, int sw, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  if (x < 0) x = 0; if (x > 1023) x = 1023;
  if (y < 0) y = 0; if (y > 1023) y = 1023;
  return snprintf(out, out_size, "JOY %d %d %d\n", x, y, sw ? 1 : 0);
}

int link_build_stat(int free_ram, unsigned long uptime_s, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  if (free_ram < 0) free_ram = 0;
  return snprintf(out, out_size, "STAT %d %lu\n", free_ram, uptime_s);
}

// --- RFID write/clone ---
static int hexnib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool link_parse_write(const char* line, int* out_block, uint8_t* out_data16) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "WRITE", &rest)) return false;
  while (*rest == ' ' || *rest == '\t') rest++;
  char* end = nullptr;
  long block = strtol(rest, &end, 10);
  if (end == rest) return false;
  if (out_block) *out_block = (int)block;
  while (*end == ' ' || *end == '\t') end++;
  for (int i = 0; i < 16; i++) {
    int hi = hexnib(end[i * 2]), lo = hexnib(end[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;      // менше ніж 32 hex -> невалідно
    if (out_data16) out_data16[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

int link_build_wres(int block, int ok, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "WRES %d %d\n", block, ok ? 1 : 0);
}

// --- Модульний хаб ---
int link_build_cap(int slot, const char* type, const char* name, int present, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "CAP %d %s %s %d\n",
                  slot, type ? type : "", name ? name : "", present ? 1 : 0);
}

int link_build_evt(int slot, const char* value, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "EVT %d %s\n", slot, value ? value : "");
}

bool link_parse_set(const char* line, int* out_slot, char* out_args, size_t args_size) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "SET", &rest)) return false;
  while (*rest == ' ' || *rest == '\t') rest++;
  // номер слота
  char* end = nullptr;
  long slot = strtol(rest, &end, 10);
  if (end == rest) return false;           // немає числа
  if (out_slot) *out_slot = (int)slot;
  // аргументи = решта рядка після слота (пропускаємо один роздільник, ріжемо \r\n)
  while (*end == ' ' || *end == '\t') end++;
  if (out_args && args_size) {
    size_t i = 0;
    while (end[i] && end[i] != '\n' && end[i] != '\r' && i < args_size - 1) {
      out_args[i] = end[i];
      i++;
    }
    out_args[i] = '\0';
  }
  return true;
}

bool link_is_scan(const char* line) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "SCAN", &rest)) return false;
  while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n') rest++;
  return *rest == '\0';
}
