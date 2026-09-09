// HTTP GET — запит до веб-API пристрою в LAN (базове керування/ідентифікація).
// URL вводиться з телефона (текстова подія field="url") — отримання URL одразу
// запускає запит; S5 повторює запит для поточного URL, S2 — вихід.
// Потребує STA-підключення ("WiFi Setup"). Тільки http (https потребує TLS-клієнта).
#pragma once
#include "../kernel/app_interface.h"

class HttpGetApp : public App {
public:
  const char* name() const override { return "HTTP GET"; }
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
  int  fetch_phase_ = 0;   // 0=idle, 1=показати "Fetching" кадр, 2=виконати
  bool done_ = false;
  int  code_ = 0;
  char body_[192] = "";

  bool connected() const;
  void do_fetch();
};
