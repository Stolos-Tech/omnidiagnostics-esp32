// Attacker Detect — контрсурвейланс: WiFi-скан на ознаки атакерів поблизу:
//   * Pwnagotchi — SSID як JSON-beacon (kernel/attacker_id);
//   * Evil-twin / rogue AP — той самий SSID з РІЗНИМ BSSID (клон мережі).
// Deauth-флуд ловить окремо Deauth Alert (група Air). Група Detect.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class AttackerDetectApp : public App {
public:
  const char* name() const override { return "Attacker Detect"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  enum Phase { BT_BLOCKED, LOW_POWER, SCANNING, LIST };
  static const int MAX_H = 16;
  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int  scan_frame_ = 0;

  char label_[MAX_H][16];   // "Pwnagotchi" / "Evil twin?"
  char ssid_[MAX_H][20];
  int  rssi_[MAX_H];
  int  chan_[MAX_H];
  int  count_ = 0;
  int  cursor_ = 0;

  int  items_total() const { return count_ + 1; }   // +"< Back"
  void start_scan();
  void do_scan();
  void select_current();
  void drawList();
};
