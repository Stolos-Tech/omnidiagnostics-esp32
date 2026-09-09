// Міст нативних функцій, доступних зі скриптів Berry.
// Мінімальний набір Фази 2: display_text, display_clear, button_pressed, adc_read_battery.
//
// Апаратна частина винесена в хуки (NativeApiHooks), щоб native_api.cpp був чистим
// (лише berry.h) і компілювався в хост-тестах, де хуки підмінюються моками.
// Реальні реалізації — native_api_hw.cpp (тільки для пристрою).
#pragma once
#include <stdint.h>

struct NativeApiHooks {
  void     (*display_clear)();
  void     (*display_text)(int x, int y, const char* s, uint16_t color);
  float    (*adc_read_battery)();
  int      (*button_get)();      // id натиснутої кнопки (0..4) або -1 якщо черга порожня
  uint32_t (*millis_ms)();       // час від старту, мс
  uint32_t (*free_heap)();       // вільний heap, байти
  uint32_t (*heap_total)();      // загальний heap, байти
  void     (*display_rect)(int x, int y, int w, int h, uint16_t color);       // контур
  void     (*display_fill_rect)(int x, int y, int w, int h, uint16_t color);  // заливка
  int      (*sys_temp)();        // температура чипа, °C
  int      (*cpu_mhz)();         // частота CPU, МГц
  void     (*gpio_mode)(int pin, int mode);   // 0=input, 1=output, 2=input_pullup
  void     (*gpio_write)(int pin, int val);
  int      (*gpio_read)(int pin);
  int      (*i2c_probe)(int addr);            // 1 якщо пристрій відповів
  int      (*i2c_read8)(int addr, int reg);   // байт або -1
  void     (*i2c_write8)(int addr, int reg, int val);
  // --- мережа ---
  int      (*wifi_rssi)();                        // dBm, 0 якщо не підключено
  const char* (*wifi_ip)();                       // "" якщо не підключено
  // --- мережеві проби зі скрипта (усі через реальний стек на пристрої) ---
  const char* (*net_dns)(const char* host);       // резолв імені -> IP-рядок ("" fail)
  int         (*net_ping)(const char* host);      // ICMP RTT мс (-1 fail)
  int         (*net_tcp)(const char* host, int port); // 1 якщо TCP-порт приймає, інакше 0
  int         (*net_http_get)(const char* url);   // HTTP GET -> код статусу (-1 fail); тіло кешує
  const char* (*net_http_body)();                 // тіло останнього http_get (обрізане)
  int         (*net_arp_count)();                 // знімок ARP-кешу lwIP -> к-ть записів
  const char* (*net_arp_ip)(int i);               // IP i-го запису знімка ("" поза межами)
  const char* (*net_arp_mac)(int i);              // MAC i-го запису знімка ("" поза межами)
  // --- UNO R3 копроцесор (drivers/uno_link, UART @9600) — живі датчики зі скрипта ---
  int (*uno_pot)();    // потенціометр 0..1023
  int (*uno_reed)();   // reed: 1=замкнуто (магніт поруч), 0=ні
  int (*uno_ir)();     // код IR цього циклу (0=нема нового)
  int (*uno_temp)();   // °C ціле (DHT11)
  int (*uno_hum)();    // % ціле (DHT11)
};

// Встановити набір хуків (пристрій викликає з реальними, тест — з моками).
void native_api_set_hooks(const NativeApiHooks* hooks);

// Зареєструвати всі native-функції в поточній Berry VM (через berry_vm_regfunc).
void native_api_register();

// Встановити РЕАЛЬНІ апаратні хуки (реалізовано в native_api_hw.cpp, тільки пристрій).
void native_api_install_hardware();
