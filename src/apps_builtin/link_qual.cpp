#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <string.h>
#include <stdio.h>
#include "link_qual.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

#define PING_EACH 1000

bool LinkQualApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void LinkQualApp::set_target_by_sel() {
  if      (tsel_ == 0) snprintf(target_, sizeof(target_), "%s", WiFi.gatewayIP().toString().c_str());
  else if (tsel_ == 1) snprintf(target_, sizeof(target_), "1.1.1.1");
  else if (tsel_ == 2) snprintf(target_, sizeof(target_), "8.8.8.8");
}

void LinkQualApp::reset_stats() { count_ = 0; head_ = 0; sent_ = 0; lost_ = 0; }

void LinkQualApp::init() {
  wants_exit_ = false;
  reset_stats();
  if (!connected()) { phase_ = NOT_CONN; return; }
  if (tsel_ != 3 || target_[0] == 0) { tsel_ = 0; set_target_by_sel(); }
  t_ping_ = millis() - PING_EACH;   // перший пінг одразу
  phase_ = RUNNING;
}

void LinkQualApp::do_ping() {
  bool ok = Ping.ping(target_, 1);
  int ms = ok ? (int)Ping.averageTime() : -1;
  rtt_[head_] = ms;
  head_ = (head_ + 1) % N;
  if (count_ < N) count_++;
  sent_++;
  if (!ok) lost_++;
}

void LinkQualApp::loop() {
  if (phase_ != RUNNING) return;
  if (!connected()) { phase_ = NOT_CONN; return; }
  if (millis() - t_ping_ >= PING_EACH) { t_ping_ = millis(); do_ping(); }
}

// Обчислення статистики над збереженими RTT.
static void stats(const int* rtt, int count, int head, int N,
                  int* last, int* mn, int* mx, int* avg, int* jit) {
  *last = -1; *mn = 999999; *mx = 0; *avg = 0; *jit = 0;
  int sum = 0, ok = 0, prev = -1, jsum = 0, jn = 0;
  for (int i = 0; i < count; i++) {
    int idx = (head - count + i + 2 * N) % N;
    int v = rtt[idx];
    if (i == count - 1) *last = v;
    if (v < 0) { prev = -1; continue; }
    sum += v; ok++;
    if (v < *mn) *mn = v;
    if (v > *mx) *mx = v;
    if (prev >= 0) { jsum += abs(v - prev); jn++; }
    prev = v;
  }
  if (ok) *avg = sum / ok; else *mn = 0;
  if (jn) *jit = jsum / jn;
}


void LinkQualApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  display_top_bar("LINK QUAL");
  char b[52];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString(target_, SCR_W - 6, 3, 2); spr.setTextDatum(TL_DATUM);

  int last, mn, mx, avg, jit;
  stats(rtt_, count_, head_, N, &last, &mn, &mx, &avg, &jit);
  int loss = sent_ ? (int)(lost_ * 100 / sent_) : 0;

  spr.setTextColor(C_TEXT, C_BG);
  if (last >= 0) snprintf(b, sizeof(b), "RTT %d ms", last);
  else           snprintf(b, sizeof(b), "RTT --");
  spr.drawString(b, 8, 20, 2);
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "min %d  avg %d  max %d", mn, avg, mx);
  spr.drawString(b, 8, 38, 1);
  snprintf(b, sizeof(b), "jitter %d ms", jit);
  spr.drawString(b, 8, 50, 1);
  spr.setTextColor(loss > 20 ? C_BAD : loss > 0 ? C_WARN : C_GOOD, C_BG);
  snprintf(b, sizeof(b), "loss %d%% (%lu/%lu)", loss, (unsigned long)lost_, (unsigned long)sent_);
  spr.drawString(b, 8, 62, 1);

  // графік RTT: 0..(max) -> висота
  int gy = 76, gh = 44, gx = 6, gw = SCR_W - 12;
  spr.drawRect(gx, gy, gw, gh, C_GRID);
  int scale = mx > 0 ? mx : 1;
  for (int i = 0; i < count_; i++) {
    int idx = (head_ - count_ + i + 2 * N) % N;
    int v = rtt_[idx];
    int x = gx + 2 + i * ((gw - 4) / N);
    if (v < 0) { spr.drawFastVLine(x, gy + 2, gh - 4, C_BAD); continue; }
    int h = (gh - 4) * v / scale;
    spr.fillRect(x, gy + gh - 2 - h, 2, h, C_ACCENT);
  }
  spr.setTextColor(C_DIM, C_BG); spr.drawString("S2=cil  S5=skyd", 8, SCR_H - 11, 1);
}

void LinkQualApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2) { tsel_ = (tsel_ + 1) % 3; set_target_by_sel(); reset_stats(); }
  else if (id == BTN_S5) reset_stats();
}

void LinkQualApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "target") == 0 && value && value[0]) {
    snprintf(target_, sizeof(target_), "%s", value);
    tsel_ = 3; reset_stats();
    if (phase_ == NOT_CONN && connected()) phase_ = RUNNING;
  }
}

std::string LinkQualApp::remote_state() {
  int last, mn, mx, avg, jit;
  stats(rtt_, count_, head_, N, &last, &mn, &mx, &avg, &jit);
  int loss = sent_ ? (int)(lost_ * 100 / sent_) : 0;
  char lines[5][40];
  snprintf(lines[0], 40, "target %s", target_);
  if (last >= 0) snprintf(lines[1], 40, "RTT %d ms", last); else snprintf(lines[1], 40, "RTT --");
  snprintf(lines[2], 40, "min %d avg %d max %d", mn, avg, mx);
  snprintf(lines[3], 40, "jitter %d ms", jit);
  snprintf(lines[4], 40, "loss %d%% (%lu/%lu)", loss, (unsigned long)lost_, (unsigned long)sent_);
  const char* items[5]; for (int i = 0; i < 5; i++) items[i] = lines[i];
  return protocol_build_menu("link_qual", items, 5, -1);
}
