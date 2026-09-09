#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "captive_portal.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

#define PROBE_URL "http://connectivitycheck.gstatic.com/generate_204"

bool CaptivePortalApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void CaptivePortalApp::run_check() {
  detail_[0] = 0; code_ = 0;
  HTTPClient http;
  WiFiClient client;
  if (!http.begin(client, PROBE_URL)) { verdict_ = V_NONET; snprintf(detail_, sizeof(detail_), "begin failed"); phase_ = DONE; return; }
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(5000);
  const char* keys[] = { "Location" };
  http.collectHeaders(keys, 1);
  int code = http.GET();
  code_ = code;
  if (code <= 0) {
    verdict_ = V_NONET;
    snprintf(detail_, sizeof(detail_), "%s", http.errorToString(code).c_str());
  } else if (code == 204) {
    verdict_ = V_OPEN;
    snprintf(detail_, sizeof(detail_), "204 No Content - clean");
  } else {
    verdict_ = V_CAPTIVE;
    String loc = http.header("Location");
    if (loc.length()) {
      snprintf(portal_, sizeof(portal_), "%s", loc.c_str());
      snprintf(detail_, sizeof(detail_), "-> %.58s", loc.c_str());
    } else {
      // портал підмінив саму відповідь (без редіректу) — вітальна на тому ж URL
      snprintf(portal_, sizeof(portal_), "%s", PROBE_URL);
      int len = http.getSize();
      snprintf(detail_, sizeof(detail_), "code %d, body %d B", code, len);
    }
  }
  http.end();
  phase_ = DONE;
}

// Резолвить відносний URL rel відносно base -> out (best-effort, http-портали).
static void resolve_url(const char* base, const char* rel, char* out, size_t cap) {
  if (!rel || !rel[0]) { snprintf(out, cap, "%s", base); return; }
  if (strncasecmp(rel, "http", 4) == 0) { snprintf(out, cap, "%s", rel); return; }
  char host[96] = ""; const char* p = strstr(base, "://");
  int hn = 0;
  if (p) { p += 3; while (p[hn] && p[hn] != '/' && hn < (int)sizeof(host) - 1) { host[hn] = p[hn]; hn++; } host[hn] = 0; }
  if (rel[0] == '/') { snprintf(out, cap, "http://%s%s", host, rel); return; }
  char dir[160]; snprintf(dir, sizeof(dir), "%s", base);
  char* slash = strrchr(p ? (dir + (int)(p - base)) : dir, '/');
  if (slash) *(slash + 1) = 0;
  snprintf(out, cap, "%s%s", dir, rel);
}

// Best-effort авто-клік вітальної сторінки: завантажити портал, знайти першу
// <form> і сабмітнути її (типовий "I accept" часто без обовʼязкових полів),
// потім повторно перевірити 204. Складні портали (логін/SMS/JS/HTTPS) чесно
// звітують "manual login". Пуш-підтвердження неможливе (не наш пристрій).
void CaptivePortalApp::try_accept() {
  // 1. Завантажити вітальну сторінку у фіксований буфер (heap-safe).
  HTTPClient http; WiFiClient client;
  if (!http.begin(client, portal_)) { snprintf(detail_, sizeof(detail_), "portal begin failed"); phase_ = DONE; return; }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(6000);
  int code = http.GET();
  if (code <= 0) { snprintf(detail_, sizeof(detail_), "portal GET fail"); http.end(); phase_ = DONE; return; }
  // Буфер вітальної сторінки — на КУПІ лише на час розбору (не тримаємо 4КБ статики
  // постійно; авто-клік — рідка одноразова дія). Звільняємо перед виходом із функції.
  const int BUFN = 4096;
  char* buf = (char*)malloc(BUFN);
  if (!buf) { snprintf(detail_, sizeof(detail_), "low RAM for parse"); http.end(); phase_ = DONE; return; }
  int n = 0; WiFiClient* st = http.getStreamPtr(); uint32_t t0 = millis();
  while (http.connected() && n < BUFN - 1 && millis() - t0 < 4000) {
    while (st->available() && n < BUFN - 1) buf[n++] = (char)st->read();
    if (!st->available()) delay(10);
  }
  buf[n] = 0;
  http.end();

  // 2. Знайти першу <form action=... method=...> (пошук без урахування регістру).
  String body(buf); free(buf);          // вміст скопійовано в String -> буфер більше не потрібен
  String low = body; low.toLowerCase();
  int fpos = low.indexOf("<form");
  char action[160] = ""; bool post = false; bool submitted = false;
  if (fpos >= 0) {
    int tagend = low.indexOf('>', fpos); if (tagend < 0) tagend = fpos + 200;
    String tag = body.substring(fpos, tagend), tagl = low.substring(fpos, tagend);
    int mp = tagl.indexOf("method="); if (mp >= 0 && tagl.indexOf("post") >= 0) post = true;
    int ap = tagl.indexOf("action=");
    if (ap >= 0) {
      int q1 = tag.indexOf('"', ap), q2 = q1 >= 0 ? tag.indexOf('"', q1 + 1) : -1;
      if (q1 >= 0 && q2 > q1) resolve_url(portal_, tag.substring(q1 + 1, q2).c_str(), action, sizeof(action));
    }
    if (!action[0]) snprintf(action, sizeof(action), "%s", portal_);   // форма без action -> той самий URL
    // 3. Сабмітнути форму (без полів).
    HTTPClient h2; WiFiClient c2;
    if (h2.begin(c2, action)) {
      h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); h2.setTimeout(6000);
      int rc = post ? h2.POST((uint8_t*)"", 0) : h2.GET();
      submitted = (rc > 0);
      h2.end();
    }
  }

  // 4. Повторна проба 204 — чи відкрився інтернет.
  HTTPClient h3; WiFiClient c3; int rc204 = -1;
  if (h3.begin(c3, PROBE_URL)) { h3.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); h3.setTimeout(5000); rc204 = h3.GET(); h3.end(); }
  code_ = rc204;
  if (rc204 == 204)      { verdict_ = V_OPEN;    snprintf(detail_, sizeof(detail_), "auto-accepted!"); }
  else if (fpos < 0)     { verdict_ = V_CAPTIVE; snprintf(detail_, sizeof(detail_), "no form: manual login"); }
  else if (!submitted)   { verdict_ = V_CAPTIVE; snprintf(detail_, sizeof(detail_), "submit fail: manual"); }
  else                   { verdict_ = V_CAPTIVE; snprintf(detail_, sizeof(detail_), "still captive: manual"); }
  phase_ = DONE;
}

void CaptivePortalApp::init() {
  wants_exit_ = false;
  if (!connected()) { phase_ = NOT_CONN; return; }
  phase_ = CHECKING;   // сама проба — у loop (щоб намалювати "Checking" кадр)
}

void CaptivePortalApp::loop() {
  if (phase_ == CHECKING) run_check();
  else if (phase_ == ACCEPTING) try_accept();   // блокуючий best-effort, як runDiagnostics
}

void CaptivePortalApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("CAPTIVE PORTAL", 6, 3, 2);

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Run 'WiFi Setup' first", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == CHECKING) {
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Checking...", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString(PROBE_URL, 8, 62, 1);
    return;
  }
  if (phase_ == ACCEPTING) {
    spr.setTextColor(C_TEXT, C_BG); spr.drawString("Auto-accepting...", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString(portal_, 8, 62, 1);
    return;
  }

  // DONE
  const char* vt; uint16_t vc;
  if (verdict_ == V_OPEN)         { vt = "OPEN INTERNET"; vc = C_GOOD; }
  else if (verdict_ == V_CAPTIVE) { vt = "CAPTIVE PORTAL!";    vc = C_WARN; }
  else                            { vt = "NO NETWORK";       vc = C_BAD;  }
  spr.setTextColor(vc, C_BG); spr.drawString(vt, 8, 34, 4);
  char b[72];
  snprintf(b, sizeof(b), "HTTP %d", code_);
  spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 8, 66, 2);
  spr.setTextColor(C_DIM, C_BG); spr.drawString(detail_, 8, 88, 1);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString(verdict_ == V_CAPTIVE ? "S5=auto-accept  S2=exit" : "S5 = check again", 8, SCR_H - 12, 1);
}

void CaptivePortalApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) {
    if (phase_ == DONE && verdict_ == V_CAPTIVE) phase_ = ACCEPTING;  // best-effort авто-клік
    else                                          phase_ = CHECKING;   // перепробувати
  }
}

std::string CaptivePortalApp::remote_state() {
  if (phase_ == CHECKING)  { const char* it[1] = { "Checking..." };        return protocol_build_menu("captive", it, 1, -1); }
  if (phase_ == ACCEPTING) { const char* it[1] = { "Auto-accepting..." };  return protocol_build_menu("captive", it, 1, -1); }
  const char* vt = verdict_ == V_OPEN ? "Open Internet" : verdict_ == V_CAPTIVE ? "CAPTIVE PORTAL!" : "No network";
  char lines[4][48];
  int n = 0;
  snprintf(lines[n++], 48, "%s", vt);
  snprintf(lines[n++], 48, "HTTP %d  %.30s", code_, detail_);
  if (verdict_ == V_CAPTIVE) {
    snprintf(lines[n++], 48, "%.46s", portal_);
    snprintf(lines[n++], 48, "S5 = try auto-accept");
  }
  const char* items[4] = { lines[0], lines[1], lines[2], lines[3] };
  return protocol_build_menu("captive", items, n, -1);
}
