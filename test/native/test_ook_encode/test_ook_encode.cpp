// Юніт-тести OOK-енкодера + round-trip (encode -> decode = той самий code).
#include <unity.h>
#include "kernel/ook_encode.h"
#include "kernel/ook_decode.h"

void setUp() {}
void tearDown() {}

// Round-trip: закодований сигнал має декодуватись назад у той самий 24-бітний код.
static void roundtrip(uint32_t code, int te) {
  uint16_t buf[256];
  int n = ook_encode(code, 24, te, 2, buf, 256);   // 2 повтори -> декодер знайде sync і прочитає кадр
  TEST_ASSERT_EQUAL_INT(2 * (24 * 2 + 2), n);       // 100 елементів
  OokDecoded d = ook_decode(buf, n);
  TEST_ASSERT_TRUE(d.valid);
  TEST_ASSERT_EQUAL_HEX32(code & 0xFFFFFF, d.code);
  TEST_ASSERT_EQUAL_INT(24, d.bits);
  TEST_ASSERT_INT_WITHIN(te / 10 + 1, te, d.te_us); // Te відновлено з точністю
}

void test_roundtrip_codes() {
  roundtrip(0xA53C7, 350);
  roundtrip(0xFFFFFF, 350);   // усі одиниці
  roundtrip(0x000001, 350);   // майже нулі
  roundtrip(0x5A5A5A, 400);   // чергування
}

void test_roundtrip_te_variants() {
  roundtrip(0x123456, 200);
  roundtrip(0x123456, 700);
}

void test_encode_layout() {
  uint16_t b[64];
  int n = ook_encode(0x1, 4, 300, 1, b, 64);   // 4 біти, 1 повтор -> 4*2+2=10
  TEST_ASSERT_EQUAL_INT(10, n);
  // біт0 (з чотирьох: 0001) -> перші 3 біти '0' = 1Te/3Te, останній '1' = 3Te/1Te
  TEST_ASSERT_EQUAL_UINT16(300, b[0]); TEST_ASSERT_EQUAL_UINT16(900, b[1]);  // '0'
  TEST_ASSERT_EQUAL_UINT16(900, b[6]); TEST_ASSERT_EQUAL_UINT16(300, b[7]);  // '1' (4-й біт)
  TEST_ASSERT_EQUAL_UINT16(300, b[8]); TEST_ASSERT_EQUAL_UINT16(9300, b[9]); // синхро 1Te/31Te
}

void test_encode_guards() {
  uint16_t b[8];
  TEST_ASSERT_EQUAL_INT(0, ook_encode(0x1, 24, 350, 1, b, 8));   // переповнення out_cap
  TEST_ASSERT_EQUAL_INT(0, ook_encode(0x1, 0, 350, 1, b, 8));    // bits<=0
  TEST_ASSERT_EQUAL_INT(0, ook_encode(0x1, 24, 0, 1, b, 8));     // te<=0
  TEST_ASSERT_EQUAL_INT(0, ook_encode(0x1, 24, 3000, 1, b, 200));// 31*3000 > 65535
  TEST_ASSERT_EQUAL_INT(0, ook_encode(0x1, 24, 350, 0, b, 200)); // repeats<1
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_codes);
  RUN_TEST(test_roundtrip_te_variants);
  RUN_TEST(test_encode_layout);
  RUN_TEST(test_encode_guards);
  return UNITY_END();
}
