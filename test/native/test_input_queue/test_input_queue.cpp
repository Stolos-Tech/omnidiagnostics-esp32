// Юніт-тести спільної черги подій (pio test -e native)
#include <unity.h>
#include <string.h>
#include "kernel/input_queue.h"

void setUp() {}
void tearDown() {}

// Порожня черга: pop() = false, empty() = true
void test_empty_queue() {
  InputQueue q;
  InputEvent ev;
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_EQUAL_INT(0, q.size());
  TEST_ASSERT_FALSE(q.pop(ev));
}

// FIFO: події виходять у порядку надходження
void test_fifo_order() {
  InputQueue q;
  q.push_button(BTN_S1);
  q.push_button(BTN_S3);
  q.push_button(BTN_S5);
  InputEvent ev;
  TEST_ASSERT_TRUE(q.pop(ev));
  TEST_ASSERT_EQUAL_INT(EV_BUTTON, ev.type);
  TEST_ASSERT_EQUAL_INT(BTN_S1, ev.btn);
  TEST_ASSERT_TRUE(q.pop(ev));
  TEST_ASSERT_EQUAL_INT(BTN_S3, ev.btn);
  TEST_ASSERT_TRUE(q.pop(ev));
  TEST_ASSERT_EQUAL_INT(BTN_S5, ev.btn);
  TEST_ASSERT_TRUE(q.empty());
}

// Переповнення: push у повну чергу повертає false, події не губляться мовчки
void test_overflow() {
  InputQueue q;
  for (int i = 0; i < InputQueue::CAPACITY; i++)
    TEST_ASSERT_TRUE(q.push_button(BTN_S1));
  TEST_ASSERT_FALSE(q.push_button(BTN_S2));
  TEST_ASSERT_EQUAL_INT(InputQueue::CAPACITY, q.size());
}

// Кільцевий буфер: після pop місце звільняється
void test_ring_reuse() {
  InputQueue q;
  InputEvent ev;
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < InputQueue::CAPACITY; i++)
      TEST_ASSERT_TRUE(q.push_button(BTN_S4));
    for (int i = 0; i < InputQueue::CAPACITY; i++)
      TEST_ASSERT_TRUE(q.pop(ev));
    TEST_ASSERT_TRUE(q.empty());
  }
}

// clear(): черга порожніє
void test_clear() {
  InputQueue q;
  q.push_button(BTN_S1);
  q.push_button(BTN_S2);
  q.clear();
  InputEvent ev;
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_FALSE(q.pop(ev));
}

// Текстова подія: field/value зберігаються і читаються
void test_push_text() {
  InputQueue q;
  TEST_ASSERT_TRUE(q.push_text("password", "secret123"));
  InputEvent ev;
  TEST_ASSERT_TRUE(q.pop(ev));
  TEST_ASSERT_EQUAL_INT(EV_TEXT, ev.type);
  TEST_ASSERT_EQUAL_STRING("password", ev.field);
  TEST_ASSERT_EQUAL_STRING("secret123", ev.value);
}

// Змішана черга: кнопки й текст у порядку FIFO
void test_mixed_fifo() {
  InputQueue q;
  q.push_button(BTN_S1);
  q.push_text("f", "v");
  q.push_button(BTN_S2);
  InputEvent ev;
  q.pop(ev); TEST_ASSERT_EQUAL_INT(EV_BUTTON, ev.type); TEST_ASSERT_EQUAL_INT(BTN_S1, ev.btn);
  q.pop(ev); TEST_ASSERT_EQUAL_INT(EV_TEXT, ev.type);   TEST_ASSERT_EQUAL_STRING("v", ev.value);
  q.pop(ev); TEST_ASSERT_EQUAL_INT(EV_BUTTON, ev.type); TEST_ASSERT_EQUAL_INT(BTN_S2, ev.btn);
}

// Задовге значення обрізається до буфера, без переповнення
void test_text_truncation() {
  InputQueue q;
  char big[200];
  for (int i = 0; i < 199; i++) big[i] = 'a';
  big[199] = '\0';
  TEST_ASSERT_TRUE(q.push_text("field", big));
  InputEvent ev;
  q.pop(ev);
  TEST_ASSERT_TRUE((int)strlen(ev.value) < EV_VALUE_MAX);
}

// Глобальна черга ядра — один і той самий екземпляр
void test_global_singleton() {
  input_queue().clear();
  input_queue().push_button(BTN_S3);
  InputEvent ev;
  TEST_ASSERT_TRUE(input_queue().pop(ev));
  TEST_ASSERT_EQUAL_INT(BTN_S3, ev.btn);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_queue);
  RUN_TEST(test_fifo_order);
  RUN_TEST(test_overflow);
  RUN_TEST(test_ring_reuse);
  RUN_TEST(test_clear);
  RUN_TEST(test_push_text);
  RUN_TEST(test_mixed_fifo);
  RUN_TEST(test_text_truncation);
  RUN_TEST(test_global_singleton);
  return UNITY_END();
}
