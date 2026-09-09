#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include "ble_scan.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define SCAN_SECONDS 5

static BLEClient* s_client = nullptr;

void BleScanApp::start_scan() {
  // WiFi і BT ділять одне радіо; Remote WiFi/BT теж тримає Bluedroid зайнятим.
  if (remote_active_mode() != MODE_OFF) { phase_ = BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING;
  scan_frame_ = 0;
}

void BleScanApp::init() {
  wants_exit_ = false;
  count_ = 0;
  cursor_ = 0;
  connected_ = false;
  start_scan();
}

void BleScanApp::do_scan() {
  wifi_sta_stop();
  static bool s_ble_init = false;
  if (!s_ble_init) { BLEDevice::init(""); s_ble_init = true; }
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);   // ~99% duty cycle (вікно≈інтервал) -> максимум вилову реклами,
  scan->setWindow(99);      // радіо майже не «спить» під час скану (дефолт ~60% пропускає адв.)
  BLEScanResults res = scan->start(SCAN_SECONDS, false);   // блокуюче ~5с
  count_ = 0;
  int n = res.getCount();
  for (int i = 0; i < n && count_ < MAX_DEV; i++) {
    BLEAdvertisedDevice d = res.getDevice(i);
    std::string nm = d.getName();
    snprintf(names_[count_], sizeof(names_[0]), "%s", nm.empty() ? "(bez imeni)" : nm.c_str());
    snprintf(addrs_[count_], sizeof(addrs_[0]), "%s", d.getAddress().toString().c_str());
    rssi_[count_] = d.getRSSI();
    count_++;
  }
  scan->clearResults();
  cursor_ = 0;
  phase_ = LIST;
}

void BleScanApp::connect_selected() {
  phase_ = CONNECTING;
  if (!s_client) s_client = BLEDevice::createClient();
  BLEAddress addr(std::string(addrs_[cursor_]));
  if (!s_client->connect(addr)) { phase_ = CONNECT_FAIL; return; }
  connected_ = true;
  auto* svcs = s_client->getServices();
  svc_count_ = svcs ? (int)svcs->size() : 0;
  svc_cursor_ = 0;
  phase_ = GATT;
}

void BleScanApp::disconnect_gatt() {
  if (connected_ && s_client) { s_client->disconnect(); connected_ = false; }
  phase_ = LIST;
}

void BleScanApp::on_exit() {
  if (connected_ && s_client) { s_client->disconnect(); connected_ = false; }
  wifi_sta_resume();   // #4: BLE-скан глушив WiFi -> повертаємо мережу на виході
}

void BleScanApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }  // один кадр "Skanuvannia"
    do_scan();
  }
}


void BleScanApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("BLE SCAN");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d found", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  const int VISIBLE = 5, ROW_H = 18, total = items_total();
  int first = cursor_ - VISIBLE / 2;
  if (first < 0) first = 0;
  if (first > total - VISIBLE) first = total - VISIBLE;
  if (first < 0) first = 0;
  for (int row = 0; row < VISIBLE; row++) {
    int idx = first + row;
    if (idx >= total) break;
    int y = 20 + row * ROW_H;
    bool sel = (idx == cursor_);
    if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
    spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
    char b[24];
    if (idx < count_) snprintf(b, sizeof(b), "%.16s", names_[idx]);
    else              snprintf(b, sizeof(b), "< Back");
    spr.drawString(b, 10, y + 1, 2);
    if (idx < count_) {
      spr.setTextDatum(TR_DATUM); spr.setTextColor(sel ? C_ACCENT : C_DIM, sel ? C_PANEL : C_BG);
      char r[12]; snprintf(r, sizeof(r), "%d", rssi_[idx]);
      spr.drawString(r, SCR_W - 8, y + 1, 1);
      spr.setTextDatum(TL_DATUM);
    }
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
}

void BleScanApp::drawGatt() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("BLE: GATT");
  if (svc_count_ == 0 || !s_client) {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_WARN, C_BG);
    spr.drawString("No services", 8, 34, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S2=back", 8, SCR_H - 12, 1);
    return;
  }
  auto* svcs = s_client->getServices();
  BLERemoteService* svc = nullptr;
  int i = 0;
  for (auto& kv : *svcs) { if (i == svc_cursor_) { svc = kv.second; break; } i++; }
  if (!svc) return;

  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_BG);
  char sb[40]; snprintf(sb, sizeof(sb), "Svc %d/%d: %.20s", svc_cursor_ + 1, svc_count_, svc->getUUID().toString().c_str());
  spr.drawString(sb, 8, 20, 1);

  auto* chars = svc->getCharacteristics();
  int cn = chars ? (int)chars->size() : 0;
  int shown = cn < MAX_CHR_SHOW ? cn : MAX_CHR_SHOW;
  int row = 0;
  for (auto& kv : *chars) {
    if (row >= shown) break;
    BLERemoteCharacteristic* c = kv.second;
    char cb[40];
    snprintf(cb, sizeof(cb), "%.22s %s%s%s", c->getUUID().toString().c_str(),
             c->canRead() ? "R" : "", c->canWrite() ? "W" : "", c->canNotify() ? "N" : "");
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(cb, 8, 34 + row * 14, 1);
    row++;
  }
  if (cn == 0) { spr.setTextColor(C_DIM, C_BG); spr.drawString("(no characteristics)", 8, 34, 1); }
  else if (cn > shown) {
    char b[16]; snprintf(b, sizeof(b), "+%d more", cn - shown);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(b, 8, 34 + shown * 14, 1);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S5=next svc  S2=back", 8, SCR_H - 12, 1);
}

void BleScanApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BLOCKED) {
    display_top_bar("BLE SCAN");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote WiFi/BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Turn off remote mode", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("BLE SCAN");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    display_top_bar("BLE SCAN");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Scanning BLE (~5s)...", 8, 50, 2);
    return;
  }
  if (phase_ == CONNECTING) {
    display_top_bar("BLE SCAN");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Connecting...", 8, 50, 2);
    return;
  }
  if (phase_ == CONNECT_FAIL) {
    display_top_bar("BLE SCAN");
    spr.setTextColor(C_BAD, C_BG); spr.drawString("Failed to connect", 8, 40, 2);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=back", 8, SCR_H - 12, 2);
    return;
  }
  if (phase_ == LIST) drawList();
  else if (phase_ == GATT) drawGatt();
}

void BleScanApp::select_current() {
  if (cursor_ >= count_) { wants_exit_ = true; return; }  // "< Back"
  connect_selected();
}

void BleScanApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) { cursor_ = idx; select_current(); }
}

void BleScanApp::button(ButtonId id) {
  if (phase_ == SCANNING || phase_ == CONNECTING) return;
  if (phase_ == BLOCKED || phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (phase_ == CONNECT_FAIL) { if (id == BTN_S5) phase_ = LIST; return; }
  if (phase_ == GATT) {
    if (id == BTN_S2) disconnect_gatt();
    else if (id == BTN_S5) svc_cursor_ = svc_count_ ? (svc_cursor_ + 1) % svc_count_ : 0;
    return;
  }
  // LIST
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

std::string BleScanApp::remote_state() {
  char lines[MAX_DEV + 1][32];
  const char* items[MAX_DEV + 1];
  int n = 0;
  int cnt = count_ < MAX_DEV ? count_ : MAX_DEV;
  for (int i = 0; i < cnt; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%.18s %ddBm", names_[i], rssi_[i]);
    items[n] = lines[n]; n++;
  }
  if (n == 0) { snprintf(lines[0], sizeof(lines[0]), "0 devices"); items[0] = lines[0]; n = 1; }
  return protocol_build_menu("ble_scan", items, n, cursor_ < n ? cursor_ : -1);
}
