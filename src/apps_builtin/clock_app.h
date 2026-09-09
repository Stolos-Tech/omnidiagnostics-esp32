// Clock — годинник (час/дата через NTP), секундомір і таймер зворотного відліку.
// S5 — старт/стоп секундоміра, S2 — скидання. Таймер задається з телефона
// (text field="timer", значення — хвилини). Потребує "WiFi Setup" для NTP.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class ClockApp : public App {
public:
  const char* name() const override { return "Clock"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  bool ntp_started_ = false;
  // секундомір
  bool sw_run_ = false;
  uint32_t sw_start_ = 0, sw_accum_ = 0;
  // таймер (зворотний відлік)
  uint32_t cd_end_ = 0;      // millis дедлайну, 0 = нема

  bool connected() const;
  uint32_t sw_elapsed_ms() const;
};
