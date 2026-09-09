#include "uno_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_with_tag(const char* line, const char* tag, const char** rest) {
  size_t n = 0;
  while (tag[n]) n++;
  for (size_t i = 0; i < n; i++) if (line[i] != tag[i]) return false;
  char d = line[n];
  if (d != ' ' && d != '\t' && d != '\0' && d != '\r' && d != '\n') return false;
  *rest = line + n;
  return true;
}

bool uno_parse_sensors(const char* line, UnoSensors* out) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  if (line[0] != 'S' || (line[1] != ' ' && line[1] != '\t')) return false;

  int pot = 0, reed = 0;
  unsigned ir = 0;
  int got = sscanf(line + 1, "%d %d %x", &pot, &reed, &ir);
  if (got < 2) return false;                 // мінімум pot і reed
  if (pot < 0) pot = 0;
  if (pot > 1023) pot = 1023;
  if (out) {
    out->pot = pot;
    out->reed = reed != 0;
    out->ir = (got >= 3) ? (uint32_t)ir : 0;
  }
  return true;
}

bool uno_parse_env(const char* line, UnoEnv* out) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "ENV", &rest)) return false;
  int t = 0, h = 0;
  if (sscanf(rest, "%d %d", &t, &h) != 2) return false;
  if (out) { out->temp_x10 = t; out->hum_x10 = h; }
  return true;
}

bool uno_parse_joy(const char* line, int* out_x, int* out_y, int* out_sw) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "JOY", &rest)) return false;
  int x = 0, y = 0, sw = 0;
  if (sscanf(rest, "%d %d %d", &x, &y, &sw) != 3) return false;
  if (x < 0) x = 0; if (x > 1023) x = 1023;
  if (y < 0) y = 0; if (y > 1023) y = 1023;
  if (out_x)  *out_x = x;
  if (out_y)  *out_y = y;
  if (out_sw) *out_sw = sw != 0;
  return true;
}

bool uno_parse_stat(const char* line, int* out_free_ram, uint32_t* out_uptime_s) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "STAT", &rest)) return false;
  int fr = 0; unsigned long up = 0;
  if (sscanf(rest, "%d %lu", &fr, &up) != 2) return false;
  if (out_free_ram) *out_free_ram = fr;
  if (out_uptime_s) *out_uptime_s = (uint32_t)up;
  return true;
}

bool uno_parse_rfid(const char* line, uint32_t* out_uid) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "RFID", &rest)) return false;
  unsigned uid = 0;
  if (sscanf(rest, "%x", &uid) != 1) return false;
  if (out_uid) *out_uid = (uint32_t)uid;
  return true;
}

// --- Модульний хаб ---
bool uno_parse_cap(const char* line, int* out_slot, char* out_type, size_t type_sz,
                   char* out_name, size_t name_sz, int* out_present) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "CAP", &rest)) return false;
  int slot = 0, present = 1;
  char type[16] = {0}, name[24] = {0};
  int got = sscanf(rest, "%d %15s %23s %d", &slot, type, name, &present);
  if (got < 3) return false;                 // мінімум slot type name
  if (out_slot) *out_slot = slot;
  if (out_type && type_sz) { strncpy(out_type, type, type_sz - 1); out_type[type_sz - 1] = 0; }
  if (out_name && name_sz) { strncpy(out_name, name, name_sz - 1); out_name[name_sz - 1] = 0; }
  if (out_present) *out_present = (got >= 4) ? (present ? 1 : 0) : 1;
  return true;
}

bool uno_parse_evt(const char* line, int* out_slot, char* out_value, size_t value_sz) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "EVT", &rest)) return false;
  while (*rest == ' ' || *rest == '\t') rest++;
  char* end = nullptr;
  long slot = strtol(rest, &end, 10);
  if (end == rest) return false;
  if (out_slot) *out_slot = (int)slot;
  while (*end == ' ' || *end == '\t') end++;
  if (out_value && value_sz) {
    size_t i = 0;
    while (end[i] && end[i] != '\n' && end[i] != '\r' && i < value_sz - 1) { out_value[i] = end[i]; i++; }
    out_value[i] = '\0';
  }
  return true;
}

int uno_build_set(int slot, const char* args, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  if (args && args[0]) return snprintf(out, out_size, "SET %d %s\n", slot, args);
  return snprintf(out, out_size, "SET %d\n", slot);
}

int uno_build_scan(char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "SCAN\n");
}

// --- Red-team RFID-аудит ---
bool uno_is_rfa(const char* line) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  return starts_with_tag(line, "RFA", &rest);
}

bool uno_parse_rfa_uid(const char* line, char* uid, size_t uidsz, char* type, size_t typesz) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "RFA", &rest)) return false;
  while (*rest == ' ' || *rest == '\t') rest++;
  char u[24] = {0}, ty[24] = {0}; unsigned sak = 0;
  if (sscanf(rest, "uid %23s sak %x %23s", u, &sak, ty) != 3) return false;
  if (uid && uidsz)  { strncpy(uid, u, uidsz - 1);  uid[uidsz - 1] = 0; }
  if (type && typesz){ strncpy(type, ty, typesz - 1); type[typesz - 1] = 0; }
  return true;
}

bool uno_parse_rfa_end(const char* line, int* cracked, int* total, char* verdict, size_t vsz) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "RFA", &rest)) return false;
  while (*rest == ' ' || *rest == '\t') rest++;
  int c = 0, t = 0; char v[24] = {0};
  if (sscanf(rest, "end %d/%d %23s", &c, &t, v) != 3) return false;
  if (cracked) *cracked = c;
  if (total)   *total = t;
  if (verdict && vsz) { strncpy(verdict, v, vsz - 1); verdict[vsz - 1] = 0; }
  return true;
}

// --- RFID write/clone ---
int uno_build_write(int block, const uint8_t* data16, char* out, size_t out_size) {
  if (!out || out_size == 0 || !data16) return 0;
  static const char H[] = "0123456789ABCDEF";
  char hex[33];
  for (int i = 0; i < 16; i++) { hex[i * 2] = H[data16[i] >> 4]; hex[i * 2 + 1] = H[data16[i] & 0x0F]; }
  hex[32] = 0;
  return snprintf(out, out_size, "WRITE %d %s\n", block, hex);
}

bool uno_parse_wres(const char* line, int* out_block, int* out_ok) {
  if (!line) return false;
  while (*line == ' ' || *line == '\t') line++;
  const char* rest;
  if (!starts_with_tag(line, "WRES", &rest)) return false;
  int block = 0, ok = 0;
  if (sscanf(rest, "%d %d", &block, &ok) != 2) return false;
  if (out_block) *out_block = block;
  if (out_ok)    *out_ok = ok ? 1 : 0;
  return true;
}
