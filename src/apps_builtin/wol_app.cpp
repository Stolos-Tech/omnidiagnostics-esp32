#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "wol_app.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../remote/protocol.h"

#define WOL_PORT 9

// Збережені цілі WoL — будити прямо з плати (S1 гортає), без вводу з телефона.
// УВАГА: реальне пробудження лише якщо ціль підтримує+має увімкнений WoL: Ethernet —
// надійно; WiFi-WoWLAN — рідко; телефони — майже ніколи. Пресети зручні як швидкий вибір.
struct WolPreset { const char* name; const char* mac; };
static const WolPreset PRESETS[] = {
  { "PC (WiFi)", "F8:FE:5E:8A:3C:DE" },   // цей ПК, адаптер WiFi у мережі плати
};
static const int NPRESET = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

bool WolApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void WolApp::init() {
  wants_exit_ = false;
  sent_ = false;
  preset_ = -1;
}

void WolApp::send_wol() {
  uint8_t mac[6];
  last_ok_ = false;
  if (connected() && net_parse_mac(mac_str_, mac)) {
    uint8_t pkt[102];
    int n = wol_build_packet(mac, pkt, sizeof(pkt));
    if (n > 0) {
      WiFiUDP udp;
      udp.beginPacket(IPAddress(255, 255, 255, 255), WOL_PORT);
      udp.write(pkt, n);
      udp.endPacket();
      last_ok_ = true;
    }
  }
  sent_ = true;
  Serial.printf("[WOL] %s -> %s\n", mac_str_, last_ok_ ? "sent" : "fail");
}

void WolApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "mac") == 0 && value) {
    snprintf(mac_str_, sizeof(mac_str_), "%s", value);
    preset_ = -1;                    // ручний ввід з телефона
    sent_ = false;
    if (connected()) send_wol();
  }
}

void WolApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("WAKE ON LAN", 6, 3, 2);
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
  if (mac_str_[0] == '\0') {
    spr.drawString("S1=preset | MAC z telefona", 8, 24, 1);
  } else {
    spr.setTextColor(C_TEXT, C_BG);
    char b[40]; snprintf(b, sizeof(b), "MAC: %.20s", mac_str_);
    spr.drawString(b, 8, 24, 2);
    if (preset_ >= 0) {
      spr.setTextColor(C_ACCENT, C_BG);
      char t[40]; snprintf(t, sizeof(t), "Target: %.16s", PRESETS[preset_].name);
      spr.drawString(t, 8, 40, 1);
    }
  }

  if (sent_) {
    spr.setTextColor(last_ok_ ? C_GOOD : C_BAD, C_BG);
    spr.drawString(last_ok_ ? "Magic packet sent" : "Error (check MAC)", 8, 54, 2);
  }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(mac_str_[0] ? "S1=preset S5=send S2=exit" : "S1=preset  S2=exit", 8, SCR_H - 12, 1);
}

void WolApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S1 && NPRESET > 0) {          // гортати збережені цілі
    preset_ = (preset_ + 1) % NPRESET;
    snprintf(mac_str_, sizeof(mac_str_), "%s", PRESETS[preset_].mac);
    sent_ = false;
  }
  if (id == BTN_S5 && connected() && mac_str_[0]) send_wol();
}

std::string WolApp::remote_state() {
  char l0[40]; snprintf(l0, sizeof(l0), "MAC: %.24s", mac_str_[0] ? mac_str_ : "(none)");
  char l1[32] = "";
  const char* items[2] = { l0, l1 };
  int n = 1;
  if (sent_) { snprintf(l1, sizeof(l1), "%s", last_ok_ ? "Sent" : "Failed"); n = 2; }
  return protocol_build_menu("wol", items, n, -1);
}
