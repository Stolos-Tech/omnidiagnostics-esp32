// Юніт-тести протоколу лінка з боку UNO (pio test -e native, у arduino/uno_r3/).
#include <unity.h>
#include <string.h>
#include "link_proto.h"

void setUp() {}
void tearDown() {}

void test_build_sensors() {
  char b[32];
  link_build_sensors(512, true, 0xffa25d, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("S 512 1 ffa25d\n", b);
}

void test_build_sensors_no_ir_clamp() {
  char b[32];
  link_build_sensors(5000, false, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("S 1023 0 0\n", b);
}

void test_build_env() {
  char b[24];
  link_build_env(235, 481, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("ENV 235 481\n", b);
}

void test_build_joy() {
  char b[24];
  link_build_joy(512, 1023, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("JOY 512 1023 1\n", b);
}

void test_build_joy_clamp() {
  char b[24];
  link_build_joy(5000, -20, 0, b, sizeof(b));   // затиск у 0..1023
  TEST_ASSERT_EQUAL_STRING("JOY 1023 0 0\n", b);
}

void test_build_stat() {
  char b[24];
  link_build_stat(1450, 3600UL, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("STAT 1450 3600\n", b);
}

void test_build_rfid() {
  char b[24];
  link_build_rfid(0xa1b2c3d4, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("RFID a1b2c3d4\n", b);
}

void test_parse_write() {
  int block = -1; uint8_t d[16];
  TEST_ASSERT_TRUE(link_parse_write("WRITE 8 000102030405060708090A0B0C0D0E0F", &block, d));
  TEST_ASSERT_EQUAL_INT(8, block);
  TEST_ASSERT_EQUAL_HEX8(0x00, d[0]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, d[15]);
  TEST_ASSERT_FALSE(link_parse_write("WRITE 8 0011", &block, d));   // короткий hex
  TEST_ASSERT_FALSE(link_parse_write("WR 8 x", &block, d));
  TEST_ASSERT_FALSE(link_parse_write(nullptr, &block, d));
}

void test_build_wres() {
  char b[24];
  link_build_wres(8, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("WRES 8 1\n", b);
  link_build_wres(9, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("WRES 9 0\n", b);
}

void test_build_cap() {
  char b[48];
  link_build_cap(3, "rfid", "rc522", 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("CAP 3 rfid rc522 1\n", b);
  link_build_cap(2, "env", "dht11", 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("CAP 2 env dht11 0\n", b);
}

void test_build_evt() {
  char b[32];
  link_build_evt(0, "517", b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("EVT 0 517\n", b);
}

void test_parse_set() {
  int slot = -1; char args[24] = "x";
  TEST_ASSERT_TRUE(link_parse_set("SET 10 90", &slot, args, sizeof(args)));
  TEST_ASSERT_EQUAL_INT(10, slot);
  TEST_ASSERT_EQUAL_STRING("90", args);
  // кілька аргументів + \r\n зрізається
  TEST_ASSERT_TRUE(link_parse_set("SET 13 -200 fast\r\n", &slot, args, sizeof(args)));
  TEST_ASSERT_EQUAL_INT(13, slot);
  TEST_ASSERT_EQUAL_STRING("-200 fast", args);
  // порожні аргументи
  TEST_ASSERT_TRUE(link_parse_set("SET 7", &slot, args, sizeof(args)));
  TEST_ASSERT_EQUAL_INT(7, slot);
  TEST_ASSERT_EQUAL_STRING("", args);
  // не той тег / нема числа
  TEST_ASSERT_FALSE(link_parse_set("SETX 1", &slot, args, sizeof(args)));
  TEST_ASSERT_FALSE(link_parse_set("SET x", &slot, args, sizeof(args)));
  TEST_ASSERT_FALSE(link_parse_set(nullptr, &slot, args, sizeof(args)));
}

void test_is_scan() {
  TEST_ASSERT_TRUE(link_is_scan("SCAN"));
  TEST_ASSERT_TRUE(link_is_scan("SCAN\r\n"));
  TEST_ASSERT_TRUE(link_is_scan("  SCAN  "));
  TEST_ASSERT_FALSE(link_is_scan("SCANX"));
  TEST_ASSERT_FALSE(link_is_scan("SCAN 1"));
  TEST_ASSERT_FALSE(link_is_scan(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_build_sensors);
  RUN_TEST(test_build_sensors_no_ir_clamp);
  RUN_TEST(test_build_env);
  RUN_TEST(test_build_joy);
  RUN_TEST(test_build_joy_clamp);
  RUN_TEST(test_build_stat);
  RUN_TEST(test_build_rfid);
  RUN_TEST(test_parse_write);
  RUN_TEST(test_build_wres);
  RUN_TEST(test_build_cap);
  RUN_TEST(test_build_evt);
  RUN_TEST(test_parse_set);
  RUN_TEST(test_is_scan);
  return UNITY_END();
}
