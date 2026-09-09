#include "button_edge.h"

ButtonEdgeDetector::ButtonEdgeDetector() {
  for (int i = 0; i < MAX_BUTTONS; i++) {
    prev_level_[i] = true;   // pullup: у спокої HIGH
    raw_level_[i] = true;
    last_change_ms_[i] = 0;
  }
}

bool ButtonEdgeDetector::update(int idx, bool level, uint32_t now_ms) {
  if (idx < 0 || idx >= MAX_BUTTONS) return false;

  // Сирий рівень змінився — перезапускаємо вікно антибрязкоту
  if (level != raw_level_[idx]) {
    raw_level_[idx] = level;
    last_change_ms_[idx] = now_ms;
    return false;
  }

  // Рівень стабільний недостатньо довго — чекаємо
  if (now_ms - last_change_ms_[idx] < DEBOUNCE_MS) return false;

  // Стійкий рівень відрізняється від попереднього стійкого — це перехід
  if (level != prev_level_[idx]) {
    bool was_high = prev_level_[idx];
    prev_level_[idx] = level;
    return was_high && !level;  // подія тільки на HIGH->LOW
  }
  return false;
}
