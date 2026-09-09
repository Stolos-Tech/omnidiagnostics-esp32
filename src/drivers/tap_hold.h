// Детектор коротко/довго натискання (tap vs hold) для однієї кнопки — чиста
// логіка з ін'єкцією часу, тестується на хості. Використовується для лівої
// кнопки: короткий тап = дія (S5), довге утримання = універсальний "назад".
//
// Tap віддається на ВІДПУСКАННІ (щоб довге утримання не спрацювало як тап).
#pragma once
#include <stdint.h>

class TapHoldDetector {
public:
  static const uint32_t DEBOUNCE_MS = 30;
  static const uint32_t HOLD_MS     = 600;

  enum Event : uint8_t { NONE = 0, TAP, HOLD };

  TapHoldDetector();

  // level: true = HIGH (відпущено, pullup), false = LOW (натиснуто).
  // Повертає TAP (короткий, на відпусканні) або HOLD (при досягненні порогу,
  // один раз, ще під час утримання) або NONE.
  Event update(bool level, uint32_t now_ms);

private:
  bool raw_;             // останній сирий рівень
  bool stable_;          // стійкий рівень
  uint32_t last_change_; // час останньої зміни сирого рівня
  uint32_t press_start_; // час початку стійкого натиску
  bool hold_fired_;      // HOLD уже віддано в цьому натиску
};
