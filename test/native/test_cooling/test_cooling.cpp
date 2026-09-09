// Тести керування охолодженням (temp -> fan-duty з гістерезисом + стан).
#include <unity.h>
#include "kernel/cooling.h"

void setUp() {}
void tearDown() {}

static const CoolingCfg CFG = { 45.0f, 65.0f, 3.0f };

void test_off_below_start() {
  TEST_ASSERT_EQUAL_INT(0, cooling_fan_duty(40.0f, 0, CFG));   // холодно, стоїть
}
void test_starts_at_min() {
  TEST_ASSERT_EQUAL_INT(110, cooling_fan_duty(45.0f, 0, CFG)); // рівно старт -> MIN duty
}
void test_linear_mid() {
  // 55°C = середина 45..65 -> 110 + 145*0.5 = 182
  TEST_ASSERT_EQUAL_INT(182, cooling_fan_duty(55.0f, 110, CFG));
}
void test_full_at_max() {
  TEST_ASSERT_EQUAL_INT(255, cooling_fan_duty(65.0f, 182, CFG));
  TEST_ASSERT_EQUAL_INT(255, cooling_fan_duty(72.0f, 255, CFG));
}
void test_hysteresis() {
  // Крутиться (prev=110), температура 44 (нижче start=45, але вище start-hyst=42) -> лишається MIN
  TEST_ASSERT_EQUAL_INT(110, cooling_fan_duty(44.0f, 110, CFG));
  // 41 (нижче 42) -> вимикається
  TEST_ASSERT_EQUAL_INT(0, cooling_fan_duty(41.0f, 110, CFG));
  // Стояв (prev=0), 44 -> НЕ вмикається (поріг увімкнення = 45)
  TEST_ASSERT_EQUAL_INT(0, cooling_fan_duty(44.0f, 0, CFG));
}
void test_invalid_cfg() {
  CoolingCfg bad = { 65.0f, 45.0f, 3.0f };   // full<start
  TEST_ASSERT_EQUAL_INT(0, cooling_fan_duty(70.0f, 0, bad));
}
void test_state() {
  TEST_ASSERT_EQUAL_INT(0, cooling_state(50.0f, 65.0f, 75.0f));  // OK
  TEST_ASSERT_EQUAL_INT(1, cooling_state(68.0f, 65.0f, 75.0f));  // WARN
  TEST_ASSERT_EQUAL_INT(2, cooling_state(80.0f, 65.0f, 75.0f));  // CRITICAL
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_below_start);
  RUN_TEST(test_starts_at_min);
  RUN_TEST(test_linear_mid);
  RUN_TEST(test_full_at_max);
  RUN_TEST(test_hysteresis);
  RUN_TEST(test_invalid_cfg);
  RUN_TEST(test_state);
  return UNITY_END();
}
