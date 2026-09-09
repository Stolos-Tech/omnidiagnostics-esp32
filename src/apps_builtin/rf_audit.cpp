#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "rf_audit.h"
#include "../drivers/display.h"
#include "../drivers/uno_link.h"
#include "../drivers/report_store.h"
#include "../remote/protocol.h"

static uint16_t verdict_color(const char* v) {
  if (!strcmp(v, "WIDE-OPEN")) return C_BAD;
  if (!strcmp(v, "PARTIAL"))   return C_WARN;
  if (!strcmp(v, "SECURED"))   return C_GOOD;
  return C_DIM;   // NOT-CLASSIC / інше
}

void RfAuditApp::init() {
  wants_exit_ = false;
  last_seq_ = uno_link_audit().seq;   // не показувати старий аудит як «новий»
  saved_ = false;
  uno_link_send_scan();               // оживити presence rc522, якщо щойно зайшли
}

void RfAuditApp::loop() {
  const RfAudit& a = uno_link_audit();
  if (a.seq != last_seq_) { last_seq_ = a.seq; saved_ = false; }   // новий аудит -> дозволити зберегти
}

void RfAuditApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("RF AUDIT", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString("red-team", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  const RfAudit& a = uno_link_audit();
  if (!a.valid) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Priklady kartu do RC522", 8, 44, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("audit poide avtomatychno", 8, 66, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }

  char b[40];
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "UID:  %s", a.uid);   spr.drawString(b, 8, 22, 2);
  snprintf(b, sizeof(b), "Type: %s", a.type);  spr.drawString(b, 8, 42, 2);
  snprintf(b, sizeof(b), "%d/%d sektoriv na FFFF", a.cracked, a.total);
  spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 62, 2);

  spr.setTextColor(verdict_color(a.verdict), C_BG);
  spr.drawString(a.verdict, 8, 84, 4);

  spr.setTextColor(saved_ ? C_GOOD : C_ACCENT, C_BG);
  spr.drawString(saved_ ? "zbereženo -> reports" : "S5=zberegty zvit  S2=exit", 8, SCR_H - 12, 1);
}

void RfAuditApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) {
    const RfAudit& a = uno_link_audit();
    if (a.valid && !saved_) {
      char label[32]; snprintf(label, sizeof(label), "rfaudit_%s", a.uid);  // 32 щоб довгий UID не обрізало в ім'я звіту
      report_save(label, uno_link_audit_body());
      saved_ = true;
    }
  }
}

std::string RfAuditApp::remote_state() {
  const RfAudit& a = uno_link_audit();
  char lines[5][40];
  const char* items[5];
  int n = 0;
  if (!a.valid) {
    static const char* tap = "Tap a card on RC522";
    items[n++] = tap;
    return protocol_build_menu("rf_audit", items, n, -1);
  }
  snprintf(lines[0], 40, "UID: %s", a.uid);                 items[n++] = lines[0];
  snprintf(lines[1], 40, "Type: %s", a.type);               items[n++] = lines[1];
  snprintf(lines[2], 40, "%d/%d sectors on FFFF", a.cracked, a.total); items[n++] = lines[2];
  snprintf(lines[3], 40, "Verdict: %s", a.verdict);         items[n++] = lines[3];
  snprintf(lines[4], 40, "%s", saved_ ? "saved to reports" : "S5 = save report"); items[n++] = lines[4];
  return protocol_build_menu("rf_audit", items, n, -1);
}
