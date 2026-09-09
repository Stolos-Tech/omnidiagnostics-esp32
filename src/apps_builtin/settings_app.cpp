#include <Arduino.h>
#include <string.h>
#include "settings_app.h"
#include "../drivers/display.h"
#include "../kernel/settings.h"
#include "../drivers/mdns_service.h"
#include "../remote/protocol.h"

// Рівні яскравості й сну для циклічного перебору.
static const int BRIGHTS[] = { 26, 77, 128, 191, 255 };   // 10/30/50/75/100%
static const int NBRIGHT = 5;
static const int SLEEPS[] = { 0, 60, 300, 600, 1800 };    // Off/1m/5m/10m/30m
static const int NSLEEP = 5;

void SettingsApp::configure(const char* const* app_names, int app_count) {
  names_ = app_names; names_count_ = app_count;
}

void SettingsApp::init() { wants_exit_ = false; field_ = F_BRIGHT; }

void SettingsApp::apply_brightness() { display_set_brightness(settings_brightness()); }

const char* SettingsApp::auto_label() const {
  const char* a = settings_autostart_name();
  return (a && a[0]) ? a : "None";
}

void SettingsApp::cycle_current() {
  if (field_ == F_BRIGHT) {
    int cur = settings_brightness(), idx = 0;
    for (int i = 0; i < NBRIGHT; i++) if (BRIGHTS[i] >= cur) { idx = i; break; }
    settings_set_brightness(BRIGHTS[(idx + 1) % NBRIGHT]);
    apply_brightness();
  } else if (field_ == F_SLEEP) {
    int cur = settings_sleep_sec(), idx = 0;
    for (int i = 0; i < NSLEEP; i++) if (SLEEPS[i] == cur) { idx = i; break; }
    settings_set_sleep_sec(SLEEPS[(idx + 1) % NSLEEP]);
  } else if (field_ == F_AUTO) {
    // None -> names_[0] -> ... -> names_[count-1] -> None. Зберігається за ІМ'ЯМ
    // (стійко до зміни складу/порядку меню — стале ім'я просто не автозапуститься).
    const char* cur = settings_autostart_name();
    int idx = -1;
    for (int i = 0; i < names_count_ && names_; i++) if (cur[0] && strcmp(cur, names_[i]) == 0) { idx = i; break; }
    idx++;
    settings_set_autostart_name((idx >= names_count_ || !names_) ? "" : names_[idx]);
  } else if (field_ == F_STEALTH) {
    settings_set_stealth(!settings_stealth());    // застосується після reboot/реконекту
  } else if (field_ == F_MDNS) {
    settings_set_mdns_enabled(!settings_mdns_enabled());
    mdns_service_stop();                          // гейт у main підніме заново з новим станом/ім'ям
  } else if (field_ == F_THEME) {
    theme_set((theme_current() + 1) % theme_count());   // циклічно; застосовується й зберігається одразу
  } else if (field_ == F_WEBUI) {
    settings_set_webui(!settings_webui_enabled());      // HTML-UI on/off; REST/WS лишаються
  } else if (field_ == F_EXIT) {
    wants_exit_ = true;
  }
}

void SettingsApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("SETTINGS", 6, 3, 2);

  char v[24];
  const char* labels[F_COUNT] = { "Brightness", "Sleep", "Autostart", "Stealth", "mDNS name", "Theme", "Web UI", "< Exit" };
  // Список довший за екран (240x135) -> прокрутка: тримаємо вибране поле у вікні VIS рядків.
  const int VIS = 5, dy = 19;
  int first = (field_ >= VIS) ? field_ - VIS + 1 : 0;
  int y = 20;
  for (int r = 0; r < VIS && first + r < F_COUNT; r++) {
    int i = first + r;
    bool sel = (i == field_);
    if (sel) spr.fillRoundRect(4, y - 2, SCR_W - 8, dy - 2, 3, C_PANEL);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
    spr.drawString(labels[i], 10, y, 2);
    // значення праворуч
    v[0] = '\0';
    if (i == F_BRIGHT) snprintf(v, sizeof(v), "%d%%", settings_brightness() * 100 / 255);
    else if (i == F_SLEEP) { int s = settings_sleep_sec();
      if (s == 0) snprintf(v, sizeof(v), "Off"); else snprintf(v, sizeof(v), "%dm", s / 60); }
    else if (i == F_AUTO) snprintf(v, sizeof(v), "%.14s", auto_label());
    else if (i == F_STEALTH) snprintf(v, sizeof(v), "%s", settings_stealth() ? "ON" : "off");
    else if (i == F_MDNS) snprintf(v, sizeof(v), "%s", settings_mdns_enabled() ? "ON" : "off");
    else if (i == F_THEME) snprintf(v, sizeof(v), "%s", theme_name(theme_current()));
    else if (i == F_WEBUI) snprintf(v, sizeof(v), "%s", settings_webui_enabled() ? "ON" : "off");
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(sel ? C_ACCENT : C_DIM, sel ? C_PANEL : C_BG);
    spr.drawString(v, SCR_W - 12, y, 2);
    y += dy;
  }
  // Індикатори прокрутки (є поля вище/нижче вікна).
  spr.setTextColor(C_ACCENT, C_BG);
  spr.setTextDatum(TR_DATUM);
  if (first > 0)                 spr.drawString("^", SCR_W - 4, 20, 2);          // є вище
  if (first + VIS < F_COUNT)     spr.drawString("v", SCR_W - 4, SCR_H - 26, 2);  // є нижче
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=field  S5=change", 8, SCR_H - 12, 1);
}

void SettingsApp::button(ButtonId id) {
  if (id == BTN_S2)      field_ = (field_ + 1) % F_COUNT;
  else if (id == BTN_S1) field_ = (field_ + F_COUNT - 1) % F_COUNT;
  else if (id == BTN_S5) cycle_current();
}

void SettingsApp::text(const char* field, const char* value) {
  if (!field || !value) return;
  int v = atoi(value);
  if (strcmp(field, "bright") == 0)     { settings_set_brightness(v); apply_brightness(); }
  else if (strcmp(field, "sleep") == 0) settings_set_sleep_sec(v);
  else if (strcmp(field, "auto") == 0)  settings_set_autostart_name((v >= 0 && v < names_count_ && names_) ? names_[v] : "");
  else if (strcmp(field, "stealth") == 0) settings_set_stealth(v);
  else if (strcmp(field, "mdnsen") == 0)  { settings_set_mdns_enabled(v); mdns_service_stop(); }
  else if (strcmp(field, "mdnsname") == 0) { settings_set_mdns_name(value); mdns_service_stop(); }
  else if (strcmp(field, "boturl") == 0)   settings_set_bot_url(value);
  else if (strcmp(field, "theme") == 0)    theme_set(v);
  else if (strcmp(field, "webui") == 0)    settings_set_webui(v);
}

std::string SettingsApp::remote_state() {
  char l[7][48];
  snprintf(l[0], sizeof(l[0]), "Brightness: %d%%", settings_brightness() * 100 / 255);
  int s = settings_sleep_sec();
  if (s == 0) snprintf(l[1], sizeof(l[1]), "Sleep: Off");
  else        snprintf(l[1], sizeof(l[1]), "Sleep: %dm", s / 60);
  snprintf(l[2], sizeof(l[2]), "Autostart: %.20s", auto_label());
  snprintf(l[3], sizeof(l[3]), "Stealth: %s", settings_stealth() ? "ON" : "off");
  snprintf(l[4], sizeof(l[4]), "mDNS: %s (%s.local)", settings_mdns_enabled() ? "ON" : "off", settings_mdns_name());
  snprintf(l[5], sizeof(l[5]), "Theme: %s", theme_name(theme_current()));
  snprintf(l[6], sizeof(l[6]), "Web UI: %s", settings_webui_enabled() ? "ON" : "off");
  const char* items[7] = { l[0], l[1], l[2], l[3], l[4], l[5], l[6] };
  return protocol_build_menu("settings", items, 7, -1);
}
