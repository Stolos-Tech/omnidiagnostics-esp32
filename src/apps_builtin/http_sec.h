// HTTP Sec — інспектор заголовків безпеки HTTP(S)-відповіді. Робить GET і
// перевіряє наявність HSTS / CSP / X-Frame-Options / X-Content-Type-Options /
// Referrer-Policy / Permissions-Policy, показує оцінку й сервер. URL з телефона
// (text field "url", типово https://example.com). Потребує STA ("WiFi Setup").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class HttpSecApp : public App {
public:
  static const int NH = 6;   // скільки security-заголовків оцінюємо

  const char* name() const override { return "HTTP Sec"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, CHECKING, DONE };
  Phase phase_ = DONE;
  bool wants_exit_ = false;
  char url_[96] = "http://example.com";   // http за замовч. — https потребує heap для TLS
  int  code_ = 0;
  bool present_[NH] = { false };
  char server_[40] = "";
  bool have_result_ = false;

  bool connected() const;
  void run_check();
};
