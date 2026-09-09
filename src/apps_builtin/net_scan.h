// Net Scan — виявлення пристроїв у локальній підмережі (STA). Інкрементальний
// TCP-свіп (WiFiClient, короткий таймаут): живий хост приймає зʼєднання або
// миттєво відкидає (RST), мертвий — таймаутить. Список живих хостів -> вибір ->
// деталі (RTT + скан типових TCP-портів). Потребує "WiFi Setup".
//
// Керування: S2 — далі по списку, S5 — вибір/дія, S5 на "< Back" — вихід.
// З телефона можна задати діапазон (text field="range", напр. "192.168.0.0/24")
// і список портів для деталей (text field="ports", напр. "80,443,22,8080").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>
#include <stddef.h>

// Глибокий аналіз ОДНОГО пристрою для remote-API («функціонал плати»): скан типових
// TCP-портів + мітки сервісів + категоризація + детект рандомного MAC. Пише компактний
// JSON у out ({ip,mac,random_mac,alive,ports[],port_labels{},category}). ip_str = "a.b.c.d".
// false -> offline/невалідний IP. Автономно: не потребує сервера.
bool net_scan_device_json(const char* ip_str, char* out, size_t cap);

class NetScanApp : public App {
public:
  static const int MAX_HOSTS = 32;
  static const int MAX_SWEEP = 254;  // ліміт хостів у свіпі (зазвичай /24)
  static const int MAX_PORTS = 16;   // ліміт портів (стандартних або заданих)

  const char* name() const override { return "Net Scan"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  void select_index(int idx) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, SWEEP, LIST, PORTSCAN, DETAIL };

  Phase phase_ = SWEEP;
  bool wants_exit_ = false;

  uint32_t first_host_ = 0;   // перший хост підмережі
  uint32_t sweep_total_ = 0;  // скільки адрес свіпимо
  uint32_t sweep_i_ = 0;      // поточний зсув свіпу

  uint32_t hosts_[MAX_HOSTS]; // знайдені живі хости
  int host_count_ = 0;
  int cursor_ = 0;            // позиція в списку (0..host_count_, останній = "< Back")

  // деталі вибраного хоста
  uint32_t detail_ip_ = 0;
  int detail_rtt_ = -1;              // мс, -1 = нема відповіді
  uint16_t open_ports_[MAX_PORTS];   // знайдені відкриті порти
  char     banner_[MAX_PORTS][48];   // банер сервісу для кожного відкритого порту
  int open_count_ = 0;
  char detail_mac_[18] = "";         // MAC з ARP ("" якщо не в кеші)
  char detail_vendor_[20] = "";      // вендор за OUI ("" якщо невідомий)
  char detail_type_[20] = "";        // здогад типу пристрою

  // заданий користувачем діапазон / порти (з телефона)
  bool custom_range_ = false;
  uint32_t custom_base_ = 0, custom_count_ = 0;
  uint16_t custom_ports_[MAX_PORTS];
  int custom_port_count_ = 0;        // 0 => стандартний список

  bool connected() const;
  void start_sweep();
  int  items_total() const { return host_count_ + 1; }  // +"< Back"
  void run_portscan();
  void select_current();   // дія над поточним cursor_ у LIST
};
