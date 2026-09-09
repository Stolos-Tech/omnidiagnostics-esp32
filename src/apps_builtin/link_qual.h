// Link Qual — монітор якості звʼязку: періодичний пінг до цілі, RTT у часі,
// джитер, втрати пакетів. Ціль за замовчуванням — шлюз; S2 циклить
// шлюз/1.1.1.1/8.8.8.8; з телефона можна задати свою (text field "target").
// Потребує STA-підключення ("WiFi Setup").
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class LinkQualApp : public App {
public:
  static const int N = 58;   // точок у графіку RTT

  const char* name() const override { return "Link Qual"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void text(const char* field, const char* value) override;
  std::string remote_state() override;
  bool wants_exit() const override { return wants_exit_; }

private:
  enum Phase { NOT_CONN, RUNNING };
  Phase phase_ = RUNNING;
  bool wants_exit_ = false;
  char target_[40] = "";
  int  tsel_ = 0;             // 0=шлюз 1=1.1.1.1 2=8.8.8.8 3=кастом
  uint32_t t_ping_ = 0;
  int  rtt_[N];              // мс, -1 = втрата
  int  count_ = 0;           // скільки заповнено (до N)
  int  head_ = 0;            // кільцевий індекс наступного запису
  uint32_t sent_ = 0, lost_ = 0;

  bool connected() const;
  void set_target_by_sel();
  void reset_stats();
  void do_ping();
};
