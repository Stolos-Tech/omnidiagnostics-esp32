// Сховище звітів у LittleFS (/reports/*.txt): збереження знімків сканів/списків
// з телефона (кнопка "Зберегти вид" у веб), перегляд і завантаження назад.
// Кожен файл має заголовок з міткою часу (NTP, якщо синхронізовано, інакше uptime).
#pragma once
#include <stddef.h>
#include <Arduino.h>

#define REPORT_MAX 20          // максимум файлів (старіші видаляються)
#define REPORT_NAME_MAX 44

bool report_store_init();      // створити /reports (ідемпотентно)
// Зберегти content під санітизованою міткою; додає заголовок з часом. false — помилка.
bool report_save(const char* label, const char* content);
// Список базових імен (відсортовано за іменем). Повертає кількість.
int  report_list(char names[][REPORT_NAME_MAX], int max);
bool report_read(const char* name, String& out);   // name = базове імʼя
bool report_delete(const char* name);
