// mDNS Browser — виявлення сервісів у LAN за іменами (Bonjour/zeroconf).
// Опитує набір типів (_http._tcp, _https._tcp, _googlecast._tcp, _printer._tcp,
// _ssh._tcp, _workstation._tcp), показує знайдені сервіси (імʼя :порт @ IP).
// Зручніше за сирі IP: одразу видно, ЩО за пристрій. Потребує "WiFi Setup".
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class MdnsBrowserApp : public App {
public:
  static const int MAX_SVC = 24;

  const char* name() const override { return "mDNS"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  void on_exit() override { finish(); }   // MDNS.end при виході (в т.ч. hold-назад)
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, QUERYING, LIST, DETAIL };

  struct Svc {
    char name[28];
    uint32_t ip;
    uint16_t port;
    uint8_t  type_idx;
  };

  Phase phase_ = QUERYING;
  bool wants_exit_ = false;
  bool mdns_up_ = false;
  int  query_i_ = 0;
  Svc  svc_[MAX_SVC];
  int  svc_count_ = 0;
  int  cursor_ = 0;

  bool connected() const;
  int  items_total() const { return svc_count_ + 1; }  // +"< Back"
  void finish();  // MDNS.end() перед виходом
};
