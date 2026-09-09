// Traceroute — трасування маршруту до цілі через ICMP з інкрементом TTL.
// Raw ICMP-сокет: на кожен TTL шле echo, ловить Time-Exceeded від проміжного
// вузла (його IP = хоп) або Echo-Reply від цілі (маршрут пройдено). Ціль з
// телефона (text field "target", типово 8.8.8.8). Потребує STA ("WiFi Setup").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class TracerouteApp : public App {
public:
  static const int MAX_HOPS = 20;

  const char* name() const override { return "Traceroute"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  void on_exit() override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, SOCK_FAIL, RESOLVE_FAIL, RUNNING, DONE };
  struct Hop { uint32_t ip; int rtt; bool got; };  // ip a.b.c.d MSB; got=false -> '*'

  Phase phase_ = RUNNING;
  bool wants_exit_ = false;
  char target_[48] = "8.8.8.8";
  uint32_t target_lwip_ = 0;   // мережевий порядок (для sockaddr)
  int  sock_ = -1;
  int  ttl_ = 1;
  Hop  hops_[MAX_HOPS];
  int  hop_n_ = 0;

  bool connected() const;
  void start();
  void step();
};
