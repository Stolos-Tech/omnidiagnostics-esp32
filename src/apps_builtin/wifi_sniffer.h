// WiFi Sniffer — ПАСИВНИЙ аналіз ефіру (без інʼєкції/деаутентифікації).
// Promiscuous-режим esp_wifi + розбір 802.11 mgmt/data-кадрів: інвентаризація
// точок доступу (BSSID/SSID/канал/шифрування/RSSI) І клієнтів (MAC, до якого AP
// приєднані або які SSID probe-ять). Доповнює WiFi Analyzer, який через
// scanNetworks() бачить лише AP і не бачить клієнтів.
//
// Архітектура без гонок: promiscuous-callback виконується у WiFi-задачі й робить
// МІНІМУМ — парсить кадр і кладе компактне "спостереження" в lock-free SPSC-ring.
// Агрегація в таблиці AP/клієнтів іде в головному потоці (loop) — тож самі таблиці
// чіпає лише один потік.
//
// Керування (2 рідні кнопки; довге утримання лівої = назад у лаунчер):
//   S2 — наступна сторінка (SUMMARY -> APS -> CLIENTS -> EXIT)
//   S5 — очистити зібране (на EXIT — підтвердити вихід)
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class WifiSnifferApp : public App {
public:
  const char* name() const override { return "WiFi Sniffer"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void on_exit() override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  enum Phase { RUNNING, BT_BLOCKED, LOW_POWER };
  enum Page { SUMMARY, APS, CLIENTS, PAGE_EXIT, PAGE_COUNT };

  Phase phase_ = RUNNING;
  bool was_wifi_ = false;       // Remote:WiFi був активний -> відновити транспорт на виході
  uint32_t t_start_ = 0;        // авто-таймаут (не рубити мережу нескінченно)
  Page  page_  = SUMMARY;
  bool  wants_exit_ = false;
  uint16_t cur_channel_ = 1;
  uint32_t t_hop_ = 0;

  void topBar(const char* title);
  void drawSummary();
  void drawAps();
  void drawClients();
  void drawExit();
};
