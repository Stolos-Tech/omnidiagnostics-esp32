#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <WiFi.h>
#include "clock_app.h"
#include "../drivers/display.h"
#include "../kernel/format_util.h"
#include "../remote/protocol.h"

// Часовий пояс Києва: UTC+2, літній час +1 (EET/EEST).
#define TZ_OFFSET  (2 * 3600)
#define TZ_DST     3600

void ClockApp::init() {
  wants_exit_ = false;
  if (connected() && !ntp_started_) {
    configTime(TZ_OFFSET, TZ_DST, "pool.ntp.org", "time.google.com");
    ntp_started_ = true;
  }
}

bool ClockApp::connected() const { return WiFi.status() == WL_CONNECTED; }

uint32_t ClockApp::sw_elapsed_ms() const {
  return sw_accum_ + (sw_run_ ? (millis() - sw_start_) : 0);
}

void ClockApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("CLOCK", 6, 3, 2);

  // Час/дата з NTP
  struct tm ti;
  bool have = getLocalTime(&ti, 0);
  spr.setTextDatum(TC_DATUM);
  if (have && ti.tm_year > 120) {
    char t[16]; snprintf(t, sizeof(t), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(t, SCR_W / 2, 22, 7);          // великий 7-сегментний
    char d[24]; strftime(d, sizeof(d), "%a %d.%m.%Y", &ti);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(d, SCR_W / 2, 66, 2);
  } else {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(connected() ? "Syncing NTP..." : "Not connected", SCR_W / 2, 34, 2);
  }

  // Секундомір
  spr.setTextDatum(TL_DATUM);
  char sw[16]; format_mmss(sw_elapsed_ms() / 1000, sw, sizeof(sw));
  spr.setTextColor(sw_run_ ? C_GOOD : C_TEXT, C_BG);
  char b[28]; snprintf(b, sizeof(b), "SW: %s", sw);
  spr.drawString(b, 8, 88, 2);

  // Таймер
  if (cd_end_ != 0) {
    uint32_t now = millis();
    spr.setTextDatum(TR_DATUM);
    if (now < cd_end_) {
      char cd[16]; format_mmss((cd_end_ - now + 999) / 1000, cd, sizeof(cd));
      spr.setTextColor(C_WARN, C_BG);
      snprintf(b, sizeof(b), "T-%s", cd);
      spr.drawString(b, SCR_W - 8, 88, 2);
    } else {
      spr.setTextColor(C_BAD, C_BG);
      spr.drawString("TIMER!", SCR_W - 8, 88, 2);
    }
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S5=start/stop  S2=reset", 8, SCR_H - 12, 1);
}

void ClockApp::button(ButtonId id) {
  if (id == BTN_S5) {                       // старт/стоп секундоміра
    if (sw_run_) { sw_accum_ += millis() - sw_start_; sw_run_ = false; }
    else { sw_start_ = millis(); sw_run_ = true; }
  } else if (id == BTN_S2) {                // скидання секундоміра і таймера
    sw_run_ = false; sw_accum_ = 0; cd_end_ = 0;
  }
}

void ClockApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "timer") == 0 && value) {
    int min = atoi(value);
    cd_end_ = (min > 0) ? millis() + (uint32_t)min * 60000u : 0;
  }
}

std::string ClockApp::remote_state() {
  char l[3][32];
  struct tm ti; bool have = getLocalTime(&ti, 0);
  if (have && ti.tm_year > 120) snprintf(l[0], sizeof(l[0]), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
  else snprintf(l[0], sizeof(l[0]), "NTP...");
  char sw[16]; format_mmss(sw_elapsed_ms() / 1000, sw, sizeof(sw));
  snprintf(l[1], sizeof(l[1]), "Stopwatch: %s", sw);
  if (cd_end_ && millis() < cd_end_) { char cd[16]; format_mmss((cd_end_ - millis()) / 1000, cd, sizeof(cd)); snprintf(l[2], sizeof(l[2]), "Timer: %s", cd); }
  else if (cd_end_) snprintf(l[2], sizeof(l[2]), "Timer: DONE");
  else snprintf(l[2], sizeof(l[2]), "Timer: -");
  const char* items[3] = { l[0], l[1], l[2] };
  return protocol_build_menu("clock", items, 3, -1);
}
