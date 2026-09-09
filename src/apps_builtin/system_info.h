// System — діагностична панель пристрою: чип/CPU/флеш і рантайм (heap, uptime,
// причина ресету, температура). Дві сторінки, S2 гортає, S5 вихід. Дзеркалиться
// на клієнта як список рядків поточної сторінки.
#pragma once
#include "../kernel/app_interface.h"

class SystemInfoApp : public App {
public:
  static const int MAX_LINES = 7;
  static const int LINE_LEN = 34;

  const char* name() const override { return "System"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Page { PAGE_CHIP, PAGE_RUNTIME, PAGE_COUNT };
  int  page_ = PAGE_CHIP;
  bool wants_exit_ = false;

  // Заповнює рядки поточної сторінки, повертає їх кількість.
  int build_lines(char lines[][LINE_LEN]) const;
  const char* page_title() const;
};
