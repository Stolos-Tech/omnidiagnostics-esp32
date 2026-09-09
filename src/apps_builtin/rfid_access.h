// RFID Access — розблокування/білий список: тап карти -> GRANTED (у списку) або
// DENIED. S5 вносить поточну картку в NVS-список, S1 чистить список, S2 вихід.
// Основа для «розблокувати девайс карткою» (їхня ідея №1).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class RfidAccessApp : public App {
public:
  const char* name() const override { return "RFID Access"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  bool     wants_exit_ = false;
  uint32_t last_seq_ = 0;      // щоб помітити новий тап
  uint32_t cur_uid_ = 0;
  bool     has_tap_ = false;
  bool     granted_ = false;
  bool     just_enrolled_ = false;
};
