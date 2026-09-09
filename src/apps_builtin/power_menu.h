// "Power" — меню живлення: Passive (екран off, плата працює), Reboot (софт-ребут
// замість зламаної фізичної RST-кнопки), Power Off (deep sleep). Замінює колишній
// хвостовий пункт "Power Off" одним застосунком.
#pragma once
#include "../kernel/app_interface.h"

class PowerApp : public App {
public:
  const char* name() const override { return "Power"; }
  void init() override;
  void loop() override {}
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  int  sel_ = 0;              // 0=Passive 1=Reboot 2=Power Off
  bool wants_exit_ = false;
  void act(int i);
};
