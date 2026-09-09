// Узагальнений застосунок-скрипт: вантажить .be з LittleFS і виконує через
// спільну Berry VM за тим самим App-інтерфейсом, що й вкомпільовані застосунки.
// Помилка скрипта (синтаксис/виконання) не рушить систему — показує "Script error"
// і дозволяє вийти назад у лаунчер.
#pragma once
#include "app_interface.h"
#include "../drivers/filesystem.h"

class BerryScriptApp : public App {
public:
  // Налаштувати перед реєстрацією: ім'я в меню + повний шлях до .be.
  void configure(const char* menu_name, const char* path);

  const char* name() const override { return name_; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  bool wants_exit() const override { return wants_exit_; }

private:
  char name_[24] = "";
  char path_[FS_MAX_PATH] = "";
  bool loaded_ok_ = false;
  bool wants_exit_ = false;
  char err_[128] = "";
};
