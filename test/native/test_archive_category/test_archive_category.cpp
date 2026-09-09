// Тести категоризатора Archive (slug звіту -> домен, аналог FlipperKeyType).
#include <unity.h>
#include "kernel/archive_category.h"
#include <string.h>

void setUp() {}
void tearDown() {}

static void eq(const char* slug, const char* cat) {
  TEST_ASSERT_EQUAL_STRING(cat, archive_category(slug));
}

void test_subghz() {
  eq("subghz_analyzer", "subghz");
  eq("subghz_capture-123_4.txt", "subghz");
}
void test_rfid() {
  eq("rfaudit_FA028732", "rfid");
  eq("rfid_access", "rfid");
  eq("nfc_dump", "rfid");
}
void test_wifi() {
  eq("wifi_analyzer", "wifi");
  eq("channel_monitor", "wifi");
  eq("deauth_alert", "wifi");
}
void test_bluetooth() {
  eq("bt_scan", "bluetooth");
  eq("ble_scan", "bluetooth");
}
void test_detect() {
  eq("tracker_detect", "detect");
  eq("camera_finder", "detect");
  eq("attacker_detect", "detect");
}
void test_network() {
  eq("net_scan", "network");
  eq("arp_scan", "network");
  eq("dns_lookup", "network");
  eq("http_get", "network");
  eq("mdns", "network");
  eq("ssdp", "network");
  eq("traceroute", "network");
  eq("link_qual", "network");
  eq("captive", "network");
  eq("tls_cert", "network");
}
void test_system() {
  eq("self_test", "system");
  eq("battery_diag", "system");
  eq("em_field", "system");
  eq("sys_chip", "system");
}
void test_counter_prefix() {
  // Нове іменування "NNNNNN_<slug>" -> категоризація за slug після лічильника.
  eq("000042_net_scan", "network");
  eq("000007_subghz_analyzer", "subghz");
  eq("001234_wifi_analyzer", "wifi");
  eq("000000_rfaudit_FA02", "rfid");
}

void test_fallback() {
  eq("something_weird", "misc");
  eq("", "misc");
  eq("000005_weirdthing", "misc");   // лічильник + невідомий slug
  TEST_ASSERT_EQUAL_STRING("misc", archive_category(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_subghz);
  RUN_TEST(test_rfid);
  RUN_TEST(test_wifi);
  RUN_TEST(test_bluetooth);
  RUN_TEST(test_detect);
  RUN_TEST(test_network);
  RUN_TEST(test_system);
  RUN_TEST(test_counter_prefix);
  RUN_TEST(test_fallback);
  return UNITY_END();
}
