#include <Arduino.h>
#include <stdio.h>
#include "subghz_watch.h"
#include "../drivers/display.h"
#include "../drivers/cc1101.h"
#include "../kernel/subghz_util.h"
#include "../remote/protocol.h"

void SubghzWatchApp::reset() {
  n_ = subghz_scan_count();
  if (n_ > MAXPTS) n_ = MAXPTS;
  for (int i = 0; i < n_; i++) { streak_[i] = 0; dbm_[i] = -120; }
  passes_ = 0;
}

void SubghzWatchApp::init() {
  wants_exit_ = false;
  present_ = cc1101_begin();
  reset();
}

void SubghzWatchApp::loop() {
  if (!present_) { present_ = cc1101_begin(); return; }
  for (int i = 0; i < n_; i++) {
    int d = cc1101_probe_rssi_dbm(subghz_scan_khz(i));
    dbm_[i] = (int8_t)(d < -128 ? -128 : d);
    if (d > THRESH_DBM) { if (streak_[i] < 255) streak_[i]++; }
    else                { streak_[i] = 0; }
  }
  passes_++;
}

// Індекси стійких точок (streak>=PERSIST), відсортовані за спаданням dBm. Повертає к-ть.
int SubghzWatchApp::collect(int* idx, int max) const {
  int m = 0;
  for (int i = 0; i < n_ && m < max; i++)
    if (streak_[i] >= PERSIST) idx[m++] = i;
  // проста сортировка вставками за dBm (спад)
  for (int a = 1; a < m; a++) {
    int key = idx[a], b = a - 1;
    while (b >= 0 && dbm_[idx[b]] < dbm_[key]) { idx[b + 1] = idx[b]; b--; }
    idx[b + 1] = key;
  }
  return m;
}

void SubghzWatchApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  display_top_bar("SUB-GHZ WATCH");
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(present_ ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(present_ ? "CC1101" : "no CC1101", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!present_) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("CC1101 ne znaydeno.", 8, 34, 2);
    spr.drawString("SCK2 MOSI15 MISO38 CS27 GDO0=39", 8, 58, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }

  int idx[8];
  int m = collect(idx, 8);
  char b[44];
  if (m == 0) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(passes_ < PERSIST ? "watching sub-GHz..." : "no persistent Tx", 8, 40, 2);
    spr.drawString("433/868 trackers/bugs show here", 8, 62, 1);
  } else {
    spr.setTextColor(C_WARN, C_BG);
    snprintf(b, sizeof(b), "persistent Tx: %d", m);
    spr.drawString(b, 8, 22, 2);
    int y = 42;
    for (int i = 0; i < m && i < 5; i++) {
      long f = subghz_scan_khz(idx[i]);
      spr.setTextColor(C_TEXT, C_BG);
      snprintf(b, sizeof(b), "%ld.%02ld MHz  %d dBm  x%d",
               f / 1000, (f % 1000) / 10, dbm_[idx[i]], streak_[idx[i]]);
      spr.drawString(b, 8, y, 1);
      y += 13;
    }
  }
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "passes %d   S5=reset  S2=exit", passes_);
  spr.drawString(b, 8, SCR_H - 12, 1);
}

void SubghzWatchApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) { reset(); }
}

std::string SubghzWatchApp::remote_state() {
  char lines[6][40];
  const char* items[6];
  int n = 0;
  if (!present_) {
    snprintf(lines[n++], 40, "CC1101: NOT FOUND");
    snprintf(lines[n++], 40, "SCK2 MOSI15 MISO38 CS27 GDO39");
  } else {
    int idx[8];
    int m = collect(idx, 8);
    snprintf(lines[n++], 40, "Watch: %d persistent (p%d)", m, passes_);
    for (int i = 0; i < m && n < 6; i++) {
      long f = subghz_scan_khz(idx[i]);
      snprintf(lines[n++], 40, "%ld.%02ld MHz %ddBm x%d",
               f / 1000, (f % 1000) / 10, dbm_[idx[i]], streak_[idx[i]]);
    }
    if (m == 0) snprintf(lines[n++], 40, passes_ < PERSIST ? "watching..." : "no persistent Tx");
  }
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("subghz_watch", items, n, -1);
}
