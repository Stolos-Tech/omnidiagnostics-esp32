// Юніт-тести крос-модульного RAM-store (pio test -e native).
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "kernel/shared_store.h"

void setUp() { shared_store_reset(); }
void tearDown() {}

void test_hosts_add_and_dedup() {
  shared_add_host("192.168.1.10", "web");
  shared_add_host("192.168.1.20", "ssh");
  shared_add_host("192.168.1.10", "web+ssh");   // дедуп за ip -> оновити note
  TEST_ASSERT_EQUAL_INT(2, shared_host_count());
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", shared_host(0)->ip);
  TEST_ASSERT_EQUAL_STRING("web+ssh", shared_host(0)->note);
  TEST_ASSERT_NULL(shared_host(2));
  TEST_ASSERT_NULL(shared_host(-1));
}

void test_hosts_reject_empty() {
  shared_add_host("", "x");
  shared_add_host(nullptr, "x");
  TEST_ASSERT_EQUAL_INT(0, shared_host_count());
}

void test_hosts_overflow() {
  char ip[16];
  for (int i = 0; i < SHARED_MAX_HOSTS + 8; i++) { snprintf(ip, sizeof(ip), "10.0.0.%d", i); shared_add_host(ip, ""); }
  TEST_ASSERT_EQUAL_INT(SHARED_MAX_HOSTS, shared_host_count());
}

void test_nets_add_dedup_keep_strongest() {
  shared_add_net("NewYork", 6, -70, "WPA2");
  shared_add_net("Sofiia",  1, -55, "WPA2");
  shared_add_net("NewYork", 6, -48, "WPA2");   // дедуп за ssid -> лишити сильніший rssi
  shared_add_net("NewYork", 6, -90, "WPA2");   // слабший -> ігнор
  TEST_ASSERT_EQUAL_INT(2, shared_net_count());
  TEST_ASSERT_EQUAL_STRING("NewYork", shared_net(0)->ssid);
  TEST_ASSERT_EQUAL_INT(-48, shared_net(0)->rssi);
  TEST_ASSERT_EQUAL_INT(6, shared_net(0)->ch);
}

void test_reset() {
  shared_add_host("1.1.1.1", "");
  shared_add_net("X", 1, -50, "OPEN");
  shared_store_reset();
  TEST_ASSERT_EQUAL_INT(0, shared_host_count());
  TEST_ASSERT_EQUAL_INT(0, shared_net_count());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hosts_add_and_dedup);
  RUN_TEST(test_hosts_reject_empty);
  RUN_TEST(test_hosts_overflow);
  RUN_TEST(test_nets_add_dedup_keep_strongest);
  RUN_TEST(test_reset);
  return UNITY_END();
}
