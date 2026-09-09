#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include "wifi_analyzer.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../kernel/shared_store.h"
#include "../drivers/sd_store.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

void WifiAnalyzerApp::start_scan() {
  // Guard: WiFi і BT ділять одне радіо (як у WiFi Setup).
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING;
  scan_frame_ = 0;
}

void WifiAnalyzerApp::init() {
  wants_exit_ = false;
  gCount_ = 0;
  tGraph_ = 0;
  page_ = LIST;
  start_scan();
}

void WifiAnalyzerApp::build_channels() {
  memset(chan_hist_, 0, sizeof(chan_hist_));
  for (int i = 0; i < count_; i++) {
    int ch = wifi_sta_channel(i);
    if (ch >= 1 && ch <= 13) chan_hist_[ch]++;
  }
}

void WifiAnalyzerApp::do_scan() {
  wifi_sta_scan_start();            // блокуючий (~2-4с), як WiFi Setup
  int n = wifi_sta_scan_state();
  if (n < 0) n = 0;
  count_ = n < MAX_NETS ? n : MAX_NETS;
  for (int i = 0; i < count_; i++) order_[i] = i;
  // сортування вставками за RSSI спадно (count_ мале, <= MAX_NETS)
  for (int i = 1; i < count_; i++) {
    int key = order_[i];
    int kr = wifi_sta_rssi(key);
    int j = i - 1;
    while (j >= 0 && wifi_sta_rssi(order_[j]) < kr) { order_[j + 1] = order_[j]; j--; }
    order_[j + 1] = key;
  }
  // Крос-модульний store: усі знайдені мережі -> інші модулі (WiFi Setup тощо) юзають без рескану.
  for (int i = 0; i < count_; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    wifi_auth_mode_t e = WiFi.encryptionType(i);
    const char* enc = (e == WIFI_AUTH_OPEN) ? "OPEN" : (e == WIFI_AUTH_WEP) ? "WEP" : "WPA2";
    shared_add_net(ss.c_str(), WiFi.channel(i), WiFi.RSSI(i), enc);
  }
  build_channels();
  sd_persist_shared();   // instant-save: мережі знайдено -> сортований дамп на SD (no-op якщо не змонт.)
  phase_ = NAV;
}

void WifiAnalyzerApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }  // один кадр "Skanuvannia"
    do_scan();
    return;
  }
  if (phase_ == NAV && page_ == RSSI_GRAPH && WiFi.status() == WL_CONNECTED) {
    if (millis() - tGraph_ >= 2000) {
      tGraph_ = millis();
      float v = (float)WiFi.RSSI();
      if (gCount_ < G_N) gBuf_[gCount_++] = v;
      else { memmove(gBuf_, gBuf_ + 1, (G_N - 1) * sizeof(float)); gBuf_[G_N - 1] = v; }
    }
  }
}


void WifiAnalyzerApp::pageDots() {
  TFT_eSprite& spr = display_sprite();
  int cx = SCR_W / 2 - (PAGE_COUNT * 10) / 2;
  for (int i = 0; i < PAGE_COUNT; i++)
    spr.fillCircle(cx + i * 10 + 4, SCR_H - 5, i == page_ ? 3 : 2, i == page_ ? C_ACCENT : C_GRID);
}

void WifiAnalyzerApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("WIFI: TOP NETWORKS");
  if (count_ == 0) {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_WARN, C_BG);
    spr.drawString("0 networks found", 8, 34, 2);
    return;
  }
  int shown = count_ < 5 ? count_ : 5;
  for (int row = 0; row < shown; row++) {
    int i = order_[row];
    int y = 20 + row * 18;
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
    const char* ssid = wifi_sta_ssid(i);
    char b[24]; snprintf(b, sizeof(b), "%.16s", ssid[0] ? ssid : "(hidden)");
    spr.drawString(b, 8, y, 2);
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_BG);
    char r[20]; snprintf(r, sizeof(r), "%ddBm c%d%s", wifi_sta_rssi(i), wifi_sta_channel(i), wifi_sta_is_open(i) ? "" : "*");
    spr.drawString(r, SCR_W - 6, y, 1);
  }
  if (count_ > 5) {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
    char b[24]; snprintf(b, sizeof(b), "+%d more (weakest)", count_ - 5);
    spr.drawString(b, 8, 20 + 5 * 18, 1);
  }
}

void WifiAnalyzerApp::drawChannels() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("WIFI: CHANNELS");
  int maxc = 1;
  for (int c = 1; c <= 13; c++) if (chan_hist_[c] > maxc) maxc = chan_hist_[c];
  int gx = 6, gy = 22, gw = SCR_W - 12, gh = SCR_H - 44;
  int bw = gw / 13;
  for (int c = 1; c <= 13; c++) {
    int h = (int)((float)gh * chan_hist_[c] / maxc);
    int x = gx + (c - 1) * bw;
    uint16_t col = chan_hist_[c] == 0 ? C_GRID : (chan_hist_[c] >= maxc ? C_BAD : C_ACCENT);
    if (h > 0) spr.fillRect(x + 1, gy + gh - h, bw - 2, h, col);
    else       spr.drawFastHLine(x + 1, gy + gh, bw - 2, C_GRID);
    spr.setTextDatum(TC_DATUM); spr.setTextColor(C_DIM, C_BG);
    char cl[3]; snprintf(cl, sizeof(cl), "%d", c);
    spr.drawString(cl, x + bw / 2, gy + gh + 2, 1);
  }
}

void WifiAnalyzerApp::drawRssiGraph() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("WIFI: RSSI (connected net)");
  if (WiFi.status() != WL_CONNECTED) {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Not connected", 8, 40, 2);
    return;
  }
  int gx = 4, gy = 20, gw = SCR_W - 8, gh = SCR_H - 34;
  spr.drawRect(gx, gy, gw, gh, C_GRID);
  float lo = -100, hi = -30;
  int prevX = -1, prevY = -1;
  for (int i = 0; i < gCount_; i++) {
    int x = gx + (gw - 2) * i / (G_N - 1) + 1;
    float v = gBuf_[i]; if (v < lo) v = lo; if (v > hi) v = hi;
    int y = gy + gh - (int)((v - lo) / (hi - lo) * (gh - 2)) - 1;
    if (prevX >= 0) spr.drawLine(prevX, prevY, x, y, C_ACCENT);
    prevX = x; prevY = y;
  }
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
  char b[24]; snprintf(b, sizeof(b), "%d dBm", WiFi.RSSI());
  spr.drawString(b, 8, SCR_H - 26, 1);
}

void WifiAnalyzerApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("EXIT");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Exit to launcher?", SCR_W / 2, SCR_H / 2 - 10, 4);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = exit   S2 = next", SCR_W / 2, SCR_H / 2 + 20, 2);
}

void WifiAnalyzerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);

  if (phase_ == SCANNING) {
    display_top_bar("WIFI ANALYZER");
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Scanning...", 8, 50, 2);
    return;
  }
  if (phase_ == BT_BLOCKED) {
    display_top_bar("WIFI ANALYZER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne mozhut razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=vymk. BT i skan", 8, SCR_H - 30, 2);
    spr.drawString("S2=exit", 8, SCR_H - 14, 2);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("WIFI ANALYZER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5=exit", 8, SCR_H - 14, 2);
    return;
  }

  switch (page_) {
    case LIST:       drawList(); break;
    case CHANNELS:    drawChannels(); break;
    case RSSI_GRAPH:  drawRssiGraph(); break;
    case PAGE_EXIT:   drawExit(); break;
    default: break;
  }
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
  if (page_ != PAGE_EXIT) spr.drawString("S2=next  S5=rescan", 8, SCR_H - 12, 1);
  pageDots();
}

void WifiAnalyzerApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (phase_ == BT_BLOCKED) {
    if (id == BTN_S5) { remote_activate(MODE_OFF); start_scan(); }
    else if (id == BTN_S2) wants_exit_ = true;
    return;
  }
  if (phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  // NAV
  if (id == BTN_S2) { page_ = (Page)((page_ + 1) % PAGE_COUNT); return; }
  if (id == BTN_S5) {
    if (page_ == PAGE_EXIT) wants_exit_ = true;
    else start_scan();
  }
}

std::string WifiAnalyzerApp::remote_state() {
  char lines[21][32];
  const char* items[21];
  int n = 0;
  int shown = count_ < 20 ? count_ : 20;   // усі мережі у веб (не лише top-5) — гортаються у веб
  for (int row = 0; row < shown && n < 20; row++) {
    int i = order_[row];
    const char* ssid = wifi_sta_ssid(i);
    snprintf(lines[n], sizeof(lines[n]), "%.14s %ddBm c%d", ssid[0] ? ssid : "(hidden)",
             wifi_sta_rssi(i), wifi_sta_channel(i));
    items[n] = lines[n]; n++;
  }
  if (n == 0) { snprintf(lines[0], sizeof(lines[0]), "0 networks"); items[0] = lines[0]; n = 1; }
  return protocol_build_menu("wifi_analyzer", items, n, -1);
}
