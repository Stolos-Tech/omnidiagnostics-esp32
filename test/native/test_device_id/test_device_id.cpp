// Юніт-тести ідентифікації пристрою (OUI-вендор + тип) — pio test -e native.
#include <unity.h>
#include <string.h>
#include "kernel/device_id.h"

void setUp() {}
void tearDown() {}

void test_oui_known() {
  uint8_t apple[3] = {0xA4,0x83,0xE7};
  TEST_ASSERT_EQUAL_STRING("Apple", net_oui_vendor(apple));
  TEST_ASSERT_EQUAL_INT(DC_PHONE, net_oui_category(apple));

  uint8_t esp[3] = {0x24,0x0A,0xC4};
  TEST_ASSERT_EQUAL_STRING("Espressif", net_oui_vendor(esp));
  TEST_ASSERT_EQUAL_INT(DC_IOT, net_oui_category(esp));

  uint8_t asus[3] = {0x2C,0x56,0xDC};
  TEST_ASSERT_EQUAL_STRING("ASUS", net_oui_vendor(asus));
  TEST_ASSERT_EQUAL_INT(DC_ROUTER, net_oui_category(asus));
}

void test_oui_unknown() {
  uint8_t x[3] = {0x02,0x11,0x22};   // локально-адміністрований, не в таблиці
  TEST_ASSERT_EQUAL_STRING("", net_oui_vendor(x));
  TEST_ASSERT_EQUAL_INT(DC_UNKNOWN, net_oui_category(x));
}

void test_type_by_port() {
  uint16_t win[] = {135, 445};
  TEST_ASSERT_EQUAL_STRING("PC (Windows)", net_device_type(false, DC_UNKNOWN, false, win, 2));

  uint16_t printer[] = {80, 9100};
  TEST_ASSERT_EQUAL_STRING("Printer", net_device_type(false, DC_UNKNOWN, false, printer, 2));

  uint16_t cam[] = {554};
  TEST_ASSERT_EQUAL_STRING("IP Camera", net_device_type(false, DC_UNKNOWN, false, cam, 1));

  uint16_t iphone[] = {62078};
  TEST_ASSERT_EQUAL_STRING("iPhone/iPad", net_device_type(false, DC_UNKNOWN, false, iphone, 1));
}

void test_type_gateway_wins_over_vendor() {
  // Шлюз із лише 80/443 -> Router, навіть якщо вендор невідомий
  uint16_t web[] = {80, 443};
  TEST_ASSERT_EQUAL_STRING("Router/Gateway", net_device_type(true, DC_UNKNOWN, false, web, 2));
}

void test_type_by_vendor_category() {
  uint16_t none[] = {80};
  TEST_ASSERT_EQUAL_STRING("Phone/Tablet", net_device_type(false, DC_PHONE, false, none, 1));
  TEST_ASSERT_EQUAL_STRING("Router/AP",    net_device_type(false, DC_ROUTER, false, none, 1));
  TEST_ASSERT_EQUAL_STRING("IoT/MCU",      net_device_type(false, DC_IOT, false, none, 1));
}

void test_type_port_beats_vendor() {
  // Порт-сигнатура сильніша за категорію вендора (принтер на Espressif-плеваті)
  uint16_t printer[] = {9100};
  TEST_ASSERT_EQUAL_STRING("Printer", net_device_type(false, DC_IOT, false, printer, 1));
}

void test_type_fallbacks() {
  uint16_t ssh[] = {22};
  TEST_ASSERT_EQUAL_STRING("Linux/Unix host", net_device_type(false, DC_UNKNOWN, false, ssh, 1));
  uint16_t none[] = {};
  TEST_ASSERT_EQUAL_STRING("Unknown", net_device_type(false, DC_UNKNOWN, false, none, 0));
}

void test_type_local_mac_phone() {
  uint8_t laa[6] = {0xE6,0xD3,0xB4,0x01,0x02,0x03};
  TEST_ASSERT_TRUE(net_mac_is_local(laa));
  uint8_t uaa[6] = {0xF8,0xFE,0x5E,0x01,0x02,0x03};
  TEST_ASSERT_FALSE(net_mac_is_local(uaa));
  uint16_t none[] = {};
  // локальний MAC + невідомий вендор + нема портів -> телефон
  TEST_ASSERT_EQUAL_STRING("Phone (rand MAC)", net_device_type(false, DC_UNKNOWN, true, none, 0));
  // але відомий вендор перебиває
  TEST_ASSERT_EQUAL_STRING("Router/AP", net_device_type(false, DC_ROUTER, true, none, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_oui_known);
  RUN_TEST(test_oui_unknown);
  RUN_TEST(test_type_by_port);
  RUN_TEST(test_type_gateway_wins_over_vendor);
  RUN_TEST(test_type_by_vendor_category);
  RUN_TEST(test_type_port_beats_vendor);
  RUN_TEST(test_type_fallbacks);
  RUN_TEST(test_type_local_mac_phone);
  return UNITY_END();
}
