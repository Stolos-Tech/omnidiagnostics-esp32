#include "protocol.h"
#include <ArduinoJson.h>
#include <string.h>

// Копіює C-рядок у фіксований буфер із гарантованим '\0'.
static void copy_str(char* dst, size_t dst_size, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  size_t i = 0;
  for (; src[i] && i < dst_size - 1; i++) dst[i] = src[i];
  dst[i] = '\0';
}

// "S1".."S5" -> ButtonId. Повертає false, якщо рядок не такий.
static bool parse_button(const char* s, ButtonId& out) {
  if (!s || s[0] != 'S' || s[1] < '1' || s[1] > '5' || s[2] != '\0') return false;
  out = (ButtonId)(s[1] - '1');  // 'S1'->0 .. 'S5'->4 (== BTN_S1..BTN_S5)
  return true;
}

RemoteEvent protocol_parse_incoming(const char* json) {
  RemoteEvent ev;
  if (!json) { ev.type = REV_INVALID; return ev; }

  size_t len = strlen(json);
  if (len == 0 || len > PROTOCOL_MAX_INCOMING) { ev.type = REV_INVALID; return ev; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) { ev.type = REV_INVALID; return ev; }
  if (!doc.is<JsonObject>()) { ev.type = REV_INVALID; return ev; }

  // {"pin":"..."}
  if (doc["pin"].is<const char*>()) {
    ev.type = REV_PIN;
    copy_str(ev.pin, sizeof(ev.pin), doc["pin"].as<const char*>());
    return ev;
  }

  // {"btn":"S1".."S5"}
  if (doc["btn"].is<const char*>()) {
    ButtonId b;
    if (parse_button(doc["btn"].as<const char*>(), b)) {
      ev.type = REV_BUTTON;
      ev.btn = b;
    } else {
      ev.type = REV_INVALID;  // є ключ btn, але значення не S1..S5
    }
    return ev;
  }

  // {"text":"<field>","value":"..."}
  if (doc["text"].is<const char*>() && doc["value"].is<const char*>()) {
    ev.type = REV_TEXT;
    copy_str(ev.field, sizeof(ev.field), doc["text"].as<const char*>());
    copy_str(ev.value, sizeof(ev.value), doc["value"].as<const char*>());
    return ev;
  }

  // {"idx": N} — прямий вибір пункту меню за індексом
  if (doc["idx"].is<int>()) {
    ev.type = REV_INDEX;
    ev.index = doc["idx"].as<int>();
    return ev;
  }

  // {"back": true} — вихід у лаунчер
  if (doc["back"].is<bool>() && doc["back"].as<bool>()) {
    ev.type = REV_BACK;
    return ev;
  }

  // Валідний JSON-об'єкт, але не наша команда
  ev.type = REV_NONE;
  return ev;
}

std::string protocol_build_state(const char* page) {
  JsonDocument doc;
  doc["page"] = page;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string protocol_build_menu(const char* page, const char* const* items, int count, int cursor) {
  JsonDocument doc;
  doc["page"] = page;
  doc["cursor"] = cursor;
  JsonArray arr = doc["items"].to<JsonArray>();
  for (int i = 0; i < count; i++) arr.add(items[i]);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string protocol_build_page_kv(const char* page, const char* key, const char* value) {
  JsonDocument doc;
  doc["page"] = page;
  doc[key] = value;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string protocol_build_log(const char* const* lines, int n) {
  JsonDocument doc;
  JsonArray arr = doc["log"].to<JsonArray>();
  for (int i = 0; i < n; i++) arr.add(lines[i]);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string protocol_build_status(bool sta, bool ap, bool bt, int rssi, int batt_mv,
                                  bool usb, int pwr, int runtime_min) {
  JsonDocument doc;
  JsonObject s = doc["status"].to<JsonObject>();
  s["sta"]  = sta ? 1 : 0;
  s["ap"]   = ap ? 1 : 0;
  s["bt"]   = bt ? 1 : 0;
  s["rssi"] = rssi;
  s["mv"]   = batt_mv;
  s["usb"]  = usb ? 1 : 0;
  s["pwr"]  = pwr;
  s["rt"]   = runtime_min;
  std::string out;
  serializeJson(doc, out);
  return out;
}
