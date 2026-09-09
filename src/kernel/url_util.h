// Чисті утиліти для URL — без мережі, тестуються на хості.
#pragma once
#include <stddef.h>

// Нормалізує введений рядок у повний HTTP-URL:
//   "192.168.1.50/status" -> "http://192.168.1.50/status"
//   "http://host/x"        -> без змін
//   "https://host/x"       -> без змін
// Пише результат у out (з '\0'). Повертає false, якщо вхід порожній або не влазить.
bool http_normalize_url(const char* in, char* out, size_t out_size);
