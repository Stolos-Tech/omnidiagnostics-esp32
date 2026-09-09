// Чиста логіка головного меню лаунчера — без апаратних викликів,
// тестується на хості через `pio test -e native`.
// Рендеринг (спрайт) і зв'язка з драйверами кнопок — окремим шаром.
#pragma once

class LauncherModel {
public:
  static const int MAX_ITEMS = 40;  // 21 вбудованих + до 12 скриптів + Power Off + запас

  void clear();
  bool add_item(const char* name);       // false — список повний або name==nullptr
  int count() const { return count_; }
  int cursor() const { return cursor_; }
  const char* item_name(int idx) const;  // nullptr якщо idx поза межами

  void move_prev();   // S1: вгору по списку, циклічно
  void move_next();   // S2: вниз по списку, циклічно
  void set_cursor(int idx);  // встановити курсор (для відновлення); поза межами -> ігнор
  int select() const; // S5: індекс обраного пункту, -1 якщо список порожній

private:
  const char* items_[MAX_ITEMS] = {};
  int count_ = 0;
  int cursor_ = 0;
};
