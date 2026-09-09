#include <Preferences.h>
#include <string.h>
#include "settings.h"

#define NVS_NS "os"

static int s_bright = 255;
static int s_sleep = 0;
static char s_autostart_name[24] = "";   // ім'я застосунку автозапуску ("" = нема)
static char s_cursor_name[24] = "";      // ім'я застосунку під курсором меню
static int s_stealth = 0;
// mDNS ДЕФОЛТ OFF: анонс mDNS-респондера з'їдає ~5.6КБ heap, а при активному WiFi
// AP+STA+WS+спрайт вільної купи ~23КБ, і −5.6КБ роблять віддачу веб-сторінки
// НЕНАДІЙНОЮ (трансфер обривається). "Знати адресу" й так вирішує показ LAN-IP на
// екрані Remote:WiFi. Хто хоче ім'я-резолвинг — `mdns on` (свідомо, ціною запасу).
static int  s_mdns_en = 0;
static int  s_webui = 1;
static char s_mdns_name[32] = "wlan-device";  // нейтральне ім'я за замовчуванням (не «esp32os»)
static char s_bot_url[48] = "192.168.50.236:8000";  // адреса Telegram-бота: плата ПУШить туди звіти

const char* settings_cursor_name() { return s_cursor_name; }

void settings_save_cursor_name(const char* name) {
  if (!name) name = "";
  if (strcmp(name, s_cursor_name) == 0) return;   // щадимо флеш
  snprintf(s_cursor_name, sizeof(s_cursor_name), "%s", name);
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.putString("curNm", s_cursor_name); p.end(); }
}

void settings_begin() {
  Preferences p;
  if (p.begin(NVS_NS, true)) {
    s_bright    = p.getInt("bright", 255);
    s_sleep     = p.getInt("sleep", 0);
    { String an = p.isKey("autoNm") ? p.getString("autoNm", "") : String();
      if (an.length() < sizeof(s_autostart_name)) snprintf(s_autostart_name, sizeof(s_autostart_name), "%s", an.c_str()); }
    { String cn = p.isKey("curNm") ? p.getString("curNm", "") : String();
      if (cn.length() < sizeof(s_cursor_name)) snprintf(s_cursor_name, sizeof(s_cursor_name), "%s", cn.c_str()); }
    s_stealth   = p.getInt("stealth", 0);
    s_mdns_en   = p.getInt("mdnsen", 1);
    s_webui     = p.getInt("webui", 1);
    String nm = p.isKey("mdnsnm") ? p.getString("mdnsnm", "") : String();  // isKey -> без шумного ERROR-логу
    if (nm.length() > 0 && nm.length() < sizeof(s_mdns_name)) snprintf(s_mdns_name, sizeof(s_mdns_name), "%s", nm.c_str());
    String bu = p.isKey("boturl") ? p.getString("boturl", "") : String();
    if (bu.length() > 0 && bu.length() < sizeof(s_bot_url)) snprintf(s_bot_url, sizeof(s_bot_url), "%s", bu.c_str());
    p.end();
  }
  if (s_bright < 10) s_bright = 10;            // не даємо повністю гасити
  if (s_bright > 255) s_bright = 255;
}

static void save_int(const char* k, int v) {
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.putInt(k, v); p.end(); }
}

int  settings_brightness() { return s_bright; }
void settings_set_brightness(int v) {
  if (v < 10) v = 10; if (v > 255) v = 255;
  if (v == s_bright) return;
  s_bright = v; save_int("bright", v);
}

int  settings_sleep_sec() { return s_sleep; }
void settings_set_sleep_sec(int s) {
  if (s < 0) s = 0;
  if (s == s_sleep) return;
  s_sleep = s; save_int("sleep", s);
}

const char* settings_autostart_name() { return s_autostart_name; }
void settings_set_autostart_name(const char* name) {
  if (!name) name = "";
  if (strcmp(name, s_autostart_name) == 0) return;
  snprintf(s_autostart_name, sizeof(s_autostart_name), "%s", name);
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.putString("autoNm", s_autostart_name); p.end(); }
}

int  settings_stealth() { return s_stealth; }
void settings_set_stealth(int on) {
  on = on ? 1 : 0;
  if (on == s_stealth) return;
  s_stealth = on; save_int("stealth", on);
}

// mDNS-анонс: стабільне мережеве ім'я (<name>.local) незалежно від DHCP-IP.
// НЕ прив'язане до стелсу — можна ховати MAC/вендора, але мати зручне ім'я.
int  settings_mdns_enabled() { return s_mdns_en; }
void settings_set_mdns_enabled(int on) {
  on = on ? 1 : 0;
  if (on == s_mdns_en) return;
  s_mdns_en = on; save_int("mdnsen", on);
}

int  settings_webui_enabled() { return s_webui; }
void settings_set_webui(int on) {
  on = on ? 1 : 0;
  if (on == s_webui) return;
  s_webui = on; save_int("webui", on);
}
const char* settings_mdns_name() { return s_mdns_name; }
void settings_set_mdns_name(const char* n) {
  if (!n) return;
  char buf[32]; int j = 0;                       // санітизація -> валідне mDNS-ім'я [a-z0-9-]
  for (int i = 0; n[i] && j < (int)sizeof(buf) - 1; i++) {
    char c = n[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') buf[j++] = c;
  }
  buf[j] = 0;
  if (j == 0 || strcmp(buf, s_mdns_name) == 0) return;
  snprintf(s_mdns_name, sizeof(s_mdns_name), "%s", buf);
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.putString("mdnsnm", s_mdns_name); p.end(); }
}

// Адреса Telegram-бота (host:port) — плата ПУШить туди звіти (телефон на SoftAP не
// може дістати ПК; тому пушить сама плата з домашньої мережі).
const char* settings_bot_url() { return s_bot_url; }
void settings_set_bot_url(const char* u) {
  if (!u || !u[0] || strlen(u) >= sizeof(s_bot_url) || strcmp(u, s_bot_url) == 0) return;
  snprintf(s_bot_url, sizeof(s_bot_url), "%s", u);
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.putString("boturl", s_bot_url); p.end(); }
}
