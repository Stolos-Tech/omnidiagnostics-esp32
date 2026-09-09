// ARP Scan — активне виявлення хостів у LAN на рівні L2. Розсилає ARP-запити
// по всій підмережі й збирає відповіді з ARP-кешу lwIP. Знаходить пристрої, які
// НЕ відповідають на TCP/ICMP (фаєрвол), тож бачить більше, ніж TCP-свіп Net Scan.
// Показує IP + MAC + вендора (OUI). Потребує STA-підключення ("WiFi Setup").
//
// Кеш ARP у lwIP малий і витісняє записи, тож харвест іде щотіку в ВЛАСНУ таблицю,
// а запити розсилаються невеликими пачками (щоб не спорожнити кеш до харвесту).
//
// Керування (2 рідні кнопки; довге утримання лівої = назад):
//   S2 — наступний хост, S5 — деталі (MAC/вендор/тип); у деталях S2 — назад.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class ArpScanApp : public App {
public:
  static const int MAX_HOSTS = 48;

  const char* name() const override { return "ARP Scan"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, SWEEPING, LIST, DETAIL };
  struct Host { uint32_t ip; uint8_t mac[6]; };

  Phase phase_ = SWEEPING;
  bool wants_exit_ = false;
  uint32_t self_ip_ = 0, gw_ip_ = 0;
  uint32_t cur_ = 0, first_ = 0, last_ = 0;
  uint32_t t_settle_ = 0;
  Host host_[MAX_HOSTS];
  int  host_count_ = 0;
  int  cursor_ = 0;

  bool connected() const;
  int  items_total() const { return host_count_ + 1; }  // +"< Back"
  void harvest();
  void add_host(uint32_t ip, const uint8_t mac[6]);
};
