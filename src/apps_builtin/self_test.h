// Self Test — автоматичний прогін усіх (не-радіо) мережевих модулів почерзі:
// для кожного викликає init() -> пампить loop() задану кількість секунд ->
// знімає remote_state() -> зберігає звіт у /reports через report_save().
// Звіти потім забирає Telegram-бот (auto-sync / веб-кнопка). Дозволяє одним
// тапом протестувати всю мережеву діагностику й отримати звіти в хмару.
//
// Радіо-модулі (Sniffer/Deauth/Analyzer/ChannelMon/BT/BLE) сюди НЕ входять —
// вони скидають SoftAP і рвуть звʼязок; тестуються окремо.
//
// Керування: у IDLE S5=старт, S2=вихід; під час прогону S2=стоп; у кінці —
// гортабельний браузер результатів (S2=наступний модуль по колу, S5=повтор,
// back/довга-ліва=вихід).
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

// Один крок прогону: який застосунок ганяти, під яким slug зберегти звіт,
// опційний text-ввід (host/url) і скільки секунд тримати.
struct SelfTestTarget {
  App* app;
  const char* slug;
  const char* field;   // nullptr, якщо ввід не потрібен
  const char* value;
  uint16_t seconds;
};

class SelfTestApp : public App {
public:
  void configure(const SelfTestTarget* targets, int count) { targets_ = targets; count_ = count; }

  const char* name() const override { return "Self Test"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void on_exit() override;  // звільнити буфер результатів при виході в лаунчер
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  enum Phase { IDLE, RUNNING, DONE };

  // Компактний зріз результату одного модуля — тримається в RAM після прогону,
  // щоб гортати на платі/телефоні (повне тіло однаково лежить у /reports).
  static const int ST_LINES = 8;    // рядків результату на модуль
  static const int ST_LLEN  = 42;   // символів у рядку
  struct Res {
    char name[24];
    char line[ST_LINES][ST_LLEN];
    int  n;      // скільки рядків заповнено
    bool ok;     // report_save вдався
  };

  Phase phase_ = IDLE;
  bool wants_exit_ = false;
  const SelfTestTarget* targets_ = nullptr;
  int count_ = 0;
  int cur_ = 0;
  int saved_ = 0;
  bool stage_inited_ = false;
  uint32_t stage_start_ = 0;

  // Буфер результатів виділяється на КУПІ лише на час прогону/перегляду і
  // звільняється у on_exit() — щоб не тримати ~5КБ статики постійно (вільна купа
  // критична для стабільності WiFi AP+STA+WS: кожен клієнт коштує ~2.3КБ).
  Res* results_ = nullptr;
  int results_count_ = 0;  // скільки модулів реально відпрацювало (для браузера)
  int view_ = 0;           // 0 = підсумок, 1..results_count_ = модуль

  void save_current();
  void enter_done();       // перехід у DONE: скинути позицію браузера
  void start_run();        // виділити буфер (за потреби) і почати прогін
};
