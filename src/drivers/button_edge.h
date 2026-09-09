// Чиста логіка edge-detection для кнопок S1-S5 — без апаратних викликів.
// Правило проєкту: подія кнопки = ТІЛЬКИ перехід HIGH->LOW (натиск),
// пряме опитування рівня заборонено (історичний баг самовільного гортання).
// Апаратний шар (digitalRead, піни) — у drivers/buttons.* (Arduino-залежний).
#pragma once
#include <stdint.h>

class ButtonEdgeDetector {
public:
  static const uint32_t DEBOUNCE_MS = 30;
  static const int MAX_BUTTONS = 5;

  ButtonEdgeDetector();

  // level: true = HIGH (відпущено, pullup), false = LOW (натиснуто).
  // Повертає true рівно один раз на стійкий перехід HIGH->LOW.
  // now_ms передається ззовні (millis() на залізі, довільне число в тестах).
  bool update(int idx, bool level, uint32_t now_ms);

private:
  bool prev_level_[MAX_BUTTONS];       // останній стійкий рівень
  uint32_t last_change_ms_[MAX_BUTTONS]; // час останньої зміни сирого рівня
  bool raw_level_[MAX_BUTTONS];        // останній сирий рівень
};
