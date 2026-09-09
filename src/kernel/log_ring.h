// Кільцевий буфер рядків логу — чиста логіка, тестується на хості.
// Тримає останні CAP рядків; total() — скільки всього додано (монотонно),
// since() віддає нові рядки з курсора для трансляції у веб-консоль.
#pragma once
#include <stdint.h>

class LogRing {
public:
  static const int CAP  = 40;
  static const int LINE = 100;

  void push(const char* s);
  uint32_t total() const { return total_; }

  // Копіює рядки з глобальними індексами [from, total_), що ще збережені,
  // у out (до max рядків). Повертає кількість. Старіші за буфер пропускаються.
  int since(uint32_t from, char out[][LINE], int max) const;

private:
  char buf_[CAP][LINE] = {};
  uint32_t total_ = 0;
};

// Глобальний буфер логу ОС.
LogRing& log_ring();
