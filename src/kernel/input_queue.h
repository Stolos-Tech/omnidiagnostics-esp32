// Єдина черга вхідних подій ядра. Сюди пишуть УСІ джерела вводу:
// фізичні кнопки (drivers/buttons), пізніше — WiFi/BT транспорти (Фаза 4).
// Застосунки читають події, не знаючи джерела.
// Чиста логіка без апаратних викликів — тестується на хості.
#pragma once
#include <stdint.h>
#include "app_interface.h"

enum EventType : uint8_t {
  EV_NONE = 0,
  EV_BUTTON,
  EV_TEXT,      // ввід тексту з телефона (напр. WiFi-пароль): field + value
  EV_BACK,      // універсальний "назад" (довге утримання лівої кнопки)
  EV_INDEX      // прямий вибір пункту меню/списку за індексом (тап у веб)
};

static const int EV_FIELD_MAX = 24;
static const int EV_VALUE_MAX = 96;

struct InputEvent {
  EventType type;
  ButtonId btn;                    // валідне при EV_BUTTON
  int index;                       // валідне при EV_INDEX
  char field[EV_FIELD_MAX];        // при EV_TEXT — назва поля
  char value[EV_VALUE_MAX];        // при EV_TEXT — значення
};

class InputQueue {
public:
  static const int CAPACITY = 16;

  bool push_button(ButtonId id);              // false якщо черга повна
  bool push_text(const char* field, const char* value);  // false якщо черга повна
  bool push_back();                           // подія "назад" (вихід у лаунчер)
  bool push_index(int idx);                   // прямий вибір пункту за індексом
  bool pop(InputEvent& out);      // false якщо черга порожня
  bool empty() const { return size_ == 0; }
  int size() const { return size_; }
  void clear();

private:
  InputEvent buf_[CAPACITY];
  int head_ = 0;  // звідки читаємо
  int tail_ = 0;  // куди пишемо
  int size_ = 0;
};

// Глобальна черга ядра — єдина точка входу для всіх джерел (push_event з розділу 3)
InputQueue& input_queue();
