#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include <stdio.h>
#include "http_sec.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

// Повні імена заголовків (для collectHeaders) і короткі підписи (для екрана).
static const char* SEC_KEYS[HttpSecApp::NH] = {
  "Strict-Transport-Security", "Content-Security-Policy", "X-Frame-Options",
  "X-Content-Type-Options", "Referrer-Policy", "Permissions-Policy"
};
static const char* SEC_LBL[HttpSecApp::NH] = { "HSTS", "CSP", "XFO", "XCTO", "Ref-Pol", "Perm-Pol" };

bool HttpSecApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void HttpSecApp::run_check() {
  for (int i = 0; i < NH; i++) present_[i] = false;
  server_[0] = 0; code_ = 0; have_result_ = false;

  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = strncmp(url_, "https", 5) == 0;
  bool ok;
  if (https) { secure.setInsecure(); ok = http.begin(secure, url_); }
  else       { ok = http.begin(plain, url_); }
  if (!ok) { snprintf(server_, sizeof(server_), "begin failed"); phase_ = DONE; return; }

  http.setTimeout(8000);
  const char* keys[HttpSecApp::NH + 1];
  for (int i = 0; i < NH; i++) keys[i] = SEC_KEYS[i];
  keys[NH] = "Server";
  http.collectHeaders(keys, NH + 1);

  int code = http.GET();
  code_ = code;
  if (code > 0) {
    for (int i = 0; i < NH; i++) present_[i] = http.header(SEC_KEYS[i]).length() > 0;
    snprintf(server_, sizeof(server_), "%.38s", http.header("Server").c_str());
    have_result_ = true;
  } else {
    snprintf(server_, sizeof(server_), "%s", http.errorToString(code).c_str());
  }
  http.end();
  phase_ = DONE;
}

void HttpSecApp::init() {
  wants_exit_ = false;
  if (!connected()) { phase_ = NOT_CONN; return; }
  phase_ = CHECKING;
}

void HttpSecApp::loop() {
  if (phase_ == CHECKING) run_check();
}

void HttpSecApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("HTTP SEC", 6, 3, 2);
  char b[64];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString(code_ ? (String("HTTP ") + code_).c_str() : "", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG); spr.drawString(url_, 8, 18, 1);

  if (phase_ == CHECKING) { spr.setTextColor(C_TEXT, C_BG); spr.drawString("Zapyt...", 8, 44, 2); return; }

  if (!have_result_) {
    spr.setTextColor(C_BAD, C_BG); spr.drawString("Zapyt ne vdavsja", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(server_, 8, 60, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5 = retry", 8, SCR_H - 12, 1);
    return;
  }

  int score = 0;
  for (int i = 0; i < NH; i++) if (present_[i]) score++;
  // сітка 2 колонки × 3 рядки заголовків
  for (int i = 0; i < NH; i++) {
    int col = i % 2, row = i / 2;
    int x = 8 + col * 116, y = 30 + row * 16;
    spr.setTextColor(present_[i] ? C_GOOD : C_BAD, C_BG);
    snprintf(b, sizeof(b), "%s %s", present_[i] ? "+" : "-", SEC_LBL[i]);
    spr.drawString(b, x, y, 1);
  }
  spr.setTextColor(score >= 5 ? C_GOOD : score >= 3 ? C_WARN : C_BAD, C_BG);
  snprintf(b, sizeof(b), "Score: %d/%d", score, NH);
  spr.drawString(b, 8, 84, 2);
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "Server: %.28s", server_[0] ? server_ : "?");
  spr.drawString(b, 8, 104, 1);
  spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5=again", 8, SCR_H - 11, 1);
}

void HttpSecApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S5) phase_ = CHECKING;
}

void HttpSecApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "url") == 0 && value && value[0]) {
    snprintf(url_, sizeof(url_), "%s", value);
    if (connected()) phase_ = CHECKING;
  }
}

std::string HttpSecApp::remote_state() {
  if (phase_ == CHECKING) {
    const char* it[1] = { "Zapyt..." };
    return protocol_build_menu("http_sec", it, 1, -1);
  }
  char lines[4][40];
  int score = 0; for (int i = 0; i < NH; i++) if (present_[i]) score++;
  snprintf(lines[0], 40, "HTTP %d  score %d/%d", code_, score, NH);
  snprintf(lines[1], 40, "%c HSTS %c CSP %c XFO",
           present_[0] ? '+' : '-', present_[1] ? '+' : '-', present_[2] ? '+' : '-');
  snprintf(lines[2], 40, "%c XCTO %c Ref %c Perm",
           present_[3] ? '+' : '-', present_[4] ? '+' : '-', present_[5] ? '+' : '-');
  snprintf(lines[3], 40, "srv %.30s", server_[0] ? server_ : "?");
  const char* items[4] = { lines[0], lines[1], lines[2], lines[3] };
  return protocol_build_menu("http_sec", items, 4, -1);
}
