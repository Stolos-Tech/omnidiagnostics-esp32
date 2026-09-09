// Юніт-тести утиліт форматування (pio test -e native).
#include <unity.h>
#include "kernel/format_util.h"

void setUp() {}
void tearDown() {}

void test_uptime_seconds() {
  char b[24];
  format_uptime(9, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0h 00m 09s", b);
}

void test_uptime_minutes() {
  char b[24];
  format_uptime(3 * 60 + 5, b, sizeof(b));  // 3m 05s
  TEST_ASSERT_EQUAL_STRING("0h 03m 05s", b);
}

void test_uptime_hours() {
  char b[24];
  format_uptime(2 * 3600 + 7 * 60 + 8, b, sizeof(b));  // 2h 07m 08s
  TEST_ASSERT_EQUAL_STRING("2h 07m 08s", b);
}

void test_uptime_days() {
  char b[24];
  format_uptime(2 * 86400 + 3 * 3600 + 4 * 60, b, sizeof(b));  // 2d 03h 04m
  TEST_ASSERT_EQUAL_STRING("2d 03h 04m", b);
}

void test_bytes_b() {
  char b[24];
  format_bytes(512, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("512 B", b);
}

void test_bytes_kb() {
  char b[24];
  format_bytes(48 * 1024, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("48 KB", b);
}

void test_bytes_mb() {
  char b[24];
  format_bytes(4 * 1024 * 1024, b, sizeof(b));  // рівно 4.00 MB
  TEST_ASSERT_EQUAL_STRING("4.00 MB", b);
}

void test_bytes_mb_frac() {
  char b[24];
  // 1.5 MB = 1572864 -> "1.50 MB"
  format_bytes(1572864u, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1.50 MB", b);
}

void test_mmss() {
  char b[16];
  format_mmss(9, b, sizeof(b));        TEST_ASSERT_EQUAL_STRING("00:09", b);
  format_mmss(65, b, sizeof(b));       TEST_ASSERT_EQUAL_STRING("01:05", b);
  format_mmss(3661, b, sizeof(b));     TEST_ASSERT_EQUAL_STRING("1:01:01", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_mmss);
  RUN_TEST(test_uptime_seconds);
  RUN_TEST(test_uptime_minutes);
  RUN_TEST(test_uptime_hours);
  RUN_TEST(test_uptime_days);
  RUN_TEST(test_bytes_b);
  RUN_TEST(test_bytes_kb);
  RUN_TEST(test_bytes_mb);
  RUN_TEST(test_bytes_mb_frac);
  return UNITY_END();
}
