// Tracker Detect — контрсурвейланс: сканує BLE й класифікує рекламу за сигнатурами
// відомих трекерів (AirTag/Find My, Tile, Samsung SmartTag). Показує знайдені з RSSI
// (близькість) і лічильником «бачений у N сканах поспіль» -> евристика «їде за тобою»
// (FOLLOW?). Живе у групі Bluetooth. Класифікація — kernel/tracker_id (тестована).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class TrackerDetectorApp : public App {
public:
  const char* name() const override { return "Tracker Detect"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  void on_exit() override;
  bool auto_snapshot() const override { return true; }   // авто-лог виявлених трекерів на SD

private:
  enum Phase { BLOCKED, LOW_POWER, SCANNING, LIST };
  static const int MAX_T = 16;
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int  scan_frame_ = 0;

  char type_[MAX_T][18];
  char addr_[MAX_T][18];
  int  rssi_[MAX_T];
  int  seen_[MAX_T];
  int  count_ = 0;
  int  cursor_ = 0;

  int  items_total() const { return count_ + 1; }   // +"< Back"
  void start_scan();
  void do_scan();
  void select_current();
  void drawList();
};
