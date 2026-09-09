// Юніт-тести edge-detection кнопок (pio test -e native)
#include <unity.h>
#include "drivers/button_edge.h"

void setUp() {}
void tearDown() {}

static const uint32_t DB = ButtonEdgeDetector::DEBOUNCE_MS;

// Стійкий натиск: рівно одна подія на перехід HIGH->LOW
void test_single_press_fires_once() {
  ButtonEdgeDetector d;
  TEST_ASSERT_FALSE(d.update(0, false, 100));       // сирий перехід — старт вікна
  TEST_ASSERT_TRUE(d.update(0, false, 100 + DB));   // стабільно 30мс — подія
  TEST_ASSERT_FALSE(d.update(0, false, 100 + DB + 50)); // утримання — без повторів
  TEST_ASSERT_FALSE(d.update(0, false, 100 + DB + 500));
}

// Відпускання (LOW->HIGH) подію не породжує
void test_release_no_event() {
  ButtonEdgeDetector d;
  d.update(0, false, 100);
  TEST_ASSERT_TRUE(d.update(0, false, 100 + DB));
  TEST_ASSERT_FALSE(d.update(0, true, 200));        // сирий перехід вгору
  TEST_ASSERT_FALSE(d.update(0, true, 200 + DB));   // стійкий HIGH — без події
}

// Повторний натиск після відпускання — нова подія
void test_second_press_after_release() {
  ButtonEdgeDetector d;
  d.update(0, false, 100);
  TEST_ASSERT_TRUE(d.update(0, false, 100 + DB));
  d.update(0, true, 300);
  d.update(0, true, 300 + DB);
  d.update(0, false, 500);
  TEST_ASSERT_TRUE(d.update(0, false, 500 + DB));
}

// Брязкіт коротший за вікно антибрязкоту — жодної події
void test_bounce_filtered() {
  ButtonEdgeDetector d;
  TEST_ASSERT_FALSE(d.update(0, false, 100)); // впало
  TEST_ASSERT_FALSE(d.update(0, true, 110));  // підскочило через 10мс
  TEST_ASSERT_FALSE(d.update(0, false, 115)); // знову впало — вікно перезапущено
  TEST_ASSERT_FALSE(d.update(0, true, 120));  // і знову вгору
  TEST_ASSERT_FALSE(d.update(0, true, 120 + DB)); // заспокоїлось на HIGH — без події
}

// Брязкіт при натиску, що завершився стійким LOW — одна подія після заспокоєння
void test_bounce_then_stable_press() {
  ButtonEdgeDetector d;
  d.update(0, false, 100);
  d.update(0, true, 105);
  d.update(0, false, 112);                          // останній сирий перехід
  TEST_ASSERT_FALSE(d.update(0, false, 112 + DB - 1));
  TEST_ASSERT_TRUE(d.update(0, false, 112 + DB));   // стабільний LOW — подія
}

// Кнопки незалежні одна від одної
void test_buttons_independent() {
  ButtonEdgeDetector d;
  d.update(0, false, 100);
  d.update(2, false, 100);
  TEST_ASSERT_TRUE(d.update(0, false, 100 + DB));
  TEST_ASSERT_TRUE(d.update(2, false, 100 + DB));
  TEST_ASSERT_FALSE(d.update(1, true, 100 + DB));
}

// Невалідний індекс — false, без падіння
void test_invalid_index() {
  ButtonEdgeDetector d;
  TEST_ASSERT_FALSE(d.update(-1, false, 100));
  TEST_ASSERT_FALSE(d.update(ButtonEdgeDetector::MAX_BUTTONS, false, 100));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_press_fires_once);
  RUN_TEST(test_release_no_event);
  RUN_TEST(test_second_press_after_release);
  RUN_TEST(test_bounce_filtered);
  RUN_TEST(test_bounce_then_stable_press);
  RUN_TEST(test_buttons_independent);
  RUN_TEST(test_invalid_index);
  return UNITY_END();
}
