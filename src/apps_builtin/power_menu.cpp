#include <Arduino.h>
#include "power_menu.h"
#include "../drivers/display.h"
#include "../kernel/power.h"
#include "../kernel/sys_ctl.h"
#include "../remote/protocol.h"

static const char* OPTS[3] = { "Passive (screen off)", "Reboot", "Power Off (sleep)" };

void PowerApp::init() { sel_ = 0; wants_exit_ = false; }

void PowerApp::act(int i) {
  if (i == 0) {
    // Пасивний режим: гасимо екран і виходимо в лаунчер (він далі не малюється,
    // бо в пасиві main пропускає рендер). Плата/мережа/WS працюють. Вихід — фіз. кнопка.
    sys_set_passive(true);
    wants_exit_ = true;
  } else if (i == 1) {
    sys_request_reboot();          // main зробить ESP.restart() у безпечній точці
  } else if (i == 2) {
    power_off();                   // deep sleep (не повертається)
  }
}

void PowerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("POWER", 6, 3, 2);
  int y = 28, dy = 26;
  for (int i = 0; i < 3; i++) {
    bool s = (i == sel_);
    if (s) spr.fillRoundRect(6, y - 3, SCR_W - 12, dy - 4, 3, C_PANEL);
    spr.setTextColor(s ? C_ACCENT : C_TEXT, s ? C_PANEL : C_BG);
    spr.drawString(OPTS[i], 12, y, 2);
    y += dy;
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 14, 1);
}

void PowerApp::button(ButtonId id) {
  if (id == BTN_S2)      sel_ = (sel_ + 1) % 3;
  else if (id == BTN_S5) act(sel_);
}

void PowerApp::select_index(int idx) { if (idx >= 0 && idx < 3) { sel_ = idx; act(idx); } }

std::string PowerApp::remote_state() {
  const char* items[3] = { OPTS[0], OPTS[1], OPTS[2] };
  return protocol_build_menu("power", items, 3, sel_);
}
