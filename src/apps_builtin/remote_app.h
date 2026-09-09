// Remote — вкомпільований застосунок керування віддаленим режимом (WiFi або BT).
// Один клас, дві інстанції (configure(MODE_WIFI,...) і configure(MODE_BT,...)).
// Активує свій режим через координатор (той зупиняє протилежний транспорт),
// показує згенерований PIN великим шрифтом і стан підключення.
// Транспорт обслуговується в головному циклі ядра й лишається активним після
// виходу з екрана (щоб клієнт дзеркалив будь-який екран).
#pragma once
#include "../kernel/app_interface.h"
#include "../remote/session.h"

class RemoteApp : public App {
public:
  void configure(RemoteMode mode, const char* menu_name);

  const char* name() const override { return name_; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  RemoteMode mode_ = MODE_WIFI;
  char name_[20] = "Remote";
  bool wants_exit_ = false;
  bool low_power_ = false;  // активацію відхилено — напруга занизька для радіо
  bool confirming_stop_ = false;  // S5 натиснуто раз — питаємо підтвердження
};
