#include <Arduino.h>
#include <stdio.h>
#include "subghz_capture.h"
#include "../drivers/display.h"
#include "../drivers/cc1101.h"
#include "../kernel/subghz_util.h"
#include "../remote/protocol.h"

static const int CAP_MAX = 256;    // максимум імпульсів на кадр
static uint16_t s_pulses[CAP_MAX]; // буфер захоплення (спільний, апка одна активна)

void SubghzCaptureApp::init() {
  wants_exit_ = false;
  have_capture_ = false;
  raw_pulses_ = 0;
  last_.valid = false; last_.proto = "?"; last_.code = 0; last_.bits = 0; last_.te_us = 0;
  present_ = cc1101_begin();
  if (present_) cc1101_set_freq_khz(subghz_preset_khz(preset_));
}

void SubghzCaptureApp::loop() {
  if (!present_) { present_ = cc1101_begin(); return; }
  // Жива RSSI на поточній частоті (дешево) — видно, чи щось у ефірі.
  rssi_ = cc1101_probe_rssi_dbm(subghz_preset_khz(preset_));
}

void SubghzCaptureApp::do_capture() {
  if (!present_) return;
  raw_pulses_ = cc1101_capture_ook(subghz_preset_khz(preset_), s_pulses, CAP_MAX, 2500);
  last_ = ook_decode(s_pulses, raw_pulses_);
  have_capture_ = true;
}

void SubghzCaptureApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  display_top_bar("SUB-GHZ CAPTURE");
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(present_ ? C_GOOD : C_BAD, C_PANEL);
  spr.drawString(present_ ? "CC1101" : "no CC1101", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!present_) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("CC1101 ne znaydeno.", 8, 34, 2);
    spr.drawString("SCK2 MOSI15 MISO38 CS27 GDO0=39", 8, 58, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }

  char b[44];
  long f = subghz_preset_khz(preset_);
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "Freq: %s MHz", subghz_preset_name(preset_));
  spr.drawString(b, 8, 24, 2);
  spr.setTextColor(rssi_ > -85 ? C_GOOD : C_DIM, C_BG);
  snprintf(b, sizeof(b), "live RSSI: %d dBm", rssi_);
  spr.drawString(b, 8, 44, 1);
  (void)f;

  if (have_capture_) {
    if (last_.valid) {
      spr.setTextColor(C_GOOD, C_BG);
      snprintf(b, sizeof(b), "%s", last_.proto);
      spr.drawString(b, 8, 62, 2);
      spr.setTextColor(C_WARN, C_BG);
      snprintf(b, sizeof(b), "code 0x%06lX (%d bit)", (unsigned long)last_.code, last_.bits);
      spr.drawString(b, 8, 82, 2);
      spr.setTextColor(C_DIM, C_BG);
      snprintf(b, sizeof(b), "Te=%d us  pulses=%d", last_.te_us, raw_pulses_);
      spr.drawString(b, 8, 102, 1);
    } else {
      spr.setTextColor(C_DIM, C_BG);
      snprintf(b, sizeof(b), raw_pulses_ > 0 ? "signal, not decoded (%d p)" : "nothing captured",
               raw_pulses_);
      spr.drawString(b, 8, 68, 2);
    }
  } else {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S5 = listen for a remote", 8, 68, 2);
  }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S5=listen  S1=freq  S2=exit", 8, SCR_H - 12, 1);
}

void SubghzCaptureApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S1) {
    preset_ = (preset_ + 1) % subghz_preset_count();
    if (present_) cc1101_set_freq_khz(subghz_preset_khz(preset_));
    have_capture_ = false;
    return;
  }
  if (id == BTN_S5) { do_capture(); }
}

std::string SubghzCaptureApp::remote_state() {
  char lines[5][40];
  const char* items[5];
  int n = 0;
  if (!present_) {
    snprintf(lines[n++], 40, "CC1101: NOT FOUND");
    snprintf(lines[n++], 40, "SCK2 MOSI15 MISO38 CS27 GDO39");
  } else {
    snprintf(lines[n++], 40, "Freq: %s MHz", subghz_preset_name(preset_));
    snprintf(lines[n++], 40, "live RSSI: %d dBm", rssi_);
    if (have_capture_ && last_.valid)
      snprintf(lines[n++], 40, "%s 0x%06lX", last_.proto, (unsigned long)last_.code);
    else if (have_capture_)
      snprintf(lines[n++], 40, raw_pulses_ > 0 ? "signal, undecoded" : "nothing captured");
    else
      snprintf(lines[n++], 40, "S5=listen S1=freq");
  }
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("subghz_capture", items, n, -1);
}
