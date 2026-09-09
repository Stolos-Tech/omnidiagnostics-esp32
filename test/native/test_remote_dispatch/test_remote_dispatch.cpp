// Юніт-тести авторизаційного бар'єра диспетчера (pio test -e native).
#include <unity.h>
#include "remote/remote_dispatch.h"
#include "remote/session.h"
#include "kernel/input_queue.h"

static uint32_t g_rng = 555555;
static uint32_t mock_rng() { return g_rng; }

void setUp() {
  session_set_rng(mock_rng);
  session_init();
  input_queue().clear();
  g_rng = 555555;
  session_set_mode(MODE_WIFI);  // PIN = "555555"
  session_reset_auth();
}
void tearDown() {}

// До автентифікації кнопка НЕ потрапляє в чергу
void test_button_ignored_before_auth() {
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"btn\":\"S1\"}"));
  TEST_ASSERT_TRUE(input_queue().empty());
}

// Вірний PIN автентифікує, дію DISCONNECT не дає
void test_correct_pin_authenticates() {
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"pin\":\"555555\"}"));
  TEST_ASSERT_TRUE(session_is_authenticated());
}

// Після auth кнопка потрапляє в чергу як звичайна подія
void test_button_after_auth_enqueued() {
  remote_handle_incoming("{\"pin\":\"555555\"}");
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"btn\":\"S3\"}"));
  InputEvent ev;
  TEST_ASSERT_TRUE(input_queue().pop(ev));
  TEST_ASSERT_EQUAL_INT(EV_BUTTON, ev.type);
  TEST_ASSERT_EQUAL_INT(BTN_S3, ev.btn);
}

// Вичерпання спроб PIN -> RA_DISCONNECT
void test_pin_bruteforce_disconnects() {
  for (int i = 1; i < SESSION_MAX_ATTEMPTS; i++)
    TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"pin\":\"000000\"}"));
  TEST_ASSERT_EQUAL_INT(RA_DISCONNECT, remote_handle_incoming("{\"pin\":\"000000\"}"));
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// Індекс після auth потрапляє в чергу як EV_INDEX
void test_index_enqueued_after_auth() {
  remote_handle_incoming("{\"pin\":\"555555\"}");
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"idx\":7}"));
  InputEvent ev;
  TEST_ASSERT_TRUE(input_queue().pop(ev));
  TEST_ASSERT_EQUAL_INT(EV_INDEX, ev.type);
  TEST_ASSERT_EQUAL_INT(7, ev.index);
}

// Індекс до auth ігнорується
void test_index_ignored_before_auth() {
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{\"idx\":2}"));
  TEST_ASSERT_TRUE(input_queue().empty());
}

// Побитий JSON ігнорується, черга порожня, дії нема
void test_broken_ignored() {
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming("{bad"));
  TEST_ASSERT_EQUAL_INT(RA_NONE, remote_handle_incoming(""));
  TEST_ASSERT_TRUE(input_queue().empty());
}

// Текст до auth ігнорується (у чергу не потрапляє)
void test_text_ignored_before_auth() {
  TEST_ASSERT_EQUAL_INT(RA_NONE,
    remote_handle_incoming("{\"text\":\"pw\",\"value\":\"abc\"}"));
  TEST_ASSERT_TRUE(input_queue().empty());
}

// Текст після auth потрапляє в чергу як EV_TEXT з field/value
void test_text_enqueued_after_auth() {
  remote_handle_incoming("{\"pin\":\"555555\"}");
  TEST_ASSERT_EQUAL_INT(RA_NONE,
    remote_handle_incoming("{\"text\":\"password\",\"value\":\"abc\"}"));
  InputEvent ev;
  TEST_ASSERT_TRUE(input_queue().pop(ev));
  TEST_ASSERT_EQUAL_INT(EV_TEXT, ev.type);
  TEST_ASSERT_EQUAL_STRING("password", ev.field);
  TEST_ASSERT_EQUAL_STRING("abc", ev.value);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_button_ignored_before_auth);
  RUN_TEST(test_correct_pin_authenticates);
  RUN_TEST(test_button_after_auth_enqueued);
  RUN_TEST(test_pin_bruteforce_disconnects);
  RUN_TEST(test_broken_ignored);
  RUN_TEST(test_text_ignored_before_auth);
  RUN_TEST(test_text_enqueued_after_auth);
  RUN_TEST(test_index_enqueued_after_auth);
  RUN_TEST(test_index_ignored_before_auth);
  return UNITY_END();
}
