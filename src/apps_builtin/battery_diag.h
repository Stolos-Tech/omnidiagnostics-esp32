// Battery Diag — монітор + діагностика Li-ion акумулятора (вейп-салваж).
// Перенесено з монолітного BatteryDiag_TDisplay.ino v2 у App-інтерфейс ядра.
//
// Керування (рідні кнопки плати):
//   BTN_S2 (права)      — наступна сторінка; на сторінці DIAG після тесту
//                         гортає підсторінки результатів, потім іде далі
//   BTN_S5 (ліва, BOOT) — дія: MAIN/ANALYSIS/GRAPH = скидання статистики,
//                         DIAG = запуск тесту / повернення на 1-шу підсторінку,
//                         EXIT = вихід у лаунчер
//
// ВАЖЛИВО: GPIO34 міряє VBAT ПІСЛЯ зарядного IC. При USB там ~4.2В (заряд),
// тож реальний стан елемента видно лише коли USB від'єднаний.
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class BatteryDiagApp : public App {
public:
  const char* name() const override { return "Battery Diag"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;   // веб-мірор даних батареї (див. .cpp)

private:
  enum Page { PAGE_MAIN, PAGE_ANALYSIS, PAGE_GRAPH, PAGE_DIAG, PAGE_EXIT, PAGE_COUNT };

  // результати останньої діагностики
  struct DiagResult {
    bool done = false;
    float ocv = 0, sag = 0, recover = 0, rint_full = 0;
    float sag_25 = 0, sag_50 = 0, sag_75 = 0;
    float rint_25 = 0, rint_50 = 0, rint_75 = 0;
    float esr_instant = 0;
    float cv_end_v = 0, cv_duration = 0;
    int health = 0;
    const char* verdict = "-";
    uint16_t vcol;
  };

  static const int G_N = 216;  // точок у рухомому графіку

  int page_ = PAGE_MAIN;
  bool wants_exit_ = false;

  float vNow_ = 0, vAvg_ = 0;
  float vMin_ = 99, vMax_ = 0;
  uint32_t rawNow_ = 0;
  float mvNow_ = 0;
  float vPrev_ = 0;
  uint32_t tPrev_ = 0;
  float dVdt_mVmin_ = 0;
  float noise_mV_ = 0;

  DiagResult diag_;
  int diag_page_ = 0;

  float gBuf_[G_N];
  int gCount_ = 0;
  uint32_t tGraph_ = 0;
  uint32_t tLast_ = 0;

  void drawMain();
  void drawAnalysis();
  void drawGraph();
  void drawDiagMenu();
  void drawExit();
  void topBar(const char* title);
  void pageDots();
  void row(int y, const char* k, const char* v, uint16_t vc);
  void diagProgress(int pct, const char* msg);
  void runDiagnostics();  // блокуюча одноразова процедура (~25с) — виняток з правила delay()
};
