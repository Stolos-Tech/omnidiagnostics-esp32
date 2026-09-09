// Юніт-тести протоколу лінка ESP32<->UNO (pio test -e native).
#include <unity.h>
#include <string.h>
#include "drivers/uno_proto.h"

void setUp() {}
void tearDown() {}

void test_parse_full() {
  UnoSensors s;
  TEST_ASSERT_TRUE(uno_parse_sensors("S 512 1 ffa25d", &s));
  TEST_ASSERT_EQUAL_INT(512, s.pot);
  TEST_ASSERT_TRUE(s.reed);
  TEST_ASSERT_EQUAL_HEX32(0xFFA25D, s.ir);
}

void test_parse_no_ir() {
  UnoSensors s;
  TEST_ASSERT_TRUE(uno_parse_sensors("S 0 0", &s));
  TEST_ASSERT_EQUAL_INT(0, s.pot);
  TEST_ASSERT_FALSE(s.reed);
  TEST_ASSERT_EQUAL_HEX32(0, s.ir);
}

void test_parse_clamp_pot() {
  UnoSensors s;
  uno_parse_sensors("S 5000 1 0", &s);
  TEST_ASSERT_EQUAL_INT(1023, s.pot);
}

void test_parse_reject() {
  UnoSensors s;
  TEST_ASSERT_FALSE(uno_parse_sensors("X 1 2 3", &s));   // не той префікс
  TEST_ASSERT_FALSE(uno_parse_sensors("S", &s));          // без даних
  TEST_ASSERT_FALSE(uno_parse_sensors("", &s));
  TEST_ASSERT_FALSE(uno_parse_sensors(nullptr, &s));
}

void test_parse_env() {
  UnoEnv e;
  TEST_ASSERT_TRUE(uno_parse_env("ENV 235 481", &e));
  TEST_ASSERT_EQUAL_INT(235, e.temp_x10);
  TEST_ASSERT_EQUAL_INT(481, e.hum_x10);
}

void test_parse_env_reject() {
  UnoEnv e;
  TEST_ASSERT_FALSE(uno_parse_env("ENVX 1 2", &e));  // не той тег (немає розділювача)
  TEST_ASSERT_FALSE(uno_parse_env("S 1 2", &e));
  TEST_ASSERT_FALSE(uno_parse_env(nullptr, &e));
}

void test_parse_joy() {
  int x = -1, y = -1, sw = -1;
  TEST_ASSERT_TRUE(uno_parse_joy("JOY 512 1023 1", &x, &y, &sw));
  TEST_ASSERT_EQUAL_INT(512, x);
  TEST_ASSERT_EQUAL_INT(1023, y);
  TEST_ASSERT_EQUAL_INT(1, sw);
}

void test_parse_joy_clamp_reject() {
  int x, y, sw;
  TEST_ASSERT_TRUE(uno_parse_joy("JOY 5000 -20 0", &x, &y, &sw));  // затиск у 0..1023
  TEST_ASSERT_EQUAL_INT(1023, x);
  TEST_ASSERT_EQUAL_INT(0, y);
  TEST_ASSERT_EQUAL_INT(0, sw);
  TEST_ASSERT_FALSE(uno_parse_joy("JOY 1 2", &x, &y, &sw));         // замало полів
  TEST_ASSERT_FALSE(uno_parse_joy("JOYX 1 2 3", &x, &y, &sw));      // не той тег
  TEST_ASSERT_FALSE(uno_parse_joy(nullptr, &x, &y, &sw));
}

void test_parse_rfid() {
  uint32_t uid = 0;
  TEST_ASSERT_TRUE(uno_parse_rfid("RFID a1b2c3d4", &uid));
  TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4, uid);
}

void test_parse_rfid_reject() {
  uint32_t uid = 0;
  TEST_ASSERT_FALSE(uno_parse_rfid("RFI a1b2c3d4", &uid));
  TEST_ASSERT_FALSE(uno_parse_rfid("RFID", &uid));
}

void test_parse_cap() {
  int slot = -1, present = -1;
  char type[10] = {0}, name[14] = {0};
  TEST_ASSERT_TRUE(uno_parse_cap("CAP 3 rfid rc522 0", &slot, type, sizeof(type), name, sizeof(name), &present));
  TEST_ASSERT_EQUAL_INT(3, slot);
  TEST_ASSERT_EQUAL_STRING("rfid", type);
  TEST_ASSERT_EQUAL_STRING("rc522", name);
  TEST_ASSERT_EQUAL_INT(0, present);
  // present опційний -> дефолт 1
  TEST_ASSERT_TRUE(uno_parse_cap("CAP 0 analog pot", &slot, type, sizeof(type), name, sizeof(name), &present));
  TEST_ASSERT_EQUAL_INT(0, slot);
  TEST_ASSERT_EQUAL_INT(1, present);
  // відкидання
  TEST_ASSERT_FALSE(uno_parse_cap("CAPX 1 a b", &slot, type, sizeof(type), name, sizeof(name), &present));
  TEST_ASSERT_FALSE(uno_parse_cap("CAP 1 only", &slot, type, sizeof(type), name, sizeof(name), &present));
  TEST_ASSERT_FALSE(uno_parse_cap(nullptr, &slot, type, sizeof(type), name, sizeof(name), &present));
}

void test_parse_evt() {
  int slot = -1; char val[20] = {0};
  TEST_ASSERT_TRUE(uno_parse_evt("EVT 0 517", &slot, val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(0, slot);
  TEST_ASSERT_EQUAL_STRING("517", val);
  // багатослівне значення + \r\n зрізається
  TEST_ASSERT_TRUE(uno_parse_evt("EVT 2 235 480\r\n", &slot, val, sizeof(val)));
  TEST_ASSERT_EQUAL_INT(2, slot);
  TEST_ASSERT_EQUAL_STRING("235 480", val);
  TEST_ASSERT_FALSE(uno_parse_evt("EVTX 1 2", &slot, val, sizeof(val)));
  TEST_ASSERT_FALSE(uno_parse_evt("EVT x", &slot, val, sizeof(val)));
}

void test_build_set_scan() {
  char b[40];
  uno_build_set(10, "90", b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("SET 10 90\n", b);
  uno_build_set(7, "", b, sizeof(b));           // порожні args -> без хвостового пробілу
  TEST_ASSERT_EQUAL_STRING("SET 7\n", b);
  uno_build_set(7, nullptr, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("SET 7\n", b);
  uno_build_scan(b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("SCAN\n", b);
}

void test_parse_rfa() {
  char uid[21] = {0}, type[18] = {0}, verdict[14] = {0};
  int cracked = -1, total = -1;
  // uid-рядок
  TEST_ASSERT_TRUE(uno_parse_rfa_uid("RFA uid FA028732 sak 08 Classic1K", uid, sizeof(uid), type, sizeof(type)));
  TEST_ASSERT_EQUAL_STRING("FA028732", uid);
  TEST_ASSERT_EQUAL_STRING("Classic1K", type);
  // end-рядок
  TEST_ASSERT_TRUE(uno_parse_rfa_end("RFA end 16/16 WIDE-OPEN", &cracked, &total, verdict, sizeof(verdict)));
  TEST_ASSERT_EQUAL_INT(16, cracked);
  TEST_ASSERT_EQUAL_INT(16, total);
  TEST_ASSERT_EQUAL_STRING("WIDE-OPEN", verdict);
  // is_rfa
  TEST_ASSERT_TRUE(uno_is_rfa("RFA sec 3 B FFFFFFFFFFFF"));
  TEST_ASSERT_FALSE(uno_is_rfa("RFID abcd"));
  // відкидання чужих
  TEST_ASSERT_FALSE(uno_parse_rfa_uid("RFA end 1/16 PARTIAL", uid, sizeof(uid), type, sizeof(type)));
  TEST_ASSERT_FALSE(uno_parse_rfa_end("RFA uid AA sak 08 Classic1K", &cracked, &total, verdict, sizeof(verdict)));
  TEST_ASSERT_FALSE(uno_parse_rfa_uid(nullptr, uid, sizeof(uid), type, sizeof(type)));
}

void test_build_write() {
  uint8_t d[16]; for (int i = 0; i < 16; i++) d[i] = (uint8_t)i;
  char b[48];
  uno_build_write(8, d, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("WRITE 8 000102030405060708090A0B0C0D0E0F\n", b);
}

void test_parse_wres() {
  int block = -1, ok = -1;
  TEST_ASSERT_TRUE(uno_parse_wres("WRES 8 1", &block, &ok));
  TEST_ASSERT_EQUAL_INT(8, block);
  TEST_ASSERT_EQUAL_INT(1, ok);
  TEST_ASSERT_TRUE(uno_parse_wres("WRES 12 0", &block, &ok));
  TEST_ASSERT_EQUAL_INT(0, ok);
  TEST_ASSERT_FALSE(uno_parse_wres("WR 8 1", &block, &ok));
  TEST_ASSERT_FALSE(uno_parse_wres(nullptr, &block, &ok));
}

void test_parse_stat() {
  int fr = -1; uint32_t up = 0;
  TEST_ASSERT_TRUE(uno_parse_stat("STAT 1450 3600", &fr, &up));
  TEST_ASSERT_EQUAL_INT(1450, fr);
  TEST_ASSERT_EQUAL_UINT32(3600, up);
  TEST_ASSERT_FALSE(uno_parse_stat("STAT 1", &fr, &up));    // замало полів
  TEST_ASSERT_FALSE(uno_parse_stat("STATX 1 2", &fr, &up)); // не той тег
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_full);
  RUN_TEST(test_parse_no_ir);
  RUN_TEST(test_parse_clamp_pot);
  RUN_TEST(test_parse_reject);
  RUN_TEST(test_parse_env);
  RUN_TEST(test_parse_env_reject);
  RUN_TEST(test_parse_joy);
  RUN_TEST(test_parse_joy_clamp_reject);
  RUN_TEST(test_parse_stat);
  RUN_TEST(test_parse_rfid);
  RUN_TEST(test_parse_rfid_reject);
  RUN_TEST(test_parse_cap);
  RUN_TEST(test_parse_evt);
  RUN_TEST(test_build_set_scan);
  RUN_TEST(test_parse_rfa);
  RUN_TEST(test_build_write);
  RUN_TEST(test_parse_wres);
  return UNITY_END();
}
