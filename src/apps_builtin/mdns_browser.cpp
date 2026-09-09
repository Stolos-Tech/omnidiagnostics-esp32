#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "mdns_browser.h"
#include "../drivers/mdns_service.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../remote/protocol.h"

struct SvcType { const char* service; const char* proto; const char* label; };
static const SvcType TYPES[] = {
  {"http",        "tcp", "http"},
  {"https",       "tcp", "https"},
  {"googlecast",  "tcp", "cast"},
  {"printer",     "tcp", "print"},
  {"ssh",         "tcp", "ssh"},
  {"workstation", "tcp", "wks"},
};
static const int TYPE_COUNT = sizeof(TYPES) / sizeof(TYPES[0]);

bool MdnsBrowserApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void MdnsBrowserApp::init() {
  wants_exit_ = false;
  svc_count_ = 0;
  cursor_ = 0;
  query_i_ = 0;
  if (!connected()) { phase_ = NOT_CONN; return; }
  mdns_service_start();                 // спільний mDNS (анонс + запити), не end-имо
  mdns_up_ = mdns_service_running();
  phase_ = QUERYING;
}

void MdnsBrowserApp::finish() {
  // mDNS-стек лишаємо піднятим для анонсу esp32os.local — не зупиняємо.
}

void MdnsBrowserApp::loop() {
  if (phase_ != QUERYING) return;
  if (query_i_ >= TYPE_COUNT) { phase_ = LIST; return; }

  // Один тип сервісу за тік (queryService блокує ~1-2с).
  int n = MDNS.queryService(TYPES[query_i_].service, TYPES[query_i_].proto);
  for (int j = 0; j < n && svc_count_ < MAX_SVC; j++) {
    Svc& s = svc_[svc_count_];
    snprintf(s.name, sizeof(s.name), "%s", MDNS.hostname(j).c_str());
    IPAddress a = MDNS.IP(j);   // (uint32_t)IPAddress reversed -> явний порядок під net_ip_to_str
    s.ip = ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | (uint32_t)a[3];
    s.port = MDNS.port(j);
    s.type_idx = (uint8_t)query_i_;
    svc_count_++;
  }
  query_i_++;
}

void MdnsBrowserApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("mDNS", 6, 3, 2);

  char b[52];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == QUERYING) {
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
    snprintf(b, sizeof(b), "%d/%d", query_i_, TYPE_COUNT);
    spr.drawString(b, SCR_W - 6, 3, 2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_TEXT, C_BG);
    const char* lbl = query_i_ < TYPE_COUNT ? TYPES[query_i_].label : "";
    snprintf(b, sizeof(b), "Searching _%s._tcp ...", lbl);
    spr.drawString(b, 8, 30, 2);
    spr.setTextColor(C_GOOD, C_BG);
    snprintf(b, sizeof(b), "Services found: %d", svc_count_);
    spr.drawString(b, 8, 54, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S5=stop", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == DETAIL) {
    const Svc& s = svc_[cursor_];
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString(s.name, 8, 22, 2);
    snprintf(b, sizeof(b), "Type: _%s._tcp", TYPES[s.type_idx].label);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 8, 44, 2);
    char ip[20]; net_ip_to_str(s.ip, ip, sizeof(ip));
    snprintf(b, sizeof(b), "IP: %s", ip);
    spr.drawString(b, 8, 62, 2);
    snprintf(b, sizeof(b), "Port: %u", s.port);
    spr.drawString(b, 8, 80, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S2=back to list", 8, SCR_H - 12, 1);
    return;
  }

  // LIST
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  snprintf(b, sizeof(b), "%d svc", svc_count_);
  spr.drawString(b, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);
  if (svc_count_ == 0) {
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("No services found", 8, 34, 2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5=exit", 8, SCR_H - 14, 1);
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
    if (idx < svc_count_) snprintf(b, sizeof(b), "%.16s :%u", svc_[idx].name, svc_[idx].port);
    else                  snprintf(b, sizeof(b), "< Back");
    spr.drawString(b, 10, y + 1, 2);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
}

void MdnsBrowserApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (phase_ == QUERYING) { if (id == BTN_S5) phase_ = LIST; return; }
  if (phase_ == DETAIL)   { if (id == BTN_S2 || id == BTN_S5) phase_ = LIST; return; }

  // LIST
  if (svc_count_ == 0) { if (id == BTN_S5) { finish(); wants_exit_ = true; } return; }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) {
    if (cursor_ >= svc_count_) { finish(); wants_exit_ = true; }  // "< Back"
    else phase_ = DETAIL;
  }
}

void MdnsBrowserApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) {
    cursor_ = idx;
    if (cursor_ >= svc_count_) { finish(); wants_exit_ = true; }  // "< Back"
    else phase_ = DETAIL;
  }
}

std::string MdnsBrowserApp::remote_state() {
  char lines[MAX_SVC][40];
  const char* items[MAX_SVC];
  if (phase_ == QUERYING) {
    static char l0[40], l1[40];
    snprintf(l0, sizeof(l0), "Search %d/%d", query_i_, TYPE_COUNT);
    snprintf(l1, sizeof(l1), "Found %d", svc_count_);
    const char* it[2] = { l0, l1 };
    return protocol_build_menu("mdns", it, 2, -1);
  }
  int cnt = svc_count_ < MAX_SVC ? svc_count_ : MAX_SVC;
  for (int i = 0; i < cnt; i++) {
    char ip[20]; net_ip_to_str(svc_[i].ip, ip, sizeof(ip));
    snprintf(lines[i], 40, "%.14s %s:%u", svc_[i].name, ip, svc_[i].port);
    items[i] = lines[i];
  }
  return protocol_build_menu("mdns", items, cnt, cursor_ < cnt ? cursor_ : -1);
}
