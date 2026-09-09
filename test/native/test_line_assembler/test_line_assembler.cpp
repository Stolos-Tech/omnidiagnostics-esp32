// Юніт-тести збирача рядків BT-потоку (pio test -e native).
#include <unity.h>
#include <string.h>
#include "remote/line_assembler.h"

void setUp() {}
void tearDown() {}

// Подає весь рядок; повертає зібраний при роздільнику. Повертає к-ть зібраних.
static int feed_str(LineAssembler& la, const char* s, char* out, size_t n) {
  int lines = 0;
  for (const char* p = s; *p; p++)
    if (la.feed(*p, out, n)) lines++;
  return lines;
}

void test_single_line() {
  LineAssembler la;
  char out[64];
  int n = feed_str(la, "{\"btn\":\"S1\"}\n", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("{\"btn\":\"S1\"}", out);
}

void test_crlf() {
  LineAssembler la;
  char out[64];
  // \r\n не повинен дати порожній другий рядок
  int n = feed_str(la, "abc\r\n", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("abc", out);
}

void test_two_lines() {
  LineAssembler la;
  char out[64];
  TEST_ASSERT_FALSE(la.feed('a', out, sizeof(out)));
  TEST_ASSERT_TRUE(la.feed('\n', out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("a", out);
  TEST_ASSERT_FALSE(la.feed('b', out, sizeof(out)));
  TEST_ASSERT_FALSE(la.feed('c', out, sizeof(out)));
  TEST_ASSERT_TRUE(la.feed('\n', out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("bc", out);
}

// Порожні рядки (подвійні роздільники) не повертаються
void test_empty_lines_ignored() {
  LineAssembler la;
  char out[64];
  TEST_ASSERT_FALSE(la.feed('\n', out, sizeof(out)));
  TEST_ASSERT_FALSE(la.feed('\n', out, sizeof(out)));
  int n = feed_str(la, "x\n", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("x", out);
}

// Рядок, довший за MAX_LINE, відкидається; наступний коректний — приймається
void test_overflow_dropped() {
  LineAssembler la;
  char out[300];
  // MAX_LINE+50 символів без роздільника -> overflow
  for (int i = 0; i < LineAssembler::MAX_LINE + 50; i++)
    TEST_ASSERT_FALSE(la.feed('x', out, sizeof(out)));
  // роздільник завершує «задовгий» рядок — його НЕ повертає
  TEST_ASSERT_FALSE(la.feed('\n', out, sizeof(out)));
  // наступний нормальний рядок приймається
  int n = feed_str(la, "ok\n", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("ok", out);
}

// Рядок рівно MAX_LINE символів — приймається
void test_max_exact() {
  LineAssembler la;
  char out[LineAssembler::MAX_LINE + 8];
  for (int i = 0; i < LineAssembler::MAX_LINE; i++)
    TEST_ASSERT_FALSE(la.feed('a', out, sizeof(out)));
  TEST_ASSERT_TRUE(la.feed('\n', out, sizeof(out)));
  TEST_ASSERT_EQUAL_INT(LineAssembler::MAX_LINE, (int)strlen(out));
}

// reset() скидає незавершений рядок
void test_reset() {
  LineAssembler la;
  char out[64];
  la.feed('a', out, sizeof(out));
  la.feed('b', out, sizeof(out));
  la.reset();
  int n = feed_str(la, "cd\n", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("cd", out);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_line);
  RUN_TEST(test_crlf);
  RUN_TEST(test_two_lines);
  RUN_TEST(test_empty_lines_ignored);
  RUN_TEST(test_overflow_dropped);
  RUN_TEST(test_max_exact);
  RUN_TEST(test_reset);
  return UNITY_END();
}
