// EM Field — пасивний зонд електромагнітного поля (варіант A). Мідна котушка-
// пікап через діод-детектор (envelope) заходить на GPIO36 (ADC1_CH0, input-only,
// працює при активному WiFi — на відміну від ADC2). За вікно в один період
// мережі (20 мс) беремо каліброваних мВ (analogReadMilliVolts, eFuse Vref) і
// рахуємо ОБИДВІ метрики: середнє DC (сигнал детектора-обгортки) і розмах
// peak-to-peak (сигнал варіанта «сира AC»). Поле = більше з двох над збереженою
// базою (S5). Порівняльний вимірювач (шукати «гарячі» точки EMI), не абсолютний.
//
// Керування: S5 — калібрувати нуль (запам'ятати поточний фон у NVS), S2 — вихід.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class EmFieldApp : public App {
public:
  const char* name() const override { return "EM Field"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог піку ЕМ-поля на SD при виході
  void on_exit() override;

private:
  static const int HIST = 120;

  bool wants_exit_ = false;
  int  raw_mean_ = 0;       // сире середнє DC вікна (мВ) — сигнал детектора-обгортки
  int  raw_pp_   = 0;       // сирий розмах вікна (мВ) — сигнал «сирого-AC» варіанта
  int  zero_mean_ = 0;      // база середнього (NVS)
  int  zero_pp_   = 0;      // база розмаху (NVS)
  int  noise_band_ = 0;     // шумовий фон ADC при калібруванні (NVS) — deadband проти хибних спрацьовувань
  int  field_mv_ = 0;       // max(mean-zero_mean, pp-zero_pp) - noise_band_, >=0
  int  peak_mv_ = 0;        // утримання піку від старту/останнього калібрування
  int  hist_[HIST] = {};    // історія field_mv_ для графіка
  int  hist_n_ = 0;
  bool just_calibrated_ = false;

  void sample_window(int* mean_mv, int* pp_mv);  // 20мс-вікно -> середнє DC і розмах
  void load_zero();
  void save_zero(int mean_mv, int pp_mv, int band_mv);
  void calibrate();                               // багатовіконне: база + шумовий deadband
  int  build_lines(char lines[][40]) const;
};
