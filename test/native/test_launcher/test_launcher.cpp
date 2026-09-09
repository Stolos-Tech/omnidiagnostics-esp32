// Юніт-тести логіки лаунчера — виконуються на хості (pio test -e native),
// без заліза ESP32.
#include <unity.h>
#include "kernel/launcher.h"

void setUp() {}
void tearDown() {}

// Порожній список: курсор 0, select() = -1, рухи не падають
void test_empty_list() {
  LauncherModel m;
  m.clear();
  TEST_ASSERT_EQUAL_INT(0, m.count());
  TEST_ASSERT_EQUAL_INT(0, m.cursor());
  TEST_ASSERT_EQUAL_INT(-1, m.select());
  m.move_next();
  m.move_prev();
  TEST_ASSERT_EQUAL_INT(0, m.cursor());
  TEST_ASSERT_NULL(m.item_name(0));
}

// Додавання пунктів: лічильник та імена
void test_add_items() {
  LauncherModel m;
  m.clear();
  TEST_ASSERT_TRUE(m.add_item("Battery Diag"));
  TEST_ASSERT_TRUE(m.add_item("Settings"));
  TEST_ASSERT_EQUAL_INT(2, m.count());
  TEST_ASSERT_EQUAL_STRING("Battery Diag", m.item_name(0));
  TEST_ASSERT_EQUAL_STRING("Settings", m.item_name(1));
  TEST_ASSERT_NULL(m.item_name(2));
  TEST_ASSERT_NULL(m.item_name(-1));
  TEST_ASSERT_FALSE(m.add_item(nullptr));
}

// Гортання вниз: рух і циклічний перехід через кінець
void test_move_next_wraps() {
  LauncherModel m;
  m.clear();
  m.add_item("A"); m.add_item("B"); m.add_item("C");
  TEST_ASSERT_EQUAL_INT(0, m.cursor());
  m.move_next();
  TEST_ASSERT_EQUAL_INT(1, m.cursor());
  m.move_next();
  TEST_ASSERT_EQUAL_INT(2, m.cursor());
  m.move_next(); // з останнього — на перший
  TEST_ASSERT_EQUAL_INT(0, m.cursor());
}

// Гортання вгору: з першого пункту — на останній
void test_move_prev_wraps() {
  LauncherModel m;
  m.clear();
  m.add_item("A"); m.add_item("B"); m.add_item("C");
  m.move_prev();
  TEST_ASSERT_EQUAL_INT(2, m.cursor());
  m.move_prev();
  TEST_ASSERT_EQUAL_INT(1, m.cursor());
}

// Вибір: select() повертає поточний курсор
void test_select_returns_cursor() {
  LauncherModel m;
  m.clear();
  m.add_item("A"); m.add_item("B");
  TEST_ASSERT_EQUAL_INT(0, m.select());
  m.move_next();
  TEST_ASSERT_EQUAL_INT(1, m.select());
}

// Переповнення: понад MAX_ITEMS не додається
void test_overflow_rejected() {
  LauncherModel m;
  m.clear();
  for (int i = 0; i < LauncherModel::MAX_ITEMS; i++)
    TEST_ASSERT_TRUE(m.add_item("x"));
  TEST_ASSERT_FALSE(m.add_item("overflow"));
  TEST_ASSERT_EQUAL_INT(LauncherModel::MAX_ITEMS, m.count());
}

// set_cursor: встановлює позицію в межах, ігнорує поза межами
void test_set_cursor() {
  LauncherModel m;
  m.clear();
  m.add_item("A"); m.add_item("B"); m.add_item("C");
  m.set_cursor(2);
  TEST_ASSERT_EQUAL_INT(2, m.cursor());
  m.set_cursor(99);            // поза межами -> без змін
  TEST_ASSERT_EQUAL_INT(2, m.cursor());
  m.set_cursor(-1);           // поза межами -> без змін
  TEST_ASSERT_EQUAL_INT(2, m.cursor());
}

// clear(): скидає список і курсор
void test_clear_resets() {
  LauncherModel m;
  m.clear();
  m.add_item("A"); m.add_item("B");
  m.move_next();
  m.clear();
  TEST_ASSERT_EQUAL_INT(0, m.count());
  TEST_ASSERT_EQUAL_INT(0, m.cursor());
  TEST_ASSERT_EQUAL_INT(-1, m.select());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_list);
  RUN_TEST(test_add_items);
  RUN_TEST(test_move_next_wraps);
  RUN_TEST(test_move_prev_wraps);
  RUN_TEST(test_select_returns_cursor);
  RUN_TEST(test_overflow_rejected);
  RUN_TEST(test_set_cursor);
  RUN_TEST(test_clear_resets);
  return UNITY_END();
}
