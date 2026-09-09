// Settings — профіль ОС: яскравість, таймаут сну, автозапуск застосунку.
// S2 — наступне поле, S5 — змінити значення (циклічно). Застосовується й
// зберігається одразу. З телефона: text field="bright"/"sleep"/"auto".
#pragma once
#include "../kernel/app_interface.h"

class SettingsApp : public App {
public:
  // Список імен застосунків для вибору автозапуску (з main).
  void configure(const char* const* app_names, int app_count);

  const char* name() const override { return "Settings"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Field { F_BRIGHT, F_SLEEP, F_AUTO, F_STEALTH, F_MDNS, F_THEME, F_WEBUI, F_EXIT, F_COUNT };
  int  field_ = F_BRIGHT;
  bool wants_exit_ = false;
  const char* const* names_ = nullptr;
  int  names_count_ = 0;

  void cycle_current();
  void apply_brightness();
  const char* auto_label() const;
};
