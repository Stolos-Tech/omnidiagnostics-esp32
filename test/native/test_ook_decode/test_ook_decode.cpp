// Юніт-тести чистого OOK-декодера (EV1527/PT2262). Синтетичні кадри будуємо тут.
#include <unity.h>
#include "kernel/ook_decode.h"

void setUp() {}
void tearDown() {}

// Будує кадр: синхро (1Te mark + 31Te space) + 24 біти (MSB->LSB) PWM.
// Повертає к-ть імпульсів. buf має вмістити 2 + 48.
static int build_frame(uint16_t* buf, uint32_t code24, int te) {
  int k = 0;
  buf[k++] = (uint16_t)te;        // синхро mark 1Te
  buf[k++] = (uint16_t)(31 * te); // синхро space 31Te
  for (int b = 23; b >= 0; b--) {
    int bit = (code24 >> b) & 1;
    if (bit) { buf[k++] = (uint16_t)(3 * te); buf[k++] = (uint16_t)te; }        // "1" = 3Te/1Te
    else     { buf[k++] = (uint16_t)te;       buf[k++] = (uint16_t)(3 * te); }  // "0" = 1Te/3Te
  }
  return k;
}

void test_decode_ev1527() {
  uint16_t buf[64];
  uint32_t code = 0x5A6C93 & 0xFFFFFF;   // довільний 24-бітний код
  int n = build_frame(buf, code, 350);   // Te = 350 мкс (типовий EV1527)
  OokDecoded d = ook_decode(buf, n);
  TEST_ASSERT_TRUE(d.valid);
  TEST_ASSERT_EQUAL_INT(24, d.bits);
  TEST_ASSERT_EQUAL_HEX32(code, d.code);
  TEST_ASSERT_INT_WITHIN(20, 350, d.te_us);
}

void test_decode_pt2262_slow() {
  uint16_t buf[64];
  uint32_t code = 0xABC123 & 0xFFFFFF;
  int n = build_frame(buf, code, 800);   // повільніший Te (PT2262)
  OokDecoded d = ook_decode(buf, n);
  TEST_ASSERT_TRUE(d.valid);
  TEST_ASSERT_EQUAL_HEX32(code, d.code);
}

void test_reject_noise() {
  // Хаотичні тривалості без синхро/структури -> не кадр.
  uint16_t buf[50];
  for (int i = 0; i < 50; i++) buf[i] = (uint16_t)(120 + (i * 37) % 200);
  OokDecoded d = ook_decode(buf, 50);
  TEST_ASSERT_FALSE(d.valid);
}

void test_reject_short() {
  uint16_t buf[10] = { 350, 10850, 350, 1050, 350, 1050, 350, 1050, 350, 1050 };
  OokDecoded d = ook_decode(buf, 10);   // замало для 24 біт
  TEST_ASSERT_FALSE(d.valid);
}

void test_null_safe() {
  OokDecoded d = ook_decode(nullptr, 100);
  TEST_ASSERT_FALSE(d.valid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_ev1527);
  RUN_TEST(test_decode_pt2262_slow);
  RUN_TEST(test_reject_noise);
  RUN_TEST(test_reject_short);
  RUN_TEST(test_null_safe);
  return UNITY_END();
}
