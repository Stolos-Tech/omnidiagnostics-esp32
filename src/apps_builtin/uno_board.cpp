#include <Arduino.h>
#include <stdio.h>
#include "uno_board.h"
#include "../drivers/display.h"
#include "../drivers/uno_link.h"
#include "../remote/protocol.h"

void UnoBoardApp::init() {
  wants_exit_ = false;
  page_ = PAGE_MODULES;   // спершу — знайдені модулі (самоопис хаба)
  last_rfid_seq_ = uno_link_rfid_seq();
  rfid_count_ = 0;
  uno_link_send_scan();   // попросити UNO переслати CAP (якщо ми підключились після її старту)
}

void UnoBoardApp::loop() {
  uint32_t seq = uno_link_rfid_seq();
  if (seq != last_rfid_seq_) { last_rfid_seq_ = seq; rfid_count_++; }
  uint32_t ir = uno_link_sensors().ir;   // латчимо останній ненульовий IR-код
  if (ir) last_ir_ = ir;
}


void UnoBoardApp::pageDots() {
  TFT_eSprite& spr = display_sprite();
  int cx = SCR_W / 2 - (PAGE_COUNT * 8) / 2;
  for (int i = 0; i < PAGE_COUNT; i++)
    spr.fillCircle(cx + i * 8 + 4, SCR_H - 5, i == page_ ? 3 : 2, i == page_ ? C_ACCENT : C_GRID);
}

void UnoBoardApp::drawModules() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("UNO: MODULES");
  int n = uno_link_module_count();
  spr.setTextDatum(TL_DATUM);
  if (n == 0) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("nema CAP (S5 = SCAN)", 8, 40, 2);
    return;
  }
  char b[40];
  int y = 22;
  for (int i = 0; i < n && y < SCR_H - 14; i++) {
    const UnoModule& m = uno_link_module(i);
    spr.setTextColor(m.present ? C_GOOD : C_DIM, C_BG);
    // слот, назва, presence, останній EVT (якщо є)
    if (m.value[0]) snprintf(b, sizeof(b), "%2d %-8.8s %s %.6s", m.slot, m.name, m.present ? "OK" : "--", m.value);
    else            snprintf(b, sizeof(b), "%2d %-8.8s %s", m.slot, m.name, m.present ? "OK" : "--");
    spr.drawString(b, 8, y, 1);
    y += 12;
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S5 = SCAN", 8, SCR_H - 13, 1);
}

void UnoBoardApp::drawSensors() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("UNO: SENSORS");
  const UnoSensors& s = uno_link_sensors();
  const UnoEnv& e = uno_link_env();
  char b[32];
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "Pot:  %d", s.pot);                     spr.drawString(b, 8, 22, 2);
  snprintf(b, sizeof(b), "Reed: %s", s.reed ? "CLOSED" : "open");  spr.drawString(b, 8, 42, 2);
  snprintf(b, sizeof(b), "IR:   %06lX", (unsigned long)s.ir);    spr.drawString(b, 8, 62, 2);
  snprintf(b, sizeof(b), "Temp: %.1f C", e.temp_x10 / 10.0f);    spr.drawString(b, 8, 82, 2);
  snprintf(b, sizeof(b), "Hum:  %.1f %%", e.hum_x10 / 10.0f);    spr.drawString(b, 8, 102, 2);
}

void UnoBoardApp::drawRfid() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("UNO: RFID (RC522)");
  uint32_t uid = uno_link_last_rfid();
  char b[32];
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
  if (uid) snprintf(b, sizeof(b), "UID: %08lX", (unsigned long)uid);
  else     snprintf(b, sizeof(b), "UID: -- nema skaniv --");
  spr.drawString(b, 8, 40, 2);
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "Skaniv: %d", rfid_count_);
  spr.drawString(b, 8, 64, 2);
}

void UnoBoardApp::drawIr() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("UNO: IR CAPTURE/REPLAY");
  spr.setTextDatum(TL_DATUM);
  char b[32];
  if (last_ir_) {
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "Last: %08lX", (unsigned long)last_ir_);
    spr.drawString(b, 8, 34, 4);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5 = replay (IR TX D6)", 8, SCR_H - 26, 2);
  } else {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Nema zahoplenoho IR", 8, 40, 2);
    spr.drawString("Napravte pult na D3 (pryymach)", 8, SCR_H - 22, 1);
  }
}

void UnoBoardApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("EXIT");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Vyity v launcher?", SCR_W / 2, SCR_H / 2 - 10, 4);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = exit   S2 = dali", SCR_W / 2, SCR_H / 2 + 20, 2);
}

void UnoBoardApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  switch (page_) {
    case PAGE_MODULES: drawModules(); break;
    case PAGE_SENSORS: drawSensors(); break;
    case PAGE_RFID:    drawRfid(); break;
    case PAGE_IR:      drawIr(); break;
    case PAGE_EXIT:    drawExit(); break;
  }
  pageDots();
}

void UnoBoardApp::button(ButtonId id) {
  if (id == BTN_S2) {
    page_ = (page_ + 1) % PAGE_COUNT;
  } else if (id == BTN_S1) {
    page_ = (page_ + PAGE_COUNT - 1) % PAGE_COUNT;
  } else if (id == BTN_S5) {
    switch (page_) {
      case PAGE_MODULES:
        uno_link_send_scan();   // перезапросити перелік модулів
        break;
      case PAGE_IR:
        uno_link_send_set(30, "");   // replay останнього захопленого IR (UNO slot 30)
        break;
      case PAGE_EXIT:
        wants_exit_ = true;
        break;
      default: break;
    }
  }
}

std::string UnoBoardApp::remote_state() {
  // Дзеркалимо ПОТОЧНУ сторінку (як на екрані плати) з ЖИВИМИ значеннями — раніше
  // мірор завжди показував лише список модулів, тож сенсори у вебі не рендерились і
  // не оновлювались. Тепер значення (pot/reed/temp...) змінюються щотіку -> JSON
  // змінюється -> ядро шле broadcast -> веб бачить сенсори в real-time.
  static char rows[UNO_MAX_MODULES + 2][40];
  const char* items[UNO_MAX_MODULES + 2];
  int k = 0;
  bool linked = uno_link_connected();
  const UnoSensors& s = uno_link_sensors();
  const UnoEnv& e = uno_link_env();

  switch (page_) {
    case PAGE_SENSORS:
      snprintf(rows[0], 40, "-- SENSORS%s --", linked ? "" : " (UNO offline)");
      snprintf(rows[1], 40, "Pot:  %d", s.pot);
      snprintf(rows[2], 40, "Reed: %s", s.reed ? "CLOSED" : "open");
      snprintf(rows[3], 40, "IR:   %06lX", (unsigned long)s.ir);
      snprintf(rows[4], 40, "Temp: %.1f C", e.temp_x10 / 10.0f);
      snprintf(rows[5], 40, "Hum:  %.1f %%", e.hum_x10 / 10.0f);
      for (int i = 0; i < 6; i++) items[k++] = rows[i];
      break;
    case PAGE_RFID:
      snprintf(rows[0], 40, "-- RFID (RC522) --");
      { uint32_t uid = uno_link_last_rfid();
        if (uid) snprintf(rows[1], 40, "UID: %08lX", (unsigned long)uid);
        else     snprintf(rows[1], 40, "UID: -- no scans --"); }
      snprintf(rows[2], 40, "Scans: %d", rfid_count_);
      for (int i = 0; i < 3; i++) items[k++] = rows[i];
      break;
    case PAGE_IR:
      snprintf(rows[0], 40, "-- IR CAPTURE/REPLAY --");
      if (last_ir_) { snprintf(rows[1], 40, "Last: %08lX", (unsigned long)last_ir_);
                      snprintf(rows[2], 40, "S5 = replay (IR TX D6)"); }
      else          { snprintf(rows[1], 40, "No IR captured");
                      snprintf(rows[2], 40, "Point remote at D3 (rx)"); }
      for (int i = 0; i < 3; i++) items[k++] = rows[i];
      break;
    case PAGE_EXIT:
      snprintf(rows[0], 40, "-- EXIT --");
      snprintf(rows[1], 40, "S5 = exit, S2 = next page");
      for (int i = 0; i < 2; i++) items[k++] = rows[i];
      break;
    case PAGE_MODULES:
    default: {
      int n = uno_link_module_count();
      snprintf(rows[0], 40, "-- MODULES%s --", linked ? "" : " (no CAP yet)");
      items[k++] = rows[0];
      for (int i = 0; i < n && k < UNO_MAX_MODULES + 1; i++) {
        const UnoModule& m = uno_link_module(i);
        snprintf(rows[k], 40, "%d %s [%s]%s%s", m.slot, m.name, m.present ? "OK" : "--",
                 m.value[0] ? " " : "", m.value);
        items[k] = rows[k]; k++;
      }
      if (n == 0) { snprintf(rows[k], 40, "S5=SCAN to request CAP"); items[k] = rows[k]; k++; }
      break;
    }
  }
  return protocol_build_menu("uno_board", items, k, -1);
}
