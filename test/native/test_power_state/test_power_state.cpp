// Юніт-тести класифікації живлення (pio test -e native).
#include <unity.h>
#include "kernel/power_state.h"

void setUp() {}
void tearDown() {}

void test_on_battery() {
  TEST_ASSERT_EQUAL_INT(PWR_ON_BATTERY, power_classify(3850, -3.0f));
  TEST_ASSERT_EQUAL_INT(PWR_ON_BATTERY, power_classify(3700, 0.0f));
}

void test_charging_by_trend() {
  // напруга помітно росте -> заряджається (навіть при середній напрузі)
  TEST_ASSERT_EQUAL_INT(PWR_CHARGING, power_classify(3900, 15.0f));
}

void test_full_on_usb() {
  TEST_ASSERT_EQUAL_INT(PWR_FULL, power_classify(4300, 1.0f));
}

void test_usb_no_batt() {
  TEST_ASSERT_EQUAL_INT(PWR_USB_NO_BATT, power_classify(4450, 0.0f));
}

void test_runtime_discharging() {
  // 3850мВ, розряд 10 мВ/хв -> (3850-3300)/10 = 55 хв
  TEST_ASSERT_EQUAL_INT(55, power_runtime_estimate_min(3850, -10.0f));
}

void test_runtime_none_when_charging() {
  TEST_ASSERT_EQUAL_INT(-1, power_runtime_estimate_min(4300, 2.0f));
  TEST_ASSERT_EQUAL_INT(-1, power_runtime_estimate_min(3850, 0.0f)); // не розряджається
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_on_battery);
  RUN_TEST(test_charging_by_trend);
  RUN_TEST(test_full_on_usb);
  RUN_TEST(test_usb_no_batt);
  RUN_TEST(test_runtime_discharging);
  RUN_TEST(test_runtime_none_when_charging);
  return UNITY_END();
}
