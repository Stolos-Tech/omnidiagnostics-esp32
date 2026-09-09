#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "tracker_detector.h"
#include "../kernel/tracker_id.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define SCAN_SECONDS     6
#define FOLLOW_THRESHOLD 3   // бачений >=N сканів поспіль -> ймовірно «їде за тобою»

// Персистентність між сканами: адреса -> к-ть послідовних появ. УВАГА: MAC у AirTag
// (і Find My) РОТУЄТЬСЯ (~15хв) -> евристика приблизна; для Tile/SmartTag стабільніша.
struct SeenEntry { char addr[18]; int count; bool hit; };
static SeenEntry s_seen[24];
static int s_seenN = 0;

static void seen_begin() { for (int i = 0; i < s_seenN; i++) s_seen[i].hit = false; }
static int seen_bump(const char* addr) {
  for (int i = 0; i < s_seenN; i++)
    if (strcmp(s_seen[i].addr, addr) == 0) { s_seen[i].count++; s_seen[i].hit = true; return s_seen[i].count; }
  if (s_seenN < (int)(sizeof(s_seen) / sizeof(s_seen[0]))) {
    snprintf(s_seen[s_seenN].addr, sizeof(s_seen[0].addr), "%s", addr);
    s_seen[s_seenN].count = 1; s_seen[s_seenN].hit = true; s_seenN++;
    return 1;
  }
  return 1;
}
static void seen_end() {   // хто не з'явився цього скану -> послідовність перервана
  for (int i = 0; i < s_seenN; i++) if (!s_seen[i].hit) s_seen[i].count = 0;
}

static bool uuid16(BLEUUID u, uint16_t* out) {
  if (u.bitSize() == 16) { *out = u.getNative()->uuid.uuid16; return true; }
  return false;
}

void TrackerDetectorApp::start_scan() {
  if (remote_active_mode() != MODE_OFF) { phase_ = BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING; scan_frame_ = 0;
}

void TrackerDetectorApp::init() {
  wants_exit_ = false; count_ = 0; cursor_ = 0;
  start_scan();
}

void TrackerDetectorApp::do_scan() {
  wifi_sta_stop();
  static bool s_ble_init = false;
  if (!s_ble_init) { BLEDevice::init(""); s_ble_init = true; }
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);   // ~99% duty cycle -> ловимо максимум adv-пакетів трекерів (AirTag/Tile/SmartTag)
  scan->setWindow(99);      // важливо для детекції «їде за тобою»: менше пропущених сканів
  BLEScanResults res = scan->start(SCAN_SECONDS, false);   // блокуюче ~6с

  seen_begin();
  count_ = 0;
  int n = res.getCount();
  for (int i = 0; i < n && count_ < MAX_T; i++) {
    BLEAdvertisedDevice d = res.getDevice(i);
    int company = -1; const uint8_t* pl = nullptr; int plen = 0;
    std::string md;
    if (d.haveManufacturerData()) {
      md = d.getManufacturerData();
      if (md.size() >= 2) {
        company = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
        pl = (const uint8_t*)md.data() + 2; plen = (int)md.size() - 2;
      }
    }
    uint16_t svc[2]; int nsvc = 0;
    if (d.haveServiceUUID()) { uint16_t v; if (uuid16(d.getServiceUUID(), &v)) svc[nsvc++] = v; }
    if (d.haveServiceData())  { uint16_t v; if (uuid16(d.getServiceDataUUID(), &v)) svc[nsvc++] = v; }

    TrackerType t = tracker_classify(company, pl, plen, svc, nsvc);
    if (t == TRK_NONE) continue;
    snprintf(type_[count_], sizeof(type_[0]), "%s", tracker_name(t));
    snprintf(addr_[count_], sizeof(addr_[0]), "%s", d.getAddress().toString().c_str());
    rssi_[count_] = d.getRSSI();
    seen_[count_] = seen_bump(addr_[count_]);
    count_++;
  }
  seen_end();
  scan->clearResults();
  cursor_ = 0;
  phase_ = LIST;
}

void TrackerDetectorApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }   // один кадр "Scanning"
    do_scan();
  }
}


void TrackerDetectorApp::drawList() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("TRACKER DETECT");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(count_ ? C_BAD : C_GOOD, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d found", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (count_ == 0) {
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString("No trackers nearby", 8, 38, 2);
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
    if (idx >= count_) {   // "< Back"
      spr.setTextColor(sel ? C_ACCENT : C_DIM, sel ? C_PANEL : C_BG);
      spr.drawString("< Back", 10, y + 5, 2);
      continue;
    }
    bool follow = seen_[idx] >= FOLLOW_THRESHOLD;
    spr.setTextColor(follow ? C_BAD : C_WARN, sel ? C_PANEL : C_BG);
    spr.drawString(type_[idx], 10, y + 1, 2);
    spr.setTextColor(C_DIM, sel ? C_PANEL : C_BG);
    char b[34];
    snprintf(b, sizeof(b), "%.17s %ddBm x%d%s", addr_[idx], rssi_[idx], seen_[idx], follow ? " FOLLOW?" : "");
    spr.drawString(b, 10, y + 13, 1);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=rescan/back", 8, SCR_H - 12, 1);
}

void TrackerDetectorApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BLOCKED) {
    display_top_bar("TRACKER DETECT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote WiFi/BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Turn off remote mode", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("TRACKER DETECT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    display_top_bar("TRACKER DETECT");
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Scanning BLE (~6s)...", 8, 50, 2);
    return;
  }
  drawList();
}

void TrackerDetectorApp::select_current() {
  if (cursor_ >= count_) { wants_exit_ = true; return; }  // "< Back"
  start_scan();                                           // на трекері -> пере-скан
}

void TrackerDetectorApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) { cursor_ = idx; select_current(); }
}

void TrackerDetectorApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (phase_ == BLOCKED || phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  // LIST
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

std::string TrackerDetectorApp::remote_state() {
  char lines[MAX_T + 1][32];
  const char* items[MAX_T + 1];
  int n = 0;
  if (count_ == 0) { snprintf(lines[0], sizeof(lines[0]), "No trackers nearby"); items[0] = lines[0]; n = 1; }
  else for (int i = 0; i < count_; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%s %ddBm x%d%s", type_[i], rssi_[i], seen_[i],
             seen_[i] >= FOLLOW_THRESHOLD ? " FOLLOW?" : "");
    items[n] = lines[n]; n++;
  }
  return protocol_build_menu("tracker_detect", items, n, cursor_ < n ? cursor_ : -1);
}

// #4: трекер-детектор глушив WiFi -> повертаємо мережу на виході.
void TrackerDetectorApp::on_exit() {
  wifi_sta_resume();
}
