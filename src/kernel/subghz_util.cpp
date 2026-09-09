#include "subghz_util.h"

// Три перестроювані вікна CC1101 (кГц), крок свіпу 2 МГц.
struct ScanWindow { long start_khz; int count; int win; };
static const long SCAN_STEP = 2000;   // 2 МГц
static const ScanWindow WINDOWS[] = {
  { 300000, 25, 0 },   // A: 300..348 МГц  (25 точок)
  { 388000, 39, 1 },   // B: 388..464 МГц  (39 точок)
  { 780000, 75, 2 },   // C: 780..928 МГц  (75 точок) — сюди 868/915, «рідне» вікно модуля
};
static const int WINDOW_COUNT = (int)(sizeof(WINDOWS) / sizeof(WINDOWS[0]));

int subghz_scan_count() {
  int n = 0;
  for (int w = 0; w < WINDOW_COUNT; w++) n += WINDOWS[w].count;
  return n;
}

long subghz_scan_khz(int i) {
  if (i < 0) return -1;
  for (int w = 0; w < WINDOW_COUNT; w++) {
    if (i < WINDOWS[w].count) return WINDOWS[w].start_khz + (long)i * SCAN_STEP;
    i -= WINDOWS[w].count;
  }
  return -1;
}

int subghz_scan_window(int i) {
  if (i < 0) return -1;
  for (int w = 0; w < WINDOW_COUNT; w++) {
    if (i < WINDOWS[w].count) return WINDOWS[w].win;
    i -= WINDOWS[w].count;
  }
  return -1;
}

void subghz_freq_to_regs(long khz, uint8_t* f2, uint8_t* f1, uint8_t* f0) {
  if (khz < 0) khz = 0;
  // reg = round(khz * 2^16 / 26000)
  uint64_t reg = ((uint64_t)khz * 65536ULL + (SUBGHZ_XTAL_KHZ / 2)) / (uint64_t)SUBGHZ_XTAL_KHZ;
  if (reg > 0xFFFFFF) reg = 0xFFFFFF;   // 24-бітне поле
  if (f2) *f2 = (uint8_t)((reg >> 16) & 0xFF);
  if (f1) *f1 = (uint8_t)((reg >> 8) & 0xFF);
  if (f0) *f0 = (uint8_t)(reg & 0xFF);
}

long subghz_regs_to_khz(uint8_t f2, uint8_t f1, uint8_t f0) {
  uint32_t reg = ((uint32_t)f2 << 16) | ((uint32_t)f1 << 8) | f0;
  // khz = round(reg * 26000 / 2^16)
  return (long)(((uint64_t)reg * (uint64_t)SUBGHZ_XTAL_KHZ + 32768ULL) >> 16);
}

int subghz_rssi_dbm(uint8_t raw) {
  int r = raw;
  if (r >= 128) r -= 256;
  return r / 2 - 74;   // CC1101: RSSI_offset ~74 dB
}

int subghz_peak_index(const int8_t* dbm, int n) {
  if (!dbm || n <= 0) return -1;
  int bi = 0; int bv = dbm[0];
  for (int i = 1; i < n; i++) if (dbm[i] > bv) { bv = dbm[i]; bi = i; }
  return bi;
}

int subghz_bar_height(int dbm, int floor_dbm, int top_dbm, int h) {
  if (h <= 0 || top_dbm <= floor_dbm) return 0;
  if (dbm <= floor_dbm) return 0;
  if (dbm >= top_dbm) return h;
  int v = (dbm - floor_dbm) * h / (top_dbm - floor_dbm);
  if (v > h) v = h;
  if (v < 0) v = 0;
  return v;
}

// Популярні ISM-частоти (кГц) для швидкого вибору в апці захоплення.
struct Preset { long khz; const char* name; };
static const Preset PRESETS[] = {
  { 433920, "433.92" },   // EU/global ISM — гаражі/пульти/датчики (Nice тощо)
  { 868350, "868.35" },   // EU ISM — «рідне» вікно модуля, wM-Bus/IoT
  { 315000, "315.00" },   // US/Азія пульти
  { 915000, "915.00" },   // US ISM
  { 434420, "434.42" },   // альт. 433-канал деяких пультів
};
static const int PRESET_COUNT = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

int subghz_preset_count() { return PRESET_COUNT; }
long subghz_preset_khz(int i) { return (i >= 0 && i < PRESET_COUNT) ? PRESETS[i].khz : -1; }
const char* subghz_preset_name(int i) { return (i >= 0 && i < PRESET_COUNT) ? PRESETS[i].name : "?"; }
