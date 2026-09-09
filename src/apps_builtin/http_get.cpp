#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "http_get.h"
#include "../kernel/url_util.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

void HttpGetApp::init() {
  wants_exit_ = false;
  fetch_phase_ = 0;
  done_ = false;
  code_ = 0;
  body_[0] = '\0';
  // target_ зберігається між входами (зручно повторювати запит)
}

bool HttpGetApp::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

void HttpGetApp::do_fetch() {
  done_ = false;
  code_ = 0;
  body_[0] = '\0';
  if (!connected() || target_[0] == '\0') return;

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(target_)) { code_ = -1; done_ = true; return; }
  code_ = http.GET();               // >0: HTTP-код; <0: помилка клієнта
  if (code_ > 0) {
    String b = http.getString();
    // прев'ю: перші символи, переноси рядків -> пробіли (для компактного показу)
    size_t j = 0;
    for (size_t i = 0; i < b.length() && j < sizeof(body_) - 1; i++) {
      char c = b[i];
      body_[j++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    body_[j] = '\0';
  }
  http.end();
  done_ = true;
  Serial.printf("[HTTP] GET %s -> %d\n", target_, code_);
}

void HttpGetApp::loop() {
  if (fetch_phase_ == 1) { fetch_phase_ = 2; return; }   // кадр "Fetching"
  if (fetch_phase_ == 2) { do_fetch(); fetch_phase_ = 0; }
}

void HttpGetApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "url") == 0 && value) {
    if (http_normalize_url(value, target_, sizeof(target_)) && connected())
      fetch_phase_ = 1;   // отримали URL -> одразу запит
  }
}

void HttpGetApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("HTTP GET", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(connected() ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(connected() ? "link" : "down", SCR_W - 6, 3, 2);

  spr.setTextDatum(TL_DATUM);
  if (!connected()) {
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  // Цільовий URL
  spr.setTextColor(C_DIM, C_BG);
  if (target_[0] == '\0') {
    spr.drawString("URL: (vvedy z telefona)", 8, 20, 1);
  } else {
    char u[40]; snprintf(u, sizeof(u), "%.38s", target_);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(u, 8, 20, 1);
  }

  if (fetch_phase_ != 0) {
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("Fetching...", 8, 44, 2);
  } else if (done_) {
    // Статус
    char s[24];
    if (code_ > 0) snprintf(s, sizeof(s), "Status: %d", code_);
    else           snprintf(s, sizeof(s), "Error: %d", code_);
    spr.setTextColor(code_ >= 200 && code_ < 400 ? C_GOOD : C_BAD, C_BG);
    spr.drawString(s, 8, 34, 2);
    // Прев'ю тіла з перенесенням у рядки ~40 симв.
    spr.setTextColor(C_DIM, C_BG);
    const int WRAP = 40, MAXROWS = 4;
    int len = strlen(body_), y = 54;
    for (int row = 0; row < MAXROWS && row * WRAP < len; row++) {
      char line[WRAP + 1];
      int off = row * WRAP;
      int cpy = (len - off < WRAP) ? (len - off) : WRAP;
      memcpy(line, body_ + off, cpy); line[cpy] = '\0';
      spr.drawString(line, 8, y, 1); y += 12;
    }
  }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(target_[0] ? "S5=repeat  S2=exit" : "S2=exit", 8, SCR_H - 12, 1);
}

void HttpGetApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5 && connected() && target_[0] && fetch_phase_ == 0)
    fetch_phase_ = 1;   // повторити запит
}

std::string HttpGetApp::remote_state() {
  char lines[4][64];
  int n = 0;
  snprintf(lines[n++], 64, "URL: %.56s", target_[0] ? target_ : "(none)");
  if (done_) {
    snprintf(lines[n++], 64, code_ > 0 ? "Status: %d" : "Error: %d", code_);
    snprintf(lines[n++], 64, "%.60s", body_);
  }
  const char* items[4];
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("http_get", items, n, -1);
}
