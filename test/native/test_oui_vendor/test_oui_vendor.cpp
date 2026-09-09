// Юніт-тести OUI-lookup камер + парсингу BSSID (pio test -e native).
#include <unity.h>
#include <string.h>
#include "kernel/oui_vendor.h"

void setUp() {}
void tearDown() {}

void test_lookup_hit() {
  TEST_ASSERT_EQUAL_STRING("Hikvision", oui_camera_vendor(0x44, 0x19, 0xB6));
  TEST_ASSERT_EQUAL_STRING("Dahua", oui_camera_vendor(0x3C, 0xEF, 0x8C));
  TEST_ASSERT_EQUAL_STRING("Axis", oui_camera_vendor(0x00, 0x40, 0x8C));
}

void test_lookup_miss() {
  TEST_ASSERT_NULL(oui_camera_vendor(0x00, 0x11, 0x22));   // не камера
  TEST_ASSERT_NULL(oui_camera_vendor(0xDE, 0xAD, 0xBE));
}

void test_parse_ok() {
  uint8_t a, b, c;
  TEST_ASSERT_TRUE(oui_parse3("44:19:B6:DE:AD:01", &a, &b, &c));
  TEST_ASSERT_EQUAL_HEX8(0x44, a);
  TEST_ASSERT_EQUAL_HEX8(0x19, b);
  TEST_ASSERT_EQUAL_HEX8(0xB6, c);
}

void test_parse_bad() {
  uint8_t a, b, c;
  TEST_ASSERT_FALSE(oui_parse3("zzz", &a, &b, &c));
  TEST_ASSERT_FALSE(oui_parse3("", &a, &b, &c));
  TEST_ASSERT_FALSE(oui_parse3(nullptr, &a, &b, &c));
}

void test_parse_then_lookup() {
  uint8_t a, b, c;
  TEST_ASSERT_TRUE(oui_parse3("90:02:A9:11:22:33", &a, &b, &c));
  TEST_ASSERT_EQUAL_STRING("Dahua", oui_camera_vendor(a, b, c));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lookup_hit);
  RUN_TEST(test_lookup_miss);
  RUN_TEST(test_parse_ok);
  RUN_TEST(test_parse_bad);
  RUN_TEST(test_parse_then_lookup);
  return UNITY_END();
}
