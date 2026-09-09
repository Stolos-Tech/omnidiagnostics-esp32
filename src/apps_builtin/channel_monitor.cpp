#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "channel_monitor.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define DWELL_MS 200
#define MAX_RUN_MS 60000   // авто-вихід через 60с: promiscuous рубить мережу, тож не даємо
                           // йому крутитись нескінченно (юзер міг запустити з телефона й втратити UI)

static uint32_t s_counts[14] = {0};   // [1..13], лічильник кадрів на канал
static bool     s_promisc_on = false;

// Викликається з WiFi-задачі на кожен прийнятий кадр — лише інкремент
// лічильника, БЕЗ Serial/дисплея/алокацій (не блокуюче, поза основним циклом).
static void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  int ch = p->rx_ctrl.channel;
  if (ch >= 1 && ch <= 13) s_counts[ch]++;
}

void ChannelMonitorApp::init() {
  wants_exit_ = false;
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }
  if (!power_guard_ok()) { phase_ = LOW_POWER; return; }
  phase_ = RUNNING;
  was_wifi_ = (remote_active_mode() == MODE_WIFI);  // щоб відновити транспорт на виході
  memset(s_counts, 0, sizeof(s_counts));
  cur_channel_ = 1;
  t_hop_ = millis();
  t_start_ = millis();
  WiFi.mode(WIFI_MODE_STA);   // піднімає WiFi-драйвер (без підключення)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
  esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  s_promisc_on = true;
}

void ChannelMonitorApp::on_exit() {
  if (s_promisc_on) {
    esp_wifi_set_promiscuous(false);
    s_promisc_on = false;
  }
  remote_restore_after_promiscuous(was_wifi_);   // відновити SoftAP+web+STA
}

void ChannelMonitorApp::loop() {
  if (phase_ != RUNNING) return;
  if (millis() - t_start_ > MAX_RUN_MS) { wants_exit_ = true; return; }  // авто-вихід -> мережа назад
  if (millis() - t_hop_ >= DWELL_MS) {
    t_hop_ = millis();
    cur_channel_ = (cur_channel_ % 13) + 1;
    esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  }
}


void ChannelMonitorApp::drawRunning() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("CHANNEL MONITOR");
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_WARN, C_PANEL);
  char cb[8]; snprintf(cb, sizeof(cb), "ch%d", cur_channel_);
  spr.drawString(cb, SCR_W - 6, 3, 2);

  uint32_t maxc = 1;
  for (int c = 1; c <= 13; c++) if (s_counts[c] > maxc) maxc = s_counts[c];
  int gx = 6, gy = 22, gw = SCR_W - 12, gh = SCR_H - 44;
  int bw = gw / 13;
  for (int c = 1; c <= 13; c++) {
    int h = (int)((float)gh * s_counts[c] / maxc);
    int x = gx + (c - 1) * bw;
    uint16_t col = c == cur_channel_ ? C_WARN : (s_counts[c] == 0 ? C_GRID : C_ACCENT);
    if (h > 0) spr.fillRect(x + 1, gy + gh - h, bw - 2, h, col);
    else       spr.drawFastHLine(x + 1, gy + gh, bw - 2, C_GRID);
    spr.setTextDatum(TC_DATUM); spr.setTextColor(C_DIM, C_BG);
    char cl[3]; snprintf(cl, sizeof(cl), "%d", c);
    spr.drawString(cl, x + bw / 2, gy + gh + 2, 1);
  }
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_DIM, C_BG);
  spr.drawString("Passive only, no injection", 8, SCR_H - 22, 1);
  spr.drawString("S5=exit", 8, SCR_H - 12, 1);
}

void ChannelMonitorApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  display_top_bar("EXIT");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Exit? (STA will recover)", SCR_W / 2, SCR_H / 2 - 10, 2);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = exit   S2 = next", SCR_W / 2, SCR_H / 2 + 20, 2);
}

void ChannelMonitorApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BT_BLOCKED) {
    display_top_bar("CHANNEL MONITOR");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne mozhut razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  if (phase_ == LOW_POWER) {
    display_top_bar("CHANNEL MONITOR");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  if (phase_ == RUNNING) drawRunning();
  else                    drawExit();
}

void ChannelMonitorApp::button(ButtonId id) {
  if (phase_ == BT_BLOCKED || phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (phase_ == RUNNING) {
    if (id == BTN_S5) phase_ = PAGE_EXIT;
    return;
  }
  // PAGE_EXIT
  if (id == BTN_S5) wants_exit_ = true;
  else if (id == BTN_S2) phase_ = RUNNING;
}

std::string ChannelMonitorApp::remote_state() {
  char l0[32]; snprintf(l0, sizeof(l0), "Hop: ch%d", cur_channel_);
  const char* items[1] = { l0 };
  return protocol_build_menu("channel_monitor", items, 1, -1);
}
