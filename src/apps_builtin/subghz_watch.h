// Tier 3: контрсурвейланс суб-ГГц. Періодично свіпить три вікна CC1101 і позначає
// СТІЙКІ передавачі (сигнал вище порогу N проходів поспіль) — трекери/жучки/телеметрія
// на 433/868, поза 2.4ГГц. Розширює групу "Detect". Лише RX, пасивно.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class SubghzWatchApp : public App {
public:
  const char* name() const override { return "Sub-GHz Watch"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог стійких Tx на SD при виході

private:
  static const int MAXPTS = 160;
  static const int THRESH_DBM = -80;   // поріг «активно»
  static const int PERSIST = 3;        // проходів поспіль -> стійкий передавач
  int     n_ = 0;
  uint8_t streak_[MAXPTS];             // к-ть активних проходів поспіль на точку
  int8_t  dbm_[MAXPTS];                // остання RSSI
  int     passes_ = 0;
  bool    present_ = false;
  bool    wants_exit_ = false;
  void reset();
  int  collect(int* idx, int max) const;   // індекси стійких точок за спаданням dBm
};
