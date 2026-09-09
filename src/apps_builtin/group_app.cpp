#include <Arduino.h>
#include "group_app.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"
#include "../kernel/app_snapshot.h"   // авто-снапшот члена на SD при виході (апки в групі не йдуть у лаунчер)

void GroupApp::configure(const char* name, const char* page, const Member* members, int count) {
  name_ = name; page_ = page; members_ = members; count_ = count;
}

void GroupApp::init() { mode_ = -1; cursor_ = 0; wants_exit_ = false; }

void GroupApp::enter(int i) {
  if (i < 0 || i >= count_) return;
  mode_ = i;
  members_[i].app->init();
}

void GroupApp::loop() { if (mode_ >= 0) members_[mode_].app->loop(); }

void GroupApp::draw() {
  if (mode_ >= 0) { members_[mode_].app->draw(); return; }  // делегуємо активному члену

  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString(name_, 6, 3, 2);

  int y = 22, dy = 20;
  for (int i = 0; i < count_; i++) {
    bool s = (i == cursor_);
    if (s) spr.fillRoundRect(4, y - 2, SCR_W - 8, dy - 2, 3, C_PANEL);
    spr.setTextColor(s ? C_ACCENT : C_TEXT, s ? C_PANEL : C_BG);
    spr.drawString(members_[i].label, 10, y, 2);
    y += dy;
  }
  bool sb = (cursor_ == count_);
  if (sb) spr.fillRoundRect(4, y - 2, SCR_W - 8, dy - 2, 3, C_PANEL);
  spr.setTextColor(sb ? C_ACCENT : C_DIM, sb ? C_PANEL : C_BG);
  spr.drawString("< Back", 10, y, 2);

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=open", 8, SCR_H - 12, 1);
}

void GroupApp::button(ButtonId id) {
  if (mode_ >= 0) {
    members_[mode_].app->button(id);
    if (members_[mode_].app->wants_exit()) {   // член попросив вихід -> назад у селектор
      app_snapshot_if_enabled(members_[mode_].app);
      members_[mode_].app->on_exit();
      mode_ = -1;
    }
    return;
  }
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % (count_ + 1);
  else if (id == BTN_S1) cursor_ = (cursor_ + count_) % (count_ + 1);
  else if (id == BTN_S5) { if (cursor_ == count_) wants_exit_ = true; else enter(cursor_); }
}

void GroupApp::select_index(int idx) {
  if (mode_ >= 0) { members_[mode_].app->select_index(idx); return; }
  if (idx == count_) wants_exit_ = true;                 // тап по "< Back" у веб
  else if (idx >= 0 && idx < count_) enter(idx);
}

void GroupApp::text(const char* field, const char* value) {
  if (mode_ >= 0) members_[mode_].app->text(field, value);
}

void GroupApp::on_exit() {
  if (mode_ >= 0) {
    app_snapshot_if_enabled(members_[mode_].app);
    members_[mode_].app->on_exit();                        // акуратно закрити активний член
  }
  mode_ = -1;
}

bool GroupApp::handle_back() {
  // Ієрархічний назад: з члена -> назад у СЕЛЕКТОР групи (а не аж у лаунчер).
  // Із самого селектора (mode_<0) -> false: ядро виходить у лаунчер.
  if (mode_ >= 0) {
    // Спершу даємо члену шанс спожити назад самому (напр. вкладений стан).
    if (members_[mode_].app->handle_back()) return true;
    app_snapshot_if_enabled(members_[mode_].app);
    members_[mode_].app->on_exit();
    mode_ = -1;
    return true;
  }
  return false;
}

std::string GroupApp::remote_state() {
  if (mode_ >= 0) return members_[mode_].app->remote_state();  // дзеркалимо активний член як є
  const char* items[9];
  int n = 0;
  for (int i = 0; i < count_ && n < 8; i++) items[n++] = members_[i].label;
  items[n++] = "< Back";
  return protocol_build_menu(page_, items, n, cursor_);
}
