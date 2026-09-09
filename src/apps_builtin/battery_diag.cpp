#include <Arduino.h>
#include "battery_diag.h"
#include "../drivers/display.h"
#include "../drivers/battery_adc.h"
#include "../remote/protocol.h"

// ---------- Крива розряду Li-ion (OCV -> %) ----------
static const float vTab[] = {3.00, 3.30, 3.45, 3.55, 3.63, 3.70, 3.78, 3.87, 3.95, 4.05, 4.20};
static const int   pTab[] = {0,    5,    10,   20,   30,   40,   50,   60,   70,   85,   100};
static const int   TAB_N  = 11;

static int voltageToPercent(float v) {
  if (v <= vTab[0]) return 0;
  if (v >= vTab[TAB_N - 1]) return 100;
  for (int i = 1; i < TAB_N; i++)
    if (v < vTab[i]) {
      float f = (v - vTab[i-1]) / (vTab[i] - vTab[i-1]);
      return pTab[i-1] + (int)(f * (pTab[i] - pTab[i-1]));
    }
  return 100;
}

static uint16_t socColor(int p){ return p>=60?C_GOOD : p>=30?C_WARN : p>=15?0xFC00 : C_BAD; }

// ~дельта струму під навантаженням CPU+екран (для оцінки Rint)
static const float ASSUMED_LOAD_A = 0.060f;

// ---------- Життєвий цикл ----------
void BatteryDiagApp::init() {
  wants_exit_ = false;
  page_ = PAGE_MAIN;
  diag_page_ = 0;
  vMin_ = 99; vMax_ = 0;
  gCount_ = 0;
  diag_.vcol = C_DIM;
  vNow_ = vAvg_ = vPrev_ = battery_read(nullptr, &rawNow_, &mvNow_);
  tPrev_ = millis();
  tLast_ = 0;
  tGraph_ = 0;
}

void BatteryDiagApp::loop() {
  if (millis() - tLast_ >= 1000) {
    tLast_ = millis();
    float sd;
    vNow_ = battery_read(&sd, &rawNow_, &mvNow_);
    noise_mV_ = sd * 1000;
    if (vNow_ > 1.0f && vNow_ < vMin_) vMin_ = vNow_;
    if (vNow_ > vMax_) vMax_ = vNow_;
    vAvg_ = vAvg_ == 0 ? vNow_ : vAvg_ * 0.9f + vNow_ * 0.1f;

    float dt = (millis() - tPrev_) / 1000.0f;
    if (dt > 0) {
      float inst = (vNow_ - vPrev_) / dt * 60000.0f;   // миттєвий тренд, мВ/хв
      dVdt_mVmin_ = dVdt_mVmin_ * 0.8f + inst * 0.2f;  // EMA -> прибирає брязкіт
    }
    vPrev_ = vNow_; tPrev_ = millis();

    Serial.printf("V=%.3f raw=%u mv=%.0f soc=%d noise=%.0fmV\n",
                  vNow_, (unsigned)rawNow_, mvNow_, voltageToPercent(vNow_), noise_mV_);
  }

  if (millis() - tGraph_ >= 2000) {
    tGraph_ = millis();
    if (gCount_ < G_N) gBuf_[gCount_++] = vNow_;
    else { memmove(gBuf_, gBuf_ + 1, (G_N - 1) * sizeof(float)); gBuf_[G_N - 1] = vNow_; }
  }
}

void BatteryDiagApp::button(ButtonId id) {
  if (id == BTN_S2) {
    // наступна сторінка; всередині DIAG з результатами — спершу підсторінки
    if (page_ == PAGE_DIAG && diag_.done && diag_page_ < 3) {
      diag_page_++;
    } else {
      if (page_ == PAGE_DIAG) diag_page_ = 0;
      page_ = (page_ + 1) % PAGE_COUNT;
    }
  } else if (id == BTN_S1) {
    // попередня сторінка (з'явиться з платою S1-S5; логіка вже готова)
    if (page_ == PAGE_DIAG && diag_.done && diag_page_ > 0) {
      diag_page_--;
    } else {
      page_ = (page_ + PAGE_COUNT - 1) % PAGE_COUNT;
    }
  } else if (id == BTN_S5) {
    // дія на поточній сторінці
    if (page_ == PAGE_EXIT) {
      wants_exit_ = true;
    } else if (page_ == PAGE_DIAG) {
      if (diag_.done) diag_page_ = 0;
      else runDiagnostics();
    } else {
      vMin_ = 99; vMax_ = 0;  // reset статистики
    }
  }
}

void BatteryDiagApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  switch (page_) {
    case PAGE_MAIN:     drawMain();     break;
    case PAGE_ANALYSIS: drawAnalysis(); break;
    case PAGE_GRAPH:    drawGraph();    break;
    case PAGE_DIAG:     drawDiagMenu(); break;
    case PAGE_EXIT:     drawExit();     break;
  }
  pageDots();
}

// ---------- Спільні елементи ----------
void BatteryDiagApp::topBar(const char* title) {
  display_top_bar(title);
  TFT_eSprite& spr = display_sprite();
  spr.setTextDatum(TR_DATUM);
  if (vNow_ > 4.25f) { spr.setTextColor(C_ACCENT, C_PANEL); spr.drawString("USB", SCR_W-6, 3, 2); }
  else               { spr.setTextColor(socColor(voltageToPercent(vNow_)), C_PANEL);
                       char b[8]; snprintf(b, sizeof(b), "%d%%", voltageToPercent(vNow_));
                       spr.drawString(b, SCR_W-6, 3, 2); }
}

void BatteryDiagApp::pageDots() {
  TFT_eSprite& spr = display_sprite();
  int cx = SCR_W/2 - (PAGE_COUNT*10)/2;
  for (int i = 0; i < PAGE_COUNT; i++)
    spr.fillCircle(cx + i*10 + 4, SCR_H-5, i==page_?3:2, i==page_?C_ACCENT:C_GRID);
}

void BatteryDiagApp::row(int y, const char* k, const char* v, uint16_t vc) {
  TFT_eSprite& spr = display_sprite();
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG); spr.drawString(k, 8, y, 2);
  spr.setTextDatum(TR_DATUM); spr.setTextColor(vc, C_BG);    spr.drawString(v, SCR_W-8, y, 2);
}

// ---------- Сторінка MAIN ----------
// Веб-мірор: до цього єдина апка без remote_state() -> телефон показував лише
// мітку "Battery Diag" без даних. Тепер мірочимо ключові числа поточної сторінки
// узагальненим списком (веб рендерить items[] рядками).
std::string BatteryDiagApp::remote_state() {
  char lines[8][40];
  int n = 0;
  int pct = voltageToPercent(vNow_);
  const char* st = vNow_ > 4.25f ? "CHARGING/USB"
                 : (vNow_ > 3.95f && fabsf(dVdt_mVmin_) < 5) ? "FULL / CV"
                 : dVdt_mVmin_ > 8  ? "CHARGING"
                 : dVdt_mVmin_ < -8 ? "DISCHARGING" : "IDLE";
  if (page_ == PAGE_DIAG && diag_.done) {
    snprintf(lines[n++], 40, "OCV: %.3f V", diag_.ocv);
    snprintf(lines[n++], 40, "SAG@100%%: %.0f mV", diag_.sag * 1000);
    snprintf(lines[n++], 40, "Rint: ~%.0f mOhm", diag_.rint_full * 1000);
    snprintf(lines[n++], 40, "Health: %d%%  %s", diag_.health, diag_.verdict);
    snprintf(lines[n++], 40, "Now: %.3f V (%d%%)", vNow_, pct);
  } else if (page_ == PAGE_ANALYSIS) {
    snprintf(lines[n++], 40, "Now: %.3f V (%d%%)", vNow_, pct);
    snprintf(lines[n++], 40, "min/max: %.2f / %.2f V", vMin_, vMax_);
    snprintf(lines[n++], 40, "Avg: %.3f V", vAvg_);
    snprintf(lines[n++], 40, "Trend: %+.0f mV/min", dVdt_mVmin_);
    snprintf(lines[n++], 40, "ADC noise: %.0f mV", noise_mV_);
  } else {
    snprintf(lines[n++], 40, "%.3f V   %d%%", vNow_, pct);
    snprintf(lines[n++], 40, "State: %s", st);
    snprintf(lines[n++], 40, "raw %u   %.0f mV", (unsigned)rawNow_, mvNow_);
    snprintf(lines[n++], 40, "Trend: %+.0f mV/min", dVdt_mVmin_);
    if (page_ == PAGE_DIAG)      snprintf(lines[n++], 40, "Select = run test (~25s)");
    else if (page_ == PAGE_EXIT) snprintf(lines[n++], 40, "Select = exit to launcher");
  }
  const char* items[8];
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("Battery Diag", items, n, -1);
}

void BatteryDiagApp::drawMain() {
  TFT_eSprite& spr = display_sprite();
  topBar("BATTERY MONITOR");
  int pct = voltageToPercent(vNow_);
  uint16_t col = socColor(pct);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawFloat(vNow_, 3, 8, 26, 6);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("V", 150, 46, 4);

  spr.setTextColor(col, C_BG);
  char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(b, SCR_W-8, 30, 6);

  int bx=8, by=80, bw=SCR_W-16, bh=16;
  spr.drawRoundRect(bx, by, bw, bh, 3, C_DIM);
  spr.fillRoundRect(bx+2, by+2, (bw-4)*pct/100, bh-4, 2, col);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG);
  const char* st = vNow_>4.25f ? "CHARGING/USB"
                 : (vNow_>3.95f && fabsf(dVdt_mVmin_)<5) ? "FULL / CV"
                 : dVdt_mVmin_>8  ? "CHARGING"
                 : dVdt_mVmin_<-8 ? "DISCHARGING"
                 : "IDLE";
  char l[48]; snprintf(l, sizeof(l), "%s   raw:%u  %.0fmV", st, (unsigned)rawNow_, mvNow_);
  spr.drawString(l, 8, 104, 2);

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("RIGHT=page  LEFT=reset", 8, SCR_H-18, 1);
}

// ---------- Сторінка ANALYSIS ----------
void BatteryDiagApp::drawAnalysis() {
  TFT_eSprite& spr = display_sprite();
  topBar("ANALYSIS");
  char b[24];
  int y=22, dy=18;
  snprintf(b,sizeof(b),"%.3f V", vNow_);             row(y,"Voltage (VBAT)", b, C_TEXT);  y+=dy;
  snprintf(b,sizeof(b),"%.3f V", vAvg_);             row(y,"Serednia",       b, C_DIM);   y+=dy;
  snprintf(b,sizeof(b),"%.2f / %.2f", vMin_, vMax_); row(y,"min / max",      b, C_DIM);   y+=dy;
  snprintf(b,sizeof(b),"%.0f mV/min", dVdt_mVmin_);
  row(y,"Trend", b, dVdt_mVmin_>8?C_GOOD:dVdt_mVmin_<-8?C_WARN:C_DIM);                    y+=dy;
  snprintf(b,sizeof(b),"%.0f mV", noise_mV_);
  row(y,"Shum ADC", b, noise_mV_>50?C_WARN:C_DIM);                                        y+=dy;
  if (diag_.done) {
    snprintf(b,sizeof(b),"~%.0f mOhm", diag_.rint_full*1000); row(y,"Rint (ocinka)", b, C_ACCENT); y+=dy;
    snprintf(b,sizeof(b),"%d %%", diag_.health);
    row(y,"Health", b, diag_.health>70?C_GOOD:diag_.health>40?C_WARN:C_BAD);
  } else {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Rint/Health: zapusty DIAG", 8, y, 2);
  }
}

// ---------- Сторінка GRAPH ----------
void BatteryDiagApp::drawGraph() {
  TFT_eSprite& spr = display_sprite();
  topBar("GRAPH  (V / time)");
  int gx=4, gy=20, gw=SCR_W-8, gh=SCR_H-32;
  spr.drawRect(gx, gy, gw, gh, C_GRID);

  float lo=99, hi=0;
  for (int i=0;i<gCount_;i++){ if(gBuf_[i]<lo)lo=gBuf_[i]; if(gBuf_[i]>hi)hi=gBuf_[i]; }
  if (gCount_<2){ lo=3.0; hi=4.3; }
  float pad=(hi-lo)*0.15f+0.02f; lo-=pad; hi+=pad;
  if (hi-lo < 0.05f) hi=lo+0.05f;

  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
  for (int i=0;i<=2;i++){
    float vv = lo + (hi-lo)*i/2.0f;
    int yy = gy+gh - (int)((float)(gh)*i/2.0f);
    spr.drawLine(gx, yy, gx+gw, yy, C_GRID);
    char b[8]; snprintf(b,sizeof(b),"%.2f",vv);
    spr.drawString(b, gx+2, yy-9, 1);
  }
  int prevX=-1, prevY=-1;
  for (int i=0;i<gCount_;i++){
    int x = gx + (gw-2)*i/(G_N-1) + 1;
    int y = gy+gh - (int)((gBuf_[i]-lo)/(hi-lo)*(gh-2)) - 1;
    if (prevX>=0) spr.drawLine(prevX, prevY, x, y, C_ACCENT);
    prevX=x; prevY=y;
  }
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_BG);
  char b[24]; snprintf(b,sizeof(b),"~%d s okno", (int)(G_N*2));
  spr.drawString(b, SCR_W-6, gy+2, 1);
}

// ---------- Сторінка DIAG (багатосторінкова) ----------
void BatteryDiagApp::drawDiagMenu() {
  TFT_eSprite& spr = display_sprite();
  topBar("DIAGNOSTIKA");

  if (!diag_.done) {
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Extended test:", 8, 30, 2);
    spr.drawString("- CV-faza", 12, 50, 2);
    spr.drawString("- Gradaciynyi load", 12, 64, 2);
    spr.drawString("- ESR-analiz", 12, 78, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Tryvaet ~25 sec", 8, 100, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = START", 8, SCR_H-18, 2);
  } else {
    const char *titles[] = {"MAIN", "LOAD", "ESR/CV", "REPORT"};
    char header[32]; snprintf(header, sizeof(header), "DIAG > %s [%d]", titles[diag_page_], diag_page_+1);
    spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
    spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
    spr.drawString(header, 6, 3, 2);

    int y=22, dy=17; char b[32];

    if (diag_page_ == 0) {
      snprintf(b,sizeof(b),"OCV: %.3f V", diag_.ocv);       spr.setTextColor(C_TEXT, C_BG);
      spr.setTextDatum(TL_DATUM); spr.drawString(b, 8, y, 2); y+=dy;

      snprintf(b,sizeof(b),"%.0f mV", diag_.sag*1000);
      spr.setTextColor(diag_.sag>0.3?C_WARN:C_DIM, C_BG);
      spr.drawString("SAG @100%: ", 8, y, 2);
      spr.setTextDatum(TR_DATUM); spr.drawString(b, SCR_W-8, y, 2); y+=dy;

      snprintf(b,sizeof(b),"~%.0f mOhm", diag_.rint_full*1000);
      spr.setTextColor(C_ACCENT, C_BG);
      spr.setTextDatum(TL_DATUM); spr.drawString("RINT @100%: ", 8, y, 2);
      spr.setTextDatum(TR_DATUM); spr.drawString(b, SCR_W-8, y, 2); y+=dy;

      snprintf(b,sizeof(b),"%d %%", diag_.health);
      spr.setTextColor(diag_.health>70?C_GOOD:diag_.health>40?C_WARN:C_BAD, C_BG);
      spr.setTextDatum(TL_DATUM); spr.drawString("HEALTH: ", 8, y, 2);
      spr.setTextDatum(TR_DATUM); spr.drawString(b, SCR_W-8, y, 2);

    } else if (diag_page_ == 1) {
      spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
      spr.drawString("Grad. load test:", 8, y, 2); y+=20;

      snprintf(b,sizeof(b),"25%%: %.0f mOhm", diag_.rint_25*1000);
      spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 8, y, 2); y+=dy;
      snprintf(b,sizeof(b),"50%%: %.0f mOhm", diag_.rint_50*1000);
      spr.drawString(b, 8, y, 2); y+=dy;
      snprintf(b,sizeof(b),"75%%: %.0f mOhm", diag_.rint_75*1000);
      spr.drawString(b, 8, y, 2); y+=dy;
      snprintf(b,sizeof(b),"100%%: %.0f mOhm", diag_.rint_full*1000);
      spr.drawString(b, 8, y, 2); y+=dy;

      spr.setTextColor(C_DIM, C_BG);
      if (diag_.rint_full > diag_.rint_25 * 1.5f)
        spr.drawString("Rint roste -> starinnist!", 8, y+4, 1);

    } else if (diag_page_ == 2) {
      snprintf(b,sizeof(b),"ESR (inst): %.0f mOhm", diag_.esr_instant*1000);
      spr.setTextColor(C_ACCENT, C_BG); spr.setTextDatum(TL_DATUM);
      spr.drawString(b, 8, y, 2); y+=dy;

      snprintf(b,sizeof(b),"CV-faza: %.1f sec", diag_.cv_duration);
      spr.setTextColor(C_TEXT, C_BG);
      spr.drawString(b, 8, y, 2); y+=dy;

      snprintf(b,sizeof(b),"CV na: %.2f V", diag_.cv_end_v);
      spr.drawString(b, 8, y, 2); y+=dy;

      spr.setTextColor(C_DIM, C_BG);
      if (diag_.cv_duration > 180)
        spr.drawString("Long CV -> may overcharge", 8, y+4, 1);

    } else if (diag_page_ == 3) {
      spr.setTextDatum(TL_DATUM); spr.setTextColor(diag_.vcol, C_BG);
      spr.drawString(diag_.verdict, 8, y, 2); y+=20;
      spr.setTextColor(C_DIM, C_BG);
      spr.drawString("Rekomendaciyi:", 8, y, 2); y+=16;
      if (diag_.rint_full < 0.150f)
        { spr.drawString("- Element ok", 8, y, 1); y+=12; }
      else if (diag_.rint_full < 0.300f)
        { spr.drawString("- Zadovil'nyy", 8, y, 1); y+=12; }
      else
        { spr.drawString("- High resistance!", 8, y, 1); y+=12; }
      if (diag_.cv_end_v < 4.0f)
        spr.drawString("- CV nizko: BMS?", 8, y, 1);
    }

    spr.setTextColor(C_DIM, C_BG); spr.setTextDatum(TL_DATUM);
    spr.drawString("RIGHT=page  LEFT=action", 8, SCR_H-18, 1);
  }
}

// ---------- Сторінка EXIT ----------
void BatteryDiagApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  topBar("EXIT");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Exit to launcher?", SCR_W/2, SCR_H/2 - 10, 4);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("LEFT = exit   RIGHT = next", SCR_W/2, SCR_H/2 + 20, 2);
}

// ---------- Діагностика (блокуюча, ~25с) ----------
void BatteryDiagApp::diagProgress(int pct, const char* msg) {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  topBar("RUNNING TEST...");
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(msg, 8, 40, 2);
  int bx=8, by=70, bw=SCR_W-16, bh=18;
  spr.drawRoundRect(bx, by, bw, bh, 3, C_DIM);
  spr.fillRoundRect(bx+2, by+2, (bw-4)*pct/100, bh-4, 2, C_ACCENT);
  char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
  spr.setTextDatum(MC_DATUM); spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(b, SCR_W/2, by+bh/2, 2);
  display_push();
}

void BatteryDiagApp::runDiagnostics() {
  diag_.done = false;
  diag_page_ = 0;

  // 1. OCV спокою
  diagProgress(5, "1/5 OCV spokoyu...");
  float sd; float ocv = battery_read(&sd);
  delay(300);

  // 2. Градаційний тест навантаження (25%, 50%, 75%, 100%)
  diagProgress(15, "2/5 Grad. load 25%...");
  uint32_t t0 = millis();
  volatile double dummy = 0;
  float v25min = 99;
  while (millis()-t0 < 1000) {
    for (uint32_t i=0;i<7500;i++) dummy += i*1.000001;
    float v = battery_read();
    if (v < v25min) v25min = v;
  }
  float sag25 = ocv - v25min;

  diagProgress(30, "2/5 Grad. load 50%...");
  t0 = millis();
  float v50min = 99;
  while (millis()-t0 < 1000) {
    for (uint32_t i=0;i<15000;i++) dummy += i*1.000001;
    float v = battery_read();
    if (v < v50min) v50min = v;
  }
  float sag50 = ocv - v50min;

  diagProgress(45, "2/5 Grad. load 75%...");
  t0 = millis();
  float v75min = 99;
  while (millis()-t0 < 1000) {
    for (uint32_t i=0;i<22000;i++) dummy += i*1.000001;
    float v = battery_read();
    if (v < v75min) v75min = v;
  }
  float sag75 = ocv - v75min;

  diagProgress(60, "2/5 Grad. load 100%...");
  t0 = millis();
  float v100min = 99;
  while (millis()-t0 < 1000) {
    for (uint32_t i=0;i<30000;i++) dummy += i*1.000001;
    float v = battery_read();
    if (v < v100min) v100min = v;
  }
  float sag = ocv - v100min;

  // 3. ESR миттєвий (перші 50мс) - крутизна падіння
  diagProgress(70, "3/5 ESR instant...");
  float vBefore = battery_read();
  delay(50);   // тримаємо full load 50мс
  float vAfter = battery_read();
  float esr_inst = (vBefore - vAfter) / ASSUMED_LOAD_A / 0.05f;

  // 4. Відновлення і тривалість CV-фази
  diagProgress(80, "4/5 Recovery + CV...");
  delay(1000); // 1с спокою - видно відновлення
  float vRec = battery_read();
  float recover = vRec - v100min;

  // Спостереження CV-фази: якщо напруга стабільна > 3.95В - це CV
  uint32_t cv_start = millis();
  float cv_v_min = 99, cv_v_max = 0;
  for (int i=0; i<20; i++) {  // 20 сек спостереження
    float v = battery_read();
    if (v < cv_v_min) cv_v_min = v;
    if (v > cv_v_max) cv_v_max = v;
    if (millis() - cv_start > 20000) break;
    diagProgress(80 + (i*20)/20, "4/5 Recovery + CV...");
    delay(1000);
  }
  float cv_end_v = cv_v_max;
  float cv_duration = (millis() - cv_start) / 1000.0f;

  diagProgress(100, "5/5 Analiz...");
  delay(300);

  // Обчислення
  float rint = sag / ASSUMED_LOAD_A;

  int health;
  if (rint < 0.15f)      health = 95;
  else if (rint < 0.30f) health = 80;
  else if (rint < 0.50f) health = 60;
  else if (rint < 0.80f) health = 40;
  else                   health = 20;
  if (sd > 0.05f) health -= 15;
  if (esr_inst > 0.5f) health -= 10;
  if (health < 5) health = 5;
  if (health > 100) health = 100;

  const char* verdict; uint16_t vc;
  if (ocv > 4.25f)       { verdict="USB connected - unplug"; vc=C_DIM; }
  else if (ocv < 3.0f)   { verdict="CRITICALLY low!";        vc=C_BAD; }
  else if (sd > 0.05f)   { verdict="Nestabilnyi kontakt/BMS"; vc=C_WARN; }
  else if (rint>0.5f)    { verdict="High resistance - worn"; vc=C_WARN; }
  else if (rint>0.3f)    { verdict="Poserednii stan";         vc=C_WARN; }
  else if (esr_inst>0.3f){ verdict="Vysokyi ESR!";            vc=C_WARN; }
  else                   { verdict="AKB u normi";             vc=C_GOOD; }

  // Результати
  diag_.ocv=ocv; diag_.sag=sag; diag_.recover=recover; diag_.rint_full=rint;
  diag_.sag_25=sag25; diag_.sag_50=sag50; diag_.sag_75=sag75;
  diag_.rint_25=sag25/ASSUMED_LOAD_A; diag_.rint_50=sag50/ASSUMED_LOAD_A; diag_.rint_75=sag75/ASSUMED_LOAD_A;
  diag_.esr_instant=esr_inst;
  diag_.cv_end_v=cv_end_v; diag_.cv_duration=cv_duration;
  diag_.health=health; diag_.verdict=verdict; diag_.vcol=vc; diag_.done=true;

  Serial.printf("DIAG ocv=%.3f sag=%.0fmV rint=%.0fmOhm esr=%.0fmOhm cv=%.1fs health=%d\n",
                ocv, sag*1000, rint*1000, esr_inst*1000, cv_duration, health);
}
