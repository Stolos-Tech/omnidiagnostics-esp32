#include "cooling.h"

static const int MIN_DUTY = 110;   // стартовий PWM: вентилятор має зрушити (подолати стик)

int cooling_fan_duty(float temp_c, int prev_duty, const CoolingCfg& cfg) {
  if (cfg.full_c <= cfg.start_c) return 0;   // невалідна крива -> безпечно off
  bool was_on = prev_duty > 0;
  // Гістерезис: якщо вже крутиться — вимикаємо лише коли впаде НИЖЧЕ (start - hyst).
  float on_thresh = was_on ? (cfg.start_c - cfg.hyst_c) : cfg.start_c;
  if (temp_c < on_thresh) return 0;
  if (temp_c >= cfg.full_c) return 255;
  float frac = (temp_c - cfg.start_c) / (cfg.full_c - cfg.start_c);
  if (frac < 0) frac = 0;             // у зоні гістерезису (нижче start) — тримаємо MIN
  int duty = MIN_DUTY + (int)((255 - MIN_DUTY) * frac);
  if (duty > 255) duty = 255;
  if (duty < MIN_DUTY) duty = MIN_DUTY;
  return duty;
}

int cooling_state(float temp_c, float full_c, float crit_c) {
  if (temp_c >= crit_c) return 2;
  if (temp_c >= full_c) return 1;
  return 0;
}
