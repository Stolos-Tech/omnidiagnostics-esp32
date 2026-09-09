// Deauth Alert — ПАСИВНИЙ детектор атак деаутентифікації/дизасоціації.
// Promiscuous-режим, ловить лише mgmt-кадри deauth(0x0C)/disassoc(0x0A),
// рахує їх по BSSID і рейт за вікно ~8с; при перевищенні порогу — тривога.
// Жодної інʼєкції — тільки слухання. Serial-рядок статистики (радіо зайняте
// promiscuous, SoftAP лежить). Хоп по каналах 1-13.
//
// Керування: S2 — сторінка (Summary/Offenders/Exit), S5 — очистити (на Exit — вихід).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class DeauthAlertApp : public App {
public:
  const char* name() const override { return "Deauth Alert"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void on_exit() override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { RUNNING, BT_BLOCKED, LOW_POWER };
  enum Page { SUMMARY, OFFENDERS, PAGE_EXIT, PAGE_COUNT };

  Phase phase_ = RUNNING;
  bool was_wifi_ = false;       // Remote:WiFi був активний -> відновити транспорт на виході
  uint32_t t_start_ = 0;        // авто-таймаут (не рубити мережу нескінченно)
  Page  page_  = SUMMARY;
  bool  wants_exit_ = false;
  uint16_t cur_channel_ = 1;
  uint32_t t_hop_ = 0;

  void topBar(const char* t);
  void drawSummary();
  void drawOffenders();
  void drawExit();
};
