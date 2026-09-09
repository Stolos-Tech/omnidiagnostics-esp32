#include <Arduino.h>
#include <stdio.h>
#include <BluetoothSerial.h>
#include "bt_scan.h"
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

// Окремий інстанс лише для інвентаризації (не SPP-сервер, як s_bt у transport_bt).
static BluetoothSerial s_bt;

void BtScanApp::start_scan() {
  // ВАЖЛИВО (апаратне обмеження): WiFi і BT ділять ОДНЕ радіо. Спроба підняти BT
  // поверх активного Remote:WiFi (або відновити WiFi зразу після BT) на ESP32-WROOM
  // фрагільна -> coexistence-зависання/WDT-ребут (перевірено). Тому BT-скан вимагає
  // ПОВНІСТЮ вимкненого remote: з вебу він блокується з поясненням, реально
  // запускається на самій платі (Remote off). Це свідома межа, а не баг.
  if (remote_active_mode() != MODE_OFF) { phase_ = BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = SCANNING;
  scan_frame_ = 0;
}

void BtScanApp::init() {
  wants_exit_ = false;
  count_ = 0;
  start_scan();
}

void BtScanApp::do_scan() {
  wifi_sta_stop();             // чисте радіо перед BT (start_scan уже гарантує remote OFF)
  s_bt.begin("OmniDiag-Scan");
  BTScanResults* res = s_bt.discover(SCAN_MS);   // блокуюче ~8с
  count_ = 0;
  if (res) {
    int n = res->getCount();
    for (int i = 0; i < n && count_ < MAX_DEV; i++) {
      BTAdvertisedDevice* d = res->getDevice(i);
      if (!d) continue;
      std::string nm = d->getName();
      snprintf(names_[count_], sizeof(names_[0]), "%s", nm.empty() ? "(bez imeni)" : nm.c_str());
      snprintf(addrs_[count_], sizeof(addrs_[0]), "%s", d->getAddress().toString().c_str());
      rssi_[count_] = d->haveRSSI() ? d->getRSSI() : 0;
      count_++;
    }
  }
  s_bt.end();
  phase_ = LIST;
}

void BtScanApp::loop() {
  if (phase_ == SCANNING) {
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }  // один кадр "Skanuvannia"
    do_scan();
  }
}

void BtScanApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("BT SCAN (Classic)", 6, 3, 2);

  if (phase_ == BLOCKED) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote WiFi/BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Turn off remote mode", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == LOW_POWER) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SCANNING) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Scanning devices (~8s)...", 8, 50, 2);
    return;
  }
  // LIST
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  char h[16]; snprintf(h, sizeof(h), "%d found", count_);
  spr.drawString(h, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);
  if (count_ == 0) {
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("No device", 8, 40, 2);
  } else {
    int shown = count_ < 5 ? count_ : 5;
    for (int i = 0; i < shown; i++) {
      int y = 20 + i * 18;
      spr.setTextColor(C_TEXT, C_BG);
      char b[24]; snprintf(b, sizeof(b), "%.16s", names_[i]);
      spr.drawString(b, 8, y, 2);
      spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_BG);
      char r[16]; snprintf(r, sizeof(r), "%ddBm", rssi_[i]);
      spr.drawString(r, SCR_W - 6, y, 1);
      spr.setTextDatum(TL_DATUM);
    }
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S5=rescan  S2=exit", 8, SCR_H - 12, 1);
}

void BtScanApp::button(ButtonId id) {
  if (phase_ == SCANNING) return;
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) start_scan();
}

std::string BtScanApp::remote_state() {
  char lines[MAX_DEV][40];
  const char* items[MAX_DEV];
  int n = 0;
  // Стани, що НЕ список — даємо явний фідбек (інакше веб показував оманливе "0 devices").
  if (phase_ == BLOCKED) {
    // Чесно пояснюємо межу заліза: WiFi(web) і BT ділять радіо -> одночасно не можна.
    snprintf(lines[0], sizeof(lines[0]), "WiFi & BT share one radio."); items[0]=lines[0];
    snprintf(lines[1], sizeof(lines[1]), "Run BT Scan on the board"); items[1]=lines[1];
    snprintf(lines[2], sizeof(lines[2]), "with remote turned off."); items[2]=lines[2];
    return protocol_build_menu("bt_scan", items, 3, -1);
  }
  if (phase_ == LOW_POWER){ snprintf(lines[0], sizeof(lines[0]), "Voltage too low for radio"); items[0]=lines[0];
                           return protocol_build_menu("bt_scan", items, 1, -1); }
  if (phase_ == SCANNING) { snprintf(lines[0], sizeof(lines[0]), "Scanning ~8s... link drops, auto-reconnects"); items[0]=lines[0];
                           return protocol_build_menu("bt_scan", items, 1, -1); }
  int shown = count_ < MAX_DEV ? count_ : MAX_DEV;
  for (int i = 0; i < shown; i++) {
    snprintf(lines[n], sizeof(lines[n]), "%.20s %s %ddBm", names_[i], addrs_[i], rssi_[i]);
    items[n] = lines[n]; n++;
  }
  if (n == 0) { snprintf(lines[0], sizeof(lines[0]), "No devices found"); items[0] = lines[0]; n = 1; }
  return protocol_build_menu("bt_scan", items, n, -1);
}
