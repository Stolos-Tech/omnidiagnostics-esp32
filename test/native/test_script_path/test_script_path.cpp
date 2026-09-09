// Юніт-тести розбору імені .be-скрипта зі шляху (pio test -e native)
#include <unity.h>
#include <string.h>
#include "kernel/script_path.h"

void setUp() {}
void tearDown() {}

void test_full_path() {
  char out[32];
  TEST_ASSERT_TRUE(script_name_from_path("/apps/battery.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("battery", out);
}

void test_bare_filename() {
  char out[32];
  TEST_ASSERT_TRUE(script_name_from_path("wifi_scan.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("wifi_scan", out);
}

void test_nested_path() {
  char out[32];
  TEST_ASSERT_TRUE(script_name_from_path("/apps/sub/thing.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("thing", out);
}

void test_not_be_extension() {
  char out[32];
  TEST_ASSERT_FALSE(script_name_from_path("/apps/readme.txt", out, sizeof(out)));
  TEST_ASSERT_FALSE(script_name_from_path("/apps/battery.bec", out, sizeof(out)));
}

void test_empty_name() {
  char out[32];
  TEST_ASSERT_FALSE(script_name_from_path(".be", out, sizeof(out)));
  TEST_ASSERT_FALSE(script_name_from_path("/apps/.be", out, sizeof(out)));
}

void test_null_and_short_buffer() {
  char out[4];
  TEST_ASSERT_FALSE(script_name_from_path(nullptr, out, sizeof(out)));
  // усічення в короткий буфер: "battery" -> "bat"
  TEST_ASSERT_TRUE(script_name_from_path("/apps/battery.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("bat", out);
}

void test_category_from_path() {
  char out[16];
  TEST_ASSERT_TRUE(script_category_from_path("/apps/net/scan.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("net", out);
  TEST_ASSERT_TRUE(script_category_from_path("/apps/gpio/pot.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("gpio", out);
}

void test_category_legacy_flat_is_misc() {
  char out[16];
  // стара пласка розкладка /apps/*.be — категорія "misc"
  TEST_ASSERT_TRUE(script_category_from_path("/apps/battery.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("misc", out);
  // невідома тека теж -> misc (чужий файл не має ламати меню)
  TEST_ASSERT_TRUE(script_category_from_path("/apps/hacks/x.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("misc", out);
}

void test_category_rejects_non_be() {
  char out[16];
  TEST_ASSERT_FALSE(script_category_from_path("/apps/net/readme.txt", out, sizeof(out)));
  TEST_ASSERT_FALSE(script_category_from_path(nullptr, out, sizeof(out)));
}

void test_category_valid() {
  TEST_ASSERT_TRUE(script_category_valid("system"));
  TEST_ASSERT_TRUE(script_category_valid("misc"));
  TEST_ASSERT_FALSE(script_category_valid("nope"));
  TEST_ASSERT_FALSE(script_category_valid(""));
  TEST_ASSERT_FALSE(script_category_valid(nullptr));
}

void test_build_path() {
  char out[64];
  TEST_ASSERT_TRUE(script_build_path("net", "scan.be", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("/apps/net/scan.be", out);
}

void test_build_path_rejects() {
  char out[64];
  TEST_ASSERT_FALSE(script_build_path("bogus", "a.be", out, sizeof(out)));   // невідома категорія
  TEST_ASSERT_FALSE(script_build_path("net", "sub/a.be", out, sizeof(out))); // слеш в імені
  TEST_ASSERT_FALSE(script_build_path("net", "a.txt", out, sizeof(out)));    // не .be
  TEST_ASSERT_FALSE(script_build_path(nullptr, "a.be", out, sizeof(out)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_path);
  RUN_TEST(test_bare_filename);
  RUN_TEST(test_nested_path);
  RUN_TEST(test_not_be_extension);
  RUN_TEST(test_empty_name);
  RUN_TEST(test_null_and_short_buffer);
  RUN_TEST(test_category_from_path);
  RUN_TEST(test_category_legacy_flat_is_misc);
  RUN_TEST(test_category_rejects_non_be);
  RUN_TEST(test_category_valid);
  RUN_TEST(test_build_path);
  RUN_TEST(test_build_path_rejects);
  return UNITY_END();
}
