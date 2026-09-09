#include <Arduino.h>
#include <stdio.h>
#include <BluetoothSerial.h>
#include "card_skimmer.h"
#include "../kernel/skimmer_id.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth Classic вимкнено у sdkconfig — потрібен BluetoothSerial"
#endif

#define SCAN_MS 8000
static BluetoothSerial s_bt;   // окремий інстанс лише для інвентаризації

void CardSkimmerApp::start_scan() {
  if (remote_active_mode() != MODE_OFF) { phase_ = BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING; scan_frame_ = 0;
}

void CardSkimmerApp::init() {
  wants_exit_ = false; count_ = 0; cursor_ = 0;
  start_scan();
}

void CardSkimmerApp::do_scan() {
  wifi_sta_stop();
  s_bt.begin("OmniDiag-Scan");
  BTScanResults* res = s_bt.discover(SCAN_MS);   // блокуюче ~8с
  count_ = 0;
  if (res) {
    int n = res->getCount();
    for (int i = 0; i < n && count_ < MAX_S; i++) {
      BTAdvertisedDevice* d = res->getDevice(i);
      if (!d) continue;
      std::string nm = d->getName();
      if (!skimmer_is_suspect_name(nm.c_str())) continue;   // лишаємо ЛИШЕ підозрілі
      snprintf(name_[count_], sizeof(name_[0]), "%s", nm.c_str());
      snprintf(addr_[count_], sizeof(addr_[0]), "%s", d->getAddress().toString().c_str());
      rssi_[count_] = d->haveRSSI() ? d->getRSSI() : 0;
      count_++;
    }
  }
  s_bt.end();
  cursor_ = 0;
  phase_ = LIST;
}

void CardSkimmerApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }
    do_scan();
  }
}


void CardSkimmerApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("CARD SKIMMER");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(count_ ? C_BAD : C_GOOD, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d suspect", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (count_ == 0) {
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString("No skimmer-like BT", 8, 38, 2);
    spr.setTextColor(C_DIM, C_BG);
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
    spr.drawString(name_[idx], 10, y + 1, 2);
    spr.setTextColor(C_DIM, sel ? C_PANEL : C_BG);
    char b[32]; snprintf(b, sizeof(b), "%.17s %ddBm", addr_[idx], rssi_[idx]);
    spr.drawString(b, 10, y + 13, 1);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=rescan/back", 8, SCR_H - 12, 1);
}

void CardSkimmerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BLOCKED) {
    display_top_bar("CARD SKIMMER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote WiFi/BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Turn off remote mode", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("CARD SKIMMER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    display_top_bar("CARD SKIMMER");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Scanning BT (~8s)...", 8, 50, 2);
    return;
  }
  drawList();
}

void CardSkimmerApp::select_current() {
  if (cursor_ >= count_) { wants_exit_ = true; return; }
  start_scan();
}

void CardSkimmerApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) { cursor_ = idx; select_current(); }
}

void CardSkimmerApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (phase_ == BLOCKED || phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

std::string CardSkimmerApp::remote_state() {
  char lines[MAX_S + 1][40];
  const char* items[MAX_S + 1];
  int n = 0;
  if (count_ == 0) { snprintf(lines[0], sizeof(lines[0]), "No skimmer-like BT"); items[0] = lines[0]; n = 1; }
  else for (int i = 0; i < count_; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%.14s %s %ddBm", name_[i], addr_[i], rssi_[i]);
    items[n] = lines[n]; n++;
  }
  return protocol_build_menu("card_skimmer", items, n, cursor_ < n ? cursor_ : -1);
}

// #4: скімер глушив WiFi (BLE-коекзистенція) -> повертаємо мережу на виході.
void CardSkimmerApp::on_exit() {
  wifi_sta_resume();
}
