#include <Arduino.h>
#include <stdio.h>
#include "subghz_analyzer.h"
#include "../drivers/display.h"
#include "../drivers/cc1101.h"
#include "../kernel/subghz_util.h"
#include "../remote/protocol.h"

static const int FLOOR_DBM = -120;   // «підлога» шкали / порожня точка
static const int TOP_DBM   = -30;    // «стеля» шкали (сильний сигнал)

void SubghzAnalyzerApp::reset_scan() {
  n_ = subghz_scan_count();
  if (n_ > MAXPTS) n_ = MAXPTS;
  for (int i = 0; i < n_; i++) rssi_[i] = FLOOR_DBM;
  passes_ = 0;
}

void SubghzAnalyzerApp::init() {
  wants_exit_ = false;
  present_ = cc1101_begin();
  reset_scan();
}

void SubghzAnalyzerApp::loop() {
  if (!present_) { present_ = cc1101_begin(); return; }   // ре-детект, якщо встромили
  // Один повний прохід свіпу (peak-hold з повільним спадом -> «жива» картина).
  for (int i = 0; i < n_; i++) {
    int dbm = cc1101_probe_rssi_dbm(subghz_scan_khz(i));
    int held = rssi_[i] - 2;                 // повільний decay попереднього піку
    if (held < FLOOR_DBM) held = FLOOR_DBM;
    int v = (dbm > held) ? dbm : held;
    if (v < FLOOR_DBM) v = FLOOR_DBM;
    if (v > 0) v = 0;
    rssi_[i] = (int8_t)v;
  }
  passes_++;
}

void SubghzAnalyzerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("SUB-GHZ ANALYZER", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(present_ ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(present_ ? "CC1101 OK" : "no CC1101", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!present_) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("CC1101 ne znaydeno.", 8, 34, 2);
    spr.drawString("SCK2 MOSI15 MISO38 CS27 GDO0=39", 8, 58, 1);
    spr.drawString("zhyvlennya 3.3V, antena 868", 8, 72, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }

  const int gx = 8, gy = 22, gw = SCR_W - 16, gh = 78;
  spr.drawFastHLine(gx, gy + gh, gw, C_GRID);

  // Вертикальні межі між вікнами A|B|C (де змінюється window()).
  for (int i = 1; i < n_; i++) {
    if (subghz_scan_window(i) != subghz_scan_window(i - 1)) {
      int x = gx + gw * i / n_;
      spr.drawFastVLine(x, gy, gh, C_GRID);
    }
  }

  int peak = subghz_peak_index(rssi_, n_);
  for (int i = 0; i < n_; i++) {
    int x = gx + gw * i / n_;
    int h = subghz_bar_height(rssi_[i], FLOOR_DBM, TOP_DBM, gh);
    if (h > 0) {
      uint16_t col = (i == peak) ? C_WARN
                    : (rssi_[i] > -60) ? C_BAD
                    : (rssi_[i] > -85) ? C_ACCENT : C_GOOD;
      spr.drawFastVLine(x, gy + gh - h, h, col);
    }
  }

  char b[44];
  long pk = subghz_scan_khz(peak < 0 ? 0 : peak);
  int  pd = (peak >= 0) ? rssi_[peak] : FLOOR_DBM;
  snprintf(b, sizeof(b), "Peak: %ld.%02ld MHz  %d dBm", pk / 1000, (pk % 1000) / 10, pd);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(b, 8, gy + gh + 3, 1);

  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "passes %d   S5=reset  S2=exit", passes_);
  spr.drawString(b, 8, SCR_H - 12, 1);
}

void SubghzAnalyzerApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) { reset_scan(); }
}

std::string SubghzAnalyzerApp::remote_state() {
  char lines[5][40];
  const char* items[5];
  int n = 0;
  if (!present_) {
    snprintf(lines[n++], 40, "CC1101: NOT FOUND");
    snprintf(lines[n++], 40, "SCK2 MOSI15 MISO38 CS27 GDO39");
    snprintf(lines[n++], 40, "3.3V, antena 868/915");
  } else {
    int peak = subghz_peak_index(rssi_, n_);
    long pk = subghz_scan_khz(peak < 0 ? 0 : peak);
    snprintf(lines[n++], 40, "CC1101: OK  passes %d", passes_);
    snprintf(lines[n++], 40, "Peak: %ld.%02ld MHz", pk / 1000, (pk % 1000) / 10);
    snprintf(lines[n++], 40, "RSSI: %d dBm", (peak >= 0) ? rssi_[peak] : FLOOR_DBM);
    snprintf(lines[n++], 40, "sweep 300-928 (3 vikna)");
  }
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("subghz_analyzer", items, n, -1);
}
