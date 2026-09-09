// RFID Clone — red-team: копіює ДАНІ-блоки з source-карти на target (обидві Mifare
// Classic на дефолтних/відомих ключах). UID НЕ клонується (для цього треба magic
// gen1a-карта). Симбіоз: ESP тримає дамп (з аудиту UNO), UNO фізично пише (WRITE->
// WRES). Блок 0 і трейлери секторів пропускаються (safety). Група RFID.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class RfidCloneApp : public App {
public:
  const char* name() const override { return "RFID Clone"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  enum Phase { WAIT_SRC, READY, WRITING, DONE };
  static const int MAX_BLK = 48;
  Phase phase_ = WAIT_SRC;
  bool wants_exit_ = false;
  uint32_t src_seq_ = 0;
  char src_uid_[21] = {0};
  int  blk_[MAX_BLK];
  uint8_t data_[MAX_BLK][16];
  int  nblk_ = 0;
  int  wr_idx_ = 0;
  uint32_t t_wr_ = 0;

  void capture_from_audit();
  void topBar(const char* title);
};
