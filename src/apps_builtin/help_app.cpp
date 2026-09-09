#include <Arduino.h>
#include "help_app.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

// Один запис довідки. gloss == nullptr -> це заголовок секції (на весь рядок, акцентом).
struct HelpEntry { const char* term; const char* gloss; };

// Матеріал синхронний із web index.html HELP та mobile BoardData. Короткі однорядкові
// формулювання (240px, шрифт 1): term <= ~10 симв., gloss <= ~25 симв.
static const HelpEntry ENTRIES[] = {
  { "GLOSSARY",   nullptr },
  { "RSSI",       "signal strength, dBm" },
  { "Sub-GHz",    "300-928MHz radio band" },
  { "OOK/ASK",    "cheap 433MHz remote mod" },
  { "nRF24",      "2.4GHz air-occupancy scan" },
  { "CC1101",     "sub-GHz OOK transceiver" },
  { "mDNS",       "esp32os.local LAN name" },
  { "PWM",        "duty-cycle backlight/fan" },
  { "HSPI",       "shared SPI (nRF/SD/CC)" },
  { "EV1527",     "OOK remote codeword fmt" },
  { "Tailscale",  "mesh-VPN for remote acc" },
  { "Gateway",    "home bridge, holds PIN" },
  { "Strap pin",  "sets ESP32 boot mode" },
  { "TOOLS",      nullptr },
  { "Analyzer",   "sub-GHz RSSI sweep" },
  { "Capture",    "decode OOK remotes" },
  { "Watch",      "persistent-TX finder" },
  { "WiFi An.",   "nets, channels, RSSI" },
  { "Detect",     "trackers/cams/skimmers" },
  { "EM Field",   "coil EMI probe (GPIO36)" },
};
static const int N = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

void HelpApp::init() { wants_exit_ = false; top_ = 0; }

int HelpApp::max_top() const { return (N > VIS) ? (N - VIS) : 0; }

void HelpApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("HELP", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  char pos[10]; snprintf(pos, sizeof(pos), "%d/%d", top_ + 1, N);
  spr.drawString(pos, SCR_W - 6, 3, 2);

  int y = 20; const int dy = 16;
  for (int r = 0; r < VIS && top_ + r < N; r++) {
    const HelpEntry& e = ENTRIES[top_ + r];
    spr.setTextDatum(TL_DATUM);
    if (e.gloss == nullptr) {                       // заголовок секції
      spr.setTextColor(C_ACCENT, C_BG);
      spr.drawString(e.term, 8, y, 2);
    } else {
      spr.setTextColor(C_TEXT, C_BG);
      spr.drawString(e.term, 8, y, 1);
      spr.setTextColor(C_DIM, C_BG);
      spr.drawString(e.gloss, 82, y, 1);
    }
    y += dy;
  }

  // індикатори прокрутки
  spr.setTextColor(C_ACCENT, C_BG);
  spr.setTextDatum(TR_DATUM);
  if (top_ > 0)            spr.drawString("^", SCR_W - 4, 20, 2);
  if (top_ < max_top())   spr.drawString("v", SCR_W - 4, SCR_H - 26, 2);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2/S1=scroll  S5=exit", 8, SCR_H - 12, 1);
}

void HelpApp::button(ButtonId id) {
  if (id == BTN_S2)      { if (top_ < max_top()) top_++; }
  else if (id == BTN_S1) { if (top_ > 0) top_--; }
  else if (id == BTN_S5) wants_exit_ = true;
}

std::string HelpApp::remote_state() {
  // Дзеркалимо видиме вікно як рядки "term - gloss" / "[SECTION]".
  char buf[VIS][48];
  const char* items[VIS];
  int n = 0;
  for (int r = 0; r < VIS && top_ + r < N; r++) {
    const HelpEntry& e = ENTRIES[top_ + r];
    if (e.gloss == nullptr) snprintf(buf[n], sizeof(buf[n]), "[ %s ]", e.term);
    else                    snprintf(buf[n], sizeof(buf[n]), "%s - %s", e.term, e.gloss);
    items[n] = buf[n]; n++;
  }
  return protocol_build_menu("help", items, n, -1);
}
