#include <Arduino.h>
#include <stdio.h>
#include "rfid_access.h"
#include "../drivers/display.h"
#include "../drivers/uno_link.h"
#include "../drivers/rfid_acl.h"
#include "../remote/protocol.h"

void RfidAccessApp::init() {
  wants_exit_ = false;
  last_seq_ = uno_link_rfid_seq();
  // Стійкість до таймінгу: показуємо ОСТАННЮ тапнуту картку одразу (не лише нові тапи).
  cur_uid_ = uno_link_last_rfid();
  has_tap_ = (cur_uid_ != 0);
  granted_ = has_tap_ && rfid_acl_contains(cur_uid_);
  just_enrolled_ = false;
  uno_link_send_scan();
}

void RfidAccessApp::loop() {
  uint32_t seq = uno_link_rfid_seq();
  if (seq != last_seq_) {
    last_seq_ = seq;
    cur_uid_ = uno_link_last_rfid();
    granted_ = rfid_acl_contains(cur_uid_);
    has_tap_ = true;
    just_enrolled_ = false;
  }
}

void RfidAccessApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("RFID ACCESS", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  char cnt[16]; snprintf(cnt, sizeof(cnt), "list:%d", rfid_acl_count());
  spr.drawString(cnt, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!has_tap_) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Priklady kartu", 8, 44, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S5=vnesty  S1=chystyty  S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  char b[32];
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "UID: %08lX", (unsigned long)cur_uid_);
  spr.drawString(b, 8, 22, 2);

  spr.setTextColor(granted_ ? C_GOOD : C_BAD, C_BG);
  spr.drawString(granted_ ? "GRANTED" : "DENIED", 8, 48, 6);

  spr.setTextColor(just_enrolled_ ? C_GOOD : C_ACCENT, C_BG);
  spr.drawString(just_enrolled_ ? "vneseno v spysok" : "S5=vnesty  S1=chystyty  S2=exit", 8, SCR_H - 12, 1);
}

void RfidAccessApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S1) { rfid_acl_clear(); granted_ = rfid_acl_contains(cur_uid_); just_enrolled_ = false; return; }
  if (id == BTN_S5) {
    if (has_tap_ && cur_uid_) {
      rfid_acl_add(cur_uid_);
      granted_ = true; just_enrolled_ = true;
    }
  }
}

std::string RfidAccessApp::remote_state() {
  char lines[4][40];
  const char* items[4];
  int n = 0;
  snprintf(lines[0], 40, "Whitelist: %d cards", rfid_acl_count()); items[n++] = lines[0];
  if (!has_tap_) {
    static const char* tap = "Tap a card...";
    items[n++] = tap;
  } else {
    snprintf(lines[1], 40, "UID: %08lX", (unsigned long)cur_uid_); items[n++] = lines[1];
    snprintf(lines[2], 40, "%s", granted_ ? "ACCESS GRANTED" : "ACCESS DENIED"); items[n++] = lines[2];
    snprintf(lines[3], 40, "%s", just_enrolled_ ? "enrolled" : "S5=enroll  S1=clear"); items[n++] = lines[3];
  }
  return protocol_build_menu("rfid_access", items, n, -1);
}
