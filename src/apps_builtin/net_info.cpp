#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include "net_info.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

void NetInfoApp::init() {
  wants_exit_ = false;
  ping_phase_ = 0;
  ping_done_ = false;
  gw_ok_ = inet_ok_ = false;
}

bool NetInfoApp::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

void NetInfoApp::do_pings() {
  gw_ok_ = inet_ok_ = false;
  gw_ms_ = inet_ms_ = 0;
  IPAddress gw = WiFi.gatewayIP();
  if ((uint32_t)gw != 0) {
    gw_ok_ = Ping.ping(gw, 3);
    if (gw_ok_) gw_ms_ = (int)Ping.averageTime();
  }
  IPAddress inet(8, 8, 8, 8);
  inet_ok_ = Ping.ping(inet, 3);
  if (inet_ok_) inet_ms_ = (int)Ping.averageTime();
  ping_done_ = true;
  Serial.printf("[NET] ping gw=%d(%dms) inet=%d(%dms)\n", gw_ok_, gw_ms_, inet_ok_, inet_ms_);
}

void NetInfoApp::loop() {
  // Пінг блокуючий (~кілька секунд) — показуємо "Pinging" один кадр, потім виконуємо.
  if (ping_phase_ == 1) { ping_phase_ = 2; return; }
  if (ping_phase_ == 2) { do_pings(); ping_phase_ = 0; }
}

// Заповнює текстові рядки стану; повертає кількість.
int NetInfoApp::build_lines(char lines[][40]) const {
  int n = 0;
  if (!connected()) {
    snprintf(lines[n++], 40, "Not connected");
    snprintf(lines[n++], 40, "Zapusty 'WiFi Setup'");
    return n;
  }
  snprintf(lines[n++], 40, "SSID: %.18s", WiFi.SSID().c_str());
  snprintf(lines[n++], 40, "IP:  %s", WiFi.localIP().toString().c_str());
  snprintf(lines[n++], 40, "GW:  %s", WiFi.gatewayIP().toString().c_str());
  snprintf(lines[n++], 40, "DNS: %s", WiFi.dnsIP().toString().c_str());
  snprintf(lines[n++], 40, "RSSI: %d dBm  ch%d", (int)WiFi.RSSI(), (int)WiFi.channel());
  if (ping_done_) {
    snprintf(lines[n++], 40, "GW ping: %s", gw_ok_ ? "" : "FAIL");
    if (gw_ok_) snprintf(lines[n-1], 40, "GW ping: %d ms", gw_ms_);
    snprintf(lines[n++], 40, "Internet: %s", inet_ok_ ? "" : "FAIL");
    if (inet_ok_) snprintf(lines[n-1], 40, "Internet: %d ms", inet_ms_);
  }
  return n;
}

void NetInfoApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("NET INFO", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(connected() ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(connected() ? "link" : "down", SCR_W - 6, 3, 2);

  char lines[8][40];
  int n = build_lines(lines);
  spr.setTextDatum(TL_DATUM);
  int y = 20, dy = 15;
  for (int i = 0; i < n; i++) {
    // рядки пінгу підсвічуємо
    uint16_t col = C_TEXT;
    if (strncmp(lines[i], "GW ping", 7) == 0)  col = gw_ok_ ? C_GOOD : C_BAD;
    if (strncmp(lines[i], "Internet", 8) == 0) col = inet_ok_ ? C_GOOD : C_BAD;
    spr.setTextColor(col, C_BG);
    spr.drawString(lines[i], 8, y, 2);
    y += dy;
  }

  spr.setTextColor(C_DIM, C_BG);
  if (ping_phase_ != 0) {
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("Pinging...", 8, SCR_H - 12, 1);
  } else if (connected()) {
    spr.drawString("S5=ping  S2=exit", 8, SCR_H - 12, 1);
  } else {
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
  }
}

void NetInfoApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5 && connected() && ping_phase_ == 0) {
    ping_phase_ = 1;  // старт пінг-тесту (виконається в loop після кадру "Pinging")
  }
}

std::string NetInfoApp::remote_state() {
  char lines[8][40];
  int n = build_lines(lines);
  const char* items[8];
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("net_info", items, n, -1);
}
