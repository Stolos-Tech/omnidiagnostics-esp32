// Спільний протокол віддаленого керування — ОДНАКОВИЙ для WiFi і Bluetooth.
// Тільки JSON, плоский і мінімальний. Парсинг/побудова живуть ТУТ, один раз;
// транспорти (transport_wifi/transport_bt) лише передають байти рядків.
//
// Вхідні повідомлення від клієнта (телефона):
//   {"pin":"123456"}                  — автентифікація (перше повідомлення)
//   {"btn":"S1".."S5"}                — натиск кнопки
//   {"text":"<field>","value":"..."}  — ввід тексту (напр. WiFi-пароль)
//
// Вихідний стан від плати:
//   {"page":"<name>", ...довільні поля залежно від сторінки}
//
// Чистий шар (std::string, без <Arduino.h>) — компілюється і в native-тестах.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include "../kernel/app_interface.h"

enum RemoteEventType : uint8_t {
  REV_NONE = 0,   // порожній/незрозумілий, але валідний JSON
  REV_PIN,        // {"pin":"..."}
  REV_BUTTON,     // {"btn":"S1".."S5"}
  REV_TEXT,       // {"text":"field","value":"..."}
  REV_INDEX,      // {"idx": N} — прямий вибір пункту меню/списку за індексом
  REV_BACK,       // {"back": true} — вихід у лаунчер (веб-аналог довгого утримання)
  REV_INVALID     // не JSON / побитий / порожній рядок / задовгий
};

struct RemoteEvent {
  RemoteEventType type = REV_NONE;
  ButtonId btn = BTN_S1;   // валідне при REV_BUTTON
  int index = 0;           // при REV_INDEX — індекс пункту
  char pin[8] = "";        // при REV_PIN (до 6 цифр + '\0')
  char field[24] = "";     // при REV_TEXT — назва поля
  char value[96] = "";     // при REV_TEXT — значення (напр. пароль)
};

// Максимальний прийнятний розмір вхідного повідомлення (захист від задовгих).
static const size_t PROTOCOL_MAX_INCOMING = 256;

// Розбирає вхідний JSON-рядок у подію. Побитий/порожній/задовгий -> REV_INVALID.
RemoteEvent protocol_parse_incoming(const char* json);

// Будує стан сторінки: {"page":"<page>"}.
std::string protocol_build_state(const char* page);

// Будує стан меню/списку: {"page":"<page>","cursor":<n>,"items":["..",..]}.
std::string protocol_build_menu(const char* page, const char* const* items, int count, int cursor);

// Будує стан сторінки з одним рядковим полем: {"page":"<page>","<key>":"<value>"}.
std::string protocol_build_page_kv(const char* page, const char* key, const char* value);

// Будує повідомлення статусу пристрою для іконок у клієнті:
//   {"status":{"sta":1,"ap":0,"bt":0,"rssi":-55,"mv":3850,"usb":1,"pwr":2,"rt":-1}}
//   sta — STA підключено, ap — SoftAP активний, bt — BT-режим, rssi — рівень STA,
//   mv — напруга батареї (мВ), usb — живлення/зарядка USB, pwr — стан живлення
//   (PowerState 0..3), rt — оцінка часу роботи (хв, -1 якщо н/д).
std::string protocol_build_status(bool sta, bool ap, bool bt, int rssi, int batt_mv,
                                  bool usb, int pwr, int runtime_min);

// Будує пакет рядків логу для веб-консолі: {"log":["...","..."]}.
std::string protocol_build_log(const char* const* lines, int n);
