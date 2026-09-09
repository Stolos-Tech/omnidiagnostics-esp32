// Юніт-тести чистої логіки суб-ГГц (CC1101): план свіпу, частоти<->регістри, RSSI.
#include <unity.h>
#include "kernel/subghz_util.h"

void setUp() {}
void tearDown() {}

void test_scan_plan() {
  TEST_ASSERT_EQUAL_INT(139, subghz_scan_count());      // 25 + 39 + 75
  TEST_ASSERT_EQUAL_INT(300000, subghz_scan_khz(0));
  TEST_ASSERT_EQUAL_INT(348000, subghz_scan_khz(24));   // кінець вікна A
  TEST_ASSERT_EQUAL_INT(388000, subghz_scan_khz(25));   // початок B
  TEST_ASSERT_EQUAL_INT(464000, subghz_scan_khz(63));   // кінець B
  TEST_ASSERT_EQUAL_INT(780000, subghz_scan_khz(64));   // початок C
  TEST_ASSERT_EQUAL_INT(928000, subghz_scan_khz(138));  // кінець C
  TEST_ASSERT_EQUAL_INT(-1, subghz_scan_khz(139));       // поза межами
  TEST_ASSERT_EQUAL_INT(-1, subghz_scan_khz(-1));
  TEST_ASSERT_EQUAL_INT(0, subghz_scan_window(0));
  TEST_ASSERT_EQUAL_INT(0, subghz_scan_window(24));
  TEST_ASSERT_EQUAL_INT(1, subghz_scan_window(25));
  TEST_ASSERT_EQUAL_INT(2, subghz_scan_window(64));
  TEST_ASSERT_EQUAL_INT(2, subghz_scan_window(138));
  TEST_ASSERT_EQUAL_INT(-1, subghz_scan_window(139));
}

void test_freq_roundtrip() {
  const long targets[] = { 433920, 868350, 315000, 915000, 300000, 928000 };
  for (int i = 0; i < 6; i++) {
    uint8_t f2, f1, f0;
    subghz_freq_to_regs(targets[i], &f2, &f1, &f0);
    long back = subghz_regs_to_khz(f2, f1, f0);
    // Крок дискретизації ~0.397 кГц -> round-trip у межах 1 кГц.
    TEST_ASSERT_INT_WITHIN(1, targets[i], back);
  }
}

void test_freq_regs_known() {
  // 433.92 МГц: reg = round(433920 * 65536 / 26000) = 1093746 -> байти 0x10 0xB0 0x72.
  uint8_t f2, f1, f0;
  subghz_freq_to_regs(433920, &f2, &f1, &f0);
  uint32_t reg = ((uint32_t)f2 << 16) | ((uint32_t)f1 << 8) | f0;
  TEST_ASSERT_UINT32_WITHIN(1, 1093746u, reg);
}

void test_rssi_dbm() {
  TEST_ASSERT_EQUAL_INT(-74, subghz_rssi_dbm(0));
  TEST_ASSERT_EQUAL_INT(-50, subghz_rssi_dbm(48));    // 48/2-74
  TEST_ASSERT_EQUAL_INT(-102, subghz_rssi_dbm(200));  // (200-256)/2-74 = -28-74
}

void test_peak_and_bar() {
  int8_t d[4] = { -90, -50, -70, -50 };
  TEST_ASSERT_EQUAL_INT(1, subghz_peak_index(d, 4));   // перший максимум при рівності
  TEST_ASSERT_EQUAL_INT(-1, subghz_peak_index(nullptr, 4));
  TEST_ASSERT_EQUAL_INT(50, subghz_bar_height(-50, -100, -30, 70));  // середина шкали
  TEST_ASSERT_EQUAL_INT(0,  subghz_bar_height(-100, -100, -30, 70)); // на підлозі
  TEST_ASSERT_EQUAL_INT(70, subghz_bar_height(-20, -100, -30, 70));  // clamp зверху
}

void test_presets() {
  TEST_ASSERT_EQUAL_INT(5, subghz_preset_count());
  TEST_ASSERT_EQUAL_INT(433920, subghz_preset_khz(0));
  TEST_ASSERT_EQUAL_STRING("433.92", subghz_preset_name(0));
  TEST_ASSERT_EQUAL_INT(-1, subghz_preset_khz(99));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scan_plan);
  RUN_TEST(test_freq_roundtrip);
  RUN_TEST(test_freq_regs_known);
  RUN_TEST(test_rssi_dbm);
  RUN_TEST(test_peak_and_bar);
  RUN_TEST(test_presets);
  return UNITY_END();
}
