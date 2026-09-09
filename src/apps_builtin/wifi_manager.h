// WiFi Setup — вкомпільований застосунок під'єднання плати до домашньої мережі.
// Сканує мережі, показує список на екрані й дзеркалить у веб (wifi_list). Пароль
// вводиться з телефона (App::text -> wifi_sta_connect). Креденшали в NVS.
//
// Керування двома рідними кнопками: S2 (права) — вниз по списку (циклічно,
// останній пункт "< Back" = вихід), S5 (ліва) — вибір/дія.
#pragma once
#include "../kernel/app_interface.h"

class WifiManagerApp : public App {
public:
  static const int MAX_NETS = 12;

  const char* name() const override { return "WiFi Setup"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  void select_index(int idx) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { SCANNING, LIST, PASSWORD, CONNECTING, CONNECTED, FAILED, SCAN_FAIL, BT_BLOCKED };

  Phase phase_ = SCANNING;
  bool wants_exit_ = false;
  int  cursor_ = 0;          // позиція в списку (0..count_, де count_ == "< Back")
  int  count_ = 0;           // к-ть мереж (без "< Back")
  int  scan_frame_ = 0;      // 0 -> показати "Scanning" кадр, потім блокуючий скан
  char ssids_[MAX_NETS][33] = {};
  bool open_[MAX_NETS] = {};
  char enc_[MAX_NETS][8] = {};   // тип захисту ("Open"/"WPA2"/... як у телефоні)
  char selected_[33] = "";

  int  items_total() const { return count_ + 1; }  // +1 на "< Back"
  const char* item_label(int i) const;             // ssid або "< Back"
  void start_scan();
  void select_current();                           // дія над поточним cursor_ у LIST
};
