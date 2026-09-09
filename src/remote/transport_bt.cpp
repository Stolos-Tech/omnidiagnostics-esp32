#include <Arduino.h>
#include "transport_bt.h"
#include "remote_dispatch.h"
#include "protocol.h"
#include "session.h"
#include "line_assembler.h"

// Діагностичний перемикач: коли визначено ESP32OS_DISABLE_BT, BluetoothSerial
// НЕ лінкується (Bluedroid не потрапляє в образ) — щоб перевірити, чи він глушить
// WiFi-скан. Функції транспорту стають заглушками.
#ifdef ESP32OS_DISABLE_BT

void transport_bt_start() { Serial.println("[BT] DISABLED (build flag)"); }
void transport_bt_stop() {}
void transport_bt_loop() {}
bool transport_bt_active() { return false; }
void transport_bt_broadcast(const char*) {}

#else

// BluetoothSerial доступний лише коли в збірці ввімкнено класичний BT.
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth Classic вимкнено у sdkconfig — потрібен BluetoothSerial"
#endif
#include <BluetoothSerial.h>

#define BT_DEVICE_NAME "OmniDiag"

static BluetoothSerial s_bt;
static LineAssembler   s_asm;
static bool s_active = false;

// Callback з'єднання/розриву SPP — новий клієнт скидає авторизацію (перше
// повідомлення має бути PIN, як і в WiFi).
static void bt_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
  (void)param;
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    s_asm.reset();
    session_reset_auth();
    Serial.println("[BT] client connected");
  } else if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println("[BT] client disconnected");
  }
}

void transport_bt_start() {
  if (s_active) return;
  s_asm.reset();
  s_bt.register_callback(bt_callback);
  s_bt.begin(BT_DEVICE_NAME);   // SPP-сервер
  s_active = true;
  Serial.printf("[BT] SPP up as '%s'\n", BT_DEVICE_NAME);
}

void transport_bt_stop() {
  if (!s_active) return;
  s_bt.end();
  s_active = false;
  Serial.println("[BT] transport stopped");
}

void transport_bt_loop() {
  if (!s_active) return;
  char line[PROTOCOL_MAX_INCOMING + 1];
  while (s_bt.available()) {
    char c = (char)s_bt.read();
    if (s_asm.feed(c, line, sizeof(line))) {
      RemoteAction a = remote_handle_incoming(line);
      if (a == RA_DISCONNECT) {
        Serial.println("[BT] client locked -> disconnect");
        s_bt.disconnect();
      }
    }
  }
}

bool transport_bt_active() { return s_active; }

void transport_bt_broadcast(const char* json) {
  if (!s_active || !s_bt.hasClient()) return;
  s_bt.println(json);  // рядок + '\n' — клієнт-термінал бачить цілі повідомлення
}

#endif  // ESP32OS_DISABLE_BT
