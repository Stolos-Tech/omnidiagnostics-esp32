// HTTP POST — надсилання даних до веб-API пристрою (керування). URL і тіло
// вводяться з телефона (text field="url" і field="body"). Надсилання тіла
// запускає запит. Content-Type: application/json. S5 — повторити, S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"

class HttpPostApp : public App {
public:
  const char* name() const override { return "HTTP POST"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  char target_[128] = "";
  char body_[128] = "";
  int  phase_ = 0;      // 0=idle, 1=показати "Sending" кадр, 2=виконати
  bool done_ = false;
  int  code_ = 0;
  char resp_[160] = "";

  bool connected() const;
  void do_post();
};
