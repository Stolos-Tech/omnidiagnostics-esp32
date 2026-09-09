// Драйвер LittleFS: монтування внутрішнього флешу як /apps + web.
// Скрипти .be заливаються через `pio run -t uploadfs` (тека data/ -> LittleFS),
// без перепрошивки основної прошивки.
#pragma once
#include <Arduino.h>

static const int FS_MAX_SCRIPTS = 12;      // ліміт пунктів-скриптів у меню
static const int FS_MAX_PATH    = 48;

// Монтує LittleFS. Спершу пробує без форматування; форматує ЛИШЕ якщо монтування
// не вдалось (і голосно про це пише). false — файлова система недоступна.
bool fs_init();

// Діагностика: рекурсивно виводить вміст файлової системи в Serial.

// Сканує бібліотеку скриптів: спершу /apps/<категорія>/*.be (див. script_path.h),
// потім /apps/*.be (стара пласка розкладка). Заповнює paths[][FS_MAX_PATH] повними
// шляхами. Повертає кількість знайдених (0..max).
int fs_list_scripts(char paths[][FS_MAX_PATH], int max);

// Створює теки категорій у /apps (ідемпотентно). Виклик у setup після fs_init.
bool fs_ensure_script_dirs();

// Читає файл повністю у out. false, якщо не відкрився.
bool fs_read_file(const char* path, String& out);

// Записує content у файл (перезаписує). false при помилці.
bool fs_write_file(const char* path, const char* content);

// Видаляє файл. false якщо не вдалось.
bool fs_delete_file(const char* path);
