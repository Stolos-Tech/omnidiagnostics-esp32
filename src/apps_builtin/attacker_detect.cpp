#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include "attacker_detect.h"
#include "../kernel/attacker_id.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

void AttackerDetectApp::start_scan() {
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING; scan_frame_ = 0;
}

void AttackerDetectApp::init() {
  wants_exit_ = false; count_ = 0; cursor_ = 0;
  start_scan();
}

// Evil-twin: той самий (непорожній) SSID зустрічається з ІНШИМ BSSID у скані.
static bool is_evil_twin(int idx, int n) {
  const char* ssid = wifi_sta_ssid(idx);
  if (!ssid || !ssid[0]) return false;
  const char* bssid = wifi_sta_bssid_str(idx);
  for (int j = 0; j < n; j++) {
    if (j == idx) continue;
    const char* s2 = wifi_sta_ssid(j);
    if (!s2 || strcmp(s2, ssid) != 0) continue;
    if (strcmp(wifi_sta_bssid_str(j), bssid) != 0) return true;   // однакове ім'я, інший BSSID
  }
  return false;
}

void AttackerDetectApp::do_scan() {
  wifi_sta_scan_start();            // блокуючий ~2-4с
  int n = wifi_sta_scan_state();
  if (n < 0) n = 0;
  count_ = 0;
  for (int i = 0; i < n && count_ < MAX_H; i++) {
    const char* ssid = wifi_sta_ssid(i);
    bool pwn = attacker_is_pwnagotchi_ssid(ssid);
    bool evil = !pwn && is_evil_twin(i, n);
    if (!pwn && !evil) continue;
    snprintf(label_[count_], sizeof(label_[0]), "%s", pwn ? "Pwnagotchi" : "Evil twin?");
    snprintf(ssid_[count_], sizeof(ssid_[0]), "%s", (ssid && ssid[0]) ? ssid : "(hidden)");
    rssi_[count_] = wifi_sta_rssi(i);
    chan_[count_] = wifi_sta_channel(i);
    count_++;
  }
  cursor_ = 0;
  phase_ = LIST;
}

void AttackerDetectApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }
    do_scan();
  }
}


void AttackerDetectApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("ATTACKER DETECT");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(count_ ? C_BAD : C_GOOD, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d hits", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (count_ == 0) {
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString("No attacker signs", 8, 34, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("deauth flood -> Deauth Alert", 8, 54, 1);
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
    if (idx >= count_) {
      spr.setTextColor(sel ? C_ACCENT : C_DIM, sel ? C_PANEL : C_BG);
      spr.drawString("< Back", 10, y + 5, 2);
      continue;
    }
    spr.setTextColor(C_BAD, sel ? C_PANEL : C_BG);
    spr.drawString(label_[idx], 10, y + 1, 2);
    spr.setTextColor(C_DIM, sel ? C_PANEL : C_BG);
    char b[32]; snprintf(b, sizeof(b), "%.14s %ddBm c%d", ssid_[idx], rssi_[idx], chan_[idx]);
    spr.drawString(b, 10, y + 13, 1);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=rescan/back", 8, SCR_H - 12, 1);
}

void AttackerDetectApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BT_BLOCKED) {
    display_top_bar("ATTACKER DETECT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=vymk.BT+skan  S2=exit", 8, SCR_H - 14, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("ATTACKER DETECT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 52, 1);
    spr.drawString("S2/S5=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    display_top_bar("ATTACKER DETECT");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Scanning WiFi...", 8, 50, 2);
    return;
  }
  drawList();
}

void AttackerDetectApp::select_current() {
  if (cursor_ >= count_) { wants_exit_ = true; return; }
  start_scan();
}

void AttackerDetectApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) { cursor_ = idx; select_current(); }
}

void AttackerDetectApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (phase_ == BT_BLOCKED) {
    if (id == BTN_S5) { remote_activate(MODE_OFF); start_scan(); }
    else if (id == BTN_S2) wants_exit_ = true;
    return;
  }
  if (phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

std::string AttackerDetectApp::remote_state() {
  char lines[MAX_H + 1][32];
  const char* items[MAX_H + 1];
  int n = 0;
  if (count_ == 0) { snprintf(lines[0], sizeof(lines[0]), "No attacker signs"); items[0] = lines[0]; n = 1; }
  else for (int i = 0; i < count_; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%s %.10s c%d %ddBm", label_[i], ssid_[i], chan_[i], rssi_[i]);
    items[n] = lines[n]; n++;
  }
  return protocol_build_menu("attacker_detect", items, n, cursor_ < n ? cursor_ : -1);
}
