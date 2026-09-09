#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <string.h>
#include <ESP32Ping.h>
#include "wifi_sta.h"
#include "logger.h"
#include "../kernel/settings.h"

#define NVS_NS   "wifi"
#define NVS_SSID "ssid"
#define NVS_PW   "pw"
#define CONNECT_TIMEOUT_MS 15000
#define STA_RETRY_MS       20000   // фонова повторна спроба після FAILED (self-heal, з backoff)
#define HEALTH_CHECK_MS    25000   // період пінгу шлюзу для виявлення «застряглого» WL_CONNECTED
#define HEALTH_MAX_FAILS   3       // стільки невдач поспіль -> лінк мертвий -> форсований реконект

static uint32_t s_last_health = 0;
static int      s_health_fails = 0;

static char s_ssid[33] = "";
static char s_pw[65] = "";
static WifiStaState s_state = WSTA_IDLE;
static uint32_t s_connect_start = 0;
static bool s_has_saved = false;

// ---- Список відомих мереж (окремо від "останньої" вище) ----
// autoconn: чи підхоплювати цю мережу автоматично (вибір користувача). За
// замовчуванням true — стара поведінка; старі записи без поля "a" читаються як true.
struct SavedNet { char ssid[33]; char pw[65]; bool autoconn; };
static SavedNet s_saved[WIFI_STA_MAX_SAVED];
static int s_saved_count = 0;
static bool s_saved_loaded = false;

static void load_saved_networks() {
  if (s_saved_loaded) return;
  s_saved_loaded = true;
  s_saved_count = 0;
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  // isKey спершу — щоб не сипати нешкідливий, але шумний ERROR-лог Preferences
  // ("nvs_get_str len fail: nets NOT_FOUND") на кожному чистому боті без збережених мереж.
  String json = p.isKey("nets") ? p.getString("nets", "") : String();
  p.end();
  if (json.length() == 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (s_saved_count >= WIFI_STA_MAX_SAVED) break;
    const char* s = o["s"] | "";
    if (!s[0]) continue;
    snprintf(s_saved[s_saved_count].ssid, sizeof(s_saved[0].ssid), "%s", s);
    snprintf(s_saved[s_saved_count].pw, sizeof(s_saved[0].pw), "%s", (const char*)(o["p"] | ""));
    s_saved[s_saved_count].autoconn = (bool)(o["a"] | 1);   // старі записи без "a" -> auto=on
    s_saved_count++;
  }
}

static void persist_saved_networks() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < s_saved_count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["s"] = s_saved[i].ssid;
    o["p"] = s_saved[i].pw;
    o["a"] = s_saved[i].autoconn ? 1 : 0;
  }
  String out;
  serializeJson(doc, out);
  p.putString("nets", out);
  p.end();
}

static int find_saved(const char* ssid) {
  if (!ssid) return -1;
  load_saved_networks();
  for (int i = 0; i < s_saved_count; i++) if (strcmp(s_saved[i].ssid, ssid) == 0) return i;
  return -1;
}

void wifi_sta_save_network(const char* ssid, const char* pw) {
  if (!ssid || !ssid[0]) return;
  load_saved_networks();
  int idx = find_saved(ssid);
  bool is_new = (idx < 0);
  if (idx < 0) idx = (s_saved_count < WIFI_STA_MAX_SAVED) ? s_saved_count++ : WIFI_STA_MAX_SAVED - 1;
  snprintf(s_saved[idx].ssid, sizeof(s_saved[0].ssid), "%s", ssid);
  snprintf(s_saved[idx].pw, sizeof(s_saved[0].pw), "%s", pw ? pw : "");
  if (is_new) s_saved[idx].autoconn = true;   // нова мережа -> авто-підключення за замовчуванням
  persist_saved_networks();
}

int wifi_sta_saved_count() { load_saved_networks(); return s_saved_count; }

const char* wifi_sta_saved_ssid(int i) {
  load_saved_networks();
  return (i >= 0 && i < s_saved_count) ? s_saved[i].ssid : "";
}

void wifi_sta_forget_network(int i) {
  load_saved_networks();
  if (i < 0 || i >= s_saved_count) return;
  for (int j = i; j < s_saved_count - 1; j++) s_saved[j] = s_saved[j + 1];
  s_saved_count--;
  persist_saved_networks();
}

bool wifi_sta_is_known(const char* ssid) { return find_saved(ssid) >= 0; }

bool wifi_sta_saved_auto(int i) {
  load_saved_networks();
  return (i >= 0 && i < s_saved_count) ? s_saved[i].autoconn : false;
}

void wifi_sta_set_saved_auto(int i, bool on) {
  load_saved_networks();
  if (i < 0 || i >= s_saved_count || s_saved[i].autoconn == on) return;
  s_saved[i].autoconn = on;
  persist_saved_networks();
}

// Вмикає STA-режим, зберігаючи активний SoftAP (якщо він піднятий remote-режимом).
static void ensure_sta_mode() {
  wifi_mode_t m = WiFi.getMode();
  bool ap_up = (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
  wifi_mode_t want = ap_up ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  WiFi.mode(want);  // ставимо безумовно (як у робочому референсі)
  delay(100);       // одноразово: дати STA-інтерфейсу піднятись перед скануванням
}

// Стелс-MAC: стабільний локально-адміністрований (біт 0x02), НЕ Espressif-OUI.
// Генерується раз і зберігається в NVS — щоб плата лишалась досяжною за тим самим
// MAC/IP, але не "світила" вендором Espressif. Мімікрує рандомізацію MAC телефонів.
// Застосовувати ПЕРЕД WiFi.begin/softAP (зміна MAC на льоту рве з'єднання).
static void apply_stealth_identity() {
  // Нейтральний hostname замість "espressif"/"esp32-xxxx".
  WiFi.setHostname("wlan-device");

  uint8_t mac[6];
  Preferences p;
  bool have = false;
  if (p.begin(NVS_NS, false)) {
    size_t n = p.getBytes("smac", mac, 6);
    have = (n == 6);
    if (!have) {
      esp_fill_random(mac, 6);
      mac[0] = (mac[0] & 0xFE) | 0x02;   // unicast + локально-адміністрований
      p.putBytes("smac", mac, 6);
    }
    p.end();
  } else {
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0xFE) | 0x02;
  }
  esp_wifi_set_mac(WIFI_IF_STA, mac);
  // SoftAP MAC МУСИТЬ відрізнятись від STA (ESP32 не дозволяє однакові) — інакше
  // set_mac для AP тихо провалюється і BSSID лишається Espressif. Робимо окремий
  // (той самий OUI, інший останній байт) — теж локально-адмін., теж не Espressif.
  uint8_t apmac[6]; memcpy(apmac, mac, 6);
  apmac[5] ^= 0x01;
  esp_wifi_set_mac(WIFI_IF_AP, apmac);
  Serial.printf("[STEALTH] STA %02X:%02X:%02X:%02X:%02X:%02X host=wlan-device\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void wifi_sta_begin() {
  // WiFi.mode перед set_mac — драйвер має бути ініціалізований. STA_STA піднімає стек.
  if (settings_stealth()) {
    WiFi.mode(WIFI_MODE_STA);
    apply_stealth_identity();
  }
  Preferences p;
  if (p.begin(NVS_NS, true)) {  // read-only
    String ss = p.getString(NVS_SSID, "");
    String pw = p.getString(NVS_PW, "");
    p.end();
    if (ss.length() > 0) {
      snprintf(s_ssid, sizeof(s_ssid), "%s", ss.c_str());
      snprintf(s_pw, sizeof(s_pw), "%s", pw.c_str());
      s_has_saved = true;
    }
  }
}

void wifi_sta_scan_start() {
  // Скан вимагає розриву STA (інакше повертає 0 мереж), але САМ ПО СОБІ вхід у
  // "WiFi Setup" не повинен назавжди відбирати мережу — тому запам'ятовуємо активне
  // підключення й відновлюємо його одразу після скану.
  bool was_connected = (WiFi.status() == WL_CONNECTED);
  char prev_ssid[33] = "", prev_pw[65] = "";
  if (was_connected) {
    snprintf(prev_ssid, sizeof(prev_ssid), "%s", s_ssid);
    snprintf(prev_pw, sizeof(prev_pw), "%s", s_pw);
  }

  ensure_sta_mode();
  // Розірвати можливе авто-підключення STA — інакше скан повертає 0 мереж.
  // disconnect(false, false): не гасить радіо і не стирає збережений AP -> SoftAP цілий.
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  // СИНХРОННИЙ скан (блокує ~2-4с) — разова дія, як runDiagnostics. Прибирає
  // крихкість асинхронного шляху (транзієнтний -2 і зайві рестарти). show_hidden=true
  // як у робочому референсі.
  int16_t r = WiFi.scanNetworks(false /* sync */, true /* show_hidden */);
  Serial.printf("[STA] scanNetworks(sync) -> %d (mode=%d)\n", (int)r, (int)WiFi.getMode());

  if (was_connected && prev_ssid[0]) {   // повернути мережу, яку відібрав скан
    WiFi.begin(prev_ssid, prev_pw);
    s_state = WSTA_CONNECTING;
    s_connect_start = millis();
    Serial.printf("[STA] scan done -> restoring '%s'\n", prev_ssid);
  }
}

int wifi_sta_scan_state() {
  int n = WiFi.scanComplete();
  // Arduino: -1 = running (WIFI_SCAN_RUNNING), -2 = failed (WIFI_SCAN_FAILED)
  return n;
}

const char* wifi_sta_ssid(int i) {
  static String s;
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return "";
  s = WiFi.SSID(i);
  return s.c_str();
}

int wifi_sta_rssi(int i) {
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return 0;
  return WiFi.RSSI(i);
}

bool wifi_sta_is_open(int i) {
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return false;
  return WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
}

// Короткий ярлик типу захисту (як «замок»/тип у налаштуваннях WiFi на телефоні).
const char* wifi_sta_enc_str(int i) {
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return "";
  switch (WiFi.encryptionType(i)) {
    case WIFI_AUTH_OPEN:            return "Open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

int wifi_sta_channel(int i) {
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return 0;
  return WiFi.channel(i);
}

const char* wifi_sta_bssid_str(int i) {
  static String s;
  int n = WiFi.scanComplete();
  if (i < 0 || n < 0 || i >= n) return "";
  s = WiFi.BSSIDstr(i);
  return s.c_str();
}

void wifi_sta_connect(const char* ssid, const char* pw) {
  if (!ssid || !ssid[0]) return;
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
  snprintf(s_pw, sizeof(s_pw), "%s", pw ? pw : "");
  ensure_sta_mode();
  WiFi.persistent(false);        // креденшали тримаємо в NVS самі -> не зношуємо WiFi-flash реконектами
  WiFi.setAutoReconnect(true);   // стек сам відновлює лінк при розриві (перша лінія оборони)
  WiFi.setSleep(false);          // ВИМКНУТИ modem-sleep: ESP32 за замовч. присипляє радіо між DTIM-
                                 // beacon'ами -> латентність 10-120мс і пропущені пакети (флакі-
                                 // реачабіліті). Без сну — стабільний низьколатентний лінк («годинник»).
                                 // Ціна ~20-30мА (враховано в енергобюджеті).
  WiFi.begin(s_ssid, s_pw);
  s_state = WSTA_CONNECTING;
  s_connect_start = millis();
  Serial.printf("[STA] connecting to '%s'\n", s_ssid);
}

// Самовідновний: тримає STA-лінк живим «як годинник».
//  CONNECTING -> CONNECTED (успіх) / FAILED (таймаут; швидкий фідбек ручному конекту).
//  CONNECTED  -> якщо лінк ВПАВ, одразу повертаємось у CONNECTING + WiFi.begin (миттєвий реконект).
//  FAILED     -> фонова повторна спроба кожні STA_RETRY_MS (плата НІКОЛИ не лишається без домашньої
//               мережі назавжди — критично для одночасного AP+STA і доступу з обох боків).
// Разом із WiFi.setAutoReconnect(true) це дві незалежні лінії оборони проти розривів.
void wifi_sta_loop() {
  bool linked = (WiFi.status() == WL_CONNECTED);
  switch (s_state) {
    case WSTA_CONNECTING:
      if (linked) {
        s_state = WSTA_CONNECTED;
        os_log("[STA] connected '%s' IP %s", s_ssid, WiFi.localIP().toString().c_str());
      } else if (millis() - s_connect_start > CONNECT_TIMEOUT_MS) {
        s_state = WSTA_FAILED;
        s_connect_start = millis();   // старт backoff-таймера
        os_log("[STA] connect timeout to '%s'", s_ssid);
      }
      break;
    case WSTA_CONNECTED:
      if (!linked) {
        s_state = WSTA_CONNECTING;
        s_connect_start = millis();
        WiFi.begin(s_ssid, s_pw);
        Serial.println("[STA] link lost -> reconnecting");
        break;
      }
      // Health-check: WL_CONNECTED буває «застряглим» (роутер деаутнув / канал змістився, а стек
      // цього не помітив -> status лишається CONNECTED, IP є, але пакети не йдуть). status-перевірка
      // цього НЕ ловить. Тому періодично пінгуємо шлюз; N невдач поспіль = мертвий лінк -> форс-реконект.
      if (millis() - s_last_health > HEALTH_CHECK_MS) {
        s_last_health = millis();
        IPAddress gw = WiFi.gatewayIP();
        bool alive = ((uint32_t)gw != 0) && Ping.ping(gw, 1);
        if (alive) {
          s_health_fails = 0;
        } else if (++s_health_fails >= HEALTH_MAX_FAILS) {
          s_health_fails = 0;
          Serial.println("[STA] health-check: gw dead -> forced reconnect (stale WL_CONNECTED)");
          WiFi.disconnect(false, false);          // рве STA, НЕ гасить радіо/AP
          s_state = WSTA_CONNECTING; s_connect_start = millis();
          WiFi.begin(s_ssid, s_pw);
        } else {
          Serial.printf("[STA] health-check fail %d/%d\n", s_health_fails, HEALTH_MAX_FAILS);
        }
      }
      break;
    case WSTA_FAILED:
      if (s_ssid[0] && millis() - s_connect_start > STA_RETRY_MS) {
        s_state = WSTA_CONNECTING;
        s_connect_start = millis();
        WiFi.begin(s_ssid, s_pw);
        Serial.println("[STA] background retry");
      }
      break;
    default: break;
  }
}

void wifi_sta_stop() {
  WiFi.disconnect(true /* wifioff */, false);  // рве STA і вимикає WiFi-радіо
  s_state = WSTA_IDLE;
  Serial.println("[STA] stopped (radio off)");
}

// Повертає STA після wifi_sta_stop(): вмикає радіо й перепідключається до ОСТАННЬОЇ
// мережі (s_ssid/s_pw збережені при stop). Неблокуюче — wifi_sta_loop() доводить до
// CONNECTED. Викликати з on_exit() модулів, що глушать WiFi (BLE-скан/скімер/трекер),
// щоб плата НЕ лишалась без мережі після аналізу (#4 save+reconnect).
void wifi_sta_resume() {
  if (!s_ssid[0]) { Serial.println("[STA] resume: no saved network"); return; }
  ensure_sta_mode();             // безумовно вмикає радіо (STA або AP+STA)
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(s_ssid, s_pw);
  s_state = WSTA_CONNECTING;
  s_connect_start = millis();
  Serial.printf("[STA] resume -> '%s'\n", s_ssid);
}

WifiStaState wifi_sta_status() { return s_state; }

const char* wifi_sta_ip() {
  static String ip;
  if (s_state != WSTA_CONNECTED) return "";
  ip = WiFi.localIP().toString();
  return ip.c_str();
}

const char* wifi_sta_current_ssid() { return s_ssid; }

void wifi_sta_save() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {  // read-write
    p.putString(NVS_SSID, s_ssid);
    p.putString(NVS_PW, s_pw);
    p.end();
    s_has_saved = true;
    Serial.println("[STA] credentials saved");
  }
  wifi_sta_save_network(s_ssid, s_pw);  // + до списку відомих мереж (кілька)
}

void wifi_sta_forget() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.remove(NVS_SSID);   // НЕ p.clear() — той стер би й список "nets" у тому ж неймспейсі
    p.remove(NVS_PW);
    p.end();
  }
  s_has_saved = false;
  s_ssid[0] = s_pw[0] = '\0';
}

bool wifi_sta_has_saved() { return s_has_saved; }

bool wifi_sta_try_connect_saved(const char* ssid) {
  int idx = find_saved(ssid);
  if (idx < 0) return false;
  wifi_sta_connect(s_saved[idx].ssid, s_saved[idx].pw);
  return true;
}

void wifi_sta_autoconnect() {
  load_saved_networks();
  if (s_saved_count == 0) return;
  wifi_sta_scan_start();               // синхронний скан (~2-4с), той самий шлях що WiFi Setup
  int n = wifi_sta_scan_state();
  if (n < 0) n = 0;
  for (int j = 0; j < n; j++) {
    const char* ss = wifi_sta_ssid(j);
    int si = find_saved(ss);
    // лише мережі з увімкненим прапорцем auto (вибір користувача)
    if (si >= 0 && s_saved[si].autoconn && wifi_sta_try_connect_saved(ss)) {
      Serial.printf("[STA] autoconnect -> '%s'\n", ss);
      return;
    }
  }
  Serial.println("[STA] autoconnect: no auto-enabled known networks in range");
}
