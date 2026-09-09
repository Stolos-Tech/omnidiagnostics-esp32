#include <Arduino.h>
#include <string.h>
#include "wifi_manager.h"
#include "../drivers/display.h"
#include "../drivers/wifi_sta.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"

#define BACK_LABEL "< Back"

void WifiManagerApp::start_scan() {
  // Guard: WiFi і BT ділять одне радіо. Доки активний Remote: BT — НЕ чіпаємо
  // WiFi-радіо взагалі (не як раніше, коли скан пробувався й падав).
  if (remote_active_mode() == MODE_BT) {
    phase_ = BT_BLOCKED;
    return;
  }
  phase_ = SCANNING;
  cursor_ = 0;
  count_ = 0;
  scan_frame_ = 0;  // спершу покажемо "Scanning" один кадр, потім блокуючий скан
}

void WifiManagerApp::init() {
  wants_exit_ = false;
  selected_[0] = '\0';
  start_scan();
}

const char* WifiManagerApp::item_label(int i) const {
  if (i >= 0 && i < count_) return ssids_[i];
  return BACK_LABEL;  // останній пункт
}

void WifiManagerApp::loop() {
  if (phase_ == SCANNING) {
    // Один кадр показуємо "Scanning...", далі — блокуючий (синхронний) скан.
    if (scan_frame_ == 0) { scan_frame_ = 1; return; }
    wifi_sta_scan_start();               // блокує ~2-4с
    int n = wifi_sta_scan_state();       // = кількість (scanComplete), <0 при збої
    bool failed = (n < 0);
    if (n < 0) n = 0;
    count_ = n < MAX_NETS ? n : MAX_NETS;
    for (int i = 0; i < count_; i++) {
      snprintf(ssids_[i], sizeof(ssids_[i]), "%s", wifi_sta_ssid(i));
      open_[i] = wifi_sta_is_open(i);
      snprintf(enc_[i], sizeof(enc_[i]), "%s", wifi_sta_enc_str(i));
    }
    cursor_ = 0;
    phase_ = failed ? SCAN_FAIL : LIST;
  } else if (phase_ == CONNECTING) {
    wifi_sta_loop();
    WifiStaState st = wifi_sta_status();
    if (st == WSTA_CONNECTED) { wifi_sta_save(); phase_ = CONNECTED; }
    else if (st == WSTA_FAILED) { phase_ = FAILED; }
  }
}

void WifiManagerApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("WiFi Setup", 6, 3, 2);

  spr.setTextDatum(TL_DATUM);
  if (phase_ == SCANNING) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Scanning networks...", 8, 50, 2);
  } else if (phase_ == SCAN_FAIL) {
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString("Scan failed", 8, 40, 2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = retry", 8, SCR_H - 18, 2);
  } else if (phase_ == BT_BLOCKED) {
    // Активний Remote: BT — WiFi заблоковано (одне радіо за раз).
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("BT i WiFi ne mozhut razom", 8, 52, 1);
    spr.drawString("(odne radio za raz)", 8, 66, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5=vymk. BT i skan", 8, SCR_H - 30, 2);
    spr.drawString("S2=exit", 8, SCR_H - 14, 2);
  } else if (phase_ == LIST && count_ == 0) {
    // Порожній список: жодної мережі не знайдено.
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("0 networks found", 8, 34, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("check if WiFi is nearby", 8, 56, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S2=exit  S5=rescan", 8, SCR_H - 14, 1);
  } else if (phase_ == LIST) {
    // ковзне вікно на 5 рядків
    const int VISIBLE = 5, ROW_H = 18;
    int total = items_total();
    int first = cursor_ - VISIBLE / 2;
    if (first < 0) first = 0;
    if (first > total - VISIBLE) first = total - VISIBLE;
    if (first < 0) first = 0;
    for (int row = 0; row < VISIBLE; row++) {
      int idx = first + row;
      if (idx >= total) break;
      int y = 20 + row * ROW_H;
      bool sel = (idx == cursor_);
      if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
      spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
      spr.drawString(item_label(idx), 10, y + 1, 2);
      if (idx < count_) {
        // Правий тег: відома мережа -> "K" (зелений); інакше тип захисту (Open дим,
        // захищена жовтий) — як «замок»/тип у налаштуваннях WiFi на телефоні.
        bool known = wifi_sta_is_known(ssids_[idx]);
        const char* tag = known ? "K" : enc_[idx];
        uint16_t tcol = sel ? C_ACCENT : known ? C_GOOD : open_[idx] ? C_DIM : C_WARN;
        spr.setTextDatum(TR_DATUM);
        spr.setTextColor(tcol, sel ? C_PANEL : C_BG);
        spr.drawString(tag, SCR_W - 12, y + 3, 1);   // font 1 (дрібний) — влазить "WPA2/3"
        spr.setTextDatum(TL_DATUM);
      }
    }
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
  } else if (phase_ == PASSWORD) {
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Network:", 8, 24, 2);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(selected_, 8, 42, 2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("Vvedy parol z telefona", 8, 72, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("LEFT = back", 8, SCR_H - 18, 2);
  } else if (phase_ == CONNECTING) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Connecting...", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(selected_, 8, 64, 2);
  } else if (phase_ == CONNECTED) {
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString("Connected!", 8, 34, 2);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(selected_, 8, 56, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(wifi_sta_ip(), 8, 78, 2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = exit", 8, SCR_H - 18, 2);
  } else if (phase_ == FAILED) {
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString("Failed to connect", 8, 34, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("nevirnyy parol?", 8, 56, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = to list", 8, SCR_H - 18, 2);
  }
}

void WifiManagerApp::button(ButtonId id) {
  if (phase_ == LIST && count_ == 0) {  // порожній список: S2=вихід, S5=повтор скану
    if (id == BTN_S2) wants_exit_ = true;
    else if (id == BTN_S5) { start_scan(); }
    return;
  }
  if (phase_ == LIST) {
    if (id == BTN_S2) {                 // вниз по списку, циклічно
      cursor_ = (cursor_ + 1) % items_total();
    } else if (id == BTN_S1) {          // вгору (спрацює з платою S1-S5)
      cursor_ = (cursor_ + items_total() - 1) % items_total();
    } else if (id == BTN_S5) {          // вибір
      select_current();
    }
  } else if (phase_ == PASSWORD) {
    if (id == BTN_S5) phase_ = LIST;    // скасувати
  } else if (phase_ == SCAN_FAIL) {
    if (id == BTN_S5) { start_scan(); }
  } else if (phase_ == BT_BLOCKED) {
    if (id == BTN_S5) {                 // явно вимкнути BT і перейти до скану
      remote_activate(MODE_OFF);
      start_scan();
    } else if (id == BTN_S2) {
      wants_exit_ = true;
    }
  } else if (phase_ == CONNECTED) {
    if (id == BTN_S5) wants_exit_ = true;
  } else if (phase_ == FAILED) {
    if (id == BTN_S5) phase_ = LIST;
  }
}

void WifiManagerApp::select_current() {
  if (phase_ != LIST) return;
  if (cursor_ >= count_) { wants_exit_ = true; return; }  // "< Back"
  snprintf(selected_, sizeof(selected_), "%s", ssids_[cursor_]);
  if (open_[cursor_]) {                              // відкрита мережа — одразу підключення
    wifi_sta_connect(selected_, "");
    phase_ = CONNECTING;
  } else if (wifi_sta_try_connect_saved(selected_)) { // пароль уже відомий — не питаємо знову
    phase_ = CONNECTING;
  } else {
    phase_ = PASSWORD;              // чекаємо пароль з телефона (нова мережа)
  }
}

void WifiManagerApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) {
    cursor_ = idx;
    select_current();
  }
}

void WifiManagerApp::text(const char* field, const char* value) {
  // Пароль з телефона на екрані PASSWORD -> старт підключення.
  if (phase_ == PASSWORD && field && strcmp(field, "password") == 0) {
    wifi_sta_connect(selected_, value);
    phase_ = CONNECTING;
  }
}

std::string WifiManagerApp::remote_state() {
  switch (phase_) {
    case SCANNING:   return protocol_build_state("wifi_scan");
    case SCAN_FAIL:  return protocol_build_state("wifi_scanfail");
    case LIST: {
      // Дзеркалимо як у налаштуваннях WiFi телефона: назва + тип захисту + позначка
      // збереженої. Індекс лишається позиційним (тап шле {idx:i}), тег — лише текст.
      static char rows[MAX_NETS + 1][48];
      const char* items[MAX_NETS + 1];
      for (int i = 0; i < count_; i++) {
        const char* saved = wifi_sta_is_known(ssids_[i]) ? " (saved)" : "";
        snprintf(rows[i], sizeof(rows[i]), "%.24s  [%s]%s", ssids_[i], enc_[i], saved);
        items[i] = rows[i];
      }
      items[count_] = BACK_LABEL;
      return protocol_build_menu("wifi_list", items, count_ + 1, cursor_);
    }
    case PASSWORD:   return protocol_build_page_kv("wifi_pass", "ssid", selected_);
    case CONNECTING: return protocol_build_page_kv("wifi_connecting", "ssid", selected_);
    case CONNECTED:  return protocol_build_page_kv("wifi_ok", "ip", wifi_sta_ip());
    case FAILED:     return protocol_build_page_kv("wifi_fail", "ssid", selected_);
    case BT_BLOCKED: return protocol_build_state("wifi_bt_blocked");
  }
  return std::string();
}
