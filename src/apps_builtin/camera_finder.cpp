#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include "camera_finder.h"
#include "../kernel/oui_vendor.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define HIDDEN_SUSPECT_RSSI (-55)   // прихований SSID сильніший за це -> «поруч, підозріло»

void CameraFinderApp::start_scan() {
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }  // WiFi і BT ділять радіо
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING; scan_frame_ = 0;
}

void CameraFinderApp::init() {
  wants_exit_ = false; count_ = 0; cursor_ = 0;
  start_scan();
}

void CameraFinderApp::do_scan() {
  wifi_sta_scan_start();            // блокуючий ~2-4с (як WiFi Analyzer)
  int n = wifi_sta_scan_state();
  if (n < 0) n = 0;
  count_ = 0;
  for (int i = 0; i < n && count_ < MAX_H; i++) {
    const char* ssid = wifi_sta_ssid(i);
    const char* bssid = wifi_sta_bssid_str(i);
    uint8_t b0, b1, b2;
    const char* vendor = nullptr;
    if (oui_parse3(bssid, &b0, &b1, &b2)) vendor = oui_camera_vendor(b0, b1, b2);
    bool hidden = (ssid == nullptr || ssid[0] == 0);
    int rssi = wifi_sta_rssi(i);
    bool suspectHidden = hidden && rssi >= HIDDEN_SUSPECT_RSSI;
    if (!vendor && !suspectHidden) continue;    // ні вендор камери, ні підозрілий прихований

    snprintf(label_[count_], sizeof(label_[0]), "%s", vendor ? vendor : "hidden AP?");
    snprintf(ssid_[count_], sizeof(ssid_[0]), "%s", hidden ? "(hidden)" : ssid);
    rssi_[count_] = rssi;
    chan_[count_] = wifi_sta_channel(i);
    suspect_[count_] = (vendor == nullptr);
    count_++;
  }
  cursor_ = 0;
  phase_ = LIST;
}

void CameraFinderApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }   // один кадр "Scanning"
    do_scan();
  }
}


void CameraFinderApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("CAMERA FINDER");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(count_ ? C_BAD : C_GOOD, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d hits", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (count_ == 0) {
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString("No camera-vendor APs", 8, 34, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("analog 2.4G -> 2.4G Analyzer", 8, 54, 1);
    spr.drawString("S5=rescan   S2/BACK=exit", 8, SCR_H - 12, 1);
    return;
  }

  const int VISIBLE = 4, ROW_H = 24, total = items_total();
  int first = cursor_ - VISIBLE / 2; if (first < 0) first = 0;
  if (first > total - VISIBLE) first = total - VISIBLE; if (first < 0) first = 0;
  for (int row = 0; row < VISIBLE; row++) {
    int idx = first + row; if (idx >= total) break;
    int y = 20 + row * ROW_H;
    bool sel = (idx == cursor_);
    if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
    if (idx >= count_) {   // "< Back"
      spr.setTextColor(sel ? C_ACCENT : C_DIM, sel ? C_PANEL : C_BG);
      spr.drawString("< Back", 10, y + 5, 2);
      continue;
    }
    spr.setTextColor(suspect_[idx] ? C_WARN : C_BAD, sel ? C_PANEL : C_BG);
    spr.drawString(label_[idx], 10, y + 1, 2);
    spr.setTextColor(C_DIM, sel ? C_PANEL : C_BG);
    char b[32]; snprintf(b, sizeof(b), "%.14s %ddBm c%d", ssid_[idx], rssi_[idx], chan_[idx]);
    spr.drawString(b, 10, y + 13, 1);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=rescan/back", 8, SCR_H - 12, 1);
}

void CameraFinderApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BT_BLOCKED) {
    display_top_bar("CAMERA FINDER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=vymk.BT+skan  S2=exit", 8, SCR_H - 14, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("CAMERA FINDER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 52, 1);
    spr.drawString("S2/S5=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    display_top_bar("CAMERA FINDER");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Scanning WiFi...", 8, 50, 2);
    return;
  }
  drawList();
}

void CameraFinderApp::select_current() {
  if (cursor_ >= count_) { wants_exit_ = true; return; }  // "< Back"
  start_scan();                                           // на знахідці -> пере-скан
}

void CameraFinderApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) { cursor_ = idx; select_current(); }
}

void CameraFinderApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (phase_ == BT_BLOCKED) {
    if (id == BTN_S5) { remote_activate(MODE_OFF); start_scan(); }
    else if (id == BTN_S2) wants_exit_ = true;
    return;
  }
  if (phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  // LIST
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

std::string CameraFinderApp::remote_state() {
  char lines[MAX_H + 1][32];
  const char* items[MAX_H + 1];
  int n = 0;
  if (count_ == 0) { snprintf(lines[0], sizeof(lines[0]), "No camera-vendor APs"); items[0] = lines[0]; n = 1; }
  else for (int i = 0; i < count_; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%s %.10s %ddBm c%d", label_[i], ssid_[i], rssi_[i], chan_[i]);
    items[n] = lines[n]; n++;
  }
  return protocol_build_menu("camera_finder", items, n, cursor_ < n ? cursor_ : -1);
}
