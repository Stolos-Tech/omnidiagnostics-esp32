// Реальні (апаратні) реалізації хуків native_api — ТІЛЬКИ для пристрою.
// Виключено з native-білду (build_src_filter у [env:native]).
#include <Arduino.h>
#include <Wire.h>
#include "native_api.h"
#include "../drivers/display.h"
#include "../drivers/battery_adc.h"
#include "../kernel/input_queue.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Ping.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include "../drivers/uno_link.h"

static void hw_display_clear() {
  display_sprite().fillSprite(C_BG);
}

static void hw_display_text(int x, int y, const char* s, uint16_t color) {
  TFT_eSprite& spr = display_sprite();
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(color, C_BG);
  spr.drawString(s, x, y, 2);
}

static float hw_adc_read_battery() {
  return battery_read();
}

static int hw_button_get() {
  InputEvent ev;
  if (input_queue().pop(ev) && ev.type == EV_BUTTON) return (int)ev.btn;
  return -1;
}

static uint32_t hw_millis() { return millis(); }
static uint32_t hw_free_heap() { return ESP.getFreeHeap(); }
static uint32_t hw_heap_total() { return ESP.getHeapSize(); }
static void hw_display_rect(int x, int y, int w, int h, uint16_t color) {
  display_sprite().drawRect(x, y, w, h, color);
}
static void hw_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
  display_sprite().fillRect(x, y, w, h, color);
}
static int hw_sys_temp() { return (int)temperatureRead(); }
static int hw_cpu_mhz() { return (int)ESP.getCpuFreqMHz(); }

static void hw_gpio_mode(int pin, int mode) {
  pinMode(pin, mode == 1 ? OUTPUT : mode == 2 ? INPUT_PULLUP : INPUT);
}
static void hw_gpio_write(int pin, int val) { digitalWrite(pin, val ? HIGH : LOW); }
static int  hw_gpio_read(int pin) { return digitalRead(pin) == HIGH ? 1 : 0; }

static bool s_wire = false;
// I2C: SDA=21, SCL=32 (НЕ дефолтний 22 — там завжди-активний UART до UNO GPIO22 -> був
// конфлікт; 32 вільний після видалення RGB). Тепер Berry i2c_* не чіпає UNO-лінк.
// SDA перенесено з 21 (тепер SPI MISO для SD/nRF24) на 38. GPIO38 на цій платі мертвий
// (input-only, не виведений) -> Berry-I2C фактично відключений, АЛЕ головне: wire_ensure
// більше НЕ чіпає GPIO21, тож виклик i2c_* не ламає SPI-шину. I2C — не частина red-team ядра.
static void wire_ensure() { if (!s_wire) { Wire.begin(38, 32); s_wire = true; } }  // SDA38(dead)/SCL32
static int hw_i2c_probe(int addr) {
  wire_ensure(); Wire.beginTransmission((uint8_t)addr);
  return Wire.endTransmission() == 0 ? 1 : 0;
}
static int hw_i2c_read8(int addr, int reg) {
  wire_ensure();
  Wire.beginTransmission((uint8_t)addr); Wire.write((uint8_t)reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}
static void hw_i2c_write8(int addr, int reg, int val) {
  wire_ensure();
  Wire.beginTransmission((uint8_t)addr);
  Wire.write((uint8_t)reg); Wire.write((uint8_t)val);
  Wire.endTransmission();
}

// --- мережа ---
static int      hw_wifi_rssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }
static const char* hw_wifi_ip() {
  static String ip;
  if (WiFi.status() != WL_CONNECTED) return "";
  ip = WiFi.localIP().toString();
  return ip.c_str();
}

// --- мережеві проби (реальний стек) ---
static const char* hw_net_dns(const char* host) {
  static String ip; IPAddress a;
  if (!host || !WiFi.hostByName(host, a)) return "";
  ip = a.toString(); return ip.c_str();
}
static int hw_net_ping(const char* host) {
  IPAddress a;
  if (!host || !WiFi.hostByName(host, a)) return -1;
  return Ping.ping(a, 1) ? (int)Ping.averageTime() : -1;
}
static int hw_net_tcp(const char* host, int port) {
  if (!host) return 0;
  WiFiClient c;
  bool ok = c.connect(host, (uint16_t)port, 800);
  c.stop();
  return ok ? 1 : 0;
}
static char s_http_body[512];
static int hw_net_http_get(const char* url) {
  s_http_body[0] = 0;
  if (!url) return -1;
  HTTPClient http;
  if (!http.begin(url)) return -1;
  int code = http.GET();
  if (code > 0) snprintf(s_http_body, sizeof(s_http_body), "%s", http.getString().c_str());
  http.end();
  return code;
}
static const char* hw_net_http_body() { return s_http_body; }

// Знімок ARP-кешу lwIP (etharp_get_entry дає мережевий порядок байтів).
static char s_arp_ip[16][20];
static char s_arp_mac[16][18];
static int  s_arp_n = 0;
static int hw_net_arp_count() {
  s_arp_n = 0;
  for (int i = 0; i < ARP_TABLE_SIZE && s_arp_n < 16; i++) {
    ip4_addr_t* ipa = nullptr; struct netif* nif = nullptr; struct eth_addr* eth = nullptr;
    if (etharp_get_entry((size_t)i, &ipa, &nif, &eth) && ipa && eth) {
      uint32_t lw = ipa->addr;
      snprintf(s_arp_ip[s_arp_n], 20, "%u.%u.%u.%u",
               (unsigned)(lw & 0xFF), (unsigned)((lw >> 8) & 0xFF),
               (unsigned)((lw >> 16) & 0xFF), (unsigned)((lw >> 24) & 0xFF));
      snprintf(s_arp_mac[s_arp_n], 18, "%02X:%02X:%02X:%02X:%02X:%02X",
               eth->addr[0], eth->addr[1], eth->addr[2], eth->addr[3], eth->addr[4], eth->addr[5]);
      s_arp_n++;
    }
  }
  return s_arp_n;
}
static const char* hw_net_arp_ip(int i)  { return (i >= 0 && i < s_arp_n) ? s_arp_ip[i]  : ""; }
static const char* hw_net_arp_mac(int i) { return (i >= 0 && i < s_arp_n) ? s_arp_mac[i] : ""; }

// --- UNO R3: читаємо кешовані значення з uno_link (оновлюються в uno_link_loop) ---
static int hw_uno_pot()  { return uno_link_sensors().pot; }
static int hw_uno_reed() { return uno_link_sensors().reed ? 1 : 0; }
static int hw_uno_ir()   { return (int)uno_link_sensors().ir; }
static int hw_uno_temp() { return uno_link_env().temp_x10 / 10; }   // *10 -> ціле °C
static int hw_uno_hum()  { return uno_link_env().hum_x10 / 10; }    // *10 -> ціле %

static const NativeApiHooks HW_HOOKS = {
  hw_display_clear,
  hw_display_text,
  hw_adc_read_battery,
  hw_button_get,
  hw_millis,
  hw_free_heap,
  hw_heap_total,
  hw_display_rect,
  hw_display_fill_rect,
  hw_sys_temp,
  hw_cpu_mhz,
  hw_gpio_mode,
  hw_gpio_write,
  hw_gpio_read,
  hw_i2c_probe,
  hw_i2c_read8,
  hw_i2c_write8,
  hw_wifi_rssi,
  hw_wifi_ip,
  hw_net_dns,
  hw_net_ping,
  hw_net_tcp,
  hw_net_http_get,
  hw_net_http_body,
  hw_net_arp_count,
  hw_net_arp_ip,
  hw_net_arp_mac,
  hw_uno_pot,
  hw_uno_reed,
  hw_uno_ir,
  hw_uno_temp,
  hw_uno_hum,
};

void native_api_install_hardware() {
  native_api_set_hooks(&HW_HOOKS);
}
