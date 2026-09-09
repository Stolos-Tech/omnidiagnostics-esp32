// Tier 1: суб-ГГц аналізатор спектра на CC1101. Свіпить три перестроювані вікна
// (300-348 / 387-464 / 779-928 МГц), міряє RSSI на кожній точці, будує гістограму
// зайнятості з позначкою піку. Пасивний (лише RX). Живе в групі "Sub-GHz".
// Драйвер — drivers/cc1101, чиста логіка/план свіпу — kernel/subghz_util.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class SubghzAnalyzerApp : public App {
public:
  const char* name() const override { return "Sub-GHz Analyzer"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог спектру на SD при виході

private:
  static const int MAXPTS = 160;   // >= subghz_scan_count() (139)
  int     n_ = 0;
  int8_t  rssi_[MAXPTS];           // згладжений dBm на точку (floor -120)
  int     passes_ = 0;
  bool    present_ = false;
  bool    wants_exit_ = false;
  void reset_scan();
};
