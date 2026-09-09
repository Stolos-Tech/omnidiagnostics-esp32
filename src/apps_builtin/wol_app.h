// Wake on LAN — надсилає magic packet для пробудження пристрою в локальній
// мережі за MAC-адресою (з телефона, text field="mac"). Потребує "WiFi Setup".
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class WolApp : public App {
public:
  const char* name() const override { return "Wake on LAN"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  char mac_str_[24] = "";
  int  preset_ = -1;        // обрана ціль-пресет (-1 = ручний ввід з телефона)
  bool sent_ = false;
  bool last_ok_ = false;

  bool connected() const;
  void send_wol();
};
