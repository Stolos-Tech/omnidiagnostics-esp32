// Card Skimmer — контрсурвейланс: Classic-BT інвентаризація + евристика скімерів за
// іменем модуля (HC-05/06, linvor, JDY... — kernel/skimmer_id). Показує лише
// підозрілі пристрої (RSSI = близькість). Група Detect.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class CardSkimmerApp : public App {
public:
  const char* name() const override { return "Card Skimmer"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  void on_exit() override;

private:
  enum Phase { BLOCKED, LOW_POWER, SCANNING, LIST };
  static const int MAX_S = 12;
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int  scan_frame_ = 0;

  char name_[MAX_S][24];
  char addr_[MAX_S][18];
  int  rssi_[MAX_S];
  int  count_ = 0;
  int  cursor_ = 0;

  int  items_total() const { return count_ + 1; }   // +"< Back"
  void start_scan();
  void do_scan();
  void select_current();
  void drawList();
};
