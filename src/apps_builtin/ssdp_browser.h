// SSDP Browser — виявлення UPnP/SSDP-пристроїв у LAN (роутери, медіа, IoT,
// смарт-ТВ, принтери), яких часто НЕ видно в mDNS. Активний M-SEARCH на
// 239.255.255.250:1900, розбір відповідей (LOCATION/SERVER/ST). Доповнює
// mDNS-браузер і Net Scan. Потребує STA-підключення ("WiFi Setup").
//
// Керування (2 рідні кнопки; довге утримання лівої = назад у лаунчер):
//   S2 — наступний пристрій, S5 — деталі; у деталях S5 відкриває LOCATION у HTTP GET.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class SsdpBrowserApp : public App {
public:
  static const int MAX_DEV = 24;

  const char* name() const override { return "SSDP"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  void on_exit() override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, SEARCHING, LIST, DETAIL };

  struct Dev {
    uint32_t ip;
    uint16_t port;
    char server[28];    // заголовок SERVER (ОС/стек)
    char st[26];        // ST/NT — тип пристрою/сервісу
    char location[72];  // LOCATION — URL опису пристрою
  };

  Phase phase_ = SEARCHING;
  bool wants_exit_ = false;
  uint32_t t_start_ = 0;
  int  resends_ = 0;
  uint32_t t_resend_ = 0;
  Dev  dev_[MAX_DEV];
  int  dev_count_ = 0;
  int  cursor_ = 0;

  bool connected() const;
  int  items_total() const { return dev_count_ + 1; }  // +"< Back"
  void send_msearch();
  void poll_udp();
};
