// Юніт-тести трьохступеневого згасання підсвітки (pio test -e native).
#include <unity.h>
#include "kernel/backlight_policy.h"

void setUp() {}
void tearDown() {}

void test_full_when_active() {
  TEST_ASSERT_EQUAL_INT(200, backlight_level_for_idle(0, 200));
  TEST_ASSERT_EQUAL_INT(200, backlight_level_for_idle(BACKLIGHT_DIM_MS, 200));
}

void test_dim_after_threshold() {
  TEST_ASSERT_EQUAL_INT(50, backlight_level_for_idle(BACKLIGHT_DIM_MS + 1, 200));
  TEST_ASSERT_EQUAL_INT(50, backlight_level_for_idle(BACKLIGHT_OFF_MS, 200));
}

void test_off_after_threshold() {
  TEST_ASSERT_EQUAL_INT(0, backlight_level_for_idle(BACKLIGHT_OFF_MS + 1, 200));
  TEST_ASSERT_EQUAL_INT(0, backlight_level_for_idle(999999, 200));
}

void test_clamps_full_level() {
  TEST_ASSERT_EQUAL_INT(255, backlight_level_for_idle(0, 999));
  TEST_ASSERT_EQUAL_INT(0, backlight_level_for_idle(0, -5));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_when_active);
  RUN_TEST(test_dim_after_threshold);
  RUN_TEST(test_off_after_threshold);
  RUN_TEST(test_clamps_full_level);
  return UNITY_END();
}
