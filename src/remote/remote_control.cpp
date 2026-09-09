#include <Preferences.h>
#include "remote_control.h"
#include "transport_wifi.h"
#include "transport_bt.h"
#include "../drivers/wifi_sta.h"
#include "../drivers/power_guard_hw.h"

#define CRED_NS "remcred"

static RemoteMode s_mode = MODE_OFF;

// PIN статичний між рестартами (зручність — на прохання користувача, свідомо
// слабший захист за рандомний-щоразу): перший запуск зберігає щойно згенерований,
// далі завжди повертає той самий, поки хтось явно не скине NVS-неймспейс "remcred".
static void apply_persistent_pin() {
  Preferences p;
  if (!p.begin(CRED_NS, false)) return;
  String saved = p.getString("pin", "");
  if (saved.length() == 6) {
    session_set_pin(saved.c_str());
  } else {
    p.putString("pin", session_pin());
  }
  p.end();
}

bool remote_activate(RemoteMode mode) {
  if (mode == s_mode) return true;

  // Вмикати радіо (не MODE_OFF) — лише коли напруга безпечна: сплеск струму
  // TX на маргінальному живленні викликає brownout-reset (див. power_guard).
  if (mode != MODE_OFF && !power_guard_ok()) return false;

  // 1) Зупинити поточний транспорт (взаємовиключність — завжди перед стартом нового).
  if (s_mode == MODE_WIFI) transport_wifi_stop();
  else if (s_mode == MODE_BT) transport_bt_stop();

  s_mode = mode;

  if (mode == MODE_OFF) {
    session_set_mode(MODE_OFF);
    return true;
  }

  // 2) Згенерувати PIN (чи взяти збережений) і скинути авторизацію під новий режим.
  session_set_mode(mode);
  apply_persistent_pin();

  // 3) Підняти потрібний транспорт. Перед BT ГАРАНТУЄМО, що WiFi-радіо вимкнене
  //    (в т.ч. STA-підключення WiFi-менеджера) — одне радіо за раз.
  if (mode == MODE_WIFI) {
    transport_wifi_start();
  } else if (mode == MODE_BT) {
    wifi_sta_stop();
    transport_bt_start();
  }
  return true;
}

RemoteMode remote_active_mode() { return s_mode; }

void remote_loop() {
  if (s_mode == MODE_WIFI) transport_wifi_loop();
  else if (s_mode == MODE_BT) transport_bt_loop();
}

// USB-міст активний -> дзеркалимо екран у transport_wifi-кеш (s_last_state), навіть
// коли WiFi-режим вимкнено. Так керування по USB бачить екран із БУДЬ-ЯКОГО стану плати.
static bool s_usb_active = false;
void remote_set_usb_active(bool a) { s_usb_active = a; }
bool remote_usb_active() { return s_usb_active; }

// USB-логін КОНСИСТЕНТНИЙ із WiFi: використовуємо ТОЙ САМИЙ статичний PIN. На лаунчері
// сесія в MODE_OFF (нема PIN) -> піднімаємо session-режим (БЕЗ радіо) і відновлюємо
// персистентний PIN, щоб session_authenticate(client_pin) звірявся з ним. Далі
// transport сам викликає session_authenticate зі своїм PIN.
void remote_prepare_usb_auth() {
  if (session_active_mode() == MODE_OFF) {
    session_set_mode(MODE_WIFI);   // лише session-стан (PIN/режим), радіо НЕ вмикаємо
    apply_persistent_pin();        // повернути статичний PIN із NVS
  }
  s_usb_active = true;             // дзеркалити екран у кеш навіть без WiFi-транспорту
}

void remote_broadcast(const char* json) {
  if (s_mode == MODE_WIFI || s_usb_active) transport_wifi_broadcast(json);
  if (s_mode == MODE_BT) transport_bt_broadcast(json);
}

void remote_restore_after_promiscuous(bool was_wifi) {
  if (!was_wifi) return;
  // Перезапуск транспорту повертає SoftAP+web самі (без ручного втручання з
  // плати), STA-підключення підхоплює авто-конект. Пропускаємо autoconnect, якщо
  // активний BT — радіо спільне.
  remote_activate(MODE_OFF);
  remote_activate(MODE_WIFI);
  if (remote_active_mode() != MODE_BT) wifi_sta_autoconnect();
}
