// Юніт-тести евристики Card Skimmer за іменем BT-модуля (pio test -e native).
#include <unity.h>
#include "kernel/skimmer_id.h"

void setUp() {}
void tearDown() {}

void test_suspect_prefixes() {
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("HC-05"));
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("HC-06"));
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("hc-05"));       // регістронезалежно
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("BT04-A"));
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("JDY-31"));
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("RNBT-1234"));
}

void test_suspect_substrings() {
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("linvor"));
  TEST_ASSERT_TRUE(skimmer_is_suspect_name("My FireFly Mod"));
}

void test_clean_names() {
  TEST_ASSERT_FALSE(skimmer_is_suspect_name("iPhone"));
  TEST_ASSERT_FALSE(skimmer_is_suspect_name("Galaxy Buds"));
  TEST_ASSERT_FALSE(skimmer_is_suspect_name("JBL Speaker"));
  TEST_ASSERT_FALSE(skimmer_is_suspect_name(""));
  TEST_ASSERT_FALSE(skimmer_is_suspect_name(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_suspect_prefixes);
  RUN_TEST(test_suspect_substrings);
  RUN_TEST(test_clean_names);
  return UNITY_END();
}
