// Юніт-тести чистої логіки 2.4GHz-аналізатора nRF24 (pio test -e native).
#include <unity.h>
#include "kernel/rf24_util.h"

void setUp() {}
void tearDown() {}

void test_channel_mhz() {
  TEST_ASSERT_EQUAL_INT(2400, rf24_channel_mhz(0));
  TEST_ASSERT_EQUAL_INT(2437, rf24_channel_mhz(37));
  TEST_ASSERT_EQUAL_INT(2525, rf24_channel_mhz(125));
  TEST_ASSERT_EQUAL_INT(2400, rf24_channel_mhz(-5));    // clamp знизу
  TEST_ASSERT_EQUAL_INT(2525, rf24_channel_mhz(999));   // clamp зверху
}

void test_wifi_channel() {
  TEST_ASSERT_EQUAL_INT(1,  rf24_wifi_channel(12));   // 2412 = WiFi ch1
  TEST_ASSERT_EQUAL_INT(6,  rf24_wifi_channel(37));   // 2437 = WiFi ch6
  TEST_ASSERT_EQUAL_INT(11, rf24_wifi_channel(62));   // 2462 = WiFi ch11
  TEST_ASSERT_EQUAL_INT(0,  rf24_wifi_channel(120));  // 2520 поза WiFi-смугою
}

void test_peak_index() {
  uint8_t c[5] = {1, 5, 3, 5, 0};
  TEST_ASSERT_EQUAL_INT(1, rf24_peak_index(c, 5));    // перший максимум при рівності
  TEST_ASSERT_EQUAL_INT(-1, rf24_peak_index(nullptr, 5));
  TEST_ASSERT_EQUAL_INT(-1, rf24_peak_index(c, 0));
}

void test_bar_height() {
  TEST_ASSERT_EQUAL_INT(40, rf24_bar_height(10, 10, 40));
  TEST_ASSERT_EQUAL_INT(20, rf24_bar_height(5, 10, 40));
  TEST_ASSERT_EQUAL_INT(0,  rf24_bar_height(0, 10, 40));   // нуль лічильник
  TEST_ASSERT_EQUAL_INT(0,  rf24_bar_height(5, 0, 40));    // нуль max
  TEST_ASSERT_EQUAL_INT(40, rf24_bar_height(99, 10, 40));  // clamp до h
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_channel_mhz);
  RUN_TEST(test_wifi_channel);
  RUN_TEST(test_peak_index);
  RUN_TEST(test_bar_height);
  return UNITY_END();
}
