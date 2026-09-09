// TCP Terminal — сирий TCP-клієнт для взаємодії з сервісами в мережі.
// Ціль (host:port, типово :23) і рядки для надсилання вводяться з телефона
// (text field="target" і field="line"). Отримані байти показуються на екрані.
// Потребує "WiFi Setup". S5 — переприєднатись/відключити, S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class TcpTerminalApp : public App {
public:
  static const int RX_MAX = 384;

  const char* name() const override { return "TCP Term"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  void on_exit() override { disconnect(); }  // закрити TCP при виході (в т.ч. hold-назад)
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  bool wants_exit_ = false;
  char host_[64] = "";
  uint16_t port_ = 0;
  char rx_[RX_MAX + 1] = "";
  int  rx_len_ = 0;

  bool connected() const;      // WiFi STA
  void connect_target();
  void disconnect();
  void rx_append(const char* data, int len);
};
