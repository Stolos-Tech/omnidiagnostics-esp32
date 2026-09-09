#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include "arp_scan.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../kernel/device_id.h"
#include "../kernel/shared_store.h"
#include "../remote/protocol.h"

#define BATCH 3               // ARP-запитів за тік (щоб не спорожнити кеш до харвесту)
#define SETTLE_MS 1500        // дозбір відповідей після останнього запиту
#define SWEEP_CAP 254         // ліміт хостів для сканування (велика підмережа)

static IPAddress to_addr(uint32_t ip) {   // a.b.c.d MSB -> IPAddress
  return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}
static uint32_t ip_u32(const IPAddress& a) {   // IPAddress -> a.b.c.d MSB
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | (uint32_t)a[3];
}
// lwIP-addr (мережевий порядок, on-LE == a|b<<8|c<<16|d<<24) -> a.b.c.d MSB
static uint32_t lwip_to_msb(uint32_t lw) {
  return net_make_ip(lw & 0xFF, (lw >> 8) & 0xFF, (lw >> 16) & 0xFF, (lw >> 24) & 0xFF);
}

bool ArpScanApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void ArpScanApp::add_host(uint32_t ip, const uint8_t mac[6]) {
  if (ip == self_ip_) return;
  uint8_t z = 0; for (int i = 0; i < 6; i++) z |= mac[i];
  if (z == 0) return;
  for (int i = 0; i < host_count_; i++) if (host_[i].ip == ip) {
    memcpy(host_[i].mac, mac, 6); return;
  }
  if (host_count_ >= MAX_HOSTS) return;
  host_[host_count_].ip = ip; memcpy(host_[host_count_].mac, mac, 6); host_count_++;
  char ipb[20]; net_ip_to_str(ip, ipb, sizeof(ipb));
  shared_add_host(ipb, net_oui_vendor(mac));   // крос-модульний store (IP + вендор)
}

void ArpScanApp::harvest() {
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t* ipa = nullptr; struct netif* nif = nullptr; struct eth_addr* eth = nullptr;
    if (etharp_get_entry((size_t)i, &ipa, &nif, &eth) && ipa && eth)
      add_host(lwip_to_msb(ipa->addr), eth->addr);
  }
}

void ArpScanApp::init() {
  wants_exit_ = false; host_count_ = 0; cursor_ = 0; t_settle_ = 0;
  if (!connected()) { phase_ = NOT_CONN; return; }
  self_ip_ = ip_u32(WiFi.localIP());
  gw_ip_   = ip_u32(WiFi.gatewayIP());
  uint32_t mask = ip_u32(WiFi.subnetMask());
  uint32_t cnt = 0;
  if (!net_subnet_range(self_ip_, mask, &first_, &last_, &cnt)) { first_ = last_ = self_ip_; }
  if (last_ - first_ > SWEEP_CAP - 1) last_ = first_ + SWEEP_CAP - 1;   // ліміт великих мереж
  cur_ = first_;
  phase_ = SWEEPING;
}

void ArpScanApp::loop() {
  if (phase_ == NOT_CONN) return;
  harvest();
  if (phase_ != SWEEPING) return;
  for (int k = 0; k < BATCH && cur_ <= last_; k++) {
    ip4_addr_t a; a.addr = (uint32_t)to_addr(cur_);
    etharp_request(netif_default, &a);
    cur_++;
  }
  if (cur_ > last_) {
    if (t_settle_ == 0) t_settle_ = millis();
    if (millis() - t_settle_ >= SETTLE_MS) phase_ = LIST;
  }
}

void ArpScanApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("ARP Scan", 6, 3, 2);
  char b[52];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == SWEEPING) {
    uint32_t span = (last_ > first_) ? (last_ - first_) : 1;
    uint32_t done = (cur_ > first_) ? (cur_ - first_) : 0; if (done > span) done = span;
    spr.setTextColor(C_TEXT, C_BG);
    char ipb[20]; net_ip_to_str(cur_ <= last_ ? cur_ : last_, ipb, sizeof(ipb));
    snprintf(b, sizeof(b), "ARP who-has %s", ipb);
    spr.drawString(b, 8, 26, 2);
    int gx = 8, gy = 50, gw = SCR_W - 16, gh = 12;
    spr.drawRect(gx, gy, gw, gh, C_GRID);
    spr.fillRect(gx + 1, gy + 1, (int)((gw - 2) * done / span), gh - 2, C_ACCENT);
    spr.setTextColor(C_GOOD, C_BG);
    snprintf(b, sizeof(b), "Hosts found: %d", host_count_);
    spr.drawString(b, 8, 70, 2);
    spr.setTextColor(C_DIM, C_BG); spr.drawString("S5=stop / to list", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == DETAIL) {
    const Host& h = host_[cursor_];
    char ip[20]; net_ip_to_str(h.ip, ip, sizeof(ip));
    bool is_gw = (h.ip == gw_ip_);
    const char* vend = net_oui_vendor(h.mac); if (!vend[0]) vend = "?";
    const char* type = net_device_type(is_gw, net_oui_category(h.mac), net_mac_is_local(h.mac), nullptr, 0);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString(ip, 8, 20, 2);
    if (is_gw) { spr.setTextDatum(TR_DATUM); spr.setTextColor(C_WARN, C_BG); spr.drawString("GW", SCR_W - 6, 22, 2); spr.setTextDatum(TL_DATUM); }
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", h.mac[0], h.mac[1], h.mac[2], h.mac[3], h.mac[4], h.mac[5]);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 8, 42, 2);
    snprintf(b, sizeof(b), "Vendor: %.24s", vend);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 62, 1);
    snprintf(b, sizeof(b), "Type: %.28s", type);
    spr.drawString(b, 8, 74, 1);
    if (net_mac_is_local(h.mac)) { spr.setTextColor(C_WARN, C_BG); spr.drawString("MAC lokal. (rand.)", 8, 88, 1); }
    spr.setTextColor(C_DIM, C_BG); spr.drawString("S2=back to list", 8, SCR_H - 12, 1);
    return;
  }

  // LIST
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  snprintf(b, sizeof(b), "%d host", host_count_);
  spr.drawString(b, SCR_W - 6, 3, 2); spr.setTextDatum(TL_DATUM);
  if (host_count_ == 0) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("No hosts found", 8, 34, 2);
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
    if (idx < host_count_) {
      char ip[20]; net_ip_to_str(host_[idx].ip, ip, sizeof(ip));
      const char* vend = net_oui_vendor(host_[idx].mac);
      snprintf(b, sizeof(b), "%-15s %.10s", ip, vend[0] ? vend : (host_[idx].ip == gw_ip_ ? "gateway" : ""));
    } else snprintf(b, sizeof(b), "< Back");
    spr.drawString(b, 10, y + 1, 2);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
}

void ArpScanApp::button(ButtonId id) {
  if (phase_ == NOT_CONN)  { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (phase_ == SWEEPING)  { if (id == BTN_S5) phase_ = LIST; return; }
  if (phase_ == DETAIL)    { if (id == BTN_S2 || id == BTN_S5) phase_ = LIST; return; }
  // LIST
  if (host_count_ == 0) { if (id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) {
    if (cursor_ >= host_count_) wants_exit_ = true;      // "< Back"
    else phase_ = DETAIL;
  }
}

void ArpScanApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) {
    cursor_ = idx;
    if (cursor_ >= host_count_) wants_exit_ = true;
    else phase_ = DETAIL;
  }
}

std::string ArpScanApp::remote_state() {
  if (phase_ == SWEEPING) {
    static char l0[40], l1[40];
    uint32_t span = (last_ > first_) ? (last_ - first_) : 1;
    uint32_t done = (cur_ > first_) ? (cur_ - first_) : 0; if (done > span) done = span;
    snprintf(l0, sizeof(l0), "ARP sweep %lu%%", (unsigned long)(done * 100 / span));
    snprintf(l1, sizeof(l1), "Found %d", host_count_);
    const char* it[2] = { l0, l1 };
    return protocol_build_menu("arp_scan", it, 2, -1);
  }
  char lines[MAX_HOSTS][40];
  const char* items[MAX_HOSTS];
  int cnt = host_count_ < MAX_HOSTS ? host_count_ : MAX_HOSTS;
  for (int i = 0; i < cnt; i++) {
    char ip[20]; net_ip_to_str(host_[i].ip, ip, sizeof(ip));
    const char* vend = net_oui_vendor(host_[i].mac);
    snprintf(lines[i], 40, "%-15s %.12s", ip, vend[0] ? vend : (host_[i].ip == gw_ip_ ? "gateway" : "?"));
    items[i] = lines[i];
  }
  return protocol_build_menu("arp_scan", items, cnt, cursor_ < cnt ? cursor_ : -1);
}
