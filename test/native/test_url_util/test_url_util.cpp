// Юніт-тести нормалізації URL (pio test -e native).
#include <unity.h>
#include "kernel/url_util.h"

void setUp() {}
void tearDown() {}

void test_bare_host_gets_http() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("192.168.1.50/status", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/status", b);
}

void test_bare_host_no_path() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("device.local", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("http://device.local", b);
}

void test_keeps_http_scheme() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("http://host/x", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("http://host/x", b);
}

void test_keeps_https_scheme() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("https://host/x", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("https://host/x", b);
}

void test_scheme_case_insensitive() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("HTTP://host", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("HTTP://host", b);  // схема лишається як є
}

void test_trims_leading_space() {
  char b[128];
  TEST_ASSERT_TRUE(http_normalize_url("  10.0.0.1", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("http://10.0.0.1", b);
}

void test_empty_invalid() {
  char b[128];
  TEST_ASSERT_FALSE(http_normalize_url("", b, sizeof(b)));
  TEST_ASSERT_FALSE(http_normalize_url("   ", b, sizeof(b)));
  TEST_ASSERT_FALSE(http_normalize_url(nullptr, b, sizeof(b)));
}

void test_overflow() {
  char b[16];
  // "http://" + довгий хост не влізе в 16 -> false
  TEST_ASSERT_FALSE(http_normalize_url("verylonghostname.example.com", b, sizeof(b)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bare_host_gets_http);
  RUN_TEST(test_bare_host_no_path);
  RUN_TEST(test_keeps_http_scheme);
  RUN_TEST(test_keeps_https_scheme);
  RUN_TEST(test_scheme_case_insensitive);
  RUN_TEST(test_trims_leading_space);
  RUN_TEST(test_empty_invalid);
  RUN_TEST(test_overflow);
  return UNITY_END();
}
