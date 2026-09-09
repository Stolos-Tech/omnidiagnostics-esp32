// Юніт-тести детектора tap/hold (pio test -e native).
#include <unity.h>
#include "drivers/tap_hold.h"

void setUp() {}
void tearDown() {}

static const uint32_t DB = TapHoldDetector::DEBOUNCE_MS;
static const uint32_t HOLD = TapHoldDetector::HOLD_MS;

// Короткий тап: подія TAP на відпусканні, не HOLD
void test_tap_on_release() {
  TapHoldDetector d;
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 100));       // натиск, вікно
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 100 + DB));  // стійкий натиск
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 300));       // тримаємо < HOLD
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(true, 320));        // відпускання, вікно
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::TAP, d.update(true, 320 + DB));    // TAP на стійкому відпусканні
}

// Довге утримання: HOLD при досягненні порогу, TAP на відпусканні НЕ приходить
void test_hold_then_no_tap() {
  TapHoldDetector d;
  d.update(false, 100);
  d.update(false, 100 + DB);                 // стійкий натиск (press_start)
  // тримаємо до порогу
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 100 + DB + HOLD - 1));
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::HOLD, d.update(false, 100 + DB + HOLD));
  // повторно HOLD не віддається
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 100 + DB + HOLD + 100));
  // відпускання після HOLD -> без TAP
  d.update(true, 2000);
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(true, 2000 + DB));
}

// Брязкіт коротший за вікно не дає подій
void test_bounce_no_event() {
  TapHoldDetector d;
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 100));
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(true, 110));
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(false, 115));
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(true, 120));
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::NONE, d.update(true, 120 + DB)); // заспокоїлось на HIGH
}

// Два тапи поспіль
void test_two_taps() {
  TapHoldDetector d;
  d.update(false, 100); d.update(false, 100 + DB);
  d.update(true, 200);
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::TAP, d.update(true, 200 + DB));
  d.update(false, 400); d.update(false, 400 + DB);
  d.update(true, 500);
  TEST_ASSERT_EQUAL_INT(TapHoldDetector::TAP, d.update(true, 500 + DB));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tap_on_release);
  RUN_TEST(test_hold_then_no_tap);
  RUN_TEST(test_bounce_no_event);
  RUN_TEST(test_two_taps);
  return UNITY_END();
}
