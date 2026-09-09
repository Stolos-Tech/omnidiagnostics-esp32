// Net Info — діагностика мережевого підключення (STA). Показує SSID/IP/шлюз/
// маску/DNS/RSSI і на вимогу пінгує шлюз та інтернет (8.8.8.8) для перевірки
// звʼязності. Потребує підключення через "WiFi Setup". S5 — пінг, S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"

class NetInfoApp : public App {
public:
  const char* name() const override { return "Net Info"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  int  ping_phase_ = 0;      // 0=idle, 1=показати "Pinging" кадр, 2=виконати
  bool ping_done_ = false;
  bool gw_ok_ = false, inet_ok_ = false;
  int  gw_ms_ = 0, inet_ms_ = 0;

  bool connected() const;
  void do_pings();
  int  build_lines(char lines[][40]) const;
};
