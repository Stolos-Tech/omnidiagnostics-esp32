// UNO Board — застосунок хаба UNO R3 (I/O-копроцесор red-team форку): модульний
// реєстр, живі датчики (pot/reed/IR/DHT), RFID, IR capture/replay.
// Актуатори (серво/реле/степер) прибрано разом з моторами. Одна апка з кількома
// сторінками (як BatteryDiag) замість кількох окремих — менший flash-відбиток.
// (Заглушку RADIO pot->freq видалено: її роль виконує реальний nRF24 «2.4G Analyzer».)
//
// Керування (рідні кнопки / свічі):
//   BTN_S2 — наступна сторінка, BTN_S1 — попередня
//   BTN_S5 — дія на поточній сторінці (напр. SCAN на сторінці MODULES)
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class UnoBoardApp : public App {
public:
  const char* name() const override { return "UNO Board"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  enum Page { PAGE_MODULES, PAGE_SENSORS, PAGE_RFID, PAGE_IR, PAGE_EXIT, PAGE_COUNT };

  int page_ = PAGE_MODULES;
  bool wants_exit_ = false;
  uint32_t last_rfid_seq_ = 0;
  int rfid_count_ = 0;
  uint32_t last_ir_ = 0;   // останній захоплений IR-код (латч для показу/replay)

  void pageDots();
  void drawModules();
  void drawSensors();
  void drawRfid();
  void drawIr();
  void drawExit();
};
