// Тести чистої power-моделі (оцінка споживання за активними модулями).
#include <unity.h>
#include "kernel/power_model.h"

void setUp() {}
void tearDown() {}

void test_idle() {
  PowerFlags f; f.backlight = 0;   // усе вимкнено, підсвітка 0
  TEST_ASSERT_EQUAL_INT(40, power_model_estimate_ma(f));
}

void test_backlight() {
  PowerFlags f; f.backlight = 255;
  TEST_ASSERT_EQUAL_INT(40 + 32, power_model_estimate_ma(f));   // base + повна підсвітка
  f.backlight = 128;
  TEST_ASSERT_EQUAL_INT(40 + 16, power_model_estimate_ma(f));   // half (32*128/255=16)
}

void test_wifi_ap() {
  PowerFlags f; f.backlight = 0; f.wifi_sta = true; f.soft_ap = true;
  TEST_ASSERT_EQUAL_INT(40 + 70 + 55, power_model_estimate_ma(f));
}

void test_radios() {
  PowerFlags f; f.backlight = 0; f.nrf24 = true; f.cc1101 = true;
  PowerBreakdown b = power_model_breakdown(f);
  TEST_ASSERT_EQUAL_INT(115 + 30, b.radios);
  TEST_ASSERT_EQUAL_INT(40 + 115 + 30, b.total);
}

void test_full() {
  PowerFlags f; f.backlight = 255; f.wifi_sta = true; f.soft_ap = true;
  f.nrf24 = true; f.cc1101 = true; f.sd_write = true; f.bt = true;
  int expect = 40 + 32 + 70 + 55 + 115 + 30 + 80 + 90;
  TEST_ASSERT_EQUAL_INT(expect, power_model_estimate_ma(f));
}

void test_breakdown_sums() {
  PowerFlags f; f.backlight = 200; f.wifi_sta = true; f.sd_write = true;
  PowerBreakdown b = power_model_breakdown(f);
  TEST_ASSERT_EQUAL_INT(b.base + b.backlight + b.wifi + b.radios + b.sd + b.bt, b.total);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_backlight);
  RUN_TEST(test_wifi_ap);
  RUN_TEST(test_radios);
  RUN_TEST(test_full);
  RUN_TEST(test_breakdown_sums);
  return UNITY_END();
}
