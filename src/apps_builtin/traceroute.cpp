#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "traceroute.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../remote/protocol.h"

#define RECV_MS 700

struct __attribute__((packed)) IcmpEcho {
  uint8_t type; uint8_t code; uint16_t cksum; uint16_t id; uint16_t seq; uint8_t data[8];
};

static uint16_t in_cksum(const uint16_t* addr, int len) {
  uint32_t sum = 0; const uint16_t* w = addr;
  while (len > 1) { sum += *w++; len -= 2; }
  if (len == 1) sum += *(const uint8_t*)w;
  sum = (sum >> 16) + (sum & 0xFFFF); sum += (sum >> 16);
  return (uint16_t)(~sum);
}

bool TracerouteApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void TracerouteApp::start() {
  hop_n_ = 0; ttl_ = 1;
  IPAddress a;
  if (!WiFi.hostByName(target_, a)) { phase_ = RESOLVE_FAIL; return; }
  target_lwip_ = (uint32_t)a;                     // мережевий порядок для s_addr
  sock_ = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock_ < 0) { phase_ = SOCK_FAIL; return; }
  struct timeval tv; tv.tv_sec = 0; tv.tv_usec = RECV_MS * 1000;
  setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  phase_ = RUNNING;
}

void TracerouteApp::init() {
  wants_exit_ = false;
  if (sock_ >= 0) { close(sock_); sock_ = -1; }
  if (!connected()) { phase_ = NOT_CONN; return; }
  start();
}

void TracerouteApp::on_exit() {
  if (sock_ >= 0) { close(sock_); sock_ = -1; }
}

void TracerouteApp::step() {
  if (sock_ < 0 || hop_n_ >= MAX_HOPS) { phase_ = DONE; return; }
  int ttl = ttl_;
  setsockopt(sock_, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

  struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET; dst.sin_addr.s_addr = target_lwip_;

  IcmpEcho pkt; memset(&pkt, 0, sizeof(pkt));
  pkt.type = 8; pkt.code = 0; pkt.id = 0xAF32; pkt.seq = (uint16_t)ttl_;
  for (int i = 0; i < 8; i++) pkt.data[i] = (uint8_t)('A' + i);
  pkt.cksum = in_cksum((const uint16_t*)&pkt, sizeof(pkt));

  uint32_t t0 = millis();
  sendto(sock_, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dst, sizeof(dst));

  struct sockaddr_in from; socklen_t fl = sizeof(from);
  uint8_t rbuf[128];
  int r = recvfrom(sock_, rbuf, sizeof(rbuf), 0, (struct sockaddr*)&from, &fl);
  int rtt = (int)(millis() - t0);

  Hop& h = hops_[hop_n_];
  bool reached = false;
  if (r > 0) {
    uint32_t lw = from.sin_addr.s_addr;
    h.ip = net_make_ip(lw & 0xFF, (lw >> 8) & 0xFF, (lw >> 16) & 0xFF, (lw >> 24) & 0xFF);
    h.rtt = rtt; h.got = true;
    int ihl = (rbuf[0] & 0x0F) * 4;
    uint8_t itype = (r > ihl) ? rbuf[ihl] : 255;
    if (itype == 0 || from.sin_addr.s_addr == target_lwip_) reached = true;  // echo-reply/від цілі
  } else {
    h.ip = 0; h.rtt = -1; h.got = false;
  }
  hop_n_++;
  if (reached || ttl_ >= MAX_HOPS) { phase_ = DONE; if (sock_ >= 0) { close(sock_); sock_ = -1; } }
  else ttl_++;
}

void TracerouteApp::loop() {
  if (phase_ == RUNNING) step();
}


void TracerouteApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  display_top_bar("TRACEROUTE");
  char b[52];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == SOCK_FAIL) {
    spr.setTextColor(C_BAD, C_BG); spr.drawString("RAW socket unavailable", 8, 40, 2);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == RESOLVE_FAIL) {
    spr.setTextColor(C_BAD, C_BG); spr.drawString("Ne rezolvyt host", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(target_, 8, 60, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=again S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString(target_, SCR_W - 6, 3, 2); spr.setTextDatum(TL_DATUM);

  // останні 6 хопів
  int show = hop_n_ > 6 ? 6 : hop_n_;
  int startHop = hop_n_ - show;
  for (int i = 0; i < show; i++) {
    int idx = startHop + i;
    Hop& h = hops_[idx];
    int y = 20 + i * 15;
    spr.setTextColor(C_DIM, C_BG);
    snprintf(b, sizeof(b), "%2d", idx + 1); spr.drawString(b, 6, y, 1);
    if (h.got) {
      char ip[20]; net_ip_to_str(h.ip, ip, sizeof(ip));
      spr.setTextColor(C_TEXT, C_BG); spr.drawString(ip, 26, y, 1);
      spr.setTextDatum(TR_DATUM); spr.setTextColor(C_ACCENT, C_BG);
      snprintf(b, sizeof(b), "%dms", h.rtt); spr.drawString(b, SCR_W - 6, y, 1);
      spr.setTextDatum(TL_DATUM);
    } else {
      spr.setTextColor(C_WARN, C_BG); spr.drawString("* (no reply)", 26, y, 1);
    }
  }
  spr.setTextColor(C_DIM, C_BG);
  if (phase_ == RUNNING) { snprintf(b, sizeof(b), "hop %d...  S5=stop", ttl_); }
  else                    { snprintf(b, sizeof(b), "Done (%d hops)  S5=again", hop_n_); }
  spr.drawString(b, 6, SCR_H - 11, 1);
}

void TracerouteApp::button(ButtonId id) {
  if (phase_ == NOT_CONN || phase_ == SOCK_FAIL) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S5) {
    if (phase_ == RUNNING) { phase_ = DONE; if (sock_ >= 0) { close(sock_); sock_ = -1; } }
    else { if (sock_ >= 0) { close(sock_); sock_ = -1; } start(); }   // рестарт
  }
}

void TracerouteApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "target") == 0 && value && value[0]) {
    snprintf(target_, sizeof(target_), "%s", value);
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
    if (connected()) start(); else phase_ = NOT_CONN;
  }
}

std::string TracerouteApp::remote_state() {
  char lines[7][40];
  const char* items[7];
  int n = 0;
  int show = hop_n_ > 6 ? 6 : hop_n_;
  int startHop = hop_n_ - show;
  for (int i = 0; i < show && n < 6; i++) {
    Hop& h = hops_[startHop + i];
    if (h.got) { char ip[20]; net_ip_to_str(h.ip, ip, sizeof(ip)); snprintf(lines[n], 40, "%2d %s %dms", startHop + i + 1, ip, h.rtt); }
    else snprintf(lines[n], 40, "%2d * (no reply)", startHop + i + 1);
    items[n] = lines[n]; n++;
  }
  snprintf(lines[n], 40, phase_ == RUNNING ? "hop %d..." : "done (%d hops)", phase_ == RUNNING ? ttl_ : hop_n_);
  items[n] = lines[n]; n++;
  return protocol_build_menu("traceroute", items, n, -1);
}
