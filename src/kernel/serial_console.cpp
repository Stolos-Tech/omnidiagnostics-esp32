#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "serial_console.h"
#include "../drivers/wifi_sta.h"
#include "../remote/remote_control.h"
#include "../remote/session.h"
#include "../remote/transport_wifi.h"
#include "settings.h"
#include "sys_ctl.h"
#include "../drivers/mdns_service.h"
#include "../drivers/sd_store.h"
#include "../drivers/nrf24.h"
#include "../drivers/display.h"

#define CON_LINE_MAX 160

static char s_buf[CON_LINE_MAX];
static int  s_len = 0;

static void cmd_wifi(char* args) {
  char* sep = strchr(args, '|');
  if (!sep) {
    Serial.println("[CON] format: wifi <ssid>|<password>");
    return;
  }
  *sep = '\0';
  const char* ssid = args;
  const char* pw = sep + 1;
  if (!ssid[0]) { Serial.println("[CON] empty SSID"); return; }
  Serial.printf("[CON] connecting to '%s'...\n", ssid);
  wifi_sta_connect(ssid, pw);
  // Чекаємо результат тут же (разова службова дія, як runDiagnostics — блокує).
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    wifi_sta_loop();
    WifiStaState st = wifi_sta_status();
    if (st == WSTA_CONNECTED) {
      wifi_sta_save();   // запам'ятати в списку відомих мереж
      Serial.printf("[CON] OK, IP %s (saved)\n", wifi_sta_ip());
      return;
    }
    if (st == WSTA_FAILED) { Serial.println("[CON] FAIL (password/network?)"); return; }
    delay(100);
  }
  Serial.println("[CON] TIMEOUT");
}

static void cmd_sd(const char* arg) {
  if (strcmp(arg, "mount") == 0)        { sd_mount(); Serial.printf("[CON] %s\n", sd_status_line()); }
  else if (strcmp(arg, "unmount") == 0) { sd_unmount(); Serial.printf("[CON] %s\n", sd_status_line()); }
  else if (strcmp(arg, "persist") == 0) { int n = sd_persist_shared(); Serial.printf("[CON] persisted %d lines (%s)\n", n, sd_status_line()); }
  else                                  Serial.printf("[CON] sd mount|unmount|persist | now: %s\n", sd_status_line());
}

static void cmd_nets() {
  int n = wifi_sta_saved_count();
  Serial.printf("[CON] saved networks: %d\n", n);
  for (int i = 0; i < n; i++) Serial.printf("  [%d] %s\n", i, wifi_sta_saved_ssid(i));
}

static void cmd_ip() {
  Serial.printf("[CON] STA status=%d IP=%s SSID='%s'\n",
                (int)wifi_sta_status(), wifi_sta_ip(), wifi_sta_current_ssid());
  Serial.printf("[CON] WiFi.status()=%d localIP=%s\n",
                (int)WiFi.status(), WiFi.localIP().toString().c_str());
}

// Діагностика nRF24: ініт + присутність + кілька проходів RPD-скану (реальна активність ефіру).
static void cmd_nrf() {
  bool init = nrf24_begin();
  bool present = nrf24_present();
  Serial.printf("[CON] nrf24 begin=%d present=%d\n", init ? 1 : 0, present ? 1 : 0);
  if (!present) { Serial.println("[CON] nrf24 NOT found -> перевір CE=12(+10k), CSN=25, MISO=38, живлення адаптера 5V"); return; }
  static uint8_t counts[NRF24_CHANNELS];
  for (int i = 0; i < NRF24_CHANNELS; i++) counts[i] = 0;
  for (int pass = 0; pass < 30; pass++) nrf24_scan_pass(counts, NRF24_CHANNELS);
  int active = 0; char line[220]; int pos = 0; line[0] = 0;
  for (int ch = 0; ch < NRF24_CHANNELS; ch++) {
    if (counts[ch]) { active++; if (pos < 200) pos += snprintf(line + pos, sizeof(line) - pos, "%d:%d ", ch, counts[ch]); }
  }
  Serial.printf("[CON] scan 30 passes -> %d active channels (2400+ch MHz)\n", active);
  if (active) Serial.printf("[CON] %s\n", line);
  else        Serial.println("[CON] ефір тихий (нема активності >поріг) — модуль живий, але RPD нічого не зловив");
}

// Діагностика піна: `gpio <n>` читає рівень (INPUT). Для перевірки живих ліній
// (напр. MISO=38) простим сигналом — джампер 3V3/GND чи свіч. Вхід-онлі 34-39 без
// внутрішніх pull, тож рівень має задаватись зовні.
static void cmd_gpio(const char* arg) {
  int pin = atoi(arg);
  pinMode(pin, INPUT);
  Serial.printf("[CON] gpio %d = %d\n", pin, digitalRead(pin));
}

static void cmd_remote(const char* arg) {
  if (strcmp(arg, "wifi") == 0)      remote_activate(MODE_WIFI);
  else if (strcmp(arg, "off") == 0)  remote_activate(MODE_OFF);
  else { Serial.println("[CON] remote wifi | remote off"); return; }
  Serial.printf("[CON] remote mode=%d PIN=%s AP_pw=%s\n",
                (int)remote_active_mode(), session_pin(), transport_wifi_ap_password());
  Serial.printf("[CON] STA IP=%s (maie zalyshytys pislia AP)\n", wifi_sta_ip());
}

static void cmd_stealth(const char* arg) {
  if (strcmp(arg, "on") == 0)       settings_set_stealth(1);
  else if (strcmp(arg, "off") == 0) settings_set_stealth(0);
  else if (strcmp(arg, "") != 0)  { Serial.println("[CON] stealth on|off"); return; }
  Serial.printf("[CON] stealth=%d (zastosuietsja pislja reboot/rekonektu)\n", settings_stealth());
}

static void cmd_mdns(const char* arg) {
  if (strncmp(arg, "name ", 5) == 0)  { settings_set_mdns_name(arg + 5); mdns_service_stop(); }
  else if (strcmp(arg, "on") == 0)    { settings_set_mdns_enabled(1); }
  else if (strcmp(arg, "off") == 0)   { settings_set_mdns_enabled(0); mdns_service_stop(); }
  else if (arg[0] != '\0')            { Serial.println("[CON] mdns on|off | mdns name <x>"); return; }
  Serial.printf("[CON] mdns=%d name=%s.local\n", settings_mdns_enabled(), settings_mdns_name());
}

static void handle_line(char* line) {
  while (*line == ' ') line++;
  if (!*line) return;
  if (strncmp(line, "wifi ", 5) == 0)      cmd_wifi(line + 5);
  else if (strncmp(line, "remote ", 7) == 0) cmd_remote(line + 7);
  else if (strncmp(line, "stealth ", 8) == 0) cmd_stealth(line + 8);
  else if (strcmp(line, "stealth") == 0)   cmd_stealth("");
  else if (strncmp(line, "mdns ", 5) == 0) cmd_mdns(line + 5);
  else if (strcmp(line, "mdns") == 0)      cmd_mdns("");
  else if (strncmp(line, "bot ", 4) == 0)  { settings_set_bot_url(line + 4); Serial.printf("[CON] bot_url=%s\n", settings_bot_url()); }
  else if (strcmp(line, "bot") == 0)       Serial.printf("[CON] bot_url=%s\n", settings_bot_url());
  else if (strncmp(line, "sd ", 3) == 0)   cmd_sd(line + 3);
  else if (strcmp(line, "sd") == 0)        cmd_sd("");
  else if (strcmp(line, "sdtest") == 0)    { int n = sd_selftest(); Serial.printf("[CON] sdtest -> read %d bytes\n", n); }
  else if (strcmp(line, "nets") == 0)      cmd_nets();
  else if (strncmp(line, "gpio ", 5) == 0) cmd_gpio(line + 5);
  else if (strcmp(line, "nrf") == 0)       cmd_nrf();
  else if (strcmp(line, "ip") == 0)        cmd_ip();
  else if (strncmp(line, "theme ", 6) == 0) {
    int i = atoi(line + 6); theme_set(i);
    Serial.printf("[CON] theme -> %d (%s)\n", theme_current(), theme_name(theme_current()));
  }
  else if (strcmp(line, "theme") == 0) {
    Serial.printf("[CON] theme %d (%s). Available: ", theme_current(), theme_name(theme_current()));
    for (int i = 0; i < theme_count(); i++) Serial.printf("%d=%s ", i, theme_name(i));
    Serial.println();
  }
  else if (strncmp(line, "webui ", 6) == 0) {
    settings_set_webui(strcmp(line + 6, "on") == 0);
    Serial.printf("[CON] web UI %s (REST/WS lishayutsya)\n", settings_webui_enabled() ? "ON" : "OFF");
  }
  else if (strcmp(line, "passive") == 0)   { sys_set_passive(true); Serial.println("[CON] passive ON (press board button to wake screen)"); }
  else if (strcmp(line, "reboot") == 0)    { Serial.println("[CON] rebooting..."); sys_request_reboot(); }
  else if (strcmp(line, "help") == 0)
    Serial.println("[CON] wifi <ssid>|<pw> | remote wifi|off | stealth on|off | mdns on|off|name <x> | sd mount|unmount|persist | nets | gpio <n> | nrf | theme [n] | webui on|off | ip | passive | reboot | help");
  // інші рядки ігноруємо тихо (у Serial сипле й звичайний лог ОС)
}

void serial_console_loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_len) { s_buf[s_len] = '\0'; handle_line(s_buf); s_len = 0; }
    } else if (s_len < CON_LINE_MAX - 1) {
      s_buf[s_len++] = c;
    }
  }
}
