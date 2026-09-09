// Channel Monitor — пасивний огляд завантаженості WiFi-каналів (1-13):
// promiscuous-режим (лише прийом, БЕЗ жодної інʼєкції/деаутентифікації),
// перемикання каналів кожні 200мс, лічильник кадрів на канал.
//
// ВАЖЛИВО: поки монітор працює, STA-з'єднання (якщо було) розривається —
// канали перемикаються під капотом. При виході ESP32 core сам спробує
// перепідключитись (auto-reconnect), якщо мережа була збережена.
//
// Керування: S5 — вихід (з підтвердженням), S2 — скасувати вихід.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class ChannelMonitorApp : public App {
public:
  const char* name() const override { return "Channel Monitor"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void on_exit() override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог завантаженості каналів на SD

private:
  enum Phase { BT_BLOCKED, LOW_POWER, RUNNING, PAGE_EXIT };
  Phase phase_ = RUNNING;
  bool wants_exit_ = false;
  bool was_wifi_ = false;      // Remote:WiFi був активний до захоплення радіо -> відновити на виході
  uint32_t t_hop_ = 0;
  uint32_t t_start_ = 0;       // для авто-таймауту (щоб не рубити мережу нескінченно)
  int cur_channel_ = 1;

  void drawRunning();
  void drawExit();
};
