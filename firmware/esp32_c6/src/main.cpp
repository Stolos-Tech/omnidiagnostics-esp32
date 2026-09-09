// ESP32-C6 ANALYSIS NODE — headless. Керується головною ESP32 T-Display по UART.
// Крутить радіо-аналіз (WiFi sniffer/monitor/deauth, nRF24, CC1101, BLE, 802.15.4),
// віддає результати рядками. Дисплея нема — уся видача в лінк.
//
// Каркас (плата ще не куплена): диспетчер + STAT-heartbeat готові; конкретні
// аналізатори — стаби з чіткими TODO для порту з головного проєкту.
#include <Arduino.h>
#include <WiFi.h>
#include "c6_proto.h"
#include "analysis/wifi_analysis.h"
#include "analysis/z15_scan.h"

#ifndef C6_LINK_BAUD
#define C6_LINK_BAUD 921600
#endif

// UART до HOST. Serial0 (native USB CDC) — лог/прошивка; Serial1 — лінк на GPIO.
// Пінaути уточнити під розводку: TX=GPIO4, RX=GPIO5 (приклад, змінити при монтажі).
static const int LINK_TX = 4, LINK_RX = 5;
#define LINK Serial1

static char s_line[256];
static size_t s_len = 0;
static uint32_t s_last_stat = 0;
static int s_active = C6_NONE;     // поточний аналіз (STOP -> C6_NONE звільняє радіо)
static long s_arg = 0;
static int s_hop = 0;

static void send(const char* s) { LINK.print(s); }

static void run_stat() {
  char o[64];
  int t10 = (int)(temperatureRead() * 10);        // вбудований датчик C6
  c6_build_stat(ESP.getFreeHeap(), t10, millis() / 1000, o, sizeof(o));
  send(o);
}

static void dispatch(C6Cmd cmd) {
  switch (cmd) {
    case C6_STOP:
      s_active = C6_NONE;
      wifi_analysis_stop();                        // повертає радіо у безпечний стан
      send("EVT stopped -\n");
      break;
    case C6_STAT:   run_stat(); break;
    case C6_SCAN_WIFI: wifi_scan_once(send); break; // одноразовий скан, не тримає режим
    case C6_MON:    s_active = cmd; wifi_monitor_begin(s_hop ? -1 : (int)s_arg); break;
    case C6_SNIFF:  s_active = cmd; wifi_sniff_begin((int)s_arg); break;
    case C6_DEAUTH: s_active = cmd; wifi_deauth_watch_begin(); break;
    case C6_RF24:   s_active = cmd; send("EVT todo rf24\n"); break;   // TODO: порт rf24_analyzer
    case C6_SUBGHZ: s_active = cmd; send("EVT todo subghz\n"); break; // TODO: порт CC1101
    case C6_BLE:    s_active = cmd; send("EVT todo ble\n"); break;    // TODO: порт ble_scan (C6 BLE5)
    case C6_Z15:    s_active = cmd; z15_scan_begin(); break;          // нова фіча (802.15.4)
    default: break;
  }
}

static void poll_link() {
  while (LINK.available()) {
    int ch = LINK.read();
    if (ch == '\n' || ch == '\r') {
      if (s_len) { s_line[s_len] = 0; long a; int h;
        C6Cmd c = c6_parse_cmd(s_line, &a, &h);
        if (c != C6_NONE) { s_arg = a; s_hop = h; dispatch(c); }
        s_len = 0;
      }
    } else if (s_len < sizeof(s_line) - 1) s_line[s_len++] = (char)ch;
  }
}

// Активний режим виробляє дані щоцикл (results стрімляться через колбек send).
static void pump_active() {
  switch (s_active) {
    case C6_MON:    wifi_monitor_pump(send); break;
    case C6_SNIFF:  wifi_sniff_pump(send); break;
    case C6_DEAUTH: wifi_deauth_pump(send); break;
    case C6_Z15:    z15_scan_pump(send); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);                 // USB CDC: лог
  LINK.begin(C6_LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  WiFi.mode(WIFI_MODE_NULL);            // радіо піднімають самі аналізатори за потреби
  Serial.println("[c6] analysis node ready");
  send("EVT boot c6-analysis\n");
}

void loop() {
  poll_link();
  pump_active();
  if (millis() - s_last_stat > 3000) { s_last_stat = millis(); run_stat(); }
}
