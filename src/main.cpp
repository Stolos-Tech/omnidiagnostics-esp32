// esp32-os — точка входу. setup()/loop() делегують у ядро:
// лаунчер + активний застосунок + єдина черга подій.
// Монолітний BatteryDiag_TDisplay.ino розібраний на модулі (Фаза 1):
//   drivers/display, drivers/buttons, drivers/battery_adc,
//   kernel/launcher(+ui), kernel/input_queue, apps_builtin/battery_diag.
#include <Arduino.h>
#include "drivers/display.h"
#include "drivers/buttons.h"
#include "drivers/battery_adc.h"
#include "drivers/filesystem.h"
#include "kernel/launcher.h"
#include "kernel/boot_state.h"
#include "remote/serial_command.h"
#include "kernel/launcher_ui.h"
#include "kernel/input_queue.h"
#include "kernel/power.h"
#include "kernel/script_path.h"
#include "kernel/berry_script_app.h"
#include "kernel/settings.h"
#include "kernel/power_state.h"
#include "kernel/intents.h"
#include "apps_builtin/battery_diag.h"
#include "apps_builtin/group_app.h"
#include "apps_builtin/remote_app.h"
#include "apps_builtin/wifi_manager.h"
#include "apps_builtin/system_info.h"
#include "apps_builtin/help_app.h"
#include "apps_builtin/net_info.h"
#include "apps_builtin/http_get.h"
#include "apps_builtin/net_scan.h"
#include "apps_builtin/mdns_browser.h"
#include "apps_builtin/ssdp_browser.h"
#include "apps_builtin/arp_scan.h"
#include "apps_builtin/settings_app.h"
#include "apps_builtin/dns_lookup.h"
#include "apps_builtin/http_post.h"
#include "apps_builtin/tcp_terminal.h"
#include "apps_builtin/clock_app.h"
#include "apps_builtin/wol_app.h"
#include "apps_builtin/wifi_analyzer.h"
#include "apps_builtin/channel_monitor.h"
#include "apps_builtin/wifi_sniffer.h"
#include "apps_builtin/bt_scan.h"
#include "apps_builtin/ble_scan.h"
#include "apps_builtin/tracker_detector.h"
#include "apps_builtin/camera_finder.h"
#include "apps_builtin/card_skimmer.h"
#include "apps_builtin/attacker_detect.h"
#include "apps_builtin/deauth_alert.h"
#include "apps_builtin/link_qual.h"
#include "apps_builtin/traceroute.h"
#include "apps_builtin/tls_cert.h"
#include "apps_builtin/http_sec.h"
#include "apps_builtin/captive_portal.h"
#include "apps_builtin/self_test.h"
#include "apps_builtin/power_menu.h"
#include "apps_builtin/uno_board.h"
#include "apps_builtin/em_field.h"
#include "apps_builtin/rf24_analyzer.h"
#include "apps_builtin/subghz_analyzer.h"
#include "apps_builtin/subghz_capture.h"
#include "apps_builtin/subghz_watch.h"
#include "apps_builtin/rf_audit.h"
#include "apps_builtin/rfid_access.h"
#include "apps_builtin/rfid_clone.h"
#include "drivers/rfid_acl.h"
#include "kernel/sys_ctl.h"
#include "drivers/uno_link.h"
#include "drivers/wifi_sta.h"
#include "drivers/sd_store.h"
#include "kernel/hw_safe.h"
#include "kernel/module_power.h"
#include "kernel/cooling.h"
#include "kernel/app_snapshot.h"   // авто-снапшот результату аналіз-апки на SD при виході
#include "drivers/mdns_service.h"
#include "drivers/report_store.h"
#include "drivers/power_guard_hw.h"
#include "kernel/power_guard.h"
#include "kernel/backlight_policy.h"
#include "kernel/serial_console.h"
#include "drivers/logger.h"
#include "kernel/log_ring.h"
#include "berry_bridge/berry_vm.h"
#include "remote/session.h"
#include "remote/auth_hmac.h"
#include "remote/protocol.h"
#include "remote/remote_control.h"
#include "remote/transport_wifi.h"   // wifi_serial_dispatch: повний USB-міст керування
#include "kernel/board_profile.h"    // config-фіча: профіль модулів
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

static LauncherModel launcher;
static BatteryDiagApp battery_app;
static RemoteApp remote_wifi_app;
static RemoteApp remote_bt_app;
static WifiManagerApp wifi_manager_app;
static SystemInfoApp system_info_app;
static HelpApp help_app;
static NetInfoApp net_info_app;
static HttpGetApp http_get_app;
static NetScanApp net_scan_app;
static MdnsBrowserApp mdns_app;
static SsdpBrowserApp ssdp_app;
static ArpScanApp arp_scan_app;
static DnsLookupApp dns_lookup_app;
static HttpPostApp http_post_app;
static TcpTerminalApp tcp_terminal_app;
static SettingsApp settings_app;
static ClockApp clock_app;
static WolApp wol_app;
static WifiAnalyzerApp wifi_analyzer_app;
static ChannelMonitorApp channel_monitor_app;
static WifiSnifferApp wifi_sniffer_app;
static BtScanApp bt_scan_app;
static BleScanApp ble_scan_app;
static TrackerDetectorApp tracker_detector_app;   // контрсурвейланс: детектор BLE-трекерів
static CameraFinderApp camera_finder_app;         // контрсурвейланс: WiFi-камери за OUI
static CardSkimmerApp card_skimmer_app;           // контрсурвейланс: BT card-скімери
static AttackerDetectApp attacker_detect_app;     // контрсурвейланс: pwnagotchi/evil-twin
static DeauthAlertApp deauth_alert_app;
static LinkQualApp link_qual_app;
static TracerouteApp traceroute_app;
static TlsCertApp tls_cert_app;
static HttpSecApp http_sec_app;
static CaptivePortalApp captive_portal_app;
static SelfTestApp self_test_app;
static PowerApp power_app;
static UnoBoardApp uno_board_app;   // копроцесор UNO R3 (датчики/RFID/мотори/реле по UART)
static EmFieldApp em_field_app;     // зонд ЕМ-поля на GPIO36 (мідна котушка + детектор)
static Rf24AnalyzerApp rf24_analyzer_app;  // 2.4GHz-аналізатор ефіру (nRF24L01+ PA/LNA)
// Охолодження: 2 фени через ULN2003 (низькосторонній ключ) на GPIO13, LEDC-PWM 25кГц (нечутно).
// Крива {старт 42°C, повні 60°C, гістерезис 3} — за температурою плати. IN1 драйвера -> GPIO13.
static const uint8_t FAN_PIN = 13;
static const uint8_t FAN_CH  = 4;                       // LEDC-канал (дисплей на 7 — не конфлікт)
static const CoolingCfg FAN_COOL = { 42.0f, 60.0f, 3.0f };
int g_fan_duty = 0;                                     // поточний PWM-duty (для sysinfo)
int g_fan_override = -1;                                // -1 = авто (крива); 0..255 = форс (тест фенів)
// Повернути LEDC-PWM на GPIO13 після того, як щось (напр. /api/pinscan) тимчасово зробило
// пін INPUT для продзвонки — інакше фен-вихід «відпадає» (ULN IN1 LED гасне, фен стоп).
void fan_pwm_reattach() { ledcAttachPin(FAN_PIN, FAN_CH); ledcWrite(FAN_CH, g_fan_duty); }

static SubghzAnalyzerApp subghz_analyzer_app;  // суб-ГГц аналізатор спектра (CC1101)
static SubghzCaptureApp  subghz_capture_app;   // суб-ГГц OOK-захоплення+декод (CC1101)
static SubghzWatchApp    subghz_watch_app;     // контрсурв: стійкі 433/868 передавачі (CC1101)
static RfAuditApp rf_audit_app;     // red-team RFID-аудит карт (RC522 на UNO)
static RfidAccessApp rfid_access_app;  // білий список UID -> GRANTED/DENIED
static RfidCloneApp rfid_clone_app;    // red-team: клон дата-блоків Mifare Classic (WRITE/WRES)

// Групи інструментів: споріднені під-апки під ОДНИМ пунктом меню (вибір режиму
// всередині). Під-апки лишаються глобальними інстансами (їх ще бачить Self Test),
// але в меню НЕ окремо — лише через свою групу. Об'єднано за задачею.
static GroupApp discover_app, web_app, netdiag_app, bt_group_app, detect_app, rfid_group, subghz_group;
// Нові доменні групи (Flipper-style консолідація): WiFi поглинає Air; Network містить
// вкладені під-групи Net Diag/Discover/Web + TCP/WoL; Hardware і System збирають standalone.
static GroupApp wifi_group, network_group, hardware_group, system_group;
static const GroupApp::Member DISCOVER_MEMBERS[] = {
  { &net_scan_app, "Net Scan" }, { &arp_scan_app, "ARP Scan" }, { &mdns_app, "mDNS" }, { &ssdp_app, "SSDP" } };
static const GroupApp::Member WEB_MEMBERS[] = {
  { &http_get_app, "HTTP GET" }, { &http_post_app, "HTTP POST" }, { &http_sec_app, "HTTP Sec" }, { &tls_cert_app, "TLS Cert" } };
static const GroupApp::Member NETDIAG_MEMBERS[] = {
  { &net_info_app, "Net Info" }, { &link_qual_app, "Link Qual" }, { &traceroute_app, "Traceroute" },
  { &dns_lookup_app, "DNS Lookup" }, { &captive_portal_app, "Captive" } };
// WiFi-домен: провізіонінг + усі 2.4ГГц-аналізатори (поглинув колишню групу Air).
static const GroupApp::Member WIFI_MEMBERS[] = {
  { &wifi_manager_app, "Setup" }, { &wifi_analyzer_app, "WiFi Analyzer" },
  { &channel_monitor_app, "Channel Monitor" }, { &wifi_sniffer_app, "WiFi Sniffer" },
  { &deauth_alert_app, "Deauth Alert" }, { &rf24_analyzer_app, "2.4G Analyzer" } };
// Network-домен: вкладені під-групи Net Diag/Discover/Web + прямі TCP/WoL.
static const GroupApp::Member NETWORK_MEMBERS[] = {
  { &netdiag_app, "Net Diag" }, { &discover_app, "Discover" }, { &web_app, "Web" },
  { &tcp_terminal_app, "TCP Term" }, { &wol_app, "Wake on LAN" } };
// Hardware-домен: датчики/проби/копроцесор.
static const GroupApp::Member HARDWARE_MEMBERS[] = {
  { &uno_board_app, "UNO Board" }, { &em_field_app, "EM Field" }, { &battery_app, "Battery Diag" } };
// System-домен: інфо/діагностика/годинник/живлення.
static const GroupApp::Member SYSTEM_MEMBERS[] = {
  { &system_info_app, "Info" }, { &help_app, "Help" }, { &self_test_app, "Self Test" },
  { &clock_app, "Clock" }, { &power_app, "Power" } };
static const GroupApp::Member BT_MEMBERS[] = {
  { &bt_scan_app, "BT Scan" }, { &ble_scan_app, "BLE Scan" } };
static const GroupApp::Member DETECT_MEMBERS[] = {
  { &tracker_detector_app, "Tracker Detect" }, { &camera_finder_app, "Camera Finder" },
  { &card_skimmer_app, "Card Skimmer" }, { &attacker_detect_app, "Attacker Detect" },
  { &subghz_watch_app, "Sub-GHz Watch" } };
static const GroupApp::Member RFID_MEMBERS[] = {
  { &rf_audit_app, "RF Audit" }, { &rfid_access_app, "RFID Access" },
  { &rfid_clone_app, "RFID Clone" } };
static const GroupApp::Member SUBGHZ_MEMBERS[] = {
  { &subghz_analyzer_app, "Sub-GHz Analyzer" }, { &subghz_capture_app, "Sub-GHz Capture" } };

// Вкомпільовані застосунки (порядок = початок меню). ~16 пунктів замість ~31:
// мережева діагностика згрупована за задачами, окремі — самодостатні інструменти.
// Flipper-style доменний топ-рівень (11 пунктів замість 21). Remote:WiFi/BT лишаються
// топ-рівнем (від них залежить автозапуск/досяжність). Решта — доменні групи; окремі
// інструменти живуть усередині свого домену (реверсивно: лише цей масив + members-таблиці).
static App* const builtin_apps[] = {
  &remote_wifi_app, &remote_bt_app,
  &wifi_group, &network_group, &bt_group_app, &subghz_group, &rfid_group, &detect_app,
  &hardware_group, &system_group, &settings_app
};

// Прогін Self Test: не-радіо мережеві модулі, slug звіту, ввід і тривалість (с).
static const SelfTestTarget self_test_targets[] = {
  { &net_info_app,       "net_info",   nullptr, nullptr,                 3 },
  { &net_scan_app,       "net_scan",   nullptr, nullptr,                 20 },
  { &arp_scan_app,       "arp_scan",   nullptr, nullptr,                 9 },
  { &mdns_app,           "mdns",       nullptr, nullptr,                 10 },
  { &ssdp_app,           "ssdp",       nullptr, nullptr,                 7 },
  { &dns_lookup_app,     "dns_lookup", "host",  "google.com",            5 },
  { &http_get_app,       "http_get",   "url",   "http://example.com",    6 },
  { &link_qual_app,      "link_qual",  nullptr, nullptr,                 6 },
  { &traceroute_app,     "traceroute", nullptr, nullptr,                 12 },
  { &http_sec_app,       "http_sec",   nullptr, nullptr,                 7 },
  { &captive_portal_app, "captive",    nullptr, nullptr,                 6 },
  // tls_cert ВИКЛЮЧЕНО з Self Test: TLS-handshake потребує ~45КБ heap, а під час
  // прогону вільно ~13КБ -> завжди "low RAM". Модуль лишається в меню Web для
  // ручного запуску. Щоб TLS реально працював — треба зменшити mbedTLS-буфери
  // (espidf/sdkconfig, важка перезбірка).
};

static const int BUILTIN_COUNT = sizeof(builtin_apps) / sizeof(builtin_apps[0]);
static const char* app_names[BUILTIN_COUNT + FS_MAX_SCRIPTS];  // для автозапуску в Settings

// Застосунки-скрипти з LittleFS (заповнюються при старті).
static BerryScriptApp script_apps[FS_MAX_SCRIPTS];
static int script_count = 0;

// Комбінований реєстр: спершу вкомпільовані, потім скрипти. Індекс == пункт меню.
static App* apps[BUILTIN_COUNT + FS_MAX_SCRIPTS];
static int app_count = 0;

static App* active_app = nullptr;  // nullptr = ми в лаунчері

// Таймаут watchdog: якщо loop() не «погодує» його стільки секунд — чип сам ресетиться.
// Прямо лікує «плата зависла й не перезавантажується без відключення дроту» (RST зламаний).
// 30с — з запасом над найдовшими легальними блокуючими операціями (sync-скан ~4с,
// captive-HTTP ~12с, TLS-handshake). Годуємо в кінці кожного loop().
#define WDT_TIMEOUT_SEC 30

// Диспетчер серійного мосту (task 6). Мінімальний набір для supervisor/proxy;
// повний REST-роутер транспорт може зареєструвати замість цього.
static int serial_dispatch(const char* method, const char* path,
                           const char* body, char* out, size_t cap) {
  (void)method;
  // Спершу — повний REST-міст (керування платою по USB: status/mirror/cmd/log/scans…).
  int r = wifi_serial_dispatch(method, path, body, out, cap);
  if (r != 501) return r;
  // Фолбек: службові шляхи, доступні навіть без WiFi-транспорту.
  if (strcmp(path, "/api/ping") == 0) {
    snprintf(out, cap, "{\"pong\":true,\"up\":%lu}", (unsigned long)(millis() / 1000));
    return 200;
  }
  if (strncmp(path, "/api/status", 11) == 0 || strncmp(path, "/api/sysinfo", 12) == 0) {
    snprintf(out, cap, "{\"heap\":%u,\"heap_min\":%u,\"up\":%lu,\"app\":\"%s\"}",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
             (unsigned long)(millis() / 1000), active_app ? active_app->name() : "launcher");
    return 200;
  }
  if (strncmp(path, "/api/cmd", 8) == 0 && strstr(body, "reboot")) {
    snprintf(out, cap, "{\"reboot\":true}");
    delay(60); ESP.restart();
    return 200;
  }
  // Headless WiFi-провізіонінг по USB: /wifi/save?ssid=..&pw=..
  if (strncmp(path, "/wifi/save", 10) == 0) {
    char ssid[33] = {0}, pw[65] = {0};
    const char* s = strstr(path, "ssid=");
    const char* w = strstr(path, "pw=");
    if (s) { s += 5; int i = 0; while (*s && *s != '&' && i < 32) ssid[i++] = *s++; }
    if (w) { w += 3; int i = 0; while (*w && *w != '&' && i < 64) pw[i++] = *w++; }
    if (ssid[0]) {
      wifi_sta_save_network(ssid, pw);
      wifi_sta_connect(ssid, pw);
      snprintf(out, cap, "{\"wifi\":\"%s\",\"saved\":true}", ssid);
      return 200;
    }
    snprintf(out, cap, "{\"error\":\"no ssid\"}");
    return 400;
  }
  return 501;
}

void setup() {
  Serial.begin(115200);
  // Watchdog піднімаємо ПЕРШИМ — щоб навіть зависання під час ініціалізації ловилось.
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true /* panic -> reset */);
  esp_task_wdt_add(NULL);   // підписати loopTask (на ньому виконуються і setup, і loop)
  setCpuFrequencyMhz(80);  // 240МГц не потрібні для UI/сенсорів; WiFi стабільний від 80 (не нижче)
  buttons_init();
  power_on_wake();  // якщо прокинулись кнопкою — дочекатись відпускання
  theme_begin();    // завантажити збережену кольорову тему ПЕРЕД першим кадром
  display_init();
  battery_adc_init();

  // Спільна HSPI-шина (nRF24 + SD): тримаємо ОБИДВА chip-select у idle-HIGH з завантаження,
  // щоб непроініціалізований модуль не «висів» на MISO і не псував читання іншому. Симптом
  // без цього: SD "AWOL or miswired", коли nRF24 фізично на шині, а його CSN(25) ще плаває
  // (2.4G Analyzer, що ставить CSN=HIGH, могли й не відкривати). Драйвери самі опускають свій
  // CS під час транзакції; тут лише гарантуємо безпечний старт.
  hw_safe_begin();        // завантажити прапорець safe-режиму з NVS (default true)
  hw_safe_apply_pins();   // виставити піни модулів: high-Z (safe) або idle-safe (active)
  ledcSetup(FAN_CH, 25000, 8); ledcAttachPin(FAN_PIN, FAN_CH); ledcWrite(FAN_CH, 0);   // фен: ULN2003 IN1 @GPIO13
  settings_begin();   // завантажити профіль (яскравість/сон/автозапуск) з NVS
  profile_begin();    // config-фіча: профіль модулів (NVS/SD) -> адаптація без перекомпіляції
  module_power_apply(); // per-module живлення/піни згідно профілю (усвідомлює спільні SCK/MOSI)
  rfid_acl_begin();   // білий список UID для RFID Access

  // Сесія віддаленого керування: апаратний RNG (esp_random) як джерело PIN.
  session_init();
  session_set_rng(esp_random);
  remote_wifi_app.configure(MODE_WIFI, "Remote: WiFi");
  remote_bt_app.configure(MODE_BT, "Remote: BT");
  wifi_sta_begin();  // завантажити збережені WiFi-креденшали (без авто-конекту)
  if (!g_hw_safe) uno_link_init();    // UART1 до UNO R3 (RX37/TX22 @9600) — у safe-режимі не жени TX22

  // Berry VM піднімається один раз при старті; heap-бюджет — у Serial-лог.
  size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  bool ok = berry_vm_init();
  size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[BERRY] vm_init=%d heap: %u -> %u (VM cost %d bytes)\n",
                ok, (unsigned)heap_before, (unsigned)heap_after,
                (int)heap_before - (int)heap_after);

  // LittleFS + сканування /apps/*.be
  bool fs_ok = fs_init();
  Serial.printf("[FS] mount=%d\n", fs_ok);
  if (fs_ok) fs_ensure_script_dirs();   // теки-категорії бібліотеки скриптів
  if (fs_ok) report_store_init();       // тека /reports для знімків сканів
  self_test_app.configure(self_test_targets, sizeof(self_test_targets) / sizeof(self_test_targets[0]));

  // Групи інструментів (об'єднані пункти меню з вибором режиму).
  discover_app.configure("Discover", "discover", DISCOVER_MEMBERS, 4);
  web_app.configure("Web", "web", WEB_MEMBERS, 4);
  netdiag_app.configure("Net Diag", "netdiag", NETDIAG_MEMBERS, 5);
  wifi_group.configure("WiFi", "wifi", WIFI_MEMBERS, 6);
  network_group.configure("Network", "network", NETWORK_MEMBERS, 5);
  hardware_group.configure("Hardware", "hardware", HARDWARE_MEMBERS, 3);
  system_group.configure("System", "system", SYSTEM_MEMBERS, 4);
  bt_group_app.configure("Bluetooth", "bluetooth", BT_MEMBERS, 2);
  detect_app.configure("Detect", "detect", DETECT_MEMBERS, 5);
  rfid_group.configure("RFID", "rfid", RFID_MEMBERS, 3);
  subghz_group.configure("Sub-GHz", "subghz", SUBGHZ_MEMBERS, 2);

  // Комбінований список: вкомпільовані...
  app_count = 0;
  for (int i = 0; i < BUILTIN_COUNT; i++) apps[app_count++] = builtin_apps[i];

  // ...потім скрипти з флешу
  char paths[FS_MAX_SCRIPTS][FS_MAX_PATH];
  int n = fs_ok ? fs_list_scripts(paths, FS_MAX_SCRIPTS) : 0;
  script_count = 0;
  for (int i = 0; i < n && script_count < FS_MAX_SCRIPTS; i++) {
    char nm[24];
    if (!script_name_from_path(paths[i], nm, sizeof(nm))) continue;
    script_apps[script_count].configure(nm, paths[i]);
    apps[app_count++] = &script_apps[script_count];
    script_count++;
    Serial.printf("[FS] script: %s -> %s\n", paths[i], nm);
  }

  // Побудова меню: усі застосунки (Power Off / Reboot / Passive тепер у застосунку
  // "Power", тож окремого хвостового пункту більше немає).
  launcher.clear();
  for (int i = 0; i < app_count; i++) {
    launcher.add_item(apps[i]->name());
    app_names[i] = apps[i]->name();
  }

  // Профіль: яскравість + список імен для автозапуску у Settings.
  display_set_brightness(settings_brightness());
  settings_app.configure(app_names, app_count);

  // Відновити останню позицію меню за ІМ'ЯМ застосунку (UX: повертаємось, де були).
  // Зберігання за ім'ям (а не індексом) робить це стійким до зміни складу/порядку
  // меню: стале ім'я просто не знайдеться -> курсор 0. Це прибрало потребу в ручному
  // MENU_LAYOUT_VERSION і закрило цілий клас багів (стале значення колись стартувало
  // не ту апку -> краш vQueueDelete під час setup).
  {
    const char* cn = settings_cursor_name();
    int cidx = 0;
    for (int i = 0; i < app_count; i++) if (strcmp(app_names[i], cn) == 0) { cidx = i; break; }
    launcher.set_cursor(cidx);
  }

  // Автозапуск застосунку за ІМ'ЯМ (якщо налаштований). Стале/невідоме ім'я -> no-op.
  // Task 8: crash-guard — не авто-відновлювати важкі/мережеві аналізатори і не
  // зациклюватись, якщо попередній автозапуск не стабілізувався (див. boot_state).
  boot_state_begin();
  {
    const char* an = settings_autostart_name();
    if (an[0] && boot_state_should_autostart(an))
      for (int i = 0; i < app_count; i++)
        if (strcmp(app_names[i], an) == 0) { active_app = apps[i]; active_app->init(); break; }
  }

  // Авто-підключення до відомої WiFi-мережі — без телефона/PIN. ПІСЛЯ автозапуску:
  // якщо той підняв Remote:WiFi (SoftAP), ensure_sta_mode() всередині коректно
  // перейде в APSTA (лишить AP живим). Пропускаємо, якщо активний BT — радіо спільне.
  if (remote_active_mode() != MODE_BT) wifi_sta_autoconnect();

  os_log("esp32-os ready: %d builtin + %d scripts", BUILTIN_COUNT, script_count);
  serial_command_begin(&serial_dispatch);   // task 6: USB-міст App->Server->Board
  auth_hmac_begin();                         // HMAC-seed доступний завжди (не лише в Remote:WiFi)
}

// Вихід з активного застосунку у лаунчер: даємо застосунку звільнити ресурси.
// Аналіз-апки (auto_snapshot) авто-логують свій результат у SD перед виходом
// (спільний хелпер — той самий шлях, що й для апок усередині груп).
static void exit_to_launcher() {
  if (active_app) {
    app_snapshot_if_enabled(active_app);
    active_app->on_exit();
    active_app = nullptr;
  }
}

// Обчислює поточний логічний стан екрана як JSON-рядок (для дзеркалення).
// У лаунчері — меню з курсором; у застосунку — його remote_state() або
// типовий {"page":"<name>"}.
static std::string current_state_json() {
  if (active_app) {
    std::string s = active_app->remote_state();
    if (!s.empty()) return s;
    return protocol_build_state(active_app->name());
  }
  const char* items[LauncherModel::MAX_ITEMS];
  int c = launcher.count();
  if (c > LauncherModel::MAX_ITEMS) c = LauncherModel::MAX_ITEMS;
  for (int i = 0; i < c; i++) items[i] = launcher.item_name(i);
  return protocol_build_menu("launcher", items, c, launcher.cursor());
}

void loop() {
  uint32_t now = millis();
  if (now > 15000) boot_state_mark_stable();   // task 8: пережили 15с -> старт «чистий»
  serial_command_poll();                       // task 6: обслуговування USB-мосту
  esp_task_wdt_reset();   // «годуємо» watchdog; якщо loop() зависне -> авто-ресет за WDT_TIMEOUT_SEC

  // Підсвітка/пасив: у пасивному режимі екран off; вихід — ДОВГЕ утримання правої
  // кнопки (не будь-який дотик), інші фізичні натиски проковтуються.
  static int bl_applied = -1;
  bool passive_at_start = sys_passive();

  buttons_poll(now);

  // Вихід із пасиву довгим утриманням правої кнопки (~700мс).
  bool right_lp = buttons_right_longpress();
  if (passive_at_start && right_lp) sys_set_passive(false);

  // Обслуговування активного транспорту (WiFi або BT) — неблокуюче, ПЕРЕД розбором
  // черги, щоб події від клієнта (після авторизації) потрапили в чергу цього ж циклу.
  remote_loop();
  serial_console_loop();   // службові команди по USB (провізіонінг WiFi без телефона)
  uno_link_loop();         // розбір телеметрії від UNO R3 (неблокуюче, кешує останнє)
  wifi_sta_loop();         // просування STA-стейту (в т.ч. відновлення після скану)

  // Фаза 4: неблокуюче скидання RAM-логу на SD обмеженим батчем, троттл 2с. No-op, якщо
  // SD не змонтована. Розмазує запис у часі -> main loop не морозиться на диску.
  static uint32_t last_logflush = 0;
  if (now - last_logflush > 2000) { last_logflush = now; sd_flush_log(16); }

  // Охолодження: кожні 2с рахуємо duty за температурою (гістерезис через g_fan_duty) і женемо PWM.
  static uint32_t last_fan = 0;
  if (now - last_fan > 2000) { last_fan = now;
    if (g_fan_override >= 0) {                          // ручний форс (тест фенів) — крива не перебиває
      g_fan_duty = g_fan_override;
    } else {
      float tc = (temperatureRead() - 32.0f) / 1.8f;
      g_fan_duty = cooling_fan_duty(tc, g_fan_duty, FAN_COOL);
    }
    ledcWrite(FAN_CH, g_fan_duty);
  }

  // Розбір черги подій: у лаунчері — навігація, у застосунку — делегування
  static uint32_t last_input_ms = 0;
  InputEvent ev;
  while (input_queue().pop(ev)) {
    last_input_ms = now;   // будь-який ввід скидає таймер сну
    // Універсальний "назад" (довге утримання лівої / футер Back). Ієрархічно:
    // спершу пропонуємо активному застосунку СПОЖИТИ назад (напр. група -> назад у
    // свій селектор, а не аж у лаунчер). Якщо не спожив — виходимо в лаунчер.
    if (ev.type == EV_BACK) {
      if (!(active_app && active_app->handle_back())) exit_to_launcher();
      continue;
    }
    // Текстовий ввід (з телефона) — лише активному застосунку через App::text().
    if (ev.type == EV_TEXT) {
      if (active_app) active_app->text(ev.field, ev.value);
      continue;
    }
    // Прямий вибір пункту за індексом (тап у веб): у застосунку — делегуємо,
    // у лаунчері — запускаємо застосунок під цим індексом (як S5 на ньому).
    if (ev.type == EV_INDEX) {
      if (active_app) {
        active_app->select_index(ev.index);
      } else {
        int idx = ev.index;
        if (idx >= 0 && idx < app_count) {
          settings_save_cursor_name(apps[idx]->name());
          launcher.set_cursor(idx);
          active_app = apps[idx];
          active_app->init();
        }
      }
      continue;
    }
    if (ev.type != EV_BUTTON) continue;
    // У пасивному режимі (екран off) ФІЗИЧНІ натиски проковтуємо — вихід лише
    // довгим утриманням правої (оброблено вище). Веб-кнопки не оновлюють
    // buttons_last_activity_ms(), тож керування з телефона працює й у пасиві.
    if (passive_at_start && buttons_last_activity_ms() == now) {
      continue;
    }
    if (active_app) {
      active_app->button(ev.btn);
    } else {
      switch (ev.btn) {
        case BTN_S1: launcher.move_prev(); break;
        case BTN_S2: launcher.move_next(); break;
        case BTN_S5: {
          int idx = launcher.select();
          if (idx >= 0 && idx < app_count) {
            settings_save_cursor_name(apps[idx]->name());   // запамʼятати позицію перед запуском
            active_app = apps[idx];
            active_app->init();
          }
        } break;
        default: break;
      }
    }
  }

  // Авто-згасання підсвітки ПРИБРАНО (за запитом): екран завжди на повній
  // яскравості, окрім свідомого пасивного режиму (екран off, мережа/логіка живі).
  {
    int bl_target = sys_passive() ? 0 : settings_brightness();
    if (bl_target != bl_applied) { display_set_brightness(bl_target); bl_applied = bl_target; }
  }

  if (active_app) {
    active_app->loop();
    if (active_app->wants_exit()) {
      exit_to_launcher();  // повернення в лаунчер (з on_exit-очищенням)
    }
  }

  // Намір "відкрити URL у HTTP GET" (ланцюжок Net Scan -> HTTP GET).
  if (!active_app) {
    char url[80];
    if (intent_take_url(url, sizeof(url))) {
      active_app = &http_get_app;
      active_app->init();
      active_app->text("url", url);   // отримання URL одразу запускає GET
    }
  }

  // Рендер + push спрайта (64КБ по SPI ~13мс) троттлимо до ~30fps: інші тіки
  // проходять швидко -> WS/HTTP обслуговуються частіше (краще під навантаженням,
  // менше CPU/живлення). У пасиві не малюємо взагалі (екран off).
  static uint32_t t_draw = 0;
  if (!sys_passive() && now - t_draw >= 33) {
    t_draw = now;
    if (active_app) active_app->draw();
    else            launcher_draw(launcher);
    display_push();
  }

  // Дзеркалення стану: надсилаємо поточний стан клієнту, коли він змінився
  // (порівнюємо JSON-рядок) або з'явився щойно автентифікований клієнт.
  // Перевірка троттлиться, щоб не будувати JSON щоцикл.
  static std::string last_sent;
  static bool prev_authed = false;
  static uint32_t t_state = 0;
  if ((remote_active_mode() != MODE_OFF || remote_usb_active()) && session_is_authenticated()) {
    if (now - t_state >= 200) {
      t_state = now;
      std::string s = current_state_json();
      if (s != last_sent || !prev_authed) {
        remote_broadcast(s.c_str());
        last_sent = s;
      }
      prev_authed = true;
    }
  } else {
    prev_authed = false;
    last_sent.clear();
  }

  // Brownout-захист: якщо напруга просіла КРИТИЧНО поки remote вже активний
  // (не лише при вмиканні — TX-сплески можуть посадити стабілізатор посеред
  // сесії), примусово гасимо радіо, доки плата сама не пішла в reset.
  static uint32_t t_pguard = 0;
  if (remote_active_mode() != MODE_OFF && now - t_pguard >= 3000) {
    t_pguard = now;
    if (!power_guard_ok()) {
      os_log("[PWR] %d mV < %d -> remote OFF (brownout guard)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
      remote_activate(MODE_OFF);
    }
  }

  // Анонс esp32os.local, коли зʼявилось STA-підключення (ідемпотентно, раз на 3с).
  // mDNS-анонс НЕЗАЛЕЖНИЙ від стелсу (окремий тумблер): стабільне <name>.local,
  // щоб не ганятися за DHCP-IP, навіть коли MAC/вендор приховані стелсом.
  static uint32_t t_mdns = 0;
  if (settings_mdns_enabled() && WiFi.status() == WL_CONNECTED
      && !mdns_service_running() && now - t_mdns > 3000) {
    t_mdns = now; mdns_service_start();
  }

  // Статус для іконок клієнта (WiFi/BT/батарея/живлення) — окреме повідомлення раз на 2с.
  static uint32_t t_status = 0;
  static float pv_ = 0; static uint32_t pt_ = 0; static float dvdt_ = 0;  // тренд напруги
  if ((remote_active_mode() != MODE_OFF || remote_usb_active()) && session_is_authenticated() && now - t_status >= 2000) {
    t_status = now;
    bool sta = WiFi.status() == WL_CONNECTED;
    bool ap  = remote_active_mode() == MODE_WIFI;
    bool bt  = remote_active_mode() == MODE_BT;
    int  rssi = sta ? WiFi.RSSI() : 0;
    float v = battery_read_cached(1000);   // спільний кеш зі status/guard/api -> один 24мс-замір на ~1с
    int  mv = (int)(v * 1000);
    if (pt_ > 0) { float dt = (now - pt_) / 1000.0f;
      if (dt > 0) { float inst = (v - pv_) / dt * 60000.0f; dvdt_ = dvdt_ * 0.7f + inst * 0.3f; } }
    pv_ = v; pt_ = now;
    PowerState ps = power_classify(mv, dvdt_);
    int rt = power_runtime_estimate_min(mv, dvdt_);
    std::string st = protocol_build_status(sta, ap, bt, rssi, mv, v > 4.25f, (int)ps, rt);
    remote_broadcast(st.c_str());
  }

  // Трансляція нових рядків логу у веб-консоль (раз на 1с).
  static uint32_t t_log = 0, log_cursor = 0;
  if ((remote_active_mode() != MODE_OFF || remote_usb_active()) && session_is_authenticated() && now - t_log >= 1000
      && log_ring().total() > log_cursor) {
    t_log = now;
    // Стейджинг лише на рядки одного тіку (1с) — ринг тримає повну історію окремо.
    // Раніше тут була копія на всю ємність ринга (CAP×LINE=4КБ статики дарма); за
    // секунду реально зʼявляється кілька рядків, тож 16 з великим запасом (лог-
    // трансляція стартує лише після авторизації клієнта, бут-логів <16).
    static const int LOG_BURST = 16;
    static char lines[LOG_BURST][LogRing::LINE];
    int ln = log_ring().since(log_cursor, lines, LOG_BURST);
    log_cursor = log_ring().total();
    const char* ptrs[LOG_BURST];
    for (int i = 0; i < ln; i++) ptrs[i] = lines[i];
    std::string lg = protocol_build_log(ptrs, ln);
    remote_broadcast(lg.c_str());
  }

  // Сон по бездіяльності (deep sleep). Не спимо, коли активний віддалений режим —
  // хтось може керувати з телефона. Sleep=Off (0) вимикає.
  int slp = settings_sleep_sec();
  if (slp > 0 && remote_active_mode() == MODE_OFF) {
    if (last_input_ms == 0) last_input_ms = now;
    if (now - last_input_ms > (uint32_t)slp * 1000) power_off();
  } else {
    last_input_ms = now;  // тримаємо таймер скинутим, поки сон недоречний
  }

  // Софт-ребут за запитом (веб /sys/reboot, serial `reboot`, меню Power) — заміна
  // зламаній фізичній RST-кнопці. Робимо в кінці тіку, щоб відповідь встигла піти.
  if (sys_take_reboot()) { delay(80); ESP.restart(); }

  delay(10);  // < 20мс, правило неблокуючого циклу
}
