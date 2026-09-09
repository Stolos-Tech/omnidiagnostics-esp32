#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "http_post.h"
#include "../kernel/url_util.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

void HttpPostApp::init() {
  wants_exit_ = false;
  phase_ = 0;
  done_ = false;
  code_ = 0;
  resp_[0] = '\0';
}

bool HttpPostApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void HttpPostApp::do_post() {
  done_ = false; code_ = 0; resp_[0] = '\0';
  if (!connected() || target_[0] == '\0') return;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(target_)) { code_ = -1; done_ = true; return; }
  http.addHeader("Content-Type", "application/json");
  code_ = http.POST((uint8_t*)body_, strlen(body_));
  if (code_ > 0) {
    String b = http.getString();
    size_t j = 0;
    for (size_t i = 0; i < b.length() && j < sizeof(resp_) - 1; i++) {
      char c = b[i];
      resp_[j++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    resp_[j] = '\0';
  }
  http.end();
  done_ = true;
  Serial.printf("[HTTP] POST %s (%d b) -> %d\n", target_, (int)strlen(body_), code_);
}

void HttpPostApp::loop() {
  if (phase_ == 1) { phase_ = 2; return; }
  if (phase_ == 2) { do_post(); phase_ = 0; }
}

void HttpPostApp::text(const char* field, const char* value) {
  if (!field || !value) return;
  if (strcmp(field, "url") == 0) {
    http_normalize_url(value, target_, sizeof(target_));
  } else if (strcmp(field, "body") == 0) {
    snprintf(body_, sizeof(body_), "%s", value);
    if (connected() && target_[0]) phase_ = 1;   // тіло отримано -> надіслати
  }
}

void HttpPostApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("HTTP POST", 6, 3, 2);
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

  char b[44];
  spr.setTextColor(target_[0] ? C_TEXT : C_DIM, C_BG);
  snprintf(b, sizeof(b), "URL: %.34s", target_[0] ? target_ : "(z telefona)");
  spr.drawString(b, 8, 20, 1);
  spr.setTextColor(body_[0] ? C_TEXT : C_DIM, C_BG);
  snprintf(b, sizeof(b), "Body: %.33s", body_[0] ? body_ : "(z telefona)");
  spr.drawString(b, 8, 32, 1);

  if (phase_ != 0) {
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("Sending...", 8, 48, 2);
  } else if (done_) {
    if (code_ > 0) snprintf(b, sizeof(b), "Status: %d", code_);
    else           snprintf(b, sizeof(b), "Error: %d", code_);
    spr.setTextColor(code_ >= 200 && code_ < 400 ? C_GOOD : C_BAD, C_BG);
    spr.drawString(b, 8, 46, 2);
    spr.setTextColor(C_DIM, C_BG);
    const int WRAP = 40, ROWS = 3; int len = strlen(resp_), y = 66;
    for (int r = 0; r < ROWS && r * WRAP < len; r++) {
      char line[WRAP + 1]; int off = r * WRAP;
      int cpy = (len - off < WRAP) ? (len - off) : WRAP;
      memcpy(line, resp_ + off, cpy); line[cpy] = '\0';
      spr.drawString(line, 8, y, 1); y += 12;
    }
  }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(target_[0] && body_[0] ? "S5=repeat  S2=exit" : "S2=exit", 8, SCR_H - 12, 1);
}

void HttpPostApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5 && connected() && target_[0] && phase_ == 0) phase_ = 1;
}

std::string HttpPostApp::remote_state() {
  char l[3][80];
  snprintf(l[0], sizeof(l[0]), "URL: %.60s", target_[0] ? target_ : "(none)");
  const char* items[3] = { l[0], l[1], l[2] };
  int n = 1;
  if (done_) {
    snprintf(l[1], sizeof(l[1]), code_ > 0 ? "Status: %d" : "Error: %d", code_);
    snprintf(l[2], sizeof(l[2]), "%.70s", resp_);
    n = 3;
  }
  return protocol_build_menu("http_post", items, n, -1);
}
