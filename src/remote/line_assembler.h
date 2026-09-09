// Збирає байтовий потік (Bluetooth SPP / Serial) у рядки, розділені '\n'/'\r'.
// BluetoothSerial віддає байти шматками — а протокол працює з цілими JSON-рядками.
// Чистий шар без заліза — тестується на хості.
#pragma once
#include <stddef.h>

class LineAssembler {
public:
  // Максимальна довжина рядка (= ліміт протоколу). Довші рядки відкидаються.
  static const int MAX_LINE = 256;

  LineAssembler() { reset(); }
  void reset();

  // Подає один байт. Якщо зібрався повний НЕПОРОЖНІЙ рядок — копіює його в out
  // (з '\0') і повертає true. Інакше false. Рядки, довші за MAX_LINE, відкидаються
  // цілком (до наступного роздільника), out при цьому не заповнюється.
  bool feed(char c, char* out, size_t out_size);

private:
  char buf_[MAX_LINE + 1];
  int  len_;
  bool overflow_;  // поточний рядок перевищив ліміт -> відкидаємо до роздільника
};
