// 2.4GHz-аналізатор ефіру на nRF24L01+ (PA/LNA): сканує 126 каналів через RPD,
// будує гістограму зайнятості з auto-scale і позначкою піку/WiFi-каналу. Живе в
// групі "Air". Драйвер — drivers/nrf24, чиста логіка/мітки — kernel/rf24_util.
// Це «оживлення» ролі радіо-аналізатора під реальний RF-модуль (замість pot-заглушки).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class Rf24AnalyzerApp : public App {
public:
  const char* name() const override { return "2.4G Analyzer"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог 2.4G спектру на SD при виході

private:
  static const int CH = 126;
  uint8_t counts_[CH] = {0};
  int  passes_ = 0;
  int  maxv_ = 0;
  bool present_ = false;
  bool wants_exit_ = false;
  void reset_scan();
};
