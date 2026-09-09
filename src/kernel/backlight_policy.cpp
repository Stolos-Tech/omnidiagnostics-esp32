#include "backlight_policy.h"

int backlight_level_for_idle(uint32_t idle_ms, int full_level) {
  if (full_level < 0) full_level = 0;
  if (full_level > 255) full_level = 255;
  if (idle_ms > BACKLIGHT_OFF_MS) return 0;
  if (idle_ms > BACKLIGHT_DIM_MS) return full_level / 4;   // ~25% — видно, але приглушено
  return full_level;
}
