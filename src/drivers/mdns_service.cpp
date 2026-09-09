#include <Arduino.h>
#include <ESPmDNS.h>
#include "mdns_service.h"
#include "../kernel/settings.h"

static bool s_up = false;

void mdns_service_start() {
  if (s_up) return;
  if (MDNS.begin(settings_mdns_name())) {   // кастомне ім'я -> <name>.local
    MDNS.addService("http", "tcp", 80);     // веб-керування (коли активний remote WiFi)
    s_up = true;
    Serial.printf("[mDNS] %s.local up\n", settings_mdns_name());
  }
}

void mdns_service_stop() {
  if (!s_up) return;
  MDNS.end();
  s_up = false;
}

bool mdns_service_running() { return s_up; }
