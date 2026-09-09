#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <strings.h>
#include "ssdp_browser.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../kernel/intents.h"
#include "../remote/protocol.h"

#define SSDP_LOCAL_PORT 1901
#define SEARCH_MS 5000        // скільки слухати відповіді
#define RESEND_EACH 700       // M-SEARCH повторюється (UDP губиться)
#define MAX_RESENDS 3

static WiFiUDP s_udp;

// (uint32_t)IPAddress дає ЗВОРОТНИЙ порядок; net_util очікує a.b.c.d старшим байтом
// першим (той самий фікс, що й ip_to_u32 у net_scan).
static uint32_t ip_u32(const IPAddress& a) {
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | (uint32_t)a[3];
}

bool SsdpBrowserApp::connected() const { return WiFi.status() == WL_CONNECTED; }

// ---- розбір одного HTTP-подібного заголовка з відповіді ----
static void hdr_value(const char* pkt, const char* key, char* out, size_t out_sz) {
  out[0] = 0;
  size_t klen = strlen(key);
  const char* p = pkt;
  while (*p) {
    // порівняння ключа без регістру на початку рядка
    if (strncasecmp(p, key, klen) == 0) {
      const char* v = p + klen;
      while (*v == ' ' || *v == ':' ) v++;     // ключ уже без ':' — але дозволимо
      const char* e = v;
      while (*e && *e != '\r' && *e != '\n') e++;
      size_t n = (size_t)(e - v);
      if (n >= out_sz) n = out_sz - 1;
      memcpy(out, v, n); out[n] = 0;
      return;
    }
    // до наступного рядка
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
  }
}

void SsdpBrowserApp::send_msearch() {
  const char* MSEARCH =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: ssdp:all\r\n"
    "\r\n";
  s_udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
  s_udp.write((const uint8_t*)MSEARCH, strlen(MSEARCH));
  s_udp.endPacket();
}

void SsdpBrowserApp::init() {
  wants_exit_ = false;
  dev_count_ = 0; cursor_ = 0; resends_ = 0;
  if (!connected()) { phase_ = NOT_CONN; return; }
  s_udp.begin(SSDP_LOCAL_PORT);
  send_msearch();
  resends_ = 1;
  t_start_ = t_resend_ = millis();
  phase_ = SEARCHING;
}

void SsdpBrowserApp::on_exit() {
  s_udp.stop();
}

void SsdpBrowserApp::poll_udp() {
  int sz;
  while ((sz = s_udp.parsePacket()) > 0) {
    char buf[512];
    int n = s_udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    // тільки відповіді/анонси SSDP
    if (strncmp(buf, "HTTP/1.1", 8) != 0 && strncmp(buf, "NOTIFY", 6) != 0) continue;

    char location[72], server[28], st[26], nt[26];
    hdr_value(buf, "LOCATION", location, sizeof(location));
    hdr_value(buf, "SERVER",   server,   sizeof(server));
    hdr_value(buf, "ST",       st,       sizeof(st));
    hdr_value(buf, "NT",       nt,       sizeof(nt));
    if (!st[0] && nt[0]) memcpy(st, nt, sizeof(st));    // NOTIFY використовує NT
    if (!location[0]) continue;                         // без LOCATION нецікаво

    uint32_t ip = ip_u32(s_udp.remoteIP());             // a.b.c.d, старший байт першим
    // дедуп за LOCATION (одна картка на пристрій, попри багато ST)
    bool found = false;
    for (int i = 0; i < dev_count_; i++)
      if (strcmp(dev_[i].location, location) == 0) { found = true; break; }
    if (found || dev_count_ >= MAX_DEV) continue;

    Dev& d = dev_[dev_count_++];
    d.ip = ip; d.port = s_udp.remotePort();
    snprintf(d.location, sizeof(d.location), "%s", location);
    snprintf(d.server,   sizeof(d.server),   "%s", server[0] ? server : "?");
    snprintf(d.st,       sizeof(d.st),       "%s", st[0] ? st : "?");
  }
}

void SsdpBrowserApp::loop() {
  if (phase_ == NOT_CONN) return;
  poll_udp();
  uint32_t now = millis();
  if (phase_ == SEARCHING) {
    if (resends_ < MAX_RESENDS && now - t_resend_ >= RESEND_EACH) {
      send_msearch(); resends_++; t_resend_ = now;
    }
    if (now - t_start_ >= SEARCH_MS) phase_ = LIST;
  }
}

void SsdpBrowserApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("SSDP / UPnP", 6, 3, 2);
  char b[64];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == SEARCHING) {
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
    int left = (int)((SEARCH_MS - (millis() - t_start_)) / 1000) + 1;
    snprintf(b, sizeof(b), "%ds", left < 0 ? 0 : left);
    spr.drawString(b, SCR_W - 6, 3, 2); spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("M-SEARCH ssdp:all ...", 8, 30, 2);
    spr.setTextColor(C_GOOD, C_BG);
    snprintf(b, sizeof(b), "Devices found: %d", dev_count_);
    spr.drawString(b, 8, 54, 2);
    spr.setTextColor(C_DIM, C_BG); spr.drawString("S5=stop / to list", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == DETAIL) {
    const Dev& d = dev_[cursor_];
    char ip[20]; net_ip_to_str(d.ip, ip, sizeof(ip));
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString(d.server, 8, 20, 2);
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "IP: %s", ip);          spr.drawString(b, 8, 40, 2);
    snprintf(b, sizeof(b), "ST: %.28s", d.st);     spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 58, 1);
    snprintf(b, sizeof(b), "%.40s", d.location);   spr.drawString(b, 8, 70, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=open in HTTP GET", 8, SCR_H - 24, 1);
    spr.setTextColor(C_DIM, C_BG);    spr.drawString("S2=back to list", 8, SCR_H - 12, 1);
    return;
  }

  // LIST
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  snprintf(b, sizeof(b), "%d dev", dev_count_);
  spr.drawString(b, SCR_W - 6, 3, 2); spr.setTextDatum(TL_DATUM);
  if (dev_count_ == 0) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("No devices found", 8, 34, 2);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=exit", 8, SCR_H - 14, 1);
    return;
  }
  const int VISIBLE = 5, ROW_H = 18, total = items_total();
  int first = cursor_ - VISIBLE / 2;
  if (first < 0) first = 0;
  if (first > total - VISIBLE) first = total - VISIBLE;
  if (first < 0) first = 0;
  for (int row = 0; row < VISIBLE; row++) {
    int idx = first + row;
    if (idx >= total) break;
    int y = 20 + row * ROW_H;
    bool sel = (idx == cursor_);
    if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
    spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
    if (idx < dev_count_) {
      char ip[20]; net_ip_to_str(dev_[idx].ip, ip, sizeof(ip));
      snprintf(b, sizeof(b), "%.15s %s", dev_[idx].server, ip);
    } else snprintf(b, sizeof(b), "< Back");
    spr.drawString(b, 10, y + 1, 2);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
}

void SsdpBrowserApp::button(ButtonId id) {
  if (phase_ == NOT_CONN)  { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (phase_ == SEARCHING) { if (id == BTN_S5) phase_ = LIST; return; }
  if (phase_ == DETAIL) {
    if (id == BTN_S5) { intent_open_url(dev_[cursor_].location); wants_exit_ = true; }  // -> HTTP GET
    else if (id == BTN_S2) phase_ = LIST;
    return;
  }
  // LIST
  if (dev_count_ == 0) { if (id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) {
    if (cursor_ >= dev_count_) wants_exit_ = true;     // "< Back"
    else phase_ = DETAIL;
  }
}

void SsdpBrowserApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) {
    cursor_ = idx;
    if (cursor_ >= dev_count_) wants_exit_ = true;      // "< Back"
    else phase_ = DETAIL;
  }
}

std::string SsdpBrowserApp::remote_state() {
  if (phase_ == SEARCHING) {
    static char l0[40], l1[40];
    int left = (int)((SEARCH_MS - (millis() - t_start_)) / 1000) + 1;
    snprintf(l0, sizeof(l0), "M-SEARCH... %ds", left < 0 ? 0 : left);
    snprintf(l1, sizeof(l1), "Found %d", dev_count_);
    const char* it[2] = { l0, l1 };
    return protocol_build_menu("ssdp", it, 2, -1);
  }
  char lines[MAX_DEV][40];
  const char* items[MAX_DEV];
  int cnt = dev_count_ < MAX_DEV ? dev_count_ : MAX_DEV;
  for (int i = 0; i < cnt; i++) {
    char ip[20]; net_ip_to_str(dev_[i].ip, ip, sizeof(ip));
    snprintf(lines[i], 40, "%.16s %s", dev_[i].server, ip);
    items[i] = lines[i];
  }
  return protocol_build_menu("ssdp", items, cnt, cursor_ < cnt ? cursor_ : -1);
}
