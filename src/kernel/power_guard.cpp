#include "power_guard.h"

bool power_guard_radio_ok(int mv) {
  return mv >= POWER_GUARD_MIN_MV;
}
