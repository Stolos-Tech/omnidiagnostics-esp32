// Юніт-тести евристики Attacker Detect (pwnagotchi SSID) (pio test -e native).
#include <unity.h>
#include "kernel/attacker_id.h"

void setUp() {}
void tearDown() {}

void test_pwnagotchi() {
  TEST_ASSERT_TRUE(attacker_is_pwnagotchi_ssid("{\"name\":\"pwn\",\"pwnd_tot\":5}"));
  TEST_ASSERT_TRUE(attacker_is_pwnagotchi_ssid("{\"epoch\":12}"));
}

void test_normal_ssids() {
  TEST_ASSERT_FALSE(attacker_is_pwnagotchi_ssid("HomeWiFi"));
  TEST_ASSERT_FALSE(attacker_is_pwnagotchi_ssid("{noquotes}"));  // '{' але без лапок
  TEST_ASSERT_FALSE(attacker_is_pwnagotchi_ssid(""));
  TEST_ASSERT_FALSE(attacker_is_pwnagotchi_ssid(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pwnagotchi);
  RUN_TEST(test_normal_ssids);
  return UNITY_END();
}
