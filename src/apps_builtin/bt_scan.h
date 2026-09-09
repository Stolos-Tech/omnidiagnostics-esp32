// BT Scan — пошук навколишніх Classic Bluetooth пристроїв (імʼя/адреса/RSSI)
// через BluetoothSerial::discover (той самий Bluedroid-стек, що й Remote: BT).
// WiFi і BT ділять радіо — потребує вимкненого будь-якого remote-режиму.
//
// Керування: S5 — пересканувати, S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class BtScanApp : public App {
public:
  static const int MAX_DEV = 16;

  const char* name() const override { return "BT Scan"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог BT-пристроїв на SD при виході

private:
  enum Phase { BLOCKED, LOW_POWER, SCANNING, LIST };
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int scan_frame_ = 0;

  char names_[MAX_DEV][32];
  char addrs_[MAX_DEV][18];
  int  rssi_[MAX_DEV];
  int  count_ = 0;

  void start_scan();
  void do_scan();
};
