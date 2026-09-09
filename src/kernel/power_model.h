// Оцінка споживання струму (мА) за активними підсистемами — LIVE-індикатор для додатка
// (не лабораторна точність). Числа — типові для модулів цього проєкту; калібрувати за
// реальними вимірами, коли буде INA219/шунт. БЕЗ апаратних включень -> native-тести.
#pragma once
#include <stdint.h>

struct PowerFlags {
  bool wifi_sta = false;   // STA підключено (радіо активне)
  bool soft_ap  = false;   // SoftAP піднятий
  bool bt        = false;  // Bluetooth-режим
  bool nrf24     = false;  // nRF24 присутній/активний (PA/LNA)
  bool cc1101    = false;  // CC1101 присутній/активний
  bool sd_write  = false;  // SD у записі (транзієнт)
  int  backlight = 128;    // яскравість підсвітки 0..255
};

// Складники (мА) — експортовані для UI-розбивки в додатку.
struct PowerBreakdown {
  int base;       // ESP32 @80MHz idle
  int backlight;  // TFT LED за яскравістю
  int wifi;       // STA + AP
  int radios;     // nRF24 + CC1101
  int sd;         // SD запис
  int bt;         // Bluetooth
  int total;      // сума
};

PowerBreakdown power_model_breakdown(const PowerFlags& f);
int            power_model_estimate_ma(const PowerFlags& f);   // = breakdown.total
