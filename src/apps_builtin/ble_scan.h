// BLE Scan — пошук навколишніх BLE-пристроїв (імʼя/адреса/RSSI) + огляд GATT
// (сервіси/характеристики) обраного пристрою. WiFi і BT ділять радіо —
// потребує вимкненого будь-якого remote-режиму.
//
// Керування: у списку — S2=наступний, S5=вибрати ("< Back"=вихід);
// у GATT — S5=наступний сервіс, S2=назад до списку (роз'єднання).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class BleScanApp : public App {
public:
  static const int MAX_DEV = 16;
  static const int MAX_CHR_SHOW = 4;

  const char* name() const override { return "BLE Scan"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  void on_exit() override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог BLE-пристроїв на SD при виході

private:
  enum Phase { BLOCKED, LOW_POWER, SCANNING, LIST, CONNECTING, CONNECT_FAIL, GATT };
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int scan_frame_ = 0;

  char names_[MAX_DEV][24];
  char addrs_[MAX_DEV][18];
  int  rssi_[MAX_DEV];
  int  count_ = 0;
  int  cursor_ = 0;

  int  svc_count_ = 0;
  int  svc_cursor_ = 0;
  bool connected_ = false;

  void start_scan();
  void do_scan();
  void connect_selected();
  void disconnect_gatt();
  int  items_total() const { return count_ + 1; }  // +"< Back"
  void select_current();

  void drawList();
  void drawGatt();
};
