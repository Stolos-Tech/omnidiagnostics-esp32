// DNS Lookup — резолвінг імені хоста в IP. Імʼя вводиться з телефона
// (text field="host"). Потребує "WiFi Setup". S5 — повторити, S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class DnsLookupApp : public App {
public:
  const char* name() const override { return "DNS Lookup"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  char host_[64] = "";
  int  phase_ = 0;         // 0=idle, 1=показати "Resolving" кадр, 2=виконати
  bool done_ = false;
  bool ok_ = false;
  uint32_t ip_ = 0;

  bool connected() const;
  void resolve();
};
