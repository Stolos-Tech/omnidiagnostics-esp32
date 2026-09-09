#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "deauth_alert.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define MAX_RUN_MS 90000   // авто-вихід: promiscuous рубить мережу, не даємо крутитись вічно

#define DWELL_MS 250
#define MAX_OFF 24
#define RING_SZ 32           // степінь двійки
#define ALERT_RATE 12        // кадрів за вікно ~8с -> тривога
#define WIN 8                // секунд у вікні рейту

struct Evt { uint8_t bssid[6]; int8_t rssi; uint8_t ch; uint8_t disassoc; };

static volatile uint32_t s_head = 0, s_tail = 0;
static Evt s_ring[RING_SZ];
// SPSC lock-free ring: продюсер — promiscuous-callback (ядро 0), консюмер — main loop (ядро 1).
// __sync_synchronize() між записом даних і оновленням індексу ОБОВ'ЯЗКОВИЙ: без нього компілятор/
// CPU може переставити не-volatile запис ring і volatile-запис індексу -> інше ядро побачить новий
// індекс зі СТАРИМИ даними. Бар'єр гарантує порядок (SRAM ESP32 когерентна, тож бар'єра досить).
static inline void ring_push(const Evt& e) {
  uint32_t h = s_head; if (h - s_tail >= RING_SZ) return;
  s_ring[h & (RING_SZ - 1)] = e;
  __sync_synchronize();                 // дані в ring видимі ДО публікації нового s_head
  s_head = h + 1;
}
static inline bool ring_pop(Evt& o) {
  uint32_t t = s_tail; if (t == s_head) return false;
  o = s_ring[t & (RING_SZ - 1)];
  __sync_synchronize();                 // дочитати слот ДО звільнення (advance s_tail)
  s_tail = t + 1; return true;
}

struct Off { uint8_t bssid[6]; uint16_t count; int8_t rssi; uint32_t last; };
static Off s_off[MAX_OFF];
static int s_off_n = 0;
static uint32_t s_deauth = 0, s_disassoc = 0;
static int s_buckets[WIN];
static int s_last_bucket = -1;
static bool s_promisc_on = false;

static void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* d = p->payload;
  if (p->rx_ctrl.sig_len < 24) return;
  uint8_t ftype = (d[0] >> 2) & 0x3, sub = (d[0] >> 4) & 0xF;
  if (ftype != 0 || (sub != 12 && sub != 10)) return;   // тільки deauth/disassoc
  Evt e; memcpy(e.bssid, d + 16, 6);                     // addr3 = BSSID
  e.rssi = p->rx_ctrl.rssi; e.ch = p->rx_ctrl.channel; e.disassoc = (sub == 10);
  ring_push(e);
}

static void clear_all() {
  s_off_n = 0; s_deauth = s_disassoc = 0;
  for (int i = 0; i < WIN; i++) s_buckets[i] = 0;
  s_last_bucket = -1; s_tail = s_head;
}

static int rate_now() { int r = 0; for (int i = 0; i < WIN; i++) r += s_buckets[i]; return r; }

static void bump_off(const uint8_t* bssid, int8_t rssi) {
  for (int i = 0; i < s_off_n; i++) if (memcmp(s_off[i].bssid, bssid, 6) == 0) {
    s_off[i].count++; s_off[i].rssi = rssi; s_off[i].last = millis(); return;
  }
  if (s_off_n >= MAX_OFF) return;
  Off& o = s_off[s_off_n++]; memcpy(o.bssid, bssid, 6); o.count = 1; o.rssi = rssi; o.last = millis();
}

void DeauthAlertApp::init() {
  wants_exit_ = false; page_ = SUMMARY;
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }
  if (!power_guard_ok())               { phase_ = LOW_POWER;  return; }
  phase_ = RUNNING;
  was_wifi_ = (remote_active_mode() == MODE_WIFI);
  clear_all();
  cur_channel_ = 1; t_hop_ = millis(); t_start_ = millis();
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
  esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  s_promisc_on = true;
}

void DeauthAlertApp::on_exit() {
  if (s_promisc_on) { esp_wifi_set_promiscuous(false); s_promisc_on = false; }
  remote_restore_after_promiscuous(was_wifi_);   // відновити SoftAP+web+STA, які забрав promiscuous
}

void DeauthAlertApp::loop() {
  if (phase_ != RUNNING) return;
  uint32_t now = millis();
  if (now - t_start_ > MAX_RUN_MS) { wants_exit_ = true; return; }   // авто-вихід -> мережа назад
  if (now - t_hop_ >= DWELL_MS) {
    t_hop_ = now; cur_channel_ = (cur_channel_ % 13) + 1;
    esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  }
  int bi = (int)((now / 1000) % WIN);
  if (bi != s_last_bucket) { s_buckets[bi] = 0; s_last_bucket = bi; }

  Evt e; int budget = 32;
  while (budget-- > 0 && ring_pop(e)) {
    if (e.disassoc) s_disassoc++; else s_deauth++;
    s_buckets[bi]++;
    bump_off(e.bssid, e.rssi);
  }

  static uint32_t t_dbg = 0;
  if (now - t_dbg > 3000) {
    t_dbg = now;
    Serial.printf("[DEAUTH] ch%d deauth=%lu disassoc=%lu rate8s=%d %s\n",
                  cur_channel_, (unsigned long)s_deauth, (unsigned long)s_disassoc,
                  rate_now(), rate_now() >= ALERT_RATE ? "ALERT" : "");
  }
}

static int top_off(int* order, int cap) {
  int used = 0; bool taken[MAX_OFF] = { false };
  for (int k = 0; k < cap && used < s_off_n; k++) {
    int best = -1;
    for (int i = 0; i < s_off_n; i++)
      if (!taken[i] && (best < 0 || s_off[i].count > s_off[best].count)) best = i;
    if (best < 0) break; taken[best] = true; order[used++] = best;
  }
  return used;
}

void DeauthAlertApp::topBar(const char* t) {
  display_top_bar(t);
  TFT_eSprite& spr = display_sprite();
  if (phase_ == RUNNING) {
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_WARN, C_PANEL);
    char cb[8]; snprintf(cb, sizeof(cb), "ch%d", cur_channel_);
    spr.drawString(cb, SCR_W - 6, 3, 2);
  }
}

void DeauthAlertApp::drawSummary() {
  TFT_eSprite& spr = display_sprite();
  topBar("DEAUTH ALERT");
  int rate = rate_now();
  bool alert = rate >= ALERT_RATE;
  if (alert) {
    spr.fillRect(0, 18, SCR_W, 26, C_BAD);
    spr.setTextDatum(MC_DATUM); spr.setTextColor(C_TEXT, C_BAD);
    spr.drawString("!! DEAUTH FLOOD !!", SCR_W / 2, 31, 2);
    spr.setTextDatum(TL_DATUM);
  } else {
    spr.setTextColor(C_GOOD, C_BG); spr.drawString("Efir chystyy", 10, 24, 2);
  }
  char b[40];
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "deauth: %lu", (unsigned long)s_deauth);   spr.drawString(b, 10, 52, 2);
  snprintf(b, sizeof(b), "disassoc: %lu", (unsigned long)s_disassoc); spr.drawString(b, 10, 72, 2);
  spr.setTextColor(alert ? C_BAD : C_DIM, C_BG);
  snprintf(b, sizeof(b), "rate(%ds): %d  (poriz %d)", WIN, rate, ALERT_RATE);
  spr.drawString(b, 10, 94, 1);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("Passive  S2=page S5=clear", 10, SCR_H - 11, 1);
}

void DeauthAlertApp::drawOffenders() {
  TFT_eSprite& spr = display_sprite();
  topBar("DEAUTH: BSSIDs");
  int order[6]; int n = top_off(order, 6);
  spr.setTextDatum(TL_DATUM);
  if (n == 0) { spr.setTextColor(C_DIM, C_BG); spr.drawString("(none)", 10, 40, 2); }
  for (int i = 0; i < n; i++) {
    Off& o = s_off[order[i]]; int y = 20 + i * 17;
    char b[40];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
             o.bssid[0], o.bssid[1], o.bssid[2], o.bssid[3], o.bssid[4], o.bssid[5]);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(b, 6, y, 1);
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_BAD, C_BG);
    snprintf(b, sizeof(b), "x%u", o.count); spr.drawString(b, SCR_W - 6, y, 1);
    spr.setTextDatum(TL_DATUM);
  }
  spr.setTextColor(C_DIM, C_BG); spr.drawString("S2=page  S5=clear", 6, SCR_H - 11, 1);
}

void DeauthAlertApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  topBar("DEAUTH: Exit");
  spr.setTextDatum(MC_DATUM); spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Exit? (STA will recover)", SCR_W / 2, SCR_H / 2 - 10, 2);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = exit   S2 = next", SCR_W / 2, SCR_H / 2 + 18, 2);
}

void DeauthAlertApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BT_BLOCKED) {
    topBar("DEAUTH ALERT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  if (phase_ == LOW_POWER) {
    topBar("DEAUTH ALERT");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage low", 8, 30, 2);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(b, 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  if (page_ == OFFENDERS)      drawOffenders();
  else if (page_ == PAGE_EXIT) drawExit();
  else                          drawSummary();
}

void DeauthAlertApp::button(ButtonId id) {
  if (phase_ == BT_BLOCKED || phase_ == LOW_POWER) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S2) { page_ = (Page)((page_ + 1) % PAGE_COUNT); return; }
  if (id == BTN_S5) { if (page_ == PAGE_EXIT) wants_exit_ = true; else clear_all(); }
}

std::string DeauthAlertApp::remote_state() {
  char lines[6][40];
  const char* items[6];
  int n = 0;
  if (page_ == OFFENDERS) {
    int order[5]; int m = top_off(order, 5);
    for (int i = 0; i < m && n < 6; i++) {
      Off& o = s_off[order[i]];
      snprintf(lines[n], 40, "%02X:%02X:%02X:%02X:%02X:%02X x%u",
               o.bssid[0], o.bssid[1], o.bssid[2], o.bssid[3], o.bssid[4], o.bssid[5], o.count);
      items[n] = lines[n]; n++;
    }
    if (m == 0) { snprintf(lines[0], 40, "(none)"); items[0] = lines[0]; n = 1; }
  } else {
    int rate = rate_now();
    snprintf(lines[0], 40, rate >= ALERT_RATE ? "!! DEAUTH FLOOD !!" : "efir chystyy");
    snprintf(lines[1], 40, "deauth: %lu", (unsigned long)s_deauth);
    snprintf(lines[2], 40, "disassoc: %lu", (unsigned long)s_disassoc);
    snprintf(lines[3], 40, "rate(%ds): %d / %d", WIN, rate, ALERT_RATE);
    snprintf(lines[4], 40, "ch%d (passive)", cur_channel_);
    for (int i = 0; i < 5; i++) items[i] = lines[i];
    n = 5;
  }
  return protocol_build_menu("deauth_alert", items, n, -1);
}
