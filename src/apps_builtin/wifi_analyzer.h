// WiFi Analyzer — глибша WiFi-діагностика: топ-5 мереж за RSSI (канал/шифрування),
// гістограма завантаженості каналів 1-13, графік RSSI підключеної мережі в часі.
// Використовує вже наявний драйвер wifi_sta (той самий синхронний скан, що й
// WiFi Setup) — не дублює логіку сканування.
//
// Керування (2 рідні кнопки — S1/S3/S4 з'являться з платою 5 свічів):
//   S2 — наступна сторінка (LIST -> CHANNELS -> RSSI -> EXIT -> LIST)
//   S5 — пересканувати (на EXIT — підтвердити вихід)
#pragma once
#include "../kernel/app_interface.h"
#include <stdint.h>

class WifiAnalyzerApp : public App {
public:
  static const int MAX_NETS = 20;
  static const int G_N = 60;  // точок RSSI-графіка (~2 хв при 2с/точка)

  const char* name() const override { return "WiFi Analyzer"; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;
  bool auto_snapshot() const override { return true; }   // авто-лог мереж на SD при виході

private:
  enum Phase { SCANNING, BT_BLOCKED, LOW_POWER, NAV };
  enum Page { LIST, CHANNELS, RSSI_GRAPH, PAGE_EXIT, PAGE_COUNT };

  Phase phase_ = SCANNING;
  Page page_ = LIST;
  bool wants_exit_ = false;
  int scan_frame_ = 0;

  int order_[MAX_NETS];   // індекси мереж, відсортовані за RSSI спадно
  int count_ = 0;

  int8_t chan_hist_[14] = {0};  // [1..13] кількість APs на канал

  float gBuf_[G_N];
  int gCount_ = 0;
  uint32_t tGraph_ = 0;

  void start_scan();
  void do_scan();
  void build_channels();
  void pageDots();
  void drawList();
  void drawChannels();
  void drawRssiGraph();
  void drawExit();
};
