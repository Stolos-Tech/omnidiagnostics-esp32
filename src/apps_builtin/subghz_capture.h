// Tier 2: захоплення й декодування OOK-пультів (433/868) на CC1101. Слухає обрану
// ISM-частоту, ловить таймінг через RMT (GDO0), декодує сімейство EV1527/PT2262
// (kernel/ook_decode) і показує код. Ідеально для тесту пультів (Nice тощо).
// Живе в групі "Sub-GHz". Лише RX.
#pragma once
#include "../kernel/app_interface.h"
#include "../kernel/ook_decode.h"
#include <stdint.h>

class SubghzCaptureApp : public App {
public:
  const char* name() const override { return "Sub-GHz Capture"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог захопленого коду на SD при виході

private:
  int        preset_ = 0;          // індекс у subghz_preset_*
  int        rssi_ = -120;         // жива RSSI на поточній частоті
  bool       present_ = false;
  bool       wants_exit_ = false;
  bool       have_capture_ = false;
  int        raw_pulses_ = 0;      // к-ть захоплених імпульсів (навіть якщо не декод.)
  OokDecoded last_;                // останній результат декодування
  void do_capture();
};
