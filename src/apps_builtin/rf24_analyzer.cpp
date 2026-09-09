#include <Arduino.h>
#include <stdio.h>
#include "rf24_analyzer.h"
#include "../drivers/display.h"
#include "../drivers/nrf24.h"
#include "../kernel/rf24_util.h"
#include "../remote/protocol.h"

void Rf24AnalyzerApp::reset_scan() {
  for (int i = 0; i < CH; i++) counts_[i] = 0;
  passes_ = 0; maxv_ = 0;
}

void Rf24AnalyzerApp::init() {
  wants_exit_ = false;
  present_ = nrf24_begin();
  reset_scan();
}

void Rf24AnalyzerApp::loop() {
  if (!present_) { present_ = nrf24_begin(); return; }   // спроба ре-детекту, якщо встромили
  nrf24_scan_pass(counts_, CH);
  passes_++;
  int m = 0;
  for (int i = 0; i < CH; i++) if (counts_[i] > m) m = counts_[i];
  // Rolling: коли лічильники насичуються — ділимо навпіл (decay), щоб картина
  // лишалась «живою», а не назавжди залипала на 255.
  if (m >= 250) { for (int i = 0; i < CH; i++) counts_[i] >>= 1; m >>= 1; }
  maxv_ = m;
}

void Rf24AnalyzerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("2.4G ANALYZER", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(present_ ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(present_ ? "nRF24 OK" : "no nRF24", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!present_) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("nRF24 ne znaydeno.", 8, 34, 2);
    spr.drawString("SCK2 MOSI15 MISO38 CSN25 CE12", 8, 58, 1);
    spr.drawString("+adapter 3.3V, 10k pd na GPIO12", 8, 72, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }

  const int gx = 8, gy = 22, gw = SCR_W - 16, gh = 78;
  spr.drawFastHLine(gx, gy + gh, gw, C_GRID);

  // Мітки WiFi ch1/6/11 (nRF-канали 12/37/62) — вертикальні орієнтири.
  const int wifiMarks[3] = {12, 37, 62};
  for (int i = 0; i < 3; i++) {
    int x = gx + gw * wifiMarks[i] / CH;
    spr.drawFastVLine(x, gy, gh, C_GRID);
  }

  int peak = rf24_peak_index(counts_, CH);
  for (int ch = 0; ch < CH; ch++) {
    int x = gx + gw * ch / CH;
    int h = rf24_bar_height(counts_[ch], maxv_, gh);
    if (h > 0) {
      uint16_t col = (ch == peak) ? C_WARN : (rf24_wifi_channel(ch) ? C_ACCENT : C_GOOD);
      spr.drawFastVLine(x, gy + gh - h, h, col);
    }
  }

  char b[40];
  int pm = rf24_channel_mhz(peak < 0 ? 0 : peak);
  int wc = rf24_wifi_channel(peak < 0 ? 0 : peak);
  int pc = (peak >= 0) ? counts_[peak] : 0;
  if (wc) snprintf(b, sizeof(b), "Peak: %d MHz (WiFi ch%d) x%d", pm, wc, pc);
  else    snprintf(b, sizeof(b), "Peak: %d MHz  x%d", pm, pc);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(b, 8, gy + gh + 3, 1);

  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "passes %d   S5=reset  S2=exit", passes_);
  spr.drawString(b, 8, SCR_H - 12, 1);
}

void Rf24AnalyzerApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) { reset_scan(); }
}

std::string Rf24AnalyzerApp::remote_state() {
  char lines[5][40];
  const char* items[5];
  int n = 0;
  if (!present_) {
    snprintf(lines[n++], 40, "nRF24: NOT FOUND");
    snprintf(lines[n++], 40, "SCK2 MOSI15 MISO38 CSN25 CE12");
    snprintf(lines[n++], 40, "10k pulldown na GPIO12 (CE)");
  } else {
    int peak = rf24_peak_index(counts_, CH);
    int wc = rf24_wifi_channel(peak < 0 ? 0 : peak);
    snprintf(lines[n++], 40, "nRF24: OK  passes %d", passes_);
    snprintf(lines[n++], 40, "Peak: %d MHz", rf24_channel_mhz(peak < 0 ? 0 : peak));
    if (wc) snprintf(lines[n++], 40, "overlaps WiFi ch%d", wc);
    snprintf(lines[n++], 40, "hits@peak: %d", (peak >= 0) ? counts_[peak] : 0);
  }
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("rf24_analyzer", items, n, -1);
}
