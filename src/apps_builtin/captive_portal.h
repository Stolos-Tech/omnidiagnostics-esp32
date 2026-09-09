// Captive Portal — детектор перехоплення інтернету. Робить HTTP-пробу до
// відомого "generate_204"-ендпоінта: чиста мережа має вернути 204 з порожнім
// тілом; перехоплення (готель/аеропорт) підмінює відповідь (200/редірект на
// портал). Показує вердикт + код + хост порталу. Потребує STA ("WiFi Setup").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class CaptivePortalApp : public App {
public:
  const char* name() const override { return "Captive"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, CHECKING, ACCEPTING, DONE };
  enum Verdict { V_OPEN, V_CAPTIVE, V_NONET };

  Phase phase_ = CHECKING;
  bool wants_exit_ = false;
  Verdict verdict_ = V_NONET;
  int  code_ = 0;
  char detail_[64] = "";
  char portal_[160] = "";   // URL вітальної сторінки (для best-effort авто-кліку)

  bool connected() const;
  void run_check();
  void try_accept();        // best-effort: сабмітнути форму/редірект вітальної сторінки
};
