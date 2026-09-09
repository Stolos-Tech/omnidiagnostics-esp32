// TLS Cert — витягує сертифікат сервера з TLS-рукостискання на :443 і показує
// subject (кому видано), issuer (хто видав) і термін дії. Не перевіряє довіру —
// лише інвентаризація (SNI надсилається). Host з телефона (text field "host",
// типово example.com). Потребує STA ("WiFi Setup").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class TlsCertApp : public App {
public:
  const char* name() const override { return "TLS Cert"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, FETCHING, DONE };
  Phase phase_ = DONE;
  bool wants_exit_ = false;
  bool started_ = false;
  char host_[64] = "example.com";
  bool ok_ = false;
  char subject_[72] = "";
  char issuer_[72] = "";
  char expires_[40] = "";
  char err_[56] = "";

  bool connected() const;
  void fetch();
};
