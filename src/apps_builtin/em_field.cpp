#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <limits.h>
#include "em_field.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

#define EM_PIN     36        // GPIO36 / SENSOR_VP / ADC1_CH0 (input-only, WiFi-safe)
#define EM_WINDOW  20000     // мкс: один період мережі 50Гц
#define NVS_NS     "os"
#define NVS_KMEAN  "emzmean" // база середнього DC
#define NVS_KPP    "emzpp"   // база розмаху
#define NVS_KBAND  "emzband" // шумовий deadband (розкид ADC при калібруванні)
#define EM_CAL_WINDOWS 12    // скільки вікон усереднити при калібруванні (~240мс)

void EmFieldApp::load_zero() {
  Preferences p;
  if (p.begin(NVS_NS, true)) {
    zero_mean_  = p.getInt(NVS_KMEAN, 0);
    zero_pp_    = p.getInt(NVS_KPP, 0);
    noise_band_ = p.getInt(NVS_KBAND, 0);
    p.end();
  }
  if (zero_mean_ < 0)  zero_mean_ = 0;
  if (zero_pp_ < 0)    zero_pp_ = 0;
  if (noise_band_ < 0) noise_band_ = 0;
}

void EmFieldApp::save_zero(int mean_mv, int pp_mv, int band_mv) {
  if (mean_mv < 0) mean_mv = 0;
  if (pp_mv < 0)   pp_mv = 0;
  if (band_mv < 0) band_mv = 0;
  zero_mean_  = mean_mv;
  zero_pp_    = pp_mv;
  noise_band_ = band_mv;
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putInt(NVS_KMEAN, mean_mv);
    p.putInt(NVS_KPP, pp_mv);
    p.putInt(NVS_KBAND, band_mv);
    p.end();
  }
}

// Калібрування нуля: беремо EM_CAL_WINDOWS вікон фону, база = СЕРЕДНЄ (стабільніше за одне
// вікно), а deadband = найбільший розкид сигналу над середнім за ці вікна × 1.5 + 5мВ запасу.
// Далі поле реєструється ЛИШЕ вище deadband -> ADC-шум не дає хибних спрацьовувань.
void EmFieldApp::calibrate() {
  long sum_m = 0, sum_p = 0;
  int mns[EM_CAL_WINDOWS], pps[EM_CAL_WINDOWS];
  for (int i = 0; i < EM_CAL_WINDOWS; i++) {
    sample_window(&mns[i], &pps[i]);
    sum_m += mns[i];
    sum_p += pps[i];
  }
  int base_m = (int)(sum_m / EM_CAL_WINDOWS);
  int base_p = (int)(sum_p / EM_CAL_WINDOWS);
  int band = 0;                                  // піковий розкид над базою (шумовий фон)
  for (int i = 0; i < EM_CAL_WINDOWS; i++) {
    int dm = mns[i] - base_m, dp = pps[i] - base_p;
    int d = dm > dp ? dm : dp;
    if (d > band) band = d;
  }
  band = band + band / 2 + 5;                    // 1.5× + 5мВ запас над піковим шумом
  save_zero(base_m, base_p, band);
}

void EmFieldApp::init() {
  wants_exit_ = false;
  just_calibrated_ = false;
  peak_mv_ = 0;
  hist_n_ = 0;
  // ADC1: повна шкала (11дБ ~0..3.1В), 12 біт. analogReadMilliVolts калібрує через eFuse Vref.
  analogReadResolution(12);
  analogSetPinAttenuation(EM_PIN, ADC_11db);
  load_zero();
}

// Один період мережі (20мс): читаємо якнайшвидше. Середнє = рівень DC (детектор-
// обгортка дає DC ∝ полю). Розмах max-min = амплітуда AC (варіант «сира AC»).
// ~20мс блокуючого — апка активна лише коли відкрита (як пінг у Net Info).
void EmFieldApp::sample_window(int* mean_mv, int* pp_mv) {
  int mn = INT_MAX, mx = 0, n = 0;
  long sum = 0;
  uint32_t t0 = micros();
  do {
    int v = (int)analogReadMilliVolts(EM_PIN);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    n++;
  } while ((uint32_t)(micros() - t0) < EM_WINDOW);
  *mean_mv = n ? (int)(sum / n) : 0;
  int pp = mx - mn;
  *pp_mv = pp < 0 ? 0 : pp;
}

void EmFieldApp::loop() {
  sample_window(&raw_mean_, &raw_pp_);
  // Поле = більший із двох сигналів над своєю базою: працює і для DC-детектора,
  // і для сирого-AC варіанта, не знаючи наперед, що саме зібрано.
  int dm = raw_mean_ - zero_mean_;
  int dp = raw_pp_   - zero_pp_;
  int net = dm > dp ? dm : dp;
  net -= noise_band_;          // deadband: усе в межах шумового фону -> 0 (без хибних спрацьовувань)
  if (net < 0) net = 0;
  field_mv_ = net;
  if (net > peak_mv_) peak_mv_ = net;
  if (hist_n_ < HIST) hist_[hist_n_++] = net;
  else { for (int i = 1; i < HIST; i++) hist_[i - 1] = hist_[i]; hist_[HIST - 1] = net; }
}

void EmFieldApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("EM FIELD", 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString("GPIO36", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  // Велике поточне значення
  char b[24];
  snprintf(b, sizeof(b), "%d", field_mv_);
  uint16_t col = field_mv_ > 300 ? C_BAD : field_mv_ > 80 ? C_WARN : C_GOOD;
  spr.setTextColor(col, C_BG);
  spr.drawString(b, 8, 20, 6);           // великі цифри
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("mV", 8, 56, 2);
  // сирі мітки праворуч (діагностика: DC-середнє і pp)
  char pk[28];
  snprintf(pk, sizeof(pk), "dc %d  pp %d", raw_mean_, raw_pp_);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_TEXT, C_BG); spr.drawString(pk, SCR_W - 8, 22, 2);
  snprintf(pk, sizeof(pk), "peak %d  z %d/%d", peak_mv_, zero_mean_, zero_pp_);
  spr.setTextColor(C_DIM, C_BG);  spr.drawString(pk, SCR_W - 8, 40, 1);
  spr.setTextDatum(TL_DATUM);

  // Смуга рівня (авто-шкала від піку, мінімум 100мВ щоб дрібне теж було видно)
  int scale = peak_mv_ > 100 ? peak_mv_ : 100;
  int barw = (SCR_W - 16) * field_mv_ / scale;
  if (barw < 0) barw = 0; if (barw > SCR_W - 16) barw = SCR_W - 16;
  spr.drawRect(8, 64, SCR_W - 16, 10, C_GRID);
  spr.fillRect(9, 65, barw, 8, col);

  // Графік історії
  const int gy = 78, gh = 40;
  int mx = 1; for (int i = 0; i < hist_n_; i++) if (hist_[i] > mx) mx = hist_[i];
  if (mx < 100) mx = 100;
  spr.drawFastHLine(8, gy + gh, SCR_W - 16, C_GRID);
  for (int i = 1; i < hist_n_; i++) {
    int x0 = 8 + (SCR_W - 16) * (i - 1) / (HIST - 1);
    int x1 = 8 + (SCR_W - 16) * i / (HIST - 1);
    int y0 = gy + gh - gh * hist_[i - 1] / mx;
    int y1 = gy + gh - gh * hist_[i] / mx;
    spr.drawLine(x0, y0, x1, y1, C_ACCENT);
  }

  spr.setTextColor(just_calibrated_ ? C_GOOD : C_DIM, C_BG);
  spr.drawString(just_calibrated_ ? "zero calibrated" : "S5=calibrate zero  S2=exit", 8, SCR_H - 12, 1);
}

void EmFieldApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) {
    // Калібрування нуля: тримай зонд ПОДАЛІ від джерел поля -> усереднюємо фон за кілька
    // вікон і вимірюємо шумовий deadband. Поле далі реєструється лише вище нього.
    calibrate();
    peak_mv_ = 0;
    just_calibrated_ = true;
  }
}

void EmFieldApp::on_exit() {
  just_calibrated_ = false;
}

int EmFieldApp::build_lines(char lines[][40]) const {
  int n = 0;
  snprintf(lines[n++], 40, "Field: %d mV", field_mv_);
  snprintf(lines[n++], 40, "DC mean: %d mV", raw_mean_);
  snprintf(lines[n++], 40, "PP: %d mV  peak %d", raw_pp_, peak_mv_);
  snprintf(lines[n++], 40, "Zero m/p:%d/%d band:%d", zero_mean_, zero_pp_, noise_band_);
  snprintf(lines[n++], 40, "GPIO36 ADC1 (S5=recal)");
  return n;
}

std::string EmFieldApp::remote_state() {
  char lines[5][40];
  int n = build_lines(lines);
  const char* items[5];
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu("em_field", items, n, -1);
}
