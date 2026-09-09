// Help — вбудований глосарій + короткі підказки по інструментах, прямо на платі
// (той самий матеріал, що у веб index.html і в мобільному додатку). Прокрутка:
// S2 — вниз, S1 — вгору, S5 — вихід. Дзеркалиться як список видимого вікна.
#pragma once
#include "../kernel/app_interface.h"

class HelpApp : public App {
public:
  const char* name() const override { return "Help"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  static const int VIS = 6;          // видимих рядків у вікні прокрутки
  int  top_ = 0;                     // індекс верхнього видимого запису
  bool wants_exit_ = false;
  int  max_top() const;
};
