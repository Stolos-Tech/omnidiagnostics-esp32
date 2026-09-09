#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "transport_wifi.h"
#include "remote_dispatch.h"
#include "protocol.h"
#include "session.h"
#include "../drivers/filesystem.h"
#include "../drivers/battery_adc.h"
#include "../drivers/report_store.h"
#include "../kernel/script_path.h"
#include "../kernel/sys_ctl.h"
#include "../berry_bridge/berry_vm.h"
#include "../berry_bridge/native_api.h"
#include "../apps_builtin/net_scan.h"   // net_scan_device_json — глибокий аналіз пристрою
#include "auth_hmac.h"                   // challenge-response HMAC auth (dual з PIN)
#include "../drivers/wifi_sta.h"
#include "../kernel/settings.h"
#include "../kernel/hw_safe.h"
#include "../kernel/module_power.h"
#include "../kernel/log_ring.h"
#include "../kernel/shared_store.h"
#include "../drivers/sd_store.h"
#include "../drivers/nrf24.h"
#include "../drivers/cc1101.h"
#include "../drivers/uno_link.h"
#include "../drivers/display.h"
#include "../kernel/subghz_util.h"
#include "../kernel/archive_category.h"
#include "../kernel/power_model.h"
#include "../kernel/cooling.h"
#include "../kernel/net_util.h"
#include "../kernel/device_id.h"
#include "../kernel/fw_update.h"
#include "../kernel/board_profile.h"
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include "remote_control.h"
#include <SD.h>
#include <HTTPClient.h>

#define AP_SSID   "OmniDiag-Setup"
#define WEB_PORT  80
#define WS_PORT   81
#define WEB_INDEX "/web/index.html"

static WebServer       s_http(WEB_PORT);
static WebSocketsServer s_ws(WS_PORT);
static DNSServer        s_dns;         // captive-portal: DNS-хайджек для клієнтів AP
static bool s_active = false;
static bool    s_ap_up   = false;      // чи піднятий SoftAP зараз (в AP+STA — постійно, разом зі STA)
static uint8_t s_ap_chan = 0;          // канал, на якому зараз піднятий SoftAP (0 = не піднятий)
static char    s_ap_pw[9] = "";
static char    s_ap_ssid[33] = AP_SSID;   // назва точки (редагована з додатка, у NVS)
static bool    s_ap_enabled  = true;      // чи піднімати SoftAP (вимк = STA-only)

// REST-фолбек-кеш: останній стан екрана і статус-іконок у вигляді готових JSON-рядків.
// Клієнти, чий браузер блокує WS-порт 81 (напр. пісочниця веб-агента), не можуть
// відкрити WebSocket — вони поллять /api/mirror через HTTP:80 і отримують ці ж дані.
// Заповнюється в transport_wifi_broadcast() (той самий потік мірора, що й для WS).
static String s_last_state;
static String s_last_status;

// Генерує 8-символьний WPA2-пароль (без плутаних 0/O/1/I).
static void generate_ap_password() {
  static const char charset[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
  for (int i = 0; i < 8; i++) s_ap_pw[i] = charset[esp_random() % (sizeof(charset) - 1)];
  s_ap_pw[8] = '\0';
}

// Пароль AP статичний між рестартами (той самий компроміс, що й PIN — зручність
// понад рандом-щоразу). Перший запуск генерує й зберігає, далі повертає збережений.
static void ensure_ap_password() {
  Preferences p;
  if (!p.begin("remcred", false)) { generate_ap_password(); return; }
  String saved = p.getString("appw", "");
  if (saved.length() == 8) {
    snprintf(s_ap_pw, sizeof(s_ap_pw), "%s", saved.c_str());
  } else {
    generate_ap_password();
    p.putString("appw", s_ap_pw);
  }
  // назва точки + on/off (редаговані з додатка)
  String ssid = p.getString("apssid", "");
  if (ssid.length() >= 1 && ssid.length() <= 32) snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", ssid.c_str());
  s_ap_enabled = p.getBool("apen", true);
  p.end();
}

// Змінити назву точки: зберегти в NVS + форсувати перепідняття AP з новим SSID.
static void ap_set_ssid(const char* name) {
  if (!name || !name[0] || strlen(name) > 32) return;
  snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", name);
  Preferences p; if (p.begin("remcred", false)) { p.putString("apssid", s_ap_ssid); p.end(); }
  s_ap_chan = 0; s_ap_up = false;   // форс re-apply
}
// Увімкнути/вимкнути точку: зберегти + застосувати.
static void ap_set_enabled(bool on) {
  s_ap_enabled = on;
  Preferences p; if (p.begin("remcred", false)) { p.putBool("apen", on); p.end(); }
  s_ap_chan = 0;   // форс re-apply/зупинку
}

// Захист /fs/* (читання/запис/видалення .be-скриптів): дозволено лише клієнту,
// що вже пройшов PIN-авторизацію через WS (та сама сесія, що й керування).
static bool require_auth() {
  if (session_is_authenticated()) return true;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(401, "text/plain", "unauthorized: enter PIN in the app first");
  return false;
}

// --- HTTP ---
static void handle_root() {
  // Веб-UI вимкнено (керуєш із телефона/шлюзу) -> віддаємо лише мінімальний стаб.
  // REST/WS лишаються активними (їх юзає телефон/шлюз) — це прибирає браузерний HTML-UI,
  // а не WebServer. Менша поверхня атаки, коли браузер не потрібен.
  if (!settings_webui_enabled()) {
    s_http.send(200, "text/plain",
                "ESP32-OS: web UI disabled (API-only). Use the phone app or gateway.\n");
    return;
  }
  s_http.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  // Фаза 4: СПОЧАТКУ пробуємо SD — важка/розширена статика веб-UI живе там (не роздуває
  // firmware/LittleFS; дозволяє великі бібліотеки графіків). Якщо на карті є свій index —
  // він має пріоритет. sd_bus_select() переприв'язує HSPI-MISO на SD (спільна шина з nRF24).
  if (sd_mounted()) {
    sd_bus_select();
    File sgz = SD.open("/web/index.html.gz", FILE_READ);
    if (sgz && sgz.size() > 0) { s_http.streamFile(sgz, "text/html"); sgz.close(); return; }
    if (sgz) sgz.close();
  }
  // ГОЛОВНЕ: віддаємо ПРЕДСТИСНУТУ gzip-версію (index.html.gz ~12КБ проти ~52КБ) з
  // заголовком Content-Encoding:gzip — браузер розпаковує сам. Учетверо коротший
  // трансфер надійно проходить навіть при малій вільній купі (mDNS з'їдає ~5.6КБ) —
  // раніше повна 52КБ-сторінка при ~16КБ heap віддавалась через раз (біла сторінка).
  File gz = LittleFS.open("/web/index.html.gz", "r");
  if (gz && gz.size() > 0) {
    // streamFile САМ додає Content-Encoding: gzip для файлів .gz — вручну НЕ додаємо
    // (подвійний заголовок ламає розпакування в браузері -> порожня сторінка).
    s_http.streamFile(gz, "text/html");
    gz.close();
    return;
  }
  if (gz) gz.close();
  // fallback: нестиснута (streamFile чанками, без великої аллокації)
  File f = LittleFS.open(WEB_INDEX, "r");
  if (f && f.size() > 0) {
    s_http.streamFile(f, "text/html");
    f.close();
  } else {
    if (f) f.close();
    s_http.send(200, "text/html",
      "<!doctype html><meta charset=utf-8><p>index.html missing on LittleFS "
      "(run uploadfs)</p>");
  }
}

// --- REST API (read-only, доступний з LAN без PIN) ---
// Capture-режим: коли керуємо платою по USB (serial-міст), той самий handler будує
// JsonDocument, але замість s_http.send() пишемо у s_cap_buf -> реюз усіх REST-handler'ів
// без дублювання логіки. Вмикається лише всередині wifi_serial_dispatch().
static bool   s_cap = false;
static String s_cap_buf;
static void send_json(const JsonDocument& doc) {
  if (s_cap) { s_cap_buf = ""; serializeJson(doc, s_cap_buf); return; }
  String out; serializeJson(doc, out);
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "application/json", out);
}

// Потокова віддача JSON без побудови повного рядка в RAM (chunked transfer) — знімає
// піковий тиск на heap при великих відповідях (airscan/netscan/devscan): замість
// doc(~2.5КБ)+String(~2.5КБ) віддаємо дрібними фрагментами по одному елементу.
// Працює і в USB-capture (s_cap): фрагменти йдуть у s_cap_buf.
static void out_begin() {
  if (s_cap) { s_cap_buf = ""; return; }
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_http.send(200, "application/json", "");
}
static inline void out_chunk(const String& s) { if (s_cap) s_cap_buf += s; else s_http.sendContent(s); }
static inline void out_chunk(const char* s)    { if (s_cap) s_cap_buf += s; else s_http.sendContent(s); }
static void out_end() { if (!s_cap) s_http.sendContent(""); }   // термінатор chunked

static int batt_soc(float v) {
  int p = (int)((v - 3.30f) / 0.90f * 100.0f);
  return p < 0 ? 0 : p > 100 ? 100 : p;
}

static void handle_api_battery() {
  float v = battery_read_cached(1000);   // REST-полінг не потребує свіжого 24мс-заміру щоразу
  JsonDocument doc;
  doc["mv"]  = (int)(v * 1000);
  doc["pct"] = batt_soc(v);
  doc["usb"] = v > 4.25f;
  send_json(doc);
}

static void handle_api_info() {
  JsonDocument doc;
  doc["chip"]      = ESP.getChipModel();
  doc["cpu_mhz"]   = ESP.getCpuFreqMHz();
  doc["uptime_s"]  = (uint32_t)(millis() / 1000);
  doc["free_heap"] = ESP.getFreeHeap();
  doc["ip"]        = WiFi.localIP().toString();
  send_json(doc);
}

static void handle_api_status() {
  JsonDocument doc;
  doc["sta"] = WiFi.status() == WL_CONNECTED;
  doc["ap"]  = s_ap_up;   // AP+STA: рідна точка активна постійно разом зі STA
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  float v = battery_read_cached(1000);
  doc["mv"]  = (int)(v * 1000);
  doc["usb"] = v > 4.25f;
  send_json(doc);
}

// --- Розширена статистика ресурсів (/api/stats): здоров'я купи (поточна/мінімальна
//     за весь час/найбільший вільний блок -> фрагментація), аптайм, к-сть WS-клієнтів
//     і AP-станцій, звіти, зайнятий/вільний sketch, параметри STA. Read-only, без PIN. ---
static void handle_api_stats() {
  JsonDocument doc;
  doc["uptime_s"]    = (uint32_t)(millis() / 1000);
  doc["heap"]        = ESP.getFreeHeap();
  doc["heap_min"]    = ESP.getMinFreeHeap();     // мінімум за весь час роботи (запас міцності)
  doc["heap_maxblk"] = ESP.getMaxAllocHeap();    // найбільший суцільний блок -> індикатор фрагментації
  doc["heap_total"]  = ESP.getHeapSize();
  // ESP.getSketchSize()/getFreeSketchSpace() ЧИТАЮТЬ флеш-образ (~1.2с!) — це й
  // робило /api/stats повільним (тоді як /api/status ~0.1с). Розмір скетчу сталий
  // у рантаймі, тож рахуємо ОДИН раз і кешуємо.
  static uint32_t s_sketch = 0, s_sketch_free = 0;
  if (s_sketch == 0) { s_sketch = ESP.getSketchSize(); s_sketch_free = ESP.getFreeSketchSpace(); }
  doc["sketch"]      = s_sketch;
  doc["sketch_free"] = s_sketch_free;
  doc["ws_clients"]  = s_ws.connectedClients();
  doc["ap_clients"]  = WiFi.softAPgetStationNum();
  doc["log_total"]   = log_ring().total();
  char names[REPORT_MAX][REPORT_NAME_MAX];
  doc["reports"]     = report_list(names, REPORT_MAX);
  bool sta = WiFi.status() == WL_CONNECTED;
  doc["sta"]  = sta;
  doc["ssid"] = sta ? WiFi.SSID() : String("");
  doc["ip"]   = sta ? WiFi.localIP().toString() : String("");
  doc["rssi"] = sta ? WiFi.RSSI() : 0;
  send_json(doc);
}

// --- Крос-модульний store (/api/shared): проаналізовані хости/мережі, які веб дає
//     обрати для інших модулів БЕЗ рескану. "sd" = чи під'єднаний SD-модуль для
//     персистенції (поки false — архітектурно готово, чекає залізо). Read-only. ---
static void handle_api_shared() {
  JsonDocument doc;
  doc["sd"] = sd_mounted();   // true коли SD-модуль змонтований (юзер зробив `sd mount`)
  JsonArray hosts = doc["hosts"].to<JsonArray>();
  for (int i = 0; i < shared_host_count(); i++) {
    const SharedHost* h = shared_host(i);
    JsonObject o = hosts.add<JsonObject>();
    o["ip"] = h->ip; o["note"] = h->note;
  }
  JsonArray nets = doc["nets"].to<JsonArray>();
  for (int i = 0; i < shared_net_count(); i++) {
    const SharedNet* n = shared_net(i);
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = n->ssid; o["ch"] = n->ch; o["rssi"] = n->rssi; o["enc"] = n->enc;
  }
  send_json(doc);
}

// --- REST-фолбек мірора/керування (для клієнтів із заблокованим WS-портом 81) ---
// Дублює WS-протокол через HTTP:80: логін PIN-ом, надсилання команд, полінг стану.
// Тіло POST читаємо як arg("plain") (fetch() шле text/plain -> у "plain"), той самий
// формат JSON, що й через WS -> реюз remote_handle_incoming() без дублювання логіки.

// POST /api/login  тіло {"pin":"123456"} -> автентифікує сесію.
// Спроба HMAC-логіну: {"nonce":"..","hmac":".."} -> auth_hmac_verify. true = автентифіковано.
// Якщо полів нема / не збіглось -> false, і викликач іде старим PIN-шляхом (dual-auth).
static bool try_hmac_login(const char* body) {
  if (!body || !auth_hmac_ready()) return false;
  JsonDocument d;
  if (deserializeJson(d, body)) return false;
  const char* nonce = d["nonce"] | "";
  const char* hmac  = d["hmac"]  | "";
  if (!nonce[0] || !hmac[0]) return false;
  if (auth_hmac_verify(nonce, hmac)) { session_force_auth(); return true; }
  return false;
}

static void handle_api_challenge() {   // публічний: nonce сам по собі не секрет
  char n[40]; auth_hmac_challenge(n, sizeof(n));
  JsonDocument doc; doc["nonce"] = n; doc["hmac"] = auth_hmac_ready();
  send_json(doc);
}
static void handle_api_enroll() {      // auth-only: seed для провізії app/сервера (бутстреп)
  if (!require_auth()) return;
  char s[70];
  if (!auth_hmac_seed_hex(s, sizeof(s))) { s_http.send(500, "application/json", "{\"error\":\"noseed\"}"); return; }
  JsonDocument doc; doc["seed"] = s;
  send_json(doc);
}

static void handle_api_login() {
  String body = s_http.arg("plain");
  bool locked = false;
  if (!try_hmac_login(body.c_str())) {                     // спершу HMAC; якщо ні -> старий PIN
    RemoteAction a = remote_handle_incoming(body.c_str()); // {"pin":".."} -> session_authenticate
    locked = (a == RA_DISCONNECT);                         // вичерпано спроби PIN
  }
  JsonDocument doc;
  doc["ok"]     = session_is_authenticated();
  doc["locked"] = locked;
  send_json(doc);
}

// GET /api/hwsafe -> {"safe":bool}. POST {"safe":bool} (auth) -> безпечний режим пінів:
// safe=true -> усі піни модулів high-Z (голі ніжки не коротнуть); safe=false -> робочий idle.
static void handle_api_hwsafe() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (s_http.method() == HTTP_POST) {
    if (!require_auth()) return;
    JsonDocument in;
    if (deserializeJson(in, s_http.arg("plain")) == DeserializationError::Ok && in["safe"].is<bool>())
      hw_safe_set(in["safe"].as<bool>());
  }
  JsonDocument doc;
  doc["safe"] = g_hw_safe;
  if (g_hw_blocked) {   // КЗ-детектор заблокував драйв цих пінів (активація відкотилась у safe)
    const char* names[] = { "nRF CSN(25)", "SD CS(33)", "CC1101 CS(27)", "nRF CE(12)", "SPI 2/15" };
    JsonArray a = doc["blocked"].to<JsonArray>();
    for (int i = 0; i < 5; i++) if (g_hw_blocked & (1u << i)) a.add(names[i]);
  }
  send_json(doc);
}

// GET /api/pinscan (auth) — «продзвонити» піни модулів: стан кожного (FLOAT/HIGH/LOW-pulled) +
// КЗ/мостики між пінами. Софтовий тестер безперервності. Після скану відновлює idle-стан.
static void handle_api_pinscan() {
  if (!require_auth()) return;
  struct P { uint8_t pin; const char* name; bool drivable; };
  static const P PINS[] = {
    {2,"SCK",true}, {15,"MOSI",true}, {38,"MISO_nRF",false}, {21,"MISO_SD",true},
    {25,"CSN_nRF",true}, {33,"CS_SD",true}, {27,"CS_CC1101",true}, {12,"CE_nRF",true}, {13,"FAN",true},
  };
  const int N = sizeof(PINS) / sizeof(PINS[0]);
  JsonDocument doc;
  // 1) стан кожного піна
  JsonArray arr = doc["pins"].to<JsonArray>();
  for (int i = 0; i < N; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["gpio"] = PINS[i].pin; o["name"] = PINS[i].name;
    if (!PINS[i].drivable) {                       // input-only (38): нема внутр. pull
      pinMode(PINS[i].pin, INPUT); delayMicroseconds(60);
      o["state"] = digitalRead(PINS[i].pin) ? "HIGH?" : "LOW?";
      o["note"]  = "input-only";
    } else {
      pinMode(PINS[i].pin, INPUT_PULLUP);   delayMicroseconds(70); int up = digitalRead(PINS[i].pin);
      pinMode(PINS[i].pin, INPUT_PULLDOWN); delayMicroseconds(70); int dn = digitalRead(PINS[i].pin);
      pinMode(PINS[i].pin, INPUT);
      o["state"] = (up != dn) ? "FLOAT" : (up ? "HIGH_pull" : "LOW_pull");
    }
  }
  // 2) КЗ/мостики: жену кожен drivable-пін LOW, дивлюсь хто ще впав (з'єднані).
  // ВАЖЛИВО: baseline — пін B, що й так LOW без драйву (зовн. pulldown як CE/FAN, або
  // input-only без внутр. pull-up), НЕ можна судити -> пропускаємо. Місток реальний лише
  // якщо B тримався HIGH (pull-up), а при драйві A=LOW впав у LOW.
  JsonArray sh = doc["bridges"].to<JsonArray>();
  for (int a = 0; a < N; a++) {
    if (!PINS[a].drivable) continue;
    for (int b = 0; b < N; b++) {
      if (b == a || !PINS[b].drivable) continue;   // B теж має тягнути pull-up (input-only 38 пропуск)
      pinMode(PINS[a].pin, INPUT);                  // A відпущено
      pinMode(PINS[b].pin, INPUT_PULLUP); delayMicroseconds(80);
      if (digitalRead(PINS[b].pin) == 0) continue;  // B і так LOW -> недіагностовно, пропуск
      pinMode(PINS[a].pin, OUTPUT); digitalWrite(PINS[a].pin, LOW); delayMicroseconds(80);
      int fell = (digitalRead(PINS[b].pin) == 0);
      pinMode(PINS[a].pin, INPUT); pinMode(PINS[b].pin, INPUT);
      if (fell) { JsonObject br = sh.add<JsonObject>(); br["a"] = PINS[a].name; br["b"] = PINS[b].name; }
    }
  }
  hw_safe_apply_pins();   // відновити safe-стан (high-Z у safe)
  module_power_apply();   // + per-module живлення/піни (поважає вимкнені модулі та спільні піни)
  extern void fan_pwm_reattach(); fan_pwm_reattach();   // повернути PWM на GPIO13 (пінскан робив його INPUT -> фен відпадав)
  send_json(doc);
}

// GET /api/spitest (auth) — АКТИВНА продзвонка SPI: піднімає шину й читає сирі регістри nRF24.
// Дефінітивно каже, чи живий SPI-шлях (write+readback RF_CH=0x25). Не чіпає SD (mount висне).
static void handle_api_spitest() {
  if (!require_auth()) return;
  // Опційна тактова SPI: ?hz=N (WiFi-шлях; USB-шлях виставляє її в диспатчі до виклику).
  if (s_http.hasArg("hz")) { uint32_t hz = strtoul(s_http.arg("hz").c_str(), nullptr, 10); if (hz >= 100000 && hz <= 16000000) nrf24_set_spi_hz(hz); }
  uint8_t r[6];
  uint8_t status = nrf24_regdump(r);
  JsonDocument doc;
  JsonObject nrf = doc["nrf24"].to<JsonObject>();
  nrf["spi_hz"] = nrf24_get_spi_hz();
  char hx[6];
  auto hex = [&](uint8_t v){ snprintf(hx,sizeof(hx),"0x%02X",v); return String(hx); };
  nrf["config"]    = hex(r[0]);   // очік. після ресету 0x08 (або 0x03 якщо begin піднявся)
  nrf["en_aa"]     = hex(r[1]);   // 0x3F
  nrf["setup_aw"]  = hex(r[2]);   // 0x03
  nrf["rf_setup"]  = hex(r[3]);   // 0x0E/0x06
  nrf["rf_ch"]     = hex(r[4]);
  nrf["rf_ch_rb"]  = hex(r[5]);   // readback після запису 0x25
  nrf["status"]    = hex(status);
  bool alive = (r[5] == 0x25);                 // write+readback співпав -> шина 100% жива
  bool silent = (r[0]==0x00 && r[1]==0x00 && r[2]==0x00) ||
                (r[0]==0xFF && r[1]==0xFF && r[2]==0xFF);  // MISO глухо тягне рейку
  nrf["alive"] = alive;
  nrf["verdict"] = alive ? "SPI OK — nRF відповідає (readback 0x25)"
                 : silent ? "MISO мовчить (0x00/0xFF) — обрив SPI або живлення nRF"
                 : "часткова відповідь — шина нестабільна (SCK/MOSI/CSN?)";
  send_json(doc);
}

// GET /api/unodiag (auth) — повна діагностика Arduino UNO згідно з її призначенням:
// лінк (UART GPIO37-RX/GPIO22-TX @9600), активний SCAN-пробінг (round-trip), вітали
// (free RAM/uptime), CAP-реєстр модулів, живі датчики, RFID-підсистема. Аналог spitest для UNO.
static void handle_api_unodiag() {
  if (!require_auth()) return;
  JsonDocument doc;
  bool safe = g_hw_safe;                              // у safe-режимі лінк UNO не ініціалізований
  // Активний пробінг: рахуємо модулі до, шлемо SCAN, ~400мс качаємо UART, дивимось відповідь.
  int  before   = uno_link_module_count();
  bool probed   = false, responded = false;
  if (!safe) {
    probed = true;
    uno_link_send_scan();
    uint32_t t0 = millis();
    while (millis() - t0 < 400) { uno_link_loop(); delay(5); }
    long age = uno_link_last_rx_age_ms();
    responded = uno_link_connected() || uno_link_module_count() > before || (age >= 0 && age < 500);
  }
  bool conn = uno_link_connected();

  JsonObject link = doc["link"].to<JsonObject>();
  link["uart"]        = "GPIO37-RX / GPIO22-TX @9600";
  link["safe_mode"]   = safe;                          // true -> лінк вимкнено safe-режимом
  link["connected"]   = conn;
  link["last_rx_ms"]  = uno_link_last_rx_age_ms();     // вік останнього рядка; -1 = нічого
  JsonObject probe = doc["probe"].to<JsonObject>();
  probe["scan_sent"]  = probed;
  probe["responded"]  = responded;

  JsonObject vit = doc["vitals"].to<JsonObject>();
  vit["free_ram"] = uno_link_free_ram();               // -1 доки STAT не приходив
  vit["uptime_s"] = (uint32_t)uno_link_uptime_s();

  JsonArray mods = doc["modules"].to<JsonArray>();
  for (int i = 0; i < uno_link_module_count(); i++) {
    const UnoModule& m = uno_link_module(i);
    if (m.slot < 0) continue;
    JsonObject o = mods.add<JsonObject>();
    o["slot"] = m.slot; o["type"] = m.type; o["name"] = m.name; o["present"] = m.present;
    if (m.value[0]) o["value"] = m.value;
  }
  JsonObject sn = doc["sensors"].to<JsonObject>();
  const UnoSensors& s = uno_link_sensors();
  sn["pot"] = s.pot; sn["reed"] = s.reed;
  const UnoEnv& e = uno_link_env();
  if (e.temp_x10) { sn["temp_c"] = e.temp_x10 / 10.0; sn["hum"] = e.hum_x10 / 10.0; }

  JsonObject rf = doc["rfid"].to<JsonObject>();
  rf["last_uid"] = uno_link_last_rfid();               // 0 = не було тегів
  rf["seq"]      = uno_link_rfid_seq();
  const RfAudit& a = uno_link_audit();
  if (a.valid) { rf["audit_uid"] = a.uid; rf["cracked"] = a.cracked; rf["total"] = a.total; rf["verdict"] = a.verdict; }

  doc["verdict"] = safe      ? "SAFE-режим: лінк UNO вимкнено (деактивуй SAFE, щоб діагностувати)"
                 : conn      ? "UNO онлайн — телеметрія свіжа"
                 : responded ? "UNO відповів на SCAN, але телеметрія нерегулярна (перевір живлення/GND)"
                             : "UNO мовчить — перевір UART (GPIO37-RX через дільник 1к/2к, GPIO22-TX), 9600, живлення 5V, спільну GND";
  send_json(doc);
}

// GET /api/selftest (auth) — АПАРАТНИЙ self-test: агрегує нові діагностики (heap, nRF-SPI,
// SD, піни/КЗ, UNO-лінк, батарея, охолодження, WiFi) у pass/warn/fail. Доповнює екранний
// SelfTestApp (мережеві модулі -> /reports -> бот). Результат придатний для звіту й боту.
static void handle_api_selftest() {
  if (!require_auth()) return;
  extern int g_fan_duty;
  out_begin();
  out_chunk("{\"checks\":[");
  int pass = 0, warn = 0, fail = 0, idx = 0;
  auto emit = [&](const char* name, int st, const String& detail) {
    // st: 0=ok 1=warn 2=fail
    if (st == 0) pass++; else if (st == 1) warn++; else fail++;
    JsonDocument o;
    o["name"] = name; o["status"] = st == 0 ? "ok" : st == 1 ? "warn" : "fail"; o["detail"] = detail;
    String f; serializeJson(o, f);
    if (idx++) out_chunk(","); out_chunk(f);
  };
  // heap
  uint32_t hf = ESP.getFreeHeap(), hm = ESP.getMinFreeHeap();
  emit("heap", hm < 4000 ? 2 : hf < 20000 ? 1 : 0, String("free ") + (hf/1024) + "KB min " + (hm/1024) + "KB");
  // nRF24 SPI (активна продзвонка)
  { uint8_t r[6]; nrf24_regdump(r);
    bool alive = (r[5] == 0x25);
    emit("nrf24_spi", alive ? 0 : 2, alive ? "readback 0x25 OK" : "MISO мовчить (0x00/0xFF)"); }
  // CC1101 (може бути не встановлений — тоді warn, не fail)
  { bool cc = profile_get("cc1101") && cc1101_begin() && cc1101_present();
    emit("cc1101", cc ? 0 : 1, cc ? "present" : "не виявлено"); }
  // SD
  emit("sd", sd_mounted() ? 0 : 2, sd_mounted() ? "mounted" : "не змонтовано");
  // піни (safe/blocked)
  { String d = g_hw_safe ? "SAFE (піни high-Z)" : "active"; emit("pins", g_hw_safe ? 1 : 0, d); }
  // UNO лінк
  emit("uno", uno_link_connected() ? 0 : 1, uno_link_connected() ? "онлайн" : "не підключено");
  // батарея
  { float v = battery_read_cached(1000); int mv = (int)(v*1000);
    emit("battery", mv < 3400 ? 2 : mv < 3600 ? 1 : 0, String(mv) + "mV" + (v > 4.25f ? " (USB)" : "")); }
  // охолодження (PWM активний драйв)
  emit("cooling", 0, String("duty ") + (g_fan_duty*100/255) + "%");
  // WiFi STA
  { bool up = WiFi.status() == WL_CONNECTED;
    emit("wifi", up ? 0 : 1, up ? (String("STA ") + WiFi.RSSI() + "dBm") : "offline"); }
  String tail; tail.reserve(48);
  tail = "],\"pass\":"; tail += pass; tail += ",\"warn\":"; tail += warn; tail += ",\"fail\":"; tail += fail; tail += "}";
  out_chunk(tail);
  out_end();
}

// GET/POST /api/fantest (auth) — тест фенів: POST {"duty":0..255} форсить PWM (крива не перебиває),
// {"duty":-1} -> назад в авто. GET -> поточний стан. Для перевірки, чи фени взагалі крутяться.
static void handle_api_fantest() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  extern int g_fan_override, g_fan_duty;
  if (s_http.method() == HTTP_POST) {
    if (!require_auth()) return;
    String body = s_http.arg("plain");
    JsonDocument d; deserializeJson(d, body);
    int duty = d["duty"] | -2;
    if (duty >= -1 && duty <= 255) g_fan_override = duty;
  }
  JsonDocument doc;
  doc["override"] = g_fan_override;
  doc["duty"]     = g_fan_duty;
  doc["pct"]      = g_fan_duty * 100 / 255;
  doc["mode"]     = g_fan_override >= 0 ? "форс" : "авто";
  send_json(doc);
}

// POST /api/cmd  тіло {"btn"/"idx"/"back"/"text"+value} -> подія в input-чергу (як WS).
static void handle_api_cmd() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (!session_is_authenticated()) { s_http.send(401, "application/json", "{\"ok\":false}"); return; }
  String body = s_http.arg("plain");
  remote_handle_incoming(body.c_str());                    // сам ігнорує не-дозволене до auth
  s_http.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/mirror -> [<стан-екрана>, <статус-іконок>] як JSON-масив готових об'єктів.
// JS згодовує кожен елемент у той самий диспетчер, що й ws.onmessage -> ідентичний рендер.
static void handle_api_mirror() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (!session_is_authenticated()) { s_http.send(401, "application/json", "[]"); return; }
  String out = "[";
  bool first = true;
  if (s_last_state.length())  { out += s_last_state;  first = false; }
  if (s_last_status.length()) { if (!first) out += ","; out += s_last_status; }
  out += "]";
  s_http.send(200, "application/json", out);
}

// GET /api/log?since=N -> {"log":[нові рядки],"total":M}. Курсор N — total() з минулого
// поллу. ArduinoJson екранує лапки/слеші в рядках логу (SSID тощо). Окремо від /api/mirror,
// бо лог інкрементний (append), а стан/статус — ідемпотентні знімки (replace).
static void handle_api_log() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (!session_is_authenticated()) { s_http.send(401, "application/json", "{}"); return; }
  uint32_t since = s_http.hasArg("since") ? (uint32_t)strtoul(s_http.arg("since").c_str(), nullptr, 10) : 0;
  static char lines[16][LogRing::LINE];
  int n = log_ring().since(since, lines, 16);
  JsonDocument doc;
  doc["total"] = log_ring().total();
  JsonArray arr = doc["log"].to<JsonArray>();
  for (int i = 0; i < n; i++) arr.add(lines[i]);
  send_json(doc);
}

// Фаза 5: спектр 2.4ГГц для веб-візуалізації. RPD-скан nRF24 -> зайнятість 126 каналів
// (0..125 = 2400..2525 МГц). present=false якщо модуля нема. ~230мс на 6 проходів (блокуюче;
// UI має поллити нечасто). Спільна HSPI: nrf24_* самі переприв'язують MISO=38.
static bool s_nrf_diag = false;   // USB-шлях виставляє прапор diag (query недоступний у handle)
static void handle_nrf_spectrum() {
  JsonDocument doc;
  static uint8_t counts[NRF24_CHANNELS];
  for (int i = 0; i < NRF24_CHANNELS; i++) counts[i] = 0;
  // Калібрування: ?dwell=N мкс (час слухання/канал), ?diag=1 — ініт в обхід safe.
  if (s_http.hasArg("dwell")) nrf24_set_dwell_us((uint16_t)strtoul(s_http.arg("dwell").c_str(), nullptr, 10));
  bool diag = (s_http.hasArg("diag") && s_http.arg("diag") != "0") || s_nrf_diag;
  s_nrf_diag = false;
  uint32_t t0 = millis();
  bool present = diag ? nrf24_begin_diag() : nrf24_begin();
  if (present) for (int pass = 0; pass < 6; pass++) nrf24_scan_pass(counts, NRF24_CHANNELS);
  doc["present"] = present;
  doc["dwell_us"] = nrf24_get_dwell_us();
  doc["ms"] = millis() - t0;                        // час 6 проходів — для калібрування
  int active = 0; for (int i = 0; i < NRF24_CHANNELS; i++) if (counts[i]) active++;
  doc["active"] = active;
  JsonArray ch = doc["ch"].to<JsonArray>();
  for (int i = 0; i < NRF24_CHANNELS; i++) ch.add(counts[i]);
  send_json(doc);
}

// Суб-ГГц спектр (CC1101) для веб-візуалізації: один прохід RSSI по трьох
// перестроюваних вікнах (300-348/387-464/779-928 МГц). db[] — dBm на точку,
// mhz[] — частота точки (МГц) для підписів/меж вікон. Блокуюче ~170мс (поллити рідко).
static void handle_subghz_spectrum() {
  JsonDocument doc;
  bool present = cc1101_begin();
  doc["present"] = present;
  int n = subghz_scan_count();
  JsonArray db = doc["db"].to<JsonArray>();
  JsonArray mhz = doc["mhz"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    long khz = subghz_scan_khz(i);
    mhz.add((int)(khz / 1000));
    db.add(present ? cc1101_probe_rssi_dbm(khz) : -120);
  }
  send_json(doc);
}

// --- SD-сховище: статус (read-only) + монтування (POST, потребує PIN бо чіпає
//     залізо/файли). Дані сортуються по теках; persist робиться модулями на скан-complete. ---
static void handle_api_sd() {
  JsonDocument doc;
  doc["mounted"] = sd_mounted();
  doc["status"]  = sd_status_line();
  doc["bytes"]   = (double)sd_card_bytes();
  send_json(doc);
}
static void handle_sd_mount() {
  if (!session_is_authenticated()) { s_http.send(401, "text/plain", "auth"); return; }
  bool ok = sd_mount();
  JsonDocument doc; doc["mounted"] = ok; doc["status"] = sd_status_line();
  send_json(doc);
}
static void handle_sd_unmount() {
  if (!session_is_authenticated()) { s_http.send(401, "text/plain", "auth"); return; }
  sd_unmount();
  JsonDocument doc; doc["mounted"] = false; doc["status"] = sd_status_line();
  send_json(doc);
}
// Діагностика SD прямо з веб: запис+читання /selftest.txt. Повертає к-ть прочитаних
// байтів (>0 = запис і читання працюють). Дозволяє знайти причину «не маунтиться» без
// серійки. PIN-gated (чіпає файлову систему).
static void handle_sd_selftest() {
  if (!session_is_authenticated()) { s_http.send(401, "text/plain", "auth"); return; }
  JsonDocument doc;
  doc["mounted"] = sd_mounted();
  int rd = sd_selftest();   // <0 = помилка на певному кроці; >0 = прочитані байти
  doc["read"] = rd;
  doc["ok"] = (rd > 0);
  doc["status"] = sd_status_line();
  send_json(doc);
}
// --- Кольорові теми: GET список+поточна, POST зміна (PIN, персистентно) ---
// Archive за Flipper-еталоном: звіти з ДОМЕН-КАТЕГОРІЄЮ (аналог FlipperKeyType).
// Дає клієнту (веб/мобілка) згрупувати збережене за типом (subghz/rfid/wifi/network/
// detect/system), а не показувати плоский список. Read-only.
static void handle_api_archive() {
  JsonDocument doc;
  char names[REPORT_MAX][REPORT_NAME_MAX];
  int n = report_list(names, REPORT_MAX);
  JsonArray items = doc["items"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = items.add<JsonObject>();
    o["name"] = names[i];
    o["cat"]  = archive_category(names[i]);
  }
  doc["sd"] = sd_mounted();   // чи є персистентна копія на SD
  send_json(doc);
}

// Повна системна інфо для додатка: модулі, температура, LIVE-оцінка споживання за
// активними підсистемами (kernel/power_model). Дешево — БЕЗ апаратного probing радіо
// (nrf24/cc1101 активні лише в своїх апках, не під час перегляду sysinfo).
static void handle_api_sysinfo() {
  JsonDocument doc;
  doc["chip"]     = "ESP32-D0WDQ6-V3";
  doc["cpu_mhz"]  = (int)getCpuFrequencyMhz();
  doc["heap"]     = (int)ESP.getFreeHeap();
  doc["heap_min"] = (int)ESP.getMinFreeHeap();
  // ESP32-класик temperatureRead() віддає °F на цьому ядрі -> конвертуємо. Внутрішній
  // сенсор НЕкалібрований (±кілька °C) — для тренду/порогу охолодження, не абсолюту.
  float tf = temperatureRead();
  float tc = (tf - 32.0f) / 1.8f;
  doc["temp_c"]    = tc;
  doc["temp_appx"] = true;                       // приблизно (внутрішній сенсор)
  doc["uptime_s"]  = (int)(millis() / 1000);
  // Охолодження: рекомендований fan-duty + термостан (для UI/захисту від тротлінгу).
  extern int g_fan_duty;                              // фактичний driven duty фена (main.cpp)
  JsonObject cl = doc["cooling"].to<JsonObject>();
  cl["fan_duty"] = g_fan_duty;                        // реальний PWM, а не перерахунок
  cl["state"]    = cooling_state(tc, 65.0f, 75.0f);   // 0 OK / 1 WARN / 2 CRIT

  bool sta = WiFi.status() == WL_CONNECTED;
  bool ap  = s_ap_up;
  bool bt  = remote_active_mode() == MODE_BT;
  bool sd  = sd_mounted();
  JsonObject m = doc["modules"].to<JsonObject>();
  m["wifi_sta"] = sta; m["soft_ap"] = ap; m["bt"] = bt; m["sd"] = sd;

  PowerFlags f;
  f.wifi_sta = sta; f.soft_ap = ap; f.bt = bt; f.sd_write = false;
  f.backlight = settings_brightness();
  PowerBreakdown b = power_model_breakdown(f);
  JsonObject p = doc["power"].to<JsonObject>();
  p["ma"] = b.total; p["base"] = b.base; p["backlight"] = b.backlight;
  p["wifi"] = b.wifi; p["radios"] = b.radios; p["sd"] = b.sd; p["bt"] = b.bt;
  send_json(doc);
}

// GET /api/airscan -> скан сусідніх WiFi-мереж + ДЕТЕКТ EVIL-TWIN (той самий SSID на різних BSSID =
// клон точки доступу). Оборонна фіча фортеці. Блокуюче ~2-4с (як netscan). Дизраптить STA, але лвип
// відновлює. {aps:[{ssid,bssid,rssi,ch,enc,twin}], twins:N}.
static const char* enc_str(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    default:                        return "?";
  }
}
// Компактний запис AP — щоб звільнити важкі буфери WiFi-драйвера (scanDelete) ДО стрімінгу.
struct ApRec { char ssid[33]; char bssid[18]; int8_t rssi; uint8_t ch; uint8_t enc; uint8_t flags; };
// flags біти: 1=twin 2=open 4=weak
static void handle_api_airscan() {
  if (!require_auth()) return;
  static uint32_t s_last_air = 0;                  // rate-limit: часті скани вузлять радіо (STA-дизрапт)
  uint32_t nowm = millis();
  if (s_last_air && nowm - s_last_air < 7000) {    // <7с від попереднього -> не скануємо, тонкий throttle
    s_http.sendHeader("Access-Control-Allow-Origin", "*");
    s_http.send(200, "application/json", "{\"throttled\":true}");
    return;
  }
  s_last_air = nowm;
  // Активний SoftAP тримає радіо на своєму каналі -> scanNetworks у AP_STA бачить лише той канал
  // (класика ESP32). Тимчасово STA-only на час скану = повний свіп усіх каналів; STA лишається,
  // USB-контроль не залежить від WiFi. Після скану повертаємо попередній режим (SoftAP).
  wifi_mode_t prev_mode = WiFi.getMode();
  bool restore_ap = (prev_mode == WIFI_AP_STA || prev_mode == WIFI_AP);
  if (restore_ap) { WiFi.mode(WIFI_STA); delay(80); }
  int n = WiFi.scanNetworks(false, true);          // sync, show hidden (усі канали в STA-only)
  if (n < 0) n = 0;
  int twins = 0, opens = 0, weak = 0, hidden = 0;
  ApRec* recs = n > 0 ? (ApRec*)malloc(sizeof(ApRec) * n) : nullptr;
  int m = 0;
  if (recs) {
    for (int i = 0; i < n; i++) {
      String si = WiFi.SSID(i);
      bool twin = false;
      if (si.length())
        for (int j = 0; j < n; j++)
          if (j != i && WiFi.SSID(j) == si && WiFi.BSSIDstr(j) != WiFi.BSSIDstr(i)) { twin = true; break; }
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      bool isOpen = auth == WIFI_AUTH_OPEN, isWeak = auth == WIFI_AUTH_WEP;
      if (twin) twins++; if (isOpen) opens++; if (isWeak) weak++; if (!si.length()) hidden++;
      ApRec& r = recs[m++];
      snprintf(r.ssid, sizeof(r.ssid), "%s", si.length() ? si.c_str() : "<hidden>");
      snprintf(r.bssid, sizeof(r.bssid), "%s", WiFi.BSSIDstr(i).c_str());
      r.rssi = (int8_t)WiFi.RSSI(i); r.ch = (uint8_t)WiFi.channel(i); r.enc = (uint8_t)auth;
      r.flags = (twin ? 1 : 0) | (isOpen ? 2 : 0) | (isWeak ? 4 : 0);
    }
  }
  WiFi.scanDelete();                               // звільняємо буфери драйвера ПЕРЕД відправкою
  if (restore_ap) WiFi.mode(prev_mode);            // повертаємо SoftAP (AP_STA)

  out_begin();
  out_chunk("{\"aps\":[");
  String batch; batch.reserve(900);                // батчимо ~кілька AP на один sendContent
  for (int i = 0; i < m; i++) {
    JsonDocument o;
    o["ssid"] = recs[i].ssid; o["bssid"] = recs[i].bssid;
    o["rssi"] = recs[i].rssi; o["ch"] = recs[i].ch; o["enc"] = enc_str((wifi_auth_mode_t)recs[i].enc);
    if (recs[i].flags & 1) o["twin"] = true;
    if (recs[i].flags & 2) o["open"] = true;
    if (recs[i].flags & 4) o["weak"] = true;
    String frag; serializeJson(o, frag);           // serializeJson ЗАМІНЯЄ рядок -> у temp, потім append
    if (i) batch += ",";
    batch += frag;
    if (batch.length() >= 700) { out_chunk(batch); batch = ""; }
  }
  if (batch.length()) out_chunk(batch);
  if (recs) free(recs);
  String tail; tail.reserve(72);
  tail  = "],\"twins\":";  tail += twins / 2;
  tail += ",\"open\":";    tail += opens;
  tail += ",\"weak\":";    tail += weak;
  tail += ",\"hidden\":";  tail += hidden; tail += "}";
  out_chunk(tail);
  out_end();
}

// GET /api/modules -> автодетект усіх модулів обох плат (ESP + UNO) для графічного відображення.
// ESP: probe nRF24/CC1101 (begin+present), SD, wifi/ap/bt. UNO: лінк + CAP-реєстр (RC522/DHT/IR/pot/reed).
// Probe радіо — on-demand (не в циклі), тож дорогою реконфігурацією SPI можна знехтувати.
static void handle_api_modules() {
  if (!require_auth()) return;
  JsonDocument doc;
  JsonObject esp = doc["esp"].to<JsonObject>();
  esp["board"]    = "ESP32 T-Display";
  // Профіль конфігурації: вимкнений модуль НЕ пробуємо (швидше, без хибних SPI-транзакцій
  // на голій платі). Немає профілю -> profile_get=true -> автодетект як раніше.
  esp["nrf24"]    = profile_get("nrf24")  && nrf24_begin() && nrf24_present();
  esp["cc1101"]   = profile_get("cc1101") && cc1101_begin() && cc1101_present();
  esp["sd"]       = sd_mounted();
  esp["profiled"] = profile_loaded();
  esp["wifi_sta"] = WiFi.status() == WL_CONNECTED;
  esp["soft_ap"]  = s_ap_up;
  esp["bt"]       = remote_active_mode() == MODE_BT;

  JsonObject uno = doc["uno"].to<JsonObject>();
  bool uc = uno_link_connected();
  uno["board"]     = "Arduino UNO R3";
  uno["connected"] = uc;
  if (uc && uno_link_module_count() == 0) uno_link_send_scan();   // CAP-реєстр порожній -> попросити UNO переслати (наступний виклик матиме модулі)
  JsonArray mods = uno["modules"].to<JsonArray>();
  for (int i = 0; i < uno_link_module_count(); i++) {
    const UnoModule& m = uno_link_module(i);
    if (m.slot < 0) continue;
    JsonObject o = mods.add<JsonObject>();
    o["slot"] = m.slot; o["type"] = m.type; o["name"] = m.name; o["present"] = m.present;
    if (m.value[0]) o["value"] = m.value;
  }
  if (uc) {
    if (uno_link_free_ram() >= 0) { uno["free_ram"] = uno_link_free_ram(); uno["uptime_s"] = uno_link_uptime_s(); }
    const UnoSensors& s = uno_link_sensors();
    JsonObject sn = uno["sensors"].to<JsonObject>();
    sn["pot"] = s.pot; sn["reed"] = s.reed;
    const UnoEnv& e = uno_link_env();
    if (e.temp_x10) { sn["temp_c"] = e.temp_x10 / 10.0; sn["hum"] = e.hum_x10 / 10.0; }
  }
  send_json(doc);
}

// GET /api/netscan -> активний ARP-свіп підмережі + харвест кешу; список хостів {ip,mac,vendor,gw}
// для серверного інвентарю/детекту нового пристрою (кіберфортеця). Блокуюче ~1.5с (як спектри).
// Ті самі примітиви, що ArpScanApp (etharp_request/etharp_get_entry), але одноразово в хендлері.
static void handle_api_netscan() {
  if (!require_auth()) return;
  if (WiFi.status() != WL_CONNECTED) { s_http.send(200, "application/json", "{\"error\":\"offline\"}"); return; }
  IPAddress lip = WiFi.localIP(), sm = WiFi.subnetMask(), gw = WiFi.gatewayIP();
  uint32_t self_msb = net_make_ip(lip[0], lip[1], lip[2], lip[3]);
  uint32_t mask_msb = net_make_ip(sm[0], sm[1], sm[2], sm[3]);
  uint32_t gw_msb   = net_make_ip(gw[0], gw[1], gw[2], gw[3]);
  uint32_t first, last, cnt;
  if (!net_subnet_range(self_msb, mask_msb, &first, &last, &cnt)) { first = last = self_msb; }
  if (last - first > 253) last = first + 253;                 // ліміт /24
  // Розсилаємо who-has по всій підмережі (з дрібними yield, щоб не перевантажити стек).
  for (uint32_t ip = first; ip <= last; ip++) {
    IPAddress a((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    ip4_addr_t la; la.addr = (uint32_t)a;                      // IPAddress cast = lwip-порядок
    etharp_request(netif_default, &la);
    if ((ip & 15) == 0) delay(3);
  }
  delay(1200);                                                // дозбір відповідей
  out_begin();                                                // потік — не тримати всі хости в RAM
  out_chunk("{\"hosts\":[");
  int emitted = 0;
  String batch; batch.reserve(900);
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t* ipa = nullptr; struct netif* nif = nullptr; struct eth_addr* eth = nullptr;
    if (!etharp_get_entry((size_t)i, &ipa, &nif, &eth) || !ipa || !eth) continue;
    uint32_t lw = ipa->addr;
    uint32_t ipm = net_make_ip(lw & 0xFF, (lw >> 8) & 0xFF, (lw >> 16) & 0xFF, (lw >> 24) & 0xFF);
    if (ipm == self_msb) continue;
    uint8_t z = 0; for (int k = 0; k < 6; k++) z |= eth->addr[k];
    if (!z) continue;
    char ipb[20], macb[18];
    net_ip_to_str(ipm, ipb, sizeof(ipb));
    snprintf(macb, sizeof(macb), "%02X:%02X:%02X:%02X:%02X:%02X",
             eth->addr[0], eth->addr[1], eth->addr[2], eth->addr[3], eth->addr[4], eth->addr[5]);
    JsonDocument o;
    o["ip"] = ipb; o["mac"] = macb;
    const char* v = net_oui_vendor(eth->addr); o["vendor"] = v[0] ? v : "?";
    if (ipm == gw_msb) o["gw"] = true;
    String frag; serializeJson(o, frag);
    if (emitted++) batch += ",";
    batch += frag;
    if (batch.length() >= 700) { out_chunk(batch); batch = ""; }
  }
  if (batch.length()) out_chunk(batch);
  out_chunk("]}");
  out_end();
}

// GET /api/devscan?ip=a.b.c.d — глибокий аналіз ОДНОГО пристрою НА ПЛАТІ (автономно,
// без сервера): скан типових портів + мітки сервісів + категоризація + random-MAC.
static void handle_api_devscan() {
  if (!require_auth()) return;
  String ip = s_http.arg("ip");
  if (ip.length() < 7) { s_http.send(400, "application/json", "{\"error\":\"no-ip\"}"); return; }
  char* jb = (char*)malloc(768);                   // з купи (DRAM впритул) — не статичний буфер
  if (!jb) { s_http.send(500, "application/json", "{\"error\":\"nomem\"}"); return; }
  net_scan_device_json(ip.c_str(), jb, 768);
  s_http.send(200, "application/json", jb);
  free(jb);
}

// ── БЕЗПЕЧНИЙ RF-калібратор ──────────────────────────────────────────────────
// Оптимізуємо ЛИШЕ WiFi TX-потужність, і лише в межах enum драйвера (фізично не можна
// перевищити рейтинг чипа → спалити неможливо). Таймінги/PA інших модулів НЕ чіпаємо
// (нуль ризику розсинхрону). Apply завжди валідується виміром і авто-відкочується.
static const wifi_power_t TX_ENUM[] = {
  WIFI_POWER_2dBm, WIFI_POWER_5dBm, WIFI_POWER_7dBm, WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
  WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_17dBm, WIFI_POWER_18_5dBm, WIFI_POWER_19_5dBm };
static const float TX_DBM[] = { 2, 5, 7, 8.5f, 11, 13, 15, 17, 18.5f, 19.5f };
static const int   TX_N = 10;

static float cur_tx_dbm() { return (float)WiFi.getTxPower() / 4.0f; }  // getTxPower() у 0.25дБ

// індекс найближчого enum-рівня до бажаного dBm — КЛАМП у [2..19.5] (межа безпеки).
static int tx_index_for(float dbm) {
  int best = 0; float bd = 1e9f;
  for (int i = 0; i < TX_N; i++) { float d = fabsf(TX_DBM[i] - dbm); if (d < bd) { bd = d; best = i; } }
  return best;
}

// Рекомендація: найнижча потужність, що тримає запас лінка. RSSI(downlink) = індикатор
// дальнього запасу; ріжемо потужність ЛИШЕ коли сигнал сильний (є запас), слабкий -> максимум
// (надійність важливіша за нагрів). Консервативно, ніколи наосліп.
static int recommend_tx_index(int rssi, bool connected) {
  if (!connected) return tx_index_for(11);
  float target;
  if (rssi >= -50)      target = 5;
  else if (rssi >= -58) target = 8.5f;
  else if (rssi >= -65) target = 11;
  else if (rssi >= -70) target = 13;
  else if (rssi >= -75) target = 17;
  else                  target = 19.5f;
  return tx_index_for(target);
}

static void handle_api_rfstat() {   // аналіз модуля (auth — щоб не зливати телеметрію будь-кому)
  if (!require_auth()) return;
  JsonDocument doc;
  bool c = WiFi.status() == WL_CONNECTED;
  doc["connected"] = c;
  doc["rssi"]    = c ? WiFi.RSSI() : 0;
  doc["tx_dbm"]  = cur_tx_dbm();
  doc["channel"] = c ? WiFi.channel() : 0;
  doc["temp"]    = (temperatureRead() - 32.0f) / 1.8f;   // сенсор віддає °F -> °C (як у sysinfo)
  doc["temp_appx"] = true;                               // приблизно (внутрішній некалібрований)
  doc["mv"]      = (int)(battery_read_cached(1000) * 1000);
  doc["heap"]    = ESP.getFreeHeap();
  doc["ssid"]    = c ? WiFi.SSID() : String("");
  send_json(doc);
}

static void handle_api_calibrate() {   // dry-run: рекомендація БЕЗ зміни (auth)
  if (!require_auth()) return;
  bool c = WiFi.status() == WL_CONNECTED;
  int rssi = c ? WiFi.RSSI() : 0;
  float cur = cur_tx_dbm();
  float rec = TX_DBM[recommend_tx_index(rssi, c)];
  JsonDocument doc;
  doc["connected"] = c; doc["rssi"] = rssi;
  doc["cur_dbm"] = cur; doc["rec_dbm"] = rec;
  doc["dir"] = rec < cur - 0.1f ? "lower" : (rec > cur + 0.1f ? "raise" : "keep");
  send_json(doc);
}

// Core: застосувати цільову TX (клампом у enum) + валідація + авто-відкат. Спільне для REST/USB.
static void calibrate_apply_core(float want, JsonDocument& doc) {
  bool wasConn = WiFi.status() == WL_CONNECTED;
  int rssiBefore = wasConn ? WiFi.RSSI() : 0;
  int idx = tx_index_for(want);              // КЛАМП у безпечний enum — інше неможливе
  float oldDbm = cur_tx_dbm();
  WiFi.setTxPower(TX_ENUM[idx]);
  delay(2500);                               // дати лінку відреагувати
  bool nowConn = WiFi.status() == WL_CONNECTED;
  int rssiAfter = nowConn ? WiFi.RSSI() : 0;
  bool reverted = false;
  if (wasConn && !nowConn) {                 // лінк впав -> ВІДКАТ до старого
    WiFi.setTxPower(TX_ENUM[tx_index_for(oldDbm)]);
    delay(1200);
    reverted = true;
  }
  doc["applied"]     = !reverted;
  doc["reverted"]    = reverted;
  doc["old_dbm"]     = oldDbm;
  doc["new_dbm"]     = reverted ? oldDbm : TX_DBM[idx];
  doc["rssi_before"] = rssiBefore;
  doc["rssi_after"]  = reverted ? rssiBefore : rssiAfter;
  doc["connected"]   = WiFi.status() == WL_CONNECTED;
}

static void handle_api_calibrate_apply() {   // REST
  if (!require_auth()) return;
  bool wasConn = WiFi.status() == WL_CONNECTED;
  float want = s_http.hasArg("dbm") ? s_http.arg("dbm").toFloat()
                                    : TX_DBM[recommend_tx_index(wasConn ? WiFi.RSSI() : 0, wasConn)];
  JsonDocument doc; calibrate_apply_core(want, doc); send_json(doc);
}

// Версія API/прошивки — контракт сумісності для майбутнього мобільного додатка.
// Мобілка перевіряє api-версію перед використанням ендпоінтів; features[] оголошує,
// які підсистеми доступні (щоб UI не показував недоступне).
static void handle_api_version() {
  JsonDocument doc;
  doc["api"]       = "1.0";                       // семантична версія REST/WS-контракту
  doc["fw"]        = "esp32os-redteam";
  doc["build"]     = __DATE__ " " __TIME__;
  doc["transport"] = "wifi";                       // rest(:80) + ws(:81); ble ще нема
  doc["mirror"]    = true;                          // /api/mirror стрім екрана
  JsonArray f = doc["features"].to<JsonArray>();
  f.add("wifi"); f.add("bluetooth"); f.add("subghz"); f.add("rfid");
  f.add("sd"); f.add("reports"); f.add("theme"); f.add("scripts");
  send_json(doc);
}
static void handle_theme_get() {
  JsonDocument doc;
  doc["current"] = theme_current();
  JsonArray a = doc["themes"].to<JsonArray>();
  for (int i = 0; i < theme_count(); i++) a.add(theme_name(i));
  send_json(doc);
}
static void handle_theme_set() {
  if (!require_auth()) return;
  int i = s_http.hasArg("i") ? s_http.arg("i").toInt() : -1;
  if (i < 0 || i >= theme_count()) { s_http.send(400, "text/plain", "bad theme index"); return; }
  theme_set(i);
  JsonDocument doc; doc["current"] = theme_current(); doc["name"] = theme_name(i);
  send_json(doc);
}
// Selective read: перелік файлів у категорії-теці (?dir=/hosts). Дозволяє вебу/модулю
// тягнути ЛИШЕ потрібну бібліотеку. Read-only (без PIN — імена не чутливі).
static void handle_sd_list() {
  JsonDocument doc;
  String dirS = s_http.hasArg("dir") ? s_http.arg("dir") : String("/");   // тримаємо String живим (не dangling c_str)
  doc["dir"] = dirS; doc["mounted"] = sd_mounted();
  JsonArray files = doc["files"].to<JsonArray>();
  static char names[16][40];
  int n = sd_list(dirS.c_str(), names, 16);
  for (int i = 0; i < n; i++) files.add(names[i]);
  send_json(doc);
}
// Читання одного файлу з SD (chunked через streamFile — без великої RAM-аллокації).
static void handle_sd_get() {
  if (!sd_mounted()) { s_http.send(503, "text/plain", "// no SD mounted"); return; }
  String f = s_http.hasArg("f") ? s_http.arg("f") : "";
  if (!f.length() || f.indexOf("..") >= 0) { s_http.send(400, "text/plain", "bad path"); return; }
  sd_bus_select();   // переприв'язати MISO на SD (спільна HSPI-шина з nRF24)
  File file = SD.open(f);
  if (!file || file.isDirectory()) { if (file) file.close(); s_http.send(404, "text/plain", "not found"); return; }
  // Chunked-стрім через WebServer.sendContent (НЕ client.write після send — той пише у вже
  // фіналізовану відповідь -> 0 байт). setContentLength + send("") відкриває тіло, далі
  // дописуємо блоками по 512Б. sd_bus_select перед кожним читанням (спільна HSPI-шина).
  size_t sz = file.size();
  s_http.setContentLength(sz);
  s_http.send(200, "text/plain", "");
  uint8_t buf[512];
  while (file.available()) {
    sd_bus_select();
    int n = file.read(buf, sizeof(buf));
    if (n <= 0) break;
    s_http.sendContent((const char*)buf, (size_t)n);
  }
  file.close();
}

// --- Файл-менеджер .be (бібліотека /apps/<категорія>) ---
// Санітизація імені: лише [A-Za-z0-9_-.], закінчення .be, без слешів.
// Категорія (параметр "cat") — лише зі списку SCRIPT_CATEGORIES; без неї або з
// невідомою пишемо/шукаємо у "misc" (сумісність зі старими пласкими файлами).
static bool fs_safe_path(const String& n, String& path, const String& cat) {
  if (n.length() == 0 || n.length() > 32 || !n.endsWith(".be")) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((int)c) || c == '_' || c == '-' || c == '.')) return false;
  }
  if (n.indexOf("..") >= 0) return false;

  if (cat.length() > 0 && script_category_valid(cat.c_str()) && cat != "misc") {
    char buf[FS_MAX_PATH];
    if (!script_build_path(cat.c_str(), n.c_str(), buf, sizeof(buf))) return false;
    path = buf;
    return true;
  }
  // misc / без категорії: старий плаский шлях, але якщо файл лежить у теці misc —
  // віддаємо перевагу їй (нові збереження йдуть саме туди).
  String misc = "/apps/misc/" + n;
  path = (cat == "misc" || LittleFS.exists(misc)) ? misc : ("/apps/" + n);
  return true;
}

static void handle_fs_list() {
  if (!require_auth()) return;
  char paths[FS_MAX_SCRIPTS][FS_MAX_PATH];
  int n = fs_list_scripts(paths, FS_MAX_SCRIPTS);
  JsonDocument doc;
  JsonArray arr = doc["files"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const char* base = paths[i];
    for (const char* p = paths[i]; *p; p++) if (*p == '/') base = p + 1;
    char cat[16];
    if (!script_category_from_path(paths[i], cat, sizeof(cat))) snprintf(cat, sizeof(cat), "misc");
    JsonObject o = arr.add<JsonObject>();
    o["n"] = base;    // ім'я файлу
    o["c"] = cat;     // категорія (модуль)
  }
  // Список доступних категорій — щоб редактор не хардкодив їх у JS.
  JsonArray cats = doc["cats"].to<JsonArray>();
  for (int i = 0; i < SCRIPT_CAT_COUNT; i++) cats.add(SCRIPT_CATEGORIES[i]);
  send_json(doc);
}

static void handle_fs_get() {
  if (!require_auth()) return;
  String path;
  if (!fs_safe_path(s_http.arg("name"), path, s_http.arg("cat"))) { s_http.send(400, "text/plain", "bad name"); return; }
  String content;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (fs_read_file(path.c_str(), content)) s_http.send(200, "text/plain", content);
  else s_http.send(404, "text/plain", "");
}

static void handle_fs_save() {
  if (!require_auth()) return;
  String path;
  if (!fs_safe_path(s_http.arg("name"), path, s_http.arg("cat"))) { s_http.send(400, "text/plain", "bad name"); return; }
  String body = s_http.arg("plain");               // тіло POST
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (fs_write_file(path.c_str(), body.c_str())) s_http.send(200, "text/plain", "ok");
  else s_http.send(500, "text/plain", "write failed");
}

// --- Керування точкою доступу плати (SoftAP): назва + on/off ---
static void handle_ap_get() {
  JsonDocument doc;
  doc["ssid"]    = s_ap_ssid;
  doc["enabled"] = s_ap_enabled;
  doc["up"]      = s_ap_up;
  doc["channel"] = s_ap_chan;
  doc["clients"] = WiFi.softAPgetStationNum();
  send_json(doc);
}
static void handle_ap_config() {   // POST /api/ap/config, тіло=назва (text/plain; підтримує пробіли)
  if (!require_auth()) return;
  String ssid = s_http.arg("plain"); ssid.trim();
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (ssid.length() < 1 || ssid.length() > 32) { s_http.send(400, "application/json", "{\"ok\":false,\"err\":\"1..32 chars\"}"); return; }
  ap_set_ssid(ssid.c_str());
  s_http.send(200, "application/json", "{\"ok\":true}");
}
static void handle_ap_toggle() {   // POST /api/ap/toggle?on=0|1  (PIN)
  if (!require_auth()) return;
  bool on = s_http.arg("on") != "0";
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  ap_set_enabled(on);
  JsonDocument doc; doc["ok"] = true; doc["enabled"] = on; send_json(doc);
}

// --- Профіль конфігурації модулів (config-фіча): які модулі на якій платі ---
static void handle_config_profile_get() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  const char* j = profile_json();
  s_http.send(200, "application/json", (j && j[0]) ? j : "{}");
}
static void handle_config_profile_set() {
  if (!require_auth()) return;
  String body = s_http.arg("plain");
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (body.length() < 4 || body.length() >= 512) { s_http.send(400, "application/json", "{\"ok\":false,\"err\":\"bad size\"}"); return; }
  profile_set_json(body.c_str());
  module_power_apply();                             // застосувати живлення/піни згідно нового профілю
  s_http.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/modpower (auth) — стан живлення модулів + чи є спільні активні (для UI). enabled з профілю;
// hw_pwr — чи задано апаратний power-GPIO у профілі (реальне відключення рейки) vs логічне (high-Z).
static void handle_api_modpower() {
  if (!require_auth()) return;
  JsonDocument doc;
  const char* keys[] = {"nrf24", "sd", "cc1101"};
  const char* pwr[]  = {"nrf24_pwr", "sd_pwr", "cc1101_pwr"};
  bool anyBus = false;
  JsonArray arr = doc["modules"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    bool en = profile_get(keys[i]);
    if (en) anyBus = true;
    int g = profile_get_int(pwr[i], -1);
    JsonObject o = arr.add<JsonObject>();
    o["name"] = keys[i]; o["enabled"] = en;
    o["mode"] = g >= 0 ? "hw_switch" : "logical";   // hw_switch=реальна рейка; logical=high-Z+standby
    if (g >= 0) o["pwr_gpio"] = g;
  }
  doc["shared_bus"] = "SCK=2 MOSI=15";
  doc["shared_active"] = anyBus;                    // спільна шина драйвиться, поки хоч один SPI-модуль on
  doc["safe"] = g_hw_safe;
  send_json(doc);
}

// --- Прошивка ота_0 по WiFi через SD (фаза 2) ---
// Статус: чи є factory-updater, чи SD змонтована, розмір застейдженого образу.
static void handle_fw_status() {
  if (!require_auth()) return;
  JsonDocument doc;
  doc["factory"] = fw_factory_present();
  doc["sd"]      = sd_mounted();
  doc["staged"]  = fw_staged_size();
  send_json(doc);
}
// Стрім образу на SD (multipart-upload). Auth перевіряємо на старті стріму.
static bool s_fw_ok = false;
static void handle_fw_stage_done() {
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(s_fw_ok ? 200 : 500, "application/json",
              s_fw_ok ? "{\"ok\":true}" : "{\"ok\":false}");
}
static void handle_fw_stage_upload() {
  HTTPUpload& up = s_http.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_fw_ok = session_is_authenticated() && sd_mounted() && fw_stage_begin();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_fw_ok && !fw_stage_write(up.buf, up.currentSize)) s_fw_ok = false;
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_fw_ok) s_fw_ok = fw_stage_end(s_http.arg("md5").c_str());
    else fw_stage_abort();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    fw_stage_abort(); s_fw_ok = false;
  }
}
// Застосувати: ребут у factory-updater (той запише ота_0 з SD). Зв'язок обірветься.
static void handle_fw_apply() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (!fw_factory_present()) { s_http.send(409, "application/json", "{\"ok\":false,\"err\":\"no factory\"}"); return; }
  if (fw_staged_size() == 0) { s_http.send(409, "application/json", "{\"ok\":false,\"err\":\"no image\"}"); return; }
  s_http.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  fw_apply();   // set boot factory + restart
}

static void handle_fs_del() {
  if (!require_auth()) return;
  String path;
  if (!fs_safe_path(s_http.arg("name"), path, s_http.arg("cat"))) { s_http.send(400, "text/plain", "bad name"); return; }
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(fs_delete_file(path.c_str()) ? 200 : 404, "text/plain", "");
}

// --- Звіти (/reports/*): збереження знімків списків у LittleFS (потребує auth) ---
static void handle_reports_list() {
  if (!require_auth()) return;
  char names[REPORT_MAX][REPORT_NAME_MAX];
  int n = report_list(names, REPORT_MAX);
  JsonDocument doc;
  JsonArray arr = doc["reports"].to<JsonArray>();
  for (int i = 0; i < n; i++) arr.add(names[i]);
  send_json(doc);
}
static void handle_reports_get() {
  if (!require_auth()) return;
  String out;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (report_read(s_http.arg("f").c_str(), out)) s_http.send(200, "text/plain", out);
  else s_http.send(404, "text/plain", "");
}
static void handle_reports_save() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  if (report_save(s_http.arg("name").c_str(), s_http.arg("plain").c_str())) s_http.send(200, "text/plain", "ok");
  else s_http.send(500, "text/plain", "write failed");
}
static void handle_reports_del() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(report_delete(s_http.arg("f").c_str()) ? 200 : 404, "text/plain", "");
}
// Експорт УСІХ звітів одним завантаженням (для аналізу на ПК). ЧАНКОВАНО через
// sendContent — не будуємо великий String у RAM (20 звітів × кілька КБ поклали б
// купу при малому запасі). Кожен звіт із роздільником-заголовком.
static void handle_reports_export() {
  if (!require_auth()) return;
  char names[REPORT_MAX][REPORT_NAME_MAX];
  int n = report_list(names, REPORT_MAX);
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.sendHeader("Content-Disposition", "attachment; filename=esp32os-reports.txt");
  s_http.setContentLength(CONTENT_LENGTH_UNKNOWN);   // chunked transfer
  s_http.send(200, "text/plain", "");
  s_http.sendContent("# esp32-os reports export\n");
  for (int i = 0; i < n; i++) {
    String content;
    if (!report_read(names[i], content)) continue;
    s_http.sendContent("\n===== ");
    s_http.sendContent(names[i]);
    s_http.sendContent(" =====\n");
    s_http.sendContent(content);
    if (!content.endsWith("\n")) s_http.sendContent("\n");
  }
  s_http.sendContent("");   // завершити chunked-передачу
}

// --- Запуск Berry-скрипта без заливки (/script/run): тіло POST = код, АБО
//     ?name=&cat= -> завантажити збережений .be. Захоплює вивід print() і
//     повертає {ok, out, error}. Дозволяє тестувати скрипт із телефона/чату,
//     не роблячи uploadfs через дріт. Потребує PIN-авторизації.
static void handle_script_run() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  String code = s_http.arg("plain");             // тіло POST
  String src  = "web";
  if (code.length() == 0) {                      // без тіла -> запускаємо збережений за іменем
    String path;
    if (!fs_safe_path(s_http.arg("name"), path, s_http.arg("cat"))) { s_http.send(400, "text/plain", "need POST body or ?name="); return; }
    if (!fs_read_file(path.c_str(), code)) { s_http.send(404, "text/plain", "script not found"); return; }
    src = s_http.arg("name");
  }
  // Нативи (display_*/free_heap/wifi_*/ping/...) реєструються у VM НА ВИМОГУ —
  // зазвичай при запуску скрипта з меню (BerryScriptApp::init). Тут робимо те саме
  // (ідемпотентно), щоб bare-нативи резолвились і при запуску з веб/чату.
  native_api_install_hardware();
  native_api_register();
  static char out[1024];
  bool ok = berry_vm_run_capture(src.c_str(), code.c_str(), out, sizeof(out));
  JsonDocument doc;
  doc["ok"]  = ok;
  doc["out"] = out;                              // вивід print() (може бути порожній)
  if (!ok) doc["error"] = berry_vm_last_error();
  send_json(doc);
}

// --- Керування збереженими WiFi-мережами (/wifi/*): список, прапорець auto, forget.
//     Паролі НЕ віддаємо. Дозволяє вибрати, до яких мереж авто-підключатись. ---
static void handle_wifi_saved() {
  if (!require_auth()) return;
  JsonDocument doc;
  JsonArray arr = doc["nets"].to<JsonArray>();
  int n = wifi_sta_saved_count();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["i"]    = i;
    o["ssid"] = wifi_sta_saved_ssid(i);
    o["auto"] = wifi_sta_saved_auto(i) ? 1 : 0;
  }
  send_json(doc);
}
static void handle_wifi_auto() {
  if (!require_auth()) return;
  wifi_sta_set_saved_auto(s_http.arg("i").toInt(), s_http.arg("on") == "1");
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "text/plain", "ok");
}
static void handle_wifi_forget() {
  if (!require_auth()) return;
  wifi_sta_forget_network(s_http.arg("i").toInt());
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "text/plain", "ok");
}

// --- Пуш звітів у Telegram-бота (/reports/push): плата САМА POST-ить кожен звіт
//     на адресу бота (settings_bot_url). Телефон на SoftAP не дістає ПК із ботом,
//     а плата (у домашній мережі) — дістає. Бот дедуплікує через reports.db. ---
static void handle_reports_push() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  const char* bot = settings_bot_url();
  if (!bot[0]) { s_http.send(400, "text/plain", "bot url not set"); return; }
  char names[REPORT_MAX][REPORT_NAME_MAX];
  int n = report_list(names, REPORT_MAX);
  int pushed = 0, failed = 0;
  for (int i = 0; i < n; i++) {
    String content;
    if (!report_read(names[i], content)) continue;
    HTTPClient http; WiFiClient client;
    char url[160];
    snprintf(url, sizeof(url), "http://%s/ingest?name=%s", bot, names[i]);  // ім'я звіту — безпечні символи
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "text/plain");
      http.setTimeout(6000);
      int rc = http.POST(content);
      if (rc >= 200 && rc < 300) pushed++; else failed++;
      http.end();
    } else failed++;
  }
  JsonDocument doc;
  doc["pushed"] = pushed; doc["failed"] = failed; doc["bot"] = bot;
  send_json(doc);
}

// --- Системні дії (/sys/*): ребут (заміна зламаній RST) і пасивний режим ---
static void handle_sys_reboot() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "text/plain", "rebooting");
  sys_request_reboot();          // main зробить ESP.restart() після відправки відповіді
}
static void handle_sys_passive() {
  if (!require_auth()) return;
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "text/plain", "passive");
  sys_set_passive(true);         // екран off, плата працює далі
}

// --- WebSocket ---
static void on_ws_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      // Новий клієнт: сесія має пройти авторизацію заново (перше повідомлення — PIN).
      session_reset_auth();
      Serial.printf("[WIFI] ws client %u connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WIFI] ws client %u disconnected\n", num);
      break;
    case WStype_TEXT: {
      // payload не гарантовано '\0'-термінований — робимо копію
      char buf[PROTOCOL_MAX_INCOMING + 1];
      size_t n = length < PROTOCOL_MAX_INCOMING ? length : PROTOCOL_MAX_INCOMING;
      memcpy(buf, payload, n);
      buf[n] = '\0';
      RemoteAction a = remote_handle_incoming(buf);
      if (a == RA_DISCONNECT) {
        Serial.printf("[WIFI] ws client %u locked -> disconnect\n", num);
        s_ws.disconnect(num);
      }
      break;
    }
    default:
      break;  // BIN/PING/PONG/ERROR — ігноруємо
  }
}

// AP+STA СПІВІСНУВАННЯ: рідна точка SoftAP (OmniDiag-Setup, 192.168.4.1) тримається
// ПОСТІЙНО РАЗОМ зі STA-підключенням до домашньої мережі. Телефон може заходити або на
// свою точку плати, або через спільну WiFi на LAN-IP — обидва шляхи одночасно (як у
// попередніх версіях, до STA-only рішення).
// Радіо ESP32 одне -> SoftAP МУСИТЬ бути на тому ж каналі, що й STA (інакше контролер сам
// зсуне AP на STA-канал, а клієнт AP на мить рве STA). Тож канал AP = WiFi.channel() при
// активному STA, або 1, коли STA немає. Стежимо за зміною каналу STA (реконект на іншу
// мережу) і піднімаємо AP заново на новому каналі. Веб-сервер слухає обидва інтерфейси.
static void apply_ap_state() {
  // AP вимкнено користувачем -> опустити точку, лишитись у STA-only.
  if (!s_ap_enabled) {
    if (s_ap_up) {
      s_dns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      s_ap_up = false; s_ap_chan = 0;
      Serial.println("[WIFI] SoftAP OFF (STA-only)");
    }
    return;
  }
  bool sta_ok = (WiFi.status() == WL_CONNECTED);
  uint8_t want_chan = sta_ok ? (uint8_t)WiFi.channel() : 1;
  if (want_chan < 1 || want_chan > 13) want_chan = 1;

  if (s_ap_up && s_ap_chan == want_chan) return;   // вже піднятий на правильному каналі — no-op

  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);   // повернути AP+STA (могли бути в STA-only)
  // (Пере)піднімаємо SoftAP явним повним викликом (усі поля: ssid/pw/канал) — НЕ через
  // get/set-роундтріп (той губив authmode/password, урок з історії нижче).
  if (s_ap_up) s_dns.stop();
  WiFi.softAP(s_ap_ssid, s_ap_pw, want_chan);
  delay(50);
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());   // captive-portal: DNS-хайджек клієнтів AP
  s_ap_up   = true;
  s_ap_chan = want_chan;
  Serial.printf("[WIFI] AP+STA: SoftAP '%s' @192.168.4.1 ch%u (STA %s)\n",
                s_ap_ssid, want_chan, sta_ok ? WiFi.localIP().toString().c_str() : "down");
}

// ── USB-міст: повне керування платою по OTG-кабелю (serial REQ/RES) ──────────────
// Реюз тих самих REST-handler'ів через capture-режим. Повертає HTTP-подібний статус;
// пише JSON у out. 501 -> шлях не наш (fallback у serial_dispatch у main.cpp: ping/wifi/save).
static bool cap_run(void (*fn)(), char* out, size_t cap) {
  s_cap = true; s_cap_buf = "";
  fn();
  s_cap = false;
  // Guard: якщо відповідь не влазить у буфер -> НЕ віддаємо обрізаний (=невалідний) JSON,
  // а чесну помилку (інакше додаток отримав би побитий JSON). netscan/airscan великі.
  if (s_cap_buf.length() >= cap) {
    snprintf(out, cap, "{\"error\":\"too-big\",\"len\":%u}", (unsigned)s_cap_buf.length());
  } else {
    strlcpy(out, s_cap_buf.c_str(), cap);
  }
  s_cap_buf = "";
  return true;
}
int wifi_serial_dispatch(const char* method, const char* path, const char* body, char* out, size_t cap) {
  bool authed = session_is_authenticated();
  // ── контроль (POST) ──
  if (strncmp(path, "/api/login", 10) == 0) {
    // Консистентно з WiFi: перевіряємо ТОЙ САМИЙ статичний PIN. remote_prepare_usb_auth
    // піднімає session-PIN на лаунчері (без радіо), далі звіряємо client-PIN із тіла.
    remote_prepare_usb_auth();
    if (!try_hmac_login(body)) remote_handle_incoming(body);   // HMAC -> інакше PIN
    bool ok = session_is_authenticated();
    snprintf(out, cap, "{\"ok\":%s,\"usb\":true}", ok ? "true" : "false");
    return ok ? 200 : 401;
  }
  if (strncmp(path, "/api/challenge", 14) == 0) return cap_run(handle_api_challenge, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/enroll", 11) == 0)    return cap_run(handle_api_enroll, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/cmd", 8) == 0) {
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    remote_handle_incoming(body);
    snprintf(out, cap, "{\"ok\":true}");
    return 200;
  }
  // ── дзеркало екрана (auth) ──
  if (strncmp(path, "/api/mirror", 11) == 0) {
    if (!authed) { snprintf(out, cap, "[]"); return 401; }
    String s = "[";
    bool first = true;
    if (s_last_state.length())  { s += s_last_state;  first = false; }
    if (s_last_status.length()) { if (!first) s += ","; s += s_last_status; }
    s += "]";
    strlcpy(out, s.c_str(), cap);
    return 200;
  }
  // ── лог (auth): /api/log?since=N ──
  if (strncmp(path, "/api/log", 8) == 0) {
    if (!authed) { snprintf(out, cap, "{}"); return 401; }
    uint32_t since = 0;
    const char* q = strstr(path, "since=");
    if (q) since = (uint32_t)strtoul(q + 6, nullptr, 10);
    static char lines[16][LogRing::LINE];
    int n = log_ring().since(since, lines, 16);
    JsonDocument doc;
    doc["total"] = log_ring().total();
    JsonArray arr = doc["log"].to<JsonArray>();
    for (int i = 0; i < n; i++) arr.add(lines[i]);
    s_cap = true; s_cap_buf = ""; send_json(doc); s_cap = false;
    strlcpy(out, s_cap_buf.c_str(), cap); s_cap_buf = "";
    return 200;
  }
  // ── керування точкою доступу (SoftAP) по USB ──
  if (strncmp(path, "/api/ap/config", 14) == 0) {
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    if (body && strlen(body) >= 1 && strlen(body) <= 32) { ap_set_ssid(body); snprintf(out, cap, "{\"ok\":true}"); return 200; }
    snprintf(out, cap, "{\"ok\":false,\"err\":\"1..32\"}"); return 400;
  }
  if (strncmp(path, "/api/ap/toggle", 14) == 0) {
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    const char* q = strstr(path, "on="); bool on = !(q && q[3] == '0');
    ap_set_enabled(on); snprintf(out, cap, "{\"ok\":true,\"enabled\":%s}", on ? "true" : "false"); return 200;
  }
  if (strncmp(path, "/api/ap", 7) == 0) return cap_run(handle_ap_get, out, cap) ? 200 : 500;

  // ── профіль конфігурації (config-фіча) по USB ──
  if (strncmp(path, "/config/profile", 15) == 0) {
    if (method[0] == 'P') {                       // POST -> зберегти
      if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
      if (body && strlen(body) >= 4 && strlen(body) < 512) { profile_set_json(body); module_power_apply(); }
      snprintf(out, cap, "{\"ok\":true}"); return 200;
    }
    const char* j = profile_json();               // GET -> віддати
    strlcpy(out, (j && j[0]) ? j : "{}", cap); return 200;
  }
  // ── телеметрія / read (реюз handler'ів через capture) ──
  if (strncmp(path, "/api/status", 11) == 0)          return cap_run(handle_api_status, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/sysinfo", 12) == 0)         return cap_run(handle_api_sysinfo, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/battery", 12) == 0)         return cap_run(handle_api_battery, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/stats", 10) == 0)           return cap_run(handle_api_stats, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/info", 9) == 0)             return cap_run(handle_api_info, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/modules", 12) == 0)         return cap_run(handle_api_modules, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/shared", 11) == 0)          return cap_run(handle_api_shared, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/archive", 12) == 0)         return cap_run(handle_api_archive, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/sd", 7) == 0)               return cap_run(handle_api_sd, out, cap) ? 200 : 500;
  if (strncmp(path, "/api/nrf/spectrum", 17) == 0)    {
    const char* qd = strstr(path, "dwell="); if (qd) nrf24_set_dwell_us((uint16_t)strtoul(qd + 6, nullptr, 10));
    s_nrf_diag = strstr(path, "diag=1") != nullptr;   // прапор для handle (USB-шлях)
    return cap_run(handle_nrf_spectrum, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/subghz/spectrum", 20) == 0) return cap_run(handle_subghz_spectrum, out, cap) ? 200 : 500;
  if (strncmp(path, "/theme", 6) == 0)                return cap_run(handle_theme_get, out, cap) ? 200 : 500;
  if (strncmp(path, "/wifi/saved", 11) == 0)          return cap_run(handle_wifi_saved, out, cap) ? 200 : 500;
  if (strncmp(path, "/fs/list", 8) == 0)              { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_fs_list, out, cap) ? 200 : 500; }
  // важкі скани (auth) — після перевірки сесії
  if (strncmp(path, "/api/netscan", 12) == 0) { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_netscan, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/devscan", 12) == 0) {   // глибокий аналіз пристрою по USB: ?ip=a.b.c.d
    if (!authed) { snprintf(out, cap, "{}"); return 401; }
    char ipb[24] = {0}; const char* q = strstr(path, "ip=");
    if (q) { q += 3; int j = 0; while (*q && *q != '&' && j < 23) ipb[j++] = *q++; }
    net_scan_device_json(ipb, out, cap);
    return 200;
  }
  if (strncmp(path, "/api/rfstat", 11) == 0)  { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_rfstat, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/calibrate/apply", 20) == 0) {   // безпечний apply по USB (?dbm= або авто)
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    bool conn = WiFi.status() == WL_CONNECTED;
    const char* q = strstr(path, "dbm=");
    float want = q ? (float)atof(q + 4) : TX_DBM[recommend_tx_index(conn ? WiFi.RSSI() : 0, conn)];
    JsonDocument doc; calibrate_apply_core(want, doc);
    s_cap = true; s_cap_buf = ""; send_json(doc); s_cap = false;
    strlcpy(out, s_cap_buf.c_str(), cap); s_cap_buf = ""; return 200;
  }
  if (strncmp(path, "/api/calibrate", 14) == 0) { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_calibrate, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/airscan", 12) == 0) { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_airscan, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/modpower", 13) == 0) { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_modpower, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/pinscan", 12) == 0)  { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_pinscan, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/spitest", 12) == 0)  { if (!authed) { snprintf(out, cap, "{}"); return 401; }
    const char* q = strstr(path, "hz="); if (q) { uint32_t hz = strtoul(q + 3, nullptr, 10); if (hz >= 100000 && hz <= 16000000) nrf24_set_spi_hz(hz); }
    return cap_run(handle_api_spitest, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/unodiag", 12) == 0)  { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_unodiag, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/selftest", 13) == 0) { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_selftest, out, cap) ? 200 : 500; }
  if (strncmp(path, "/api/fantest", 12) == 0)  { if (!authed) { snprintf(out, cap, "{}"); return 401; } return cap_run(handle_api_fantest, out, cap) ? 200 : 500; }
  // ── low-RAM режим по USB: повний стоп/старт WiFi+веб-транспорту (звільняє heap) ──
  if (strncmp(path, "/sys/passive", 12) == 0) {
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    transport_wifi_stop();                              // WS+HTTP+DNS+AP+WiFi OFF; serial лишається
    snprintf(out, cap, "{\"ok\":true,\"wifi\":false,\"heap\":%u}", (unsigned)ESP.getFreeHeap()); return 200;
  }
  if (strncmp(path, "/sys/resume", 11) == 0) {
    if (!authed) { snprintf(out, cap, "{\"ok\":false,\"auth\":false}"); return 401; }
    transport_wifi_start();                             // підняти транспорт назад
    snprintf(out, cap, "{\"ok\":true,\"wifi\":true,\"heap\":%u}", (unsigned)ESP.getFreeHeap()); return 200;
  }
  return 501;   // не наш шлях -> fallback у serial_dispatch (ping / wifi/save)
}

void transport_wifi_start() {
  if (s_active) return;
  ensure_ap_password();
  auth_hmac_begin();                 // завантажити/згенерувати HMAC-seed (dual-auth з PIN)
  // AP_STA: SoftAP + збережене STA-підключення до домашньої мережі одночасно.
  // ІСТОРІЯ: раніше APSTA викликав "з'єднується й одразу відвалюється" — але справжньою
  // причиною був НЕ режим, а те, що AP-пароль тоді генерувався заново щобуту (телефон
  // тримав старий -> вічне "authenticating"; відтворено на ноутбуку). Тепер пароль
  // статичний, тож APSTA безпечний і дає головне: доступ до плати з домашньої мережі
  // (з інтернетом на телефоні) БЕЗ розриву при вмиканні Remote:WiFi.
  WiFi.mode(WIFI_AP_STA);   // AP+STA одночасно: рідна точка + домашня мережа. SoftAP піднімає
                            // apply_ap_state() (постійно, на каналі STA).

  // Живлення: телефон зазвичай за метр-два, повна потужність передавача (19.5дБм)
  // тут ні до чого.
  // ПРИМІТКА: раніше тут ще був esp_wifi_get_config/set_config для beacon_interval —
  // ПРИБРАНО: re-config AP "на льоту" через get/set міг губити authmode/password
  // (симптом на залізі: точка доступу переставала питати пароль і одразу відвалювала
  // клієнта). WPA2-пароль встановлюється лише один раз через WiFi.softAP() вище.
  WiFi.setTxPower(WIFI_POWER_11dBm);

  s_http.on("/", handle_root);
  s_http.on("/api/status", handle_api_status);
  s_http.on("/api/battery", handle_api_battery);
  s_http.on("/api/info", handle_api_info);
  s_http.on("/api/version", handle_api_version);   // контракт сумісності для мобільного додатка
  s_http.on("/api/sysinfo", handle_api_sysinfo);   // модулі + температура + live-power для додатка
  s_http.on("/api/hwsafe", handle_api_hwsafe);     // безпечний режим пінів (high-Z) — кнопка в додатку
  s_http.on("/api/pinscan", handle_api_pinscan);   // софт-«продзвонка» пінів модулів (стан + КЗ/мостики)
  s_http.on("/api/spitest", handle_api_spitest);   // активна продзвонка SPI: регістр-дамп nRF24 (жива шина?)
  s_http.on("/api/unodiag", handle_api_unodiag);   // повна діагностика Arduino UNO (лінк/пробінг/вітали/модулі/датчики/RFID)
  s_http.on("/api/selftest", handle_api_selftest); // апаратний self-test (heap/SPI/SD/піни/UNO/батарея/охолодження/WiFi)
  s_http.on("/api/fantest", handle_api_fantest);   // форс PWM фенів для перевірки обертання
  s_http.on("/api/netscan", handle_api_netscan);   // ARP-інвентар підмережі (сервер-вартовий/детект)
  s_http.on("/api/devscan", handle_api_devscan);   // глибокий аналіз одного пристрою (порти/категорія)
  s_http.on("/api/rfstat", handle_api_rfstat);            // live-аналіз модуля (RSSI/TX/канал/temp)
  s_http.on("/api/calibrate", handle_api_calibrate);      // рекомендація TX (dry-run)
  s_http.on("/api/calibrate/apply", HTTP_POST, handle_api_calibrate_apply);  // безпечний apply+відкат
  s_http.on("/api/modules", handle_api_modules);   // автодетект модулів ESP+UNO (графічне відображення)
  s_http.on("/api/airscan", handle_api_airscan);   // скан WiFi + evil-twin детект (оборона фортеці)
  s_http.on("/api/stats", handle_api_stats);
  s_http.on("/api/shared", handle_api_shared);
  s_http.on("/api/login", HTTP_POST, handle_api_login);  // REST-фолбек: логін PIN-ом або HMAC
  s_http.on("/api/challenge", handle_api_challenge);     // видати nonce для HMAC-логіну
  s_http.on("/api/enroll", handle_api_enroll);           // (auth) seed для провізії app/сервера
  s_http.on("/api/cmd", HTTP_POST, handle_api_cmd);       // REST-фолбек: керування (btn/idx/back/text)
  s_http.on("/api/mirror", handle_api_mirror);            // REST-фолбек: полінг стану екрана+статусу
  s_http.on("/api/log", handle_api_log);                  // REST-фолбек: полінг нових рядків логу
  s_http.on("/api/nrf/spectrum", handle_nrf_spectrum);   // Фаза 5: спектр 2.4ГГц (126 каналів)
  s_http.on("/api/subghz/spectrum", handle_subghz_spectrum);  // суб-ГГц спектр CC1101 (3 вікна)
  s_http.on("/api/sd", handle_api_sd);
  s_http.on("/sd/mount", HTTP_POST, handle_sd_mount);
  s_http.on("/sd/unmount", HTTP_POST, handle_sd_unmount);
  s_http.on("/sd/selftest", HTTP_POST, handle_sd_selftest);   // діагностика SD з веб (write+read)
  s_http.on("/theme", HTTP_GET, handle_theme_get);            // кольорові теми: список+поточна
  s_http.on("/theme", HTTP_POST, handle_theme_set);           // зміна теми (?i=N, PIN)
  s_http.on("/api/sd/list", handle_sd_list);
  s_http.on("/api/sd/get", handle_sd_get);
  s_http.on("/fs/list", handle_fs_list);
  s_http.on("/fs/get", handle_fs_get);
  s_http.on("/fs/save", HTTP_POST, handle_fs_save);
  s_http.on("/fs/del", handle_fs_del);
  s_http.on("/api/ap", handle_ap_get);
  s_http.on("/api/ap/config", HTTP_POST, handle_ap_config);
  s_http.on("/api/ap/toggle", HTTP_POST, handle_ap_toggle);
  s_http.on("/config/profile", HTTP_GET, handle_config_profile_get);
  s_http.on("/config/profile", HTTP_POST, handle_config_profile_set);
  s_http.on("/api/modpower", handle_api_modpower);  // стан живлення модулів (per-module, спільні піни)
  s_http.on("/fw/status", handle_fw_status);
  s_http.on("/fw/stage", HTTP_POST, handle_fw_stage_done, handle_fw_stage_upload);  // multipart -> SD
  s_http.on("/fw/apply", HTTP_POST, handle_fw_apply);
  s_http.on("/api/archive", handle_api_archive);         // Flipper-style Archive: звіти за категоріями
  s_http.on("/reports/list", handle_reports_list);
  s_http.on("/reports/get", handle_reports_get);
  s_http.on("/reports/save", HTTP_POST, handle_reports_save);
  s_http.on("/reports/del", handle_reports_del);
  s_http.on("/reports/export", handle_reports_export);
  s_http.on("/reports/push", HTTP_POST, handle_reports_push);
  s_http.on("/sys/reboot", HTTP_POST, handle_sys_reboot);
  s_http.on("/sys/passive", HTTP_POST, handle_sys_passive);
  s_http.on("/script/run", HTTP_POST, handle_script_run);
  s_http.on("/wifi/saved", handle_wifi_saved);
  s_http.on("/wifi/auto", HTTP_POST, handle_wifi_auto);
  s_http.on("/wifi/forget", HTTP_POST, handle_wifi_forget);
  s_http.onNotFound(handle_root);
  s_http.begin();

  s_ws.begin();
  s_ws.onEvent(on_ws_event);

  s_active = true;
  // Піднімаємо рідну точку SoftAP одразу, на каналі STA (якщо STA вже підключений).
  apply_ap_state();
  Serial.printf("[WIFI] transport up. STA=%s AP=%s ch%u\n",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "down",
                s_ap_up ? AP_SSID : "off", s_ap_chan);
}

void transport_wifi_stop() {
  if (!s_active) return;
  s_ws.disconnect();
  s_ws.close();
  s_http.stop();
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_active = false;
  s_ap_up = false;
  s_ap_chan = 0;
  s_ap_pw[0] = '\0';
  Serial.println("[WIFI] transport stopped");
}

void transport_wifi_loop() {
  if (!s_active) return;
  // Стежимо за каналом STA: реконект на іншу мережу -> переносимо AP на новий канал.
  // Троттл 3с, щоб не смикати радіо щотіку.
  static uint32_t last_net_eval = 0;
  uint32_t now = millis();
  if (now - last_net_eval > 3000) { last_net_eval = now; apply_ap_state(); }
  if (s_ap_up) s_dns.processNextRequest();   // captive-portal DNS лише коли AP активний
  s_http.handleClient();
  s_ws.loop();
}

bool transport_wifi_active() { return s_active; }

const char* transport_wifi_ap_password() { return s_active ? s_ap_pw : ""; }

void transport_wifi_broadcast(const char* json) {
  // Кеш s_last_state/s_last_status оновлюємо ЗАВЖДИ (дешеве присвоєння String) — щоб
  // USB-міст (/api/mirror по serial) бачив екран навіть коли WiFi-транспорт вимкнено.
  // WS-розсилку робимо лише коли транспорт активний.
  if (json) {
    if      (strncmp(json, "{\"status\"", 9) == 0) s_last_status = json;
    else if (strncmp(json, "{\"log\"", 6)    == 0) { /* лог: /api/log?since= */ }
    else                                           s_last_state = json;
  }
  if (s_active) s_ws.broadcastTXT(json);
}
