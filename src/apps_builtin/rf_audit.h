// RF Audit — red-team RFID: показує аудит карти з RC522 (тип, UID, скільки секторів
// на дефолтних ключах, вердикт вразливості) і зберігає повний дамп у /reports
// (звідти бот синкає в Telegram). Аудит рахує UNO, ESP лише відображає/зберігає.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class RfAuditApp : public App {
public:
  const char* name() const override { return "RF Audit"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  bool wants_exit_ = false;
  uint32_t last_seq_ = 0;   // щоб помітити новий аудит
  bool saved_ = false;
};
