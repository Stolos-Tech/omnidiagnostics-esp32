// Юніт-тести класифікатора BLE-трекерів (pio test -e native).
#include <unity.h>
#include "kernel/tracker_id.h"

void setUp() {}
void tearDown() {}

void test_tile_service() {
  uint16_t svc[1] = {0xFEED};
  TEST_ASSERT_EQUAL_INT(TRK_TILE, tracker_classify(-1, nullptr, 0, svc, 1));
}

void test_samsung_service() {
  uint16_t svc[1] = {0xFD5A};
  TEST_ASSERT_EQUAL_INT(TRK_SAMSUNG, tracker_classify(-1, nullptr, 0, svc, 1));
}

void test_apple_findmy() {
  uint8_t pl[2] = {0x12, 0x19};   // 0x12 = Find My separated
  TEST_ASSERT_EQUAL_INT(TRK_APPLE_FINDMY, tracker_classify(0x004C, pl, 2, nullptr, 0));
}

void test_apple_nearby_not_tracker() {
  uint8_t pl[2] = {0x10, 0x05};   // 0x10 = nearby (телефон/AirPods) -> не трекер
  TEST_ASSERT_EQUAL_INT(TRK_NONE, tracker_classify(0x004C, pl, 2, nullptr, 0));
}

void test_tile_company() {
  TEST_ASSERT_EQUAL_INT(TRK_TILE, tracker_classify(0x0157, nullptr, 0, nullptr, 0));
}

void test_none_generic() {
  uint16_t svc[1] = {0x180F};   // battery service — звичайний пристрій
  TEST_ASSERT_EQUAL_INT(TRK_NONE, tracker_classify(0x00E0, nullptr, 0, svc, 1));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("Tile", tracker_name(TRK_TILE));
  TEST_ASSERT_EQUAL_STRING("Apple Find My", tracker_name(TRK_APPLE_FINDMY));
  TEST_ASSERT_EQUAL_STRING("-", tracker_name(TRK_NONE));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tile_service);
  RUN_TEST(test_samsung_service);
  RUN_TEST(test_apple_findmy);
  RUN_TEST(test_apple_nearby_not_tracker);
  RUN_TEST(test_tile_company);
  RUN_TEST(test_none_generic);
  RUN_TEST(test_names);
  return UNITY_END();
}
