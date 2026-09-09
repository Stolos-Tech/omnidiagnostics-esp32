#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include "self_test.h"
#include "../drivers/display.h"
#include "../drivers/report_store.h"
#include "../remote/protocol.h"

void SelfTestApp::init() {
  wants_exit_ = false;
  phase_ = IDLE;
  cur_ = 0; saved_ = 0; stage_inited_ = false;
  results_count_ = 0; view_ = 0;
}

void SelfTestApp::enter_done() {
  phase_ = DONE;
  view_ = 0;  // почати з підсумкової сторінки браузера
}

// Виділяє буфер результатів на купі (за потреби) і запускає прогін. Якщо купа
// не дала памʼяті — прогін однаково йде, звіти пишуться в /reports, лише
// гортабельний перегляд на екрані буде недоступний (results_count_ лишиться 0).
void SelfTestApp::start_run() {
  if (!results_ && count_ > 0) results_ = (Res*)calloc((size_t)count_, sizeof(Res));
  phase_ = RUNNING; cur_ = 0; saved_ = 0; stage_inited_ = false;
  results_count_ = 0; view_ = 0;
}

// Вихід у лаунчер. Якщо прогін ще йшов — акуратно закрити модуль, що саме
// працював (інакше його ресурси, напр. сокети Net Scan, течуть: back-жест іде
// повз S2-стоп, який це робив). Далі звільняємо буфер результатів.
void SelfTestApp::on_exit() {
  if (phase_ == RUNNING && stage_inited_ && targets_ && cur_ < count_)
    targets_[cur_].app->on_exit();
  if (results_) { free(results_); results_ = nullptr; }
  results_count_ = 0; view_ = 0; phase_ = IDLE; stage_inited_ = false;
}

// Знімає remote_state() поточного цільового застосунку, дістає items і зберігає
// як звіт у /reports (report_save пише тіло напряму, жодних HTTP/form-нюансів),
// а заодно тримає компактний зріз рядків у RAM для браузера результатів у кінці.
void SelfTestApp::save_current() {
  const SelfTestTarget& t = targets_[cur_];
  std::string js = t.app->remote_state();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, js);

  Res* r = (results_ && cur_ < count_) ? &results_[cur_] : nullptr;
  if (r) { snprintf(r->name, sizeof(r->name), "%s", t.app->name()); r->n = 0; }

  String body = String(t.app->name());
  body += "\n\n";
  if (!err && doc["items"].is<JsonArray>()) {
    for (JsonVariant v : doc["items"].as<JsonArray>()) {
      const char* s = v.as<const char*>();
      if (!s) continue;
      body += s; body += "\n";
      if (r && r->n < ST_LINES) { snprintf(r->line[r->n], ST_LLEN, "%s", s); r->n++; }
    }
  } else {
    body += "(result unavailable)\n";
    if (r && r->n < ST_LINES) { snprintf(r->line[r->n], ST_LLEN, "(result unavailable)"); r->n++; }
  }
  bool ok = report_save(t.slug, body.c_str());
  if (ok) saved_++;
  if (r) { r->ok = ok; results_count_ = cur_ + 1; }  // рахуємо лише збережені зрізи
  Serial.printf("[SELFTEST] %s -> %s (%d/%d)\n", t.app->name(), t.slug, cur_ + 1, count_);
}

void SelfTestApp::loop() {
  if (phase_ != RUNNING) return;
  if (!targets_ || cur_ >= count_) { enter_done(); return; }

  const SelfTestTarget& t = targets_[cur_];
  if (!stage_inited_) {
    t.app->init();
    if (t.field) t.app->text(t.field, t.value);
    stage_start_ = millis();
    stage_inited_ = true;
    return;
  }
  t.app->loop();                                       // ганяємо реальну логіку модуля
  if (millis() - stage_start_ >= (uint32_t)t.seconds * 1000UL) {
    save_current();
    t.app->on_exit();
    cur_++;
    stage_inited_ = false;
    if (cur_ >= count_) enter_done();
  }
}

void SelfTestApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("SELF TEST", 6, 3, 2);
  char b[48];

  if (!targets_ || count_ == 0) {
    spr.setTextColor(C_BAD, C_BG); spr.drawString("Not configured", 8, 34, 2);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2 = exit", 8, SCR_H - 14, 1);
    return;
  }

  if (phase_ == IDLE) {
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "Run %d modules", count_);
    spr.drawString(b, 8, 28, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Report each -> /reports", 8, 50, 1);
    spr.drawString("Bot will sync to Telegram", 8, 64, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5 = START   S2 = exit", 8, SCR_H - 14, 2);
    return;
  }

  if (phase_ == RUNNING) {
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "Test %d / %d", cur_ + 1, count_);
    spr.drawString(b, 8, 24, 2);
    const char* nm = (cur_ < count_) ? targets_[cur_].app->name() : "";
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString(nm, 8, 46, 2);
    int gx = 8, gy = 72, gw = SCR_W - 16, gh = 12;
    spr.drawRect(gx, gy, gw, gh, C_GRID);
    spr.fillRect(gx + 1, gy + 1, (gw - 2) * cur_ / (count_ ? count_ : 1), gh - 2, C_ACCENT);
    spr.setTextColor(C_DIM, C_BG);
    snprintf(b, sizeof(b), "saved: %d   S2=stop", saved_);
    spr.drawString(b, 8, 92, 1);
    return;
  }

  // DONE — гортабельний браузер результатів (view_ 0 = підсумок, далі — модулі)
  if (view_ == 0) {
    spr.setTextColor(C_GOOD, C_BG); spr.drawString("Done!", 8, 22, 4);
    snprintf(b, sizeof(b), "Reports: %d / %d", saved_, count_);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 8, 54, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S2 = scroll results", 8, 78, 1);
    spr.drawString("Bot will sync to Telegram", 8, 92, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5=repeat  LEFT(hold)=exit", 8, SCR_H - 12, 1);
    return;
  }
  {
    const Res& r = results_[view_ - 1];
    snprintf(b, sizeof(b), "[%d/%d] %s", view_, results_count_, r.name);
    spr.setTextColor(r.ok ? C_ACCENT : C_BAD, C_BG);
    spr.drawString(b, 8, 20, 2);
    int y = 40;
    spr.setTextColor(C_TEXT, C_BG);
    for (int i = 0; i < r.n && y < SCR_H - 14; i++) { spr.drawString(r.line[i], 8, y, 1); y += 12; }
    if (r.n == 0) { spr.setTextColor(C_DIM, C_BG); spr.drawString("(empty)", 8, 40, 1); }
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S2=next  S5=repeat", 8, SCR_H - 12, 1);
  }
}

void SelfTestApp::button(ButtonId id) {
  if (phase_ == RUNNING) {
    if (id == BTN_S2) {                                // стоп: акуратно закрити поточний модуль
      if (stage_inited_ && cur_ < count_) targets_[cur_].app->on_exit();
      enter_done();
    }
    return;
  }
  if (phase_ == DONE) {
    if (id == BTN_S2) {                                // гортати: підсумок + кожен модуль по колу
      view_ = (view_ + 1) % (results_count_ + 1);
    } else if (id == BTN_S5) {                         // повторити прогін
      start_run();
    }
    return;                                            // вихід — жестом back (довга-ліва / веб "Back")
  }
  // IDLE
  if (id == BTN_S5) {
    start_run();
  } else if (id == BTN_S2) {
    wants_exit_ = true;
  }
}

std::string SelfTestApp::remote_state() {
  static char l0[48], l1[48];
  if (phase_ == IDLE) {
    snprintf(l0, sizeof(l0), "Self Test: %d modules", count_);
    snprintf(l1, sizeof(l1), "S5 = start");
    const char* items[2] = { l0, l1 };
    return protocol_build_menu("self_test", items, 2, -1);
  }
  if (phase_ == RUNNING) {
    snprintf(l0, sizeof(l0), "Test %d/%d", cur_ + 1, count_);
    snprintf(l1, sizeof(l1), "%s", (cur_ < count_) ? targets_[cur_].app->name() : "");
    const char* items[2] = { l0, l1 };
    return protocol_build_menu("self_test", items, 2, -1);
  }
  // DONE — дзеркалимо поточну сторінку браузера результатів (S2 гортає її і у вебі)
  const char* items[ST_LINES + 1];
  int n = 0;
  if (view_ == 0) {
    snprintf(l0, sizeof(l0), "Done: %d/%d reports  (S2=scroll)", saved_, count_);
    snprintf(l1, sizeof(l1), "S5=repeat  back=exit");
    items[n++] = l0; items[n++] = l1;
  } else {
    const Res& r = results_[view_ - 1];
    snprintf(l0, sizeof(l0), "[%d/%d] %s%s", view_, results_count_, r.name, r.ok ? "" : " (!)");
    items[n++] = l0;
    for (int i = 0; i < r.n && n < ST_LINES + 1; i++) items[n++] = r.line[i];
  }
  return protocol_build_menu("self_test", items, n, -1);
}
