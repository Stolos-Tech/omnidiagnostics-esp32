#pragma once
#include <stdint.h>
#include <stddef.h>
// Лінк-протокол ESP32-C6 (analysis node) <-> ESP32 T-Display (HOST). Текстові рядки,
// одна половина будує, друга парсить. Цей файл — ДЗЕРКАЛО src/drivers/c6_link.h у
// головному проєкті; формати мусять збігатися. 3.3V↔3.3V, UART @C6_LINK_BAUD.
//
// HOST -> C6 команди (нижче — константи-префікси); C6 -> HOST результати.

// ---- команди HOST -> C6 ----
enum C6Cmd {
  C6_NONE = 0, C6_SCAN_WIFI, C6_MON, C6_SNIFF, C6_DEAUTH, C6_RF24,
  C6_SUBGHZ, C6_BLE, C6_Z15, C6_STOP, C6_STAT
};

// Парсить вхідний рядок команди від HOST. out_arg — числовий аргумент (канал/частота),
// out_hop — 1 якщо "MON HOP". Повертає C6Cmd.
C6Cmd c6_parse_cmd(const char* line, long* out_arg, int* out_hop);

// ---- будівники результатів C6 -> HOST (заповнюють out, повертають довжину) ----
int c6_build_wifi(const char* ssid, const char* bssid, int rssi, int ch, const char* enc, char* out, size_t cap);
int c6_build_monch(int ch, int count, char* out, size_t cap);
int c6_build_sniff(const char* mac, int rssi, int count, char* out, size_t cap);
int c6_build_deauth(int count, const char* src_bssid, char* out, size_t cap);
int c6_build_rf24(int ch, int count, char* out, size_t cap);
int c6_build_subghz(long freq_khz, int dbm, char* out, size_t cap);
int c6_build_ble(const char* addr, int rssi, const char* name, char* out, size_t cap);
int c6_build_z15(int ch, int energy, const char* pan_hex, char* out, size_t cap);
int c6_build_stat(int free_heap, int temp_c_x10, unsigned long uptime_s, char* out, size_t cap);
int c6_build_evt(const char* type, const char* detail, char* out, size_t cap);
