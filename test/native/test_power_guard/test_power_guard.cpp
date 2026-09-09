// Юніт-тести порогу brownout-захисту (pio test -e native).
#include <unity.h>
#include "kernel/power_guard.h"

void setUp() {}
void tearDown() {}

void test_below_threshold_rejected() {
  TEST_ASSERT_FALSE(power_guard_radio_ok(3499));
  TEST_ASSERT_FALSE(power_guard_radio_ok(3000));
  TEST_ASSERT_FALSE(power_guard_radio_ok(0));
}

void test_at_or_above_threshold_ok() {
  TEST_ASSERT_TRUE(power_guard_radio_ok(3500));
  TEST_ASSERT_TRUE(power_guard_radio_ok(4200));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_below_threshold_rejected);
  RUN_TEST(test_at_or_above_threshold_ok);
  return UNITY_END();
}
