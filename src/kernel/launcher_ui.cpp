#include "launcher_ui.h"
#include "../drivers/display.h"

void launcher_draw(const LauncherModel& model) {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);

  // Верхня панель
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("ESP32-OS", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString("v0.1", SCR_W - 6, 3, 2);

  // Список застосунків: до 5 видимих рядків, вікно ковзає за курсором
  const int ROW_H = 20;
  const int VISIBLE = 5;
  int first = 0;
  if (model.count() > VISIBLE) {
    first = model.cursor() - VISIBLE / 2;
    if (first < 0) first = 0;
    if (first > model.count() - VISIBLE) first = model.count() - VISIBLE;
  }

  for (int row = 0; row < VISIBLE; row++) {
    int idx = first + row;
    if (idx >= model.count()) break;
    int y = 20 + row * ROW_H;
    bool sel = (idx == model.cursor());
    if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
    spr.drawString(model.item_name(idx), 12, y + 2, 2);
    if (sel) {
      spr.setTextDatum(TR_DATUM);
      spr.drawString(">", SCR_W - 12, y + 2, 2);
    }
  }

  // Підказка керування (рідні кнопки плати)
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("RIGHT=next  LEFT=run", 8, SCR_H - 12, 1);
}
