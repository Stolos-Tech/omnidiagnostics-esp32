// Юніт-тести кільцевого буфера логу (pio test -e native).
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "kernel/log_ring.h"

void setUp() {}
void tearDown() {}

void test_push_and_since() {
  LogRing r;
  r.push("one"); r.push("two"); r.push("three");
  TEST_ASSERT_EQUAL_UINT32(3, r.total());
  char out[LogRing::CAP][LogRing::LINE];
  int n = r.since(0, out, LogRing::CAP);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_STRING("one", out[0]);
  TEST_ASSERT_EQUAL_STRING("three", out[2]);
}

void test_since_cursor() {
  LogRing r;
  r.push("a"); r.push("b");
  char out[LogRing::CAP][LogRing::LINE];
  int n = r.since(2, out, LogRing::CAP);   // з курсора 2 — нових нема
  TEST_ASSERT_EQUAL_INT(0, n);
  r.push("c");
  n = r.since(2, out, LogRing::CAP);        // тепер один новий
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("c", out[0]);
}

void test_overflow_keeps_latest() {
  LogRing r;
  char s[16];
  for (int i = 0; i < LogRing::CAP + 10; i++) { snprintf(s, sizeof(s), "L%d", i); r.push(s); }
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(LogRing::CAP + 10), r.total());
  char out[LogRing::CAP][LogRing::LINE];
  int n = r.since(0, out, LogRing::CAP);     // старіші за CAP відкинуті
  TEST_ASSERT_EQUAL_INT(LogRing::CAP, n);
  // перший збережений — L10 (total-CAP = 10)
  TEST_ASSERT_EQUAL_STRING("L10", out[0]);
}

void test_truncation() {
  LogRing r;
  char big[200]; for (int i = 0; i < 199; i++) big[i] = 'x'; big[199] = '\0';
  r.push(big);
  char out[LogRing::CAP][LogRing::LINE];
  r.since(0, out, LogRing::CAP);
  TEST_ASSERT_TRUE((int)strlen(out[0]) < LogRing::LINE);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_push_and_since);
  RUN_TEST(test_since_cursor);
  RUN_TEST(test_overflow_keeps_latest);
  RUN_TEST(test_truncation);
  return UNITY_END();
}
