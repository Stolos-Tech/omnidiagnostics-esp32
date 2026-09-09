#include "app_snapshot.h"
#include "app_interface.h"
#include "../drivers/sd_store.h"
#include <ArduinoJson.h>

void app_snapshot_if_enabled(App* app) {
  if (!app || !app->auto_snapshot() || !sd_mounted()) return;
  std::string js = app->remote_state();
  if (js.empty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, js) != DeserializationError::Ok) return;
  String body;
  JsonArray items = doc["items"].as<JsonArray>();
  for (JsonVariant it : items) {
    const char* s = it.as<const char*>();
    if (s) { body += s; body += '\n'; }
  }
  if (body.length()) sd_log_result(app->name(), body.c_str());
}
