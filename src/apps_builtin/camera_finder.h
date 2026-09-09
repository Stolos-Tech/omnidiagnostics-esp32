// Camera Finder — контрсурвейланс: WiFi-скан + матч OUI виробників камер
// (Hikvision/Dahua/Reolink/Foscam/Wyze/Amcrest/Axis/Vivotek — kernel/oui_vendor).
// Показує ймовірні WiFi-камери (вендор/SSID/RSSI/канал) + прихований-сильний-AP як
// «suspect». Аналогові 2.4GHz-камери -> ловить окремо «2.4G Analyzer». Група Detect.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class CameraFinderApp : public App {
public:
  const char* name() const override { return "Camera Finder"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог знайдених камер на SD

private:
  enum Phase { BT_BLOCKED, LOW_POWER, SCANNING, LIST };
  static const int MAX_H = 16;
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int  scan_frame_ = 0;

  char label_[MAX_H][24];   // вендор камери або "hidden AP?"
  char ssid_[MAX_H][20];
  int  rssi_[MAX_H];
  int  chan_[MAX_H];
  bool suspect_[MAX_H];     // true = прихований (не точний вендор)
  int  count_ = 0;
  int  cursor_ = 0;

  int  items_total() const { return count_ + 1; }   // +"< Back"
  void start_scan();
  void do_scan();
  void select_current();
  void drawList();
};
