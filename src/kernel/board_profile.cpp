#include "board_profile.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>
#include "../drivers/sd_store.h"
#include <SD.h>

#define NS "bprofile"
#define PCAP 512
// Буфер профілю — на КУПІ (DRAM статичний упритул; +512Б static переповнював dram0).
static char* s_json = nullptr;
static bool s_has = false;
static void ensure_buf() { if (!s_json) { s_json = (char*)malloc(PCAP); if (s_json) s_json[0] = '\0'; } }

static void parse_into(JsonDocument& d) {
  if (!s_has || !s_json) return;
  deserializeJson(d, s_json);
}

void profile_begin() {
  ensure_buf(); if (!s_json) return;
  Preferences p;
  if (p.begin(NS, true)) {
    String v = p.getString("json", "");
    p.end();
    if (v.length() && v.length() < PCAP) { strncpy(s_json, v.c_str(), PCAP - 1); s_json[PCAP - 1] = '\0'; s_has = true; return; }
  }
  // NVS порожній -> спробувати імпорт із SD /config/profile.json (джерело, редаговане з ПК/додатка)
  if (sd_mounted()) {
    sd_bus_select();
    File f = SD.open("/config/profile.json", FILE_READ);
    if (f && f.size() > 0 && f.size() < PCAP) {
      int n = f.read((uint8_t*)s_json, PCAP - 1); s_json[n > 0 ? n : 0] = '\0'; s_has = (n > 0);
    }
    if (f) f.close();
    if (s_has) profile_set_json(s_json);   // перенести в NVS (персистентно навіть без SD)
  }
}

bool profile_loaded() { return s_has; }
const char* profile_json() { return (s_has && s_json) ? s_json : ""; }

void profile_set_json(const char* json) {
  ensure_buf(); if (!json || !s_json) return;
  size_t n = strlen(json);
  if (n >= PCAP) n = PCAP - 1;
  memcpy(s_json, json, n); s_json[n] = '\0'; s_has = (n > 0);
  Preferences p;
  if (p.begin(NS, false)) { p.putString("json", s_json); p.end(); }
  // дзеркалимо на SD (якщо є) — щоб профіль було видно/редаговано і з ПК
  if (sd_mounted()) {
    sd_bus_select();
    File f = SD.open("/config/profile.json", FILE_WRITE);
    if (f) { f.print(s_json); f.close(); }
  }
}

int profile_get_int(const char* key, int def) {
  if (!s_has || !s_json) return def;
  JsonDocument d; parse_into(d);
  JsonVariant v = d["esp"][key];
  return v.isNull() ? def : v.as<int>();
}

bool profile_get(const char* key) {
  if (!s_has || !s_json) return true;       // немає профілю -> дозволяємо все (автодетект)
  JsonDocument d; parse_into(d);
  if (strcmp(key, "uno") == 0) {
    JsonVariant v = d["uno"]["enabled"];
    return v.isNull() ? true : v.as<bool>();
  }
  // uno-під-модулі
  for (const char* k : {"joy", "rc522", "dht", "pot", "reed"}) {
    if (strcmp(key, k) == 0) { JsonVariant v = d["uno"][key]; return v.isNull() ? true : v.as<bool>(); }
  }
  // esp-модулі (nrf24/cc1101/sd/bt/wifi_sta/soft_ap)
  JsonVariant v = d["esp"][key];
  return v.isNull() ? true : v.as<bool>();
}
