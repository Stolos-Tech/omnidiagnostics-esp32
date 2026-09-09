#include "boot_state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static const char* NS = "boot";
static const int CRASH_LIMIT = 2;      // 2 підряд нестабільних старти -> safe mode
static int  s_crash = 0;
static bool s_marked = false;

// «Важкі» апки: рвуть мережу / вантажать радіо / роблять активний аналіз. Їх НІКОЛИ
// не авто-відновлюємо після ребута — саме вони спричиняють crash-loop під час аналізу.
static bool is_heavy(const char* n) {
  static const char* H[] = {
    "BLE", "Skimmer", "Tracker", "Sniffer", "Channel Monitor", "Deauth",
    "2.4G", "rf24", "Net Scan", "Camera", "Attacker", "Sub-GHz", "Captive", "Analyzer"
  };
  for (const char* h : H) if (strstr(n, h)) return true;
  return false;
}

void boot_state_begin() {
  Preferences p; p.begin(NS, false);
  String pend = p.getString("pending", "");
  s_crash = p.getInt("crash", 0);
  if (pend.length()) {                 // минулий boot почав апку, але stable не настав -> краш
    s_crash++; p.putInt("crash", s_crash);
    Serial.printf("[boot] prev app '%s' did not stabilize; crash=%d\n", pend.c_str(), s_crash);
  }
  p.end();
}

bool boot_state_should_autostart(const char* name) {
  if (!name || !name[0]) return false;
  Preferences p; p.begin(NS, false);
  if (is_heavy(name)) {
    Serial.printf("[boot] '%s' is heavy -> no auto-resume (task 8)\n", name);
    p.remove("pending"); p.end();
    return false;
  }
  if (s_crash >= CRASH_LIMIT) {
    Serial.println("[boot] crash-loop detected -> SAFE MODE (launcher)");
    p.remove("pending"); p.putInt("crash", 0); p.end();
    return false;
  }
  p.putString("pending", name);        // armed: очищується у mark_stable
  p.end();
  return true;
}

void boot_state_mark_stable() {
  if (s_marked) return;
  s_marked = true;
  Preferences p; p.begin(NS, false);
  p.remove("pending");
  p.putInt("crash", 0);
  p.end();
  s_crash = 0;
}

int boot_state_crash_count() { return s_crash; }
