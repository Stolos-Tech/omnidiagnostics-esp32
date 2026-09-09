#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include "dns_lookup.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../remote/protocol.h"

void DnsLookupApp::init() {
  wants_exit_ = false;
  phase_ = 0;
  done_ = false;
}

bool DnsLookupApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void DnsLookupApp::resolve() {
  done_ = false; ok_ = false; ip_ = 0;
  if (!connected() || host_[0] == '\0') return;
  IPAddress res;
  ok_ = WiFi.hostByName(host_, res) == 1;
  if (ok_) ip_ = (uint32_t)res;
  done_ = true;
  Serial.printf("[DNS] %s -> %s\n", host_, ok_ ? res.toString().c_str() : "FAIL");
}

void DnsLookupApp::loop() {
  if (phase_ == 1) { phase_ = 2; return; }
  if (phase_ == 2) { resolve(); phase_ = 0; }
}

void DnsLookupApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "host") == 0 && value) {
    snprintf(host_, sizeof(host_), "%s", value);
    if (connected()) phase_ = 1;
  }
}

void DnsLookupApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("DNS LOOKUP", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(connected() ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(connected() ? "link" : "down", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!connected()) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  spr.setTextColor(C_DIM, C_BG);
  if (host_[0] == '\0') spr.drawString("Host: (vvedy z telefona)", 8, 24, 1);
  else { spr.setTextColor(C_TEXT, C_BG); char h[40]; snprintf(h,sizeof(h),"Host: %.32s",host_); spr.drawString(h, 8, 24, 2); }

  if (phase_ != 0) {
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("Resolving...", 8, 54, 2);
  } else if (done_) {
    if (ok_) {
      char ip[20]; net_ip_to_str(ip_, ip, sizeof(ip));
      spr.setTextColor(C_GOOD, C_BG);
      char b[40]; snprintf(b, sizeof(b), "IP: %s", ip);
      spr.drawString(b, 8, 54, 4);
    } else {
      spr.setTextColor(C_BAD, C_BG);
      spr.drawString("Not found (NXDOMAIN)", 8, 54, 2);
    }
  }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(host_[0] ? "S5=repeat  S2=exit" : "S2=exit", 8, SCR_H - 12, 1);
}

void DnsLookupApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5 && connected() && host_[0] && phase_ == 0) phase_ = 1;
}

std::string DnsLookupApp::remote_state() {
  char l0[72], l1[72];
  snprintf(l0, sizeof(l0), "Host: %.60s", host_[0] ? host_ : "(none)");
  const char* items[2] = { l0, l1 };
  int n = 1;
  if (done_) {
    if (ok_) { char ip[20]; net_ip_to_str(ip_, ip, sizeof(ip)); snprintf(l1, sizeof(l1), "IP: %s", ip); }
    else snprintf(l1, sizeof(l1), "Not found");
    n = 2;
  }
  return protocol_build_menu("dns_lookup", items, n, -1);
}
