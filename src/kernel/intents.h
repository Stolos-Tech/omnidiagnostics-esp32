// Наміри між застосунками: спосіб передати «відкрити URL у HTTP GET» тощо
// без прямої залежності одного застосунку від іншого. Ядро забирає намір
// у головному циклі й перемикає застосунок.
#pragma once
#include <stddef.h>

// Застосунок просить відкрити URL у HTTP GET (напр. Net Scan -> хост:80).
void intent_open_url(const char* url);

// Ядро: забирає намір (одноразово). false якщо наміру нема.
bool intent_take_url(char* out, size_t out_size);
