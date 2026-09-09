#include <Arduino.h>
#include <WiFi.h>
#include "remote_app.h"
#include "../drivers/display.h"
#include "../remote/session.h"
#include "../remote/remote_control.h"
#include "../remote/transport_wifi.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"
#include "../remote/protocol.h"
#include "../drivers/wifi_sta.h"
#include "../kernel/settings.h"

void RemoteApp::configure(RemoteMode mode, const char* menu_name) {
  mode_ = mode;
  snprintf(name_, sizeof(name_), "%s", menu_name);
}

void RemoteApp::init() {
  wants_exit_ = false;
  low_power_ = false;
  confirming_stop_ = false;
  // Активувати свій режим, якщо він ще не активний. Координатор зупинить
  // протилежний транспорт (взаємовиключність) і згенерує новий PIN.
  if (remote_active_mode() != mode_) {
    if (!remote_activate(mode_)) low_power_ = true;
  }
}

void RemoteApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);

  if (low_power_) {
    spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_ACCENT, C_PANEL);
    spr.drawString(name_, 6, 3, 2);
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.drawString("Radio blocked", 8, 66, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }

  if (confirming_stop_) {
    spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_ACCENT, C_PANEL);
    spr.drawString(name_, 6, 3, 2);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Turn off Remote?", SCR_W / 2, SCR_H / 2 - 16, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("This drops the link with the", SCR_W / 2, SCR_H / 2 + 2, 1);
    spr.drawString("same client (web/phone)", SCR_W / 2, SCR_H / 2 + 14, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("S5 = yes, turn off   S2 = no", SCR_W / 2, SCR_H / 2 + 32, 1);
    return;
  }

  const bool is_wifi = (mode_ == MODE_WIFI);

  // Верхня панель
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString(is_wifi ? "REMOTE: WiFi" : "REMOTE: BT", 6, 3, 2);

  // Індикатор клієнта (праворуч)
  bool has_client = is_wifi ? (WiFi.softAPgetStationNum() > 0)
                            : session_is_authenticated();
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(has_client ? C_GOOD : C_DIM, C_PANEL);
  spr.drawString(has_client ? "link" : "wait", SCR_W - 6, 3, 2);

  spr.setTextDatum(TL_DATUM);
  if (is_wifi) {
    // AP + пароль (для телефона на SoftAP) — компактно в один рядок
    char apl[44];
    snprintf(apl, sizeof(apl), "AP: OmniDiag-Setup  pw:%s", transport_wifi_ap_password());
    spr.setTextColor(C_DIM, C_BG); spr.drawString(apl, 8, 20, 1);

    // ДЕ ХОСТИТЬСЯ ІНТЕРФЕЙС (щоб не гадати адресу/порт) — веб завжди на :80.
    spr.setTextColor(C_GOOD, C_BG);
    if (WiFi.status() == WL_CONNECTED) {
      char lan[44]; snprintf(lan, sizeof(lan), "LAN: %s", wifi_sta_ip());
      spr.drawString(lan, 8, 33, 2);
    } else {
      spr.drawString("AP UI: 192.168.4.1", 8, 33, 2);
    }
    if (settings_mdns_enabled()) {
      char nm[44]; snprintf(nm, sizeof(nm), "Name: %s.local", settings_mdns_name());
      spr.setTextColor(C_ACCENT, C_BG); spr.drawString(nm, 8, 51, 2);
    }

    // PIN (font 4 — місце під адреси; усе одно читабельно)
    spr.setTextColor(C_DIM, C_BG); spr.drawString("PIN:", 8, 74, 2);
    spr.setTextColor(session_is_authenticated() ? C_GOOD : C_TEXT, C_BG);
    spr.drawString(session_pin(), 54, 71, 4);

    spr.setTextColor(session_is_authenticated() ? C_GOOD : C_DIM, C_BG);
    spr.drawString(session_is_authenticated() ? "authorized" : "waiting for PIN", 8, 104, 1);
  } else {
    // BT — без веб-інтерфейсу, лишаємо великий PIN
    spr.setTextColor(C_DIM, C_BG); spr.drawString("BT: OmniDiag (SPP)", 8, 22, 2);
    spr.drawString("PIN:", 8, 46, 2);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(session_is_authenticated() ? C_GOOD : C_TEXT, C_BG);
    spr.drawString(session_pin(), SCR_W / 2, 58, 7);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(session_is_authenticated() ? C_GOOD : C_DIM, C_BG);
    spr.drawString(session_is_authenticated() ? "authorized" : "waiting for PIN from client", 8, 104, 1);
  }

  // Підказки: права — лишити увімкненим і вийти; ліва — питає підтвердження перед вимкненням
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("RIGHT=keep  LEFT=stop?", 8, SCR_H - 12, 1);
}

void RemoteApp::button(ButtonId id) {
  if (low_power_) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (confirming_stop_) {
    if (id == BTN_S5) {           // підтверджено — справді вимикаємо
      remote_activate(MODE_OFF);
      wants_exit_ = true;
    } else if (id == BTN_S2) {    // скасовано — повернутись до звичайного екрана
      confirming_stop_ = false;
    }
    return;
  }
  if (id == BTN_S2) {
    // Лишити транспорт увімкненим, повернутись у лаунчер (клієнт продовжить дзеркалити)
    wants_exit_ = true;
  } else if (id == BTN_S5) {
    // НЕ вимикаємо одразу — S5 тут часто тиснуть як "OK" з веб-клієнта, підключеного
    // через ЦЕЙ САМИЙ транспорт; миттєве вимкнення рвало б власне з'єднання користувача.
    confirming_stop_ = true;
  }
}

std::string RemoteApp::remote_state() {
  // Дзеркалимо стан підтвердження вимкнення — інакше веб-клієнт (без доступу до
  // фізичного екрана плати) не бачить, що перший тап "OK" лише озброїв підтвердження,
  // і другий тап наосліп все одно вимикає власне з'єднання.
  if (confirming_stop_) {
    const char* items[1] = { "Turn off Remote? S5(OK)=yes, S2(Next)=no" };
    return protocol_build_menu("remote_confirm_stop", items, 1, -1);
  }
  if (mode_ == MODE_WIFI) {
    // Дзеркалимо АДРЕСИ інтерфейсу — щоб і у вебі було видно, як дістатись плати
    // (не гадати IP/порт). Веб завжди на :80.
    static char l0[48], l1[48], l2[48];
    const char* items[3]; int n = 0;
    if (WiFi.status() == WL_CONNECTED) snprintf(l0, sizeof(l0), "LAN: http://%s", wifi_sta_ip());
    else                              snprintf(l0, sizeof(l0), "AP UI: http://192.168.4.1");
    items[n++] = l0;
    if (settings_mdns_enabled()) { snprintf(l1, sizeof(l1), "Name: http://%s.local", settings_mdns_name()); items[n++] = l1; }
    snprintf(l2, sizeof(l2), session_is_authenticated() ? "authorized" : "PIN required");
    items[n++] = l2;
    return protocol_build_menu(name_, items, n, -1);
  }
  return std::string();  // BT — типовий {"page":name} з ядра
}
