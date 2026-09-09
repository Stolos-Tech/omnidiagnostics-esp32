// Юніт-тести спільного протоколу (pio test -e native).
#include <unity.h>
#include <string.h>
#include <string>
#include "remote/protocol.h"

void setUp() {}
void tearDown() {}

// --- Парсинг вхідних повідомлень ---

void test_parse_pin() {
  RemoteEvent ev = protocol_parse_incoming("{\"pin\":\"004821\"}");
  TEST_ASSERT_EQUAL_INT(REV_PIN, ev.type);
  TEST_ASSERT_EQUAL_STRING("004821", ev.pin);
}

void test_parse_buttons() {
  const char* names[] = {"S1", "S2", "S3", "S4", "S5"};
  ButtonId expect[] = {BTN_S1, BTN_S2, BTN_S3, BTN_S4, BTN_S5};
  for (int i = 0; i < 5; i++) {
    char buf[24];
    snprintf(buf, sizeof(buf), "{\"btn\":\"%s\"}", names[i]);
    RemoteEvent ev = protocol_parse_incoming(buf);
    TEST_ASSERT_EQUAL_INT(REV_BUTTON, ev.type);
    TEST_ASSERT_EQUAL_INT(expect[i], ev.btn);
  }
}

void test_parse_text() {
  RemoteEvent ev = protocol_parse_incoming("{\"text\":\"password\",\"value\":\"secret123\"}");
  TEST_ASSERT_EQUAL_INT(REV_TEXT, ev.type);
  TEST_ASSERT_EQUAL_STRING("password", ev.field);
  TEST_ASSERT_EQUAL_STRING("secret123", ev.value);
}

// Значення з екранованими лапками і бекслешем — має розкодуватись коректно
void test_parse_text_with_escapes() {
  RemoteEvent ev = protocol_parse_incoming("{\"text\":\"pw\",\"value\":\"a\\\"b\\\\c\"}");
  TEST_ASSERT_EQUAL_INT(REV_TEXT, ev.type);
  TEST_ASSERT_EQUAL_STRING("a\"b\\c", ev.value);
}

void test_parse_index() {
  RemoteEvent ev = protocol_parse_incoming("{\"idx\":5}");
  TEST_ASSERT_EQUAL_INT(REV_INDEX, ev.type);
  TEST_ASSERT_EQUAL_INT(5, ev.index);
}

void test_parse_invalid_button() {
  RemoteEvent ev = protocol_parse_incoming("{\"btn\":\"S9\"}");
  TEST_ASSERT_EQUAL_INT(REV_INVALID, ev.type);
}

void test_parse_broken_json() {
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming("{\"btn\":").type);
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming("not json at all").type);
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming("{").type);
}

void test_parse_empty() {
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming("").type);
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming(nullptr).type);
}

void test_parse_too_long() {
  // рядок довший за PROTOCOL_MAX_INCOMING -> INVALID, без обробки
  std::string big = "{\"value\":\"";
  big.append(PROTOCOL_MAX_INCOMING + 50, 'x');
  big += "\"}";
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming(big.c_str()).type);
}

void test_parse_unknown_object() {
  // валідний JSON, але не наша команда -> REV_NONE (не INVALID)
  RemoteEvent ev = protocol_parse_incoming("{\"foo\":\"bar\"}");
  TEST_ASSERT_EQUAL_INT(REV_NONE, ev.type);
}

void test_parse_array_is_invalid() {
  // не об'єкт -> INVALID
  TEST_ASSERT_EQUAL_INT(REV_INVALID, protocol_parse_incoming("[1,2,3]").type);
}

// --- Побудова стану ---

void test_build_state() {
  std::string s = protocol_build_state("wifi_list");
  TEST_ASSERT_EQUAL_STRING("{\"page\":\"wifi_list\"}", s.c_str());
}

void test_build_menu() {
  const char* items[] = {"Battery Diag", "Berry Demo", "battery"};
  std::string s = protocol_build_menu("launcher", items, 3, 1);
  // ключі у порядку вставки: page, cursor, items
  TEST_ASSERT_EQUAL_STRING(
    "{\"page\":\"launcher\",\"cursor\":1,\"items\":[\"Battery Diag\",\"Berry Demo\",\"battery\"]}",
    s.c_str());
}

// Побудований стан меню має парситись назад як валідний JSON (round-trip санітарій)
void test_build_menu_empty() {
  std::string s = protocol_build_menu("empty", nullptr, 0, 0);
  TEST_ASSERT_EQUAL_STRING("{\"page\":\"empty\",\"cursor\":0,\"items\":[]}", s.c_str());
}

void test_build_page_kv() {
  std::string s = protocol_build_page_kv("wifi_pass", "ssid", "HomeNet");
  TEST_ASSERT_EQUAL_STRING("{\"page\":\"wifi_pass\",\"ssid\":\"HomeNet\"}", s.c_str());
}

void test_build_status() {
  std::string s = protocol_build_status(true, false, false, -55, 3850, true, 2, -1);
  TEST_ASSERT_EQUAL_STRING(
    "{\"status\":{\"sta\":1,\"ap\":0,\"bt\":0,\"rssi\":-55,\"mv\":3850,\"usb\":1,\"pwr\":2,\"rt\":-1}}",
    s.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_pin);
  RUN_TEST(test_parse_buttons);
  RUN_TEST(test_parse_text);
  RUN_TEST(test_parse_text_with_escapes);
  RUN_TEST(test_parse_index);
  RUN_TEST(test_parse_invalid_button);
  RUN_TEST(test_parse_broken_json);
  RUN_TEST(test_parse_empty);
  RUN_TEST(test_parse_too_long);
  RUN_TEST(test_parse_unknown_object);
  RUN_TEST(test_parse_array_is_invalid);
  RUN_TEST(test_build_state);
  RUN_TEST(test_build_menu);
  RUN_TEST(test_build_menu_empty);
  RUN_TEST(test_build_page_kv);
  return UNITY_END();
}
