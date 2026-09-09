// Юніт-тести логіки сесії: режим, PIN, автентифікація, rate-limit (pio test -e native).
#include <unity.h>
#include <string.h>
#include "remote/session.h"

// Детермінований RNG для передбачуваних PIN у тестах.
static uint32_t g_rng_value = 0;
static uint32_t mock_rng() { return g_rng_value; }

void setUp() {
  session_set_rng(mock_rng);
  session_init();
}
void tearDown() {}

// Початковий стан: OFF, без PIN, не автентифіковано
void test_initial_state() {
  TEST_ASSERT_EQUAL_INT(MODE_OFF, session_active_mode());
  TEST_ASSERT_EQUAL_STRING("", session_pin());
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// PIN — рівно 6 цифр із провідними нулями
void test_pin_leading_zeros() {
  g_rng_value = 4821;  // 4821 % 1000000 = 4821 -> "004821"
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_EQUAL_STRING("004821", session_pin());
  TEST_ASSERT_EQUAL_INT(6, (int)strlen(session_pin()));
}

// PIN береться за модулем 1e6
void test_pin_modulo() {
  g_rng_value = 12345678;  // % 1000000 = 345678
  session_set_mode(MODE_BT);
  TEST_ASSERT_EQUAL_STRING("345678", session_pin());
}

// Увімкнення режиму генерує PIN; OFF — очищає
void test_mode_on_off_pin() {
  g_rng_value = 111111;
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_EQUAL_INT(MODE_WIFI, session_active_mode());
  TEST_ASSERT_EQUAL_STRING("111111", session_pin());
  session_set_mode(MODE_OFF);
  TEST_ASSERT_EQUAL_INT(MODE_OFF, session_active_mode());
  TEST_ASSERT_EQUAL_STRING("", session_pin());
}

// Новий режим вимикає попередній і генерує НОВИЙ PIN, скидає авторизацію
void test_switch_mode_disables_previous() {
  g_rng_value = 222222;
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_TRUE(session_authenticate("222222") == AUTH_OK);
  TEST_ASSERT_TRUE(session_is_authenticated());

  // перемикання на BT: активний тільки BT, новий PIN, авторизація скинута
  g_rng_value = 333333;
  session_set_mode(MODE_BT);
  TEST_ASSERT_EQUAL_INT(MODE_BT, session_active_mode());
  TEST_ASSERT_EQUAL_STRING("333333", session_pin());
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// Вірний PIN автентифікує
void test_auth_correct() {
  g_rng_value = 555555;
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_EQUAL_INT(AUTH_OK, session_authenticate("555555"));
  TEST_ASSERT_TRUE(session_is_authenticated());
}

// Невірний PIN -> WRONG, не автентифіковано
void test_auth_wrong() {
  g_rng_value = 555555;
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_EQUAL_INT(AUTH_WRONG, session_authenticate("000000"));
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// Rate-limit: після SESSION_MAX_ATTEMPTS невдач -> LOCKED, далі завжди LOCKED
void test_rate_limit_locks() {
  g_rng_value = 555555;
  session_set_mode(MODE_WIFI);
  for (int i = 1; i < SESSION_MAX_ATTEMPTS; i++)
    TEST_ASSERT_EQUAL_INT(AUTH_WRONG, session_authenticate("999999"));
  // остання дозволена спроба переводить у LOCKED
  TEST_ASSERT_EQUAL_INT(AUTH_LOCKED, session_authenticate("999999"));
  // після блокування навіть ВІРНИЙ PIN не приймається
  TEST_ASSERT_EQUAL_INT(AUTH_LOCKED, session_authenticate("555555"));
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// reset_auth дозволяє новому клієнту пробувати знову, режим/PIN незмінні
void test_reset_auth_after_lock() {
  g_rng_value = 555555;
  session_set_mode(MODE_WIFI);
  for (int i = 0; i < SESSION_MAX_ATTEMPTS; i++) session_authenticate("999999");
  TEST_ASSERT_EQUAL_INT(AUTH_LOCKED, session_authenticate("555555"));

  session_reset_auth();
  TEST_ASSERT_EQUAL_STRING("555555", session_pin());  // PIN не змінився
  TEST_ASSERT_EQUAL_INT(AUTH_OK, session_authenticate("555555"));
}

// Автентифікація в режимі OFF нічого не приймає
void test_auth_when_off() {
  TEST_ASSERT_EQUAL_INT(AUTH_WRONG, session_authenticate("000000"));
  TEST_ASSERT_FALSE(session_is_authenticated());
}

// Повторна автентифікація вже автентифікованої сесії -> OK без лічби
void test_auth_idempotent() {
  g_rng_value = 555555;
  session_set_mode(MODE_WIFI);
  TEST_ASSERT_EQUAL_INT(AUTH_OK, session_authenticate("555555"));
  TEST_ASSERT_EQUAL_INT(AUTH_OK, session_authenticate("wrong-now"));
  TEST_ASSERT_TRUE(session_is_authenticated());
}

// session_set_pin: примусово фіксує валідний 6-значний PIN замість згенерованого
void test_set_pin_overrides() {
  g_rng_value = 111111;
  session_set_mode(MODE_WIFI);
  session_set_pin("042042");
  TEST_ASSERT_EQUAL_STRING("042042", session_pin());
  TEST_ASSERT_EQUAL_INT(AUTH_OK, session_authenticate("042042"));
}

// session_set_pin ігнорує невалідний формат (не 6 цифр) і режим OFF
void test_set_pin_rejects_invalid() {
  g_rng_value = 111111;
  session_set_mode(MODE_WIFI);
  session_set_pin("12345");     // 5 цифр
  TEST_ASSERT_EQUAL_STRING("111111", session_pin());
  session_set_pin("abcdef");    // не цифри
  TEST_ASSERT_EQUAL_STRING("111111", session_pin());
  session_set_pin(nullptr);
  TEST_ASSERT_EQUAL_STRING("111111", session_pin());

  session_set_mode(MODE_OFF);
  session_set_pin("555555");    // режим OFF — ігнор
  TEST_ASSERT_EQUAL_STRING("", session_pin());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state);
  RUN_TEST(test_pin_leading_zeros);
  RUN_TEST(test_pin_modulo);
  RUN_TEST(test_mode_on_off_pin);
  RUN_TEST(test_switch_mode_disables_previous);
  RUN_TEST(test_auth_correct);
  RUN_TEST(test_auth_wrong);
  RUN_TEST(test_rate_limit_locks);
  RUN_TEST(test_reset_auth_after_lock);
  RUN_TEST(test_auth_when_off);
  RUN_TEST(test_auth_idempotent);
  RUN_TEST(test_set_pin_overrides);
  RUN_TEST(test_set_pin_rejects_invalid);
  return UNITY_END();
}
