// Чиста логіка роботи зі шляхами .be-скриптів — без файлової системи,
// тестується на хості.
#pragma once
#include <stddef.h>

// Витягує людське ім'я застосунку зі шляху скрипта:
//   "/apps/battery.be" -> "battery", "wifi_scan.be" -> "wifi_scan".
// Прибирає теку і розширення .be. Пише в out (не більше out_size, з '\0').
// Повертає true, якщо шлях виглядає як .be-файл з непорожнім іменем.
bool script_name_from_path(const char* path, char* out, size_t out_size);

// Категорії бібліотеки скриптів = модулі ОС. Скрипти лежать у /apps/<категорія>/<ім'я>.be;
// файли просто в /apps/ (стара розкладка) вважаються категорією "misc".
#define SCRIPT_CAT_COUNT 5
extern const char* const SCRIPT_CATEGORIES[SCRIPT_CAT_COUNT];

// true, якщо рядок — одна з відомих категорій (точний збіг).
bool script_category_valid(const char* cat);

// Витягує категорію зі шляху:
//   "/apps/net/scan.be" -> "net", "/apps/battery.be" -> "misc" (стара розкладка).
// Невідома тека теж дає "misc" (щоб чужі файли не ламали меню). Повертає false
// лише для явно невалідного входу (nullptr/порожній буфер/не .be).
bool script_category_from_path(const char* path, char* out, size_t out_size);

// Будує шлях зберігання: (cat="net", name="scan.be") -> "/apps/net/scan.be".
// Невідома категорія -> false. Ім'я має бути *.be без слешів.
bool script_build_path(const char* cat, const char* name, char* out, size_t out_size);
