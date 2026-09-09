#include "power_guard_hw.h"
#include "battery_adc.h"
#include "../kernel/power_guard.h"

static int s_last_mv = 0;

bool power_guard_ok() {
  s_last_mv = (int)(battery_read_cached(1000) * 1000.0f);   // кеш: guard біжить раз/3с, свіжості <1с досить
  return power_guard_radio_ok(s_last_mv);
}

int power_guard_last_mv() { return s_last_mv; }
