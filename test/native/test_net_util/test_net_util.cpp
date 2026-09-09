// Юніт-тести IPv4/підмереж (pio test -e native).
#include <unity.h>
#include <string.h>
#include "kernel/net_util.h"

void setUp() {}
void tearDown() {}

void test_ip_to_str() {
  char b[20];
  net_ip_to_str(net_make_ip(192, 168, 1, 50), b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", b);
  net_ip_to_str(net_make_ip(0, 0, 0, 0), b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", b);
  net_ip_to_str(net_make_ip(255, 255, 255, 255), b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", b);
}

void test_str_to_ip_valid() {
  uint32_t ip = 0;
  TEST_ASSERT_TRUE(net_str_to_ip("10.0.0.1", &ip));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(10, 0, 0, 1), ip);
}

void test_str_to_ip_invalid() {
  uint32_t ip = 0;
  TEST_ASSERT_FALSE(net_str_to_ip("256.1.1.1", &ip));   // октет > 255
  TEST_ASSERT_FALSE(net_str_to_ip("1.2.3", &ip));       // мало октетів
  TEST_ASSERT_FALSE(net_str_to_ip("1.2.3.4.5", &ip));   // забагато
  TEST_ASSERT_FALSE(net_str_to_ip("1.2.3.x", &ip));     // не число
  TEST_ASSERT_FALSE(net_str_to_ip("", &ip));
  TEST_ASSERT_FALSE(net_str_to_ip(nullptr, &ip));
}

void test_subnet_24() {
  uint32_t ip = net_make_ip(192, 168, 1, 50);
  uint32_t mask = net_make_ip(255, 255, 255, 0);
  uint32_t fh, lh, cnt;
  TEST_ASSERT_TRUE(net_subnet_range(ip, mask, &fh, &lh, &cnt));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(192, 168, 1, 1), fh);
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(192, 168, 1, 254), lh);
  TEST_ASSERT_EQUAL_UINT32(254, cnt);
}

void test_subnet_16() {
  uint32_t ip = net_make_ip(10, 0, 5, 7);
  uint32_t mask = net_make_ip(255, 255, 0, 0);
  uint32_t fh, lh, cnt;
  TEST_ASSERT_TRUE(net_subnet_range(ip, mask, &fh, &lh, &cnt));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(10, 0, 0, 1), fh);
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(10, 0, 255, 254), lh);
  TEST_ASSERT_EQUAL_UINT32(65534, cnt);
}

void test_subnet_no_hosts() {
  uint32_t ip = net_make_ip(192, 168, 1, 1);
  // /31 і /32 — хостів нема
  TEST_ASSERT_FALSE(net_subnet_range(ip, net_make_ip(255,255,255,254), nullptr, nullptr, nullptr));
  TEST_ASSERT_FALSE(net_subnet_range(ip, net_make_ip(255,255,255,255), nullptr, nullptr, nullptr));
}

void test_prefix_to_mask() {
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,255,0), net_prefix_to_mask(24));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,0,0),   net_prefix_to_mask(16));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu,                net_prefix_to_mask(32));
  TEST_ASSERT_EQUAL_UINT32(0u,                         net_prefix_to_mask(0));
}

void test_cidr_full() {
  uint32_t ip, mask;
  TEST_ASSERT_TRUE(net_parse_cidr("192.168.1.0/24", &ip, &mask));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(192,168,1,0), ip);
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,255,0), mask);
}

void test_cidr_16() {
  uint32_t ip, mask;
  TEST_ASSERT_TRUE(net_parse_cidr("10.0.0.5/16", &ip, &mask));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(10,0,0,5), ip);
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,0,0), mask);
}

void test_cidr_short_implies_24() {
  uint32_t ip, mask;
  TEST_ASSERT_TRUE(net_parse_cidr("192.168.1", &ip, &mask));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(192,168,1,0), ip);
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,255,0), mask);
}

void test_cidr_no_prefix_default_24() {
  uint32_t ip, mask;
  TEST_ASSERT_TRUE(net_parse_cidr("192.168.5.50", &ip, &mask));
  TEST_ASSERT_EQUAL_UINT32(net_make_ip(255,255,255,0), mask);
}

void test_cidr_invalid() {
  uint32_t ip, mask;
  TEST_ASSERT_FALSE(net_parse_cidr("192.168.1.0/33", &ip, &mask));  // префікс > 32
  TEST_ASSERT_FALSE(net_parse_cidr("300.1.1.1/24", &ip, &mask));    // октет > 255
  TEST_ASSERT_FALSE(net_parse_cidr("nonsense", &ip, &mask));
  TEST_ASSERT_FALSE(net_parse_cidr("1.2.3.4/", &ip, &mask));        // порожній префікс
}

void test_ports_basic() {
  uint16_t p[8];
  int n = net_parse_ports("80,443, 22", p, 8);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_UINT16(80, p[0]);
  TEST_ASSERT_EQUAL_UINT16(443, p[1]);
  TEST_ASSERT_EQUAL_UINT16(22, p[2]);
}

void test_ports_filters_bad() {
  uint16_t p[8];
  // 70000 > 65535 відкидається, "x" ігнорується, 0 недійсний
  int n = net_parse_ports("80,70000,x,0,8080", p, 8);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_UINT16(80, p[0]);
  TEST_ASSERT_EQUAL_UINT16(8080, p[1]);
}

void test_ports_max_cap() {
  uint16_t p[2];
  int n = net_parse_ports("1,2,3,4,5", p, 2);
  TEST_ASSERT_EQUAL_INT(2, n);
}

void test_ports_empty() {
  uint16_t p[8];
  TEST_ASSERT_EQUAL_INT(0, net_parse_ports("", p, 8));
  TEST_ASSERT_EQUAL_INT(0, net_parse_ports(",, ,", p, 8));
  TEST_ASSERT_EQUAL_INT(0, net_parse_ports(nullptr, p, 8));
}

void test_hostport_full() {
  char h[40]; uint16_t p = 0;
  TEST_ASSERT_TRUE(net_parse_hostport("192.168.1.1:23", h, sizeof(h), &p, 80));
  TEST_ASSERT_EQUAL_STRING("192.168.1.1", h);
  TEST_ASSERT_EQUAL_UINT16(23, p);
}

void test_hostport_default() {
  char h[40]; uint16_t p = 0;
  TEST_ASSERT_TRUE(net_parse_hostport("example.com", h, sizeof(h), &p, 80));
  TEST_ASSERT_EQUAL_STRING("example.com", h);
  TEST_ASSERT_EQUAL_UINT16(80, p);
}

void test_hostport_trims() {
  char h[40]; uint16_t p = 0;
  TEST_ASSERT_TRUE(net_parse_hostport("  host : 8080", h, sizeof(h), &p, 80));
  TEST_ASSERT_EQUAL_STRING("host", h);
  TEST_ASSERT_EQUAL_UINT16(8080, p);
}

void test_hostport_invalid() {
  char h[40]; uint16_t p = 0;
  TEST_ASSERT_FALSE(net_parse_hostport("", h, sizeof(h), &p, 80));
  TEST_ASSERT_FALSE(net_parse_hostport(":23", h, sizeof(h), &p, 80));       // порожній host
  TEST_ASSERT_FALSE(net_parse_hostport("host:", h, sizeof(h), &p, 80));     // порожній порт
  TEST_ASSERT_FALSE(net_parse_hostport("host:70000", h, sizeof(h), &p, 80));// порт > 65535
  TEST_ASSERT_FALSE(net_parse_hostport("host:xx", h, sizeof(h), &p, 80));   // нечисловий порт
}

void test_mac_colon() {
  uint8_t m[6];
  TEST_ASSERT_TRUE(net_parse_mac("AA:BB:CC:DD:EE:FF", m));
  uint8_t exp[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, m, 6);
}

void test_mac_dash_and_bare() {
  uint8_t m[6];
  TEST_ASSERT_TRUE(net_parse_mac("aa-bb-cc-dd-ee-ff", m));
  uint8_t exp[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, m, 6);
  TEST_ASSERT_TRUE(net_parse_mac("001122334455", m));
  uint8_t exp2[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(exp2, m, 6);
}

void test_mac_invalid() {
  uint8_t m[6];
  TEST_ASSERT_FALSE(net_parse_mac("AA:BB:CC:DD:EE", m));       // мало байтів
  TEST_ASSERT_FALSE(net_parse_mac("AA:BB:CC:DD:EE:FF:00", m)); // забагато
  TEST_ASSERT_FALSE(net_parse_mac("ZZ:BB:CC:DD:EE:FF", m));    // не hex
  TEST_ASSERT_FALSE(net_parse_mac("", m));
  TEST_ASSERT_FALSE(net_parse_mac(nullptr, m));
}

void test_wol_packet() {
  uint8_t mac[6] = {0x01,0x02,0x03,0x04,0x05,0x06};
  uint8_t pkt[102];
  TEST_ASSERT_EQUAL_INT(102, wol_build_packet(mac, pkt, sizeof(pkt)));
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(0xFF, pkt[i]);
  for (int i = 0; i < 16; i++)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, pkt + 6 + i * 6, 6);
}

void test_wol_packet_too_small() {
  uint8_t mac[6] = {0};
  uint8_t pkt[10];
  TEST_ASSERT_EQUAL_INT(0, wol_build_packet(mac, pkt, sizeof(pkt)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ip_to_str);
  RUN_TEST(test_str_to_ip_valid);
  RUN_TEST(test_str_to_ip_invalid);
  RUN_TEST(test_subnet_24);
  RUN_TEST(test_subnet_16);
  RUN_TEST(test_subnet_no_hosts);
  RUN_TEST(test_prefix_to_mask);
  RUN_TEST(test_cidr_full);
  RUN_TEST(test_cidr_16);
  RUN_TEST(test_cidr_short_implies_24);
  RUN_TEST(test_cidr_no_prefix_default_24);
  RUN_TEST(test_cidr_invalid);
  RUN_TEST(test_ports_basic);
  RUN_TEST(test_ports_filters_bad);
  RUN_TEST(test_ports_max_cap);
  RUN_TEST(test_ports_empty);
  RUN_TEST(test_hostport_full);
  RUN_TEST(test_hostport_default);
  RUN_TEST(test_hostport_trims);
  RUN_TEST(test_hostport_invalid);
  RUN_TEST(test_mac_colon);
  RUN_TEST(test_mac_dash_and_bare);
  RUN_TEST(test_mac_invalid);
  RUN_TEST(test_wol_packet);
  RUN_TEST(test_wol_packet_too_small);
  return UNITY_END();
}
