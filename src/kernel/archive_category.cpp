#include "archive_category.h"
#include <string.h>
#include <ctype.h>

// true, якщо s починається з префікса p (регістронезалежно).
static bool starts(const char* s, const char* p) {
  for (; *p; s++, p++) {
    if (!*s) return false;
    if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return false;
  }
  return true;
}

// true, якщо будь-який з префіксів збігається.
static bool any(const char* s, const char* const* prefixes, int n) {
  for (int i = 0; i < n; i++) if (starts(s, prefixes[i])) return true;
  return false;
}

const char* archive_category(const char* slug) {
  if (!slug || !slug[0]) return "misc";

  // Пропустити провідний монотонний лічильник "NNNNNN_" (нове іменування звітів),
  // щоб категоризація йшла за slug, а не за цифрами.
  const char* us = strchr(slug, '_');
  if (us && us > slug) {
    bool alldig = true;
    for (const char* d = slug; d < us; d++) if (!isdigit((unsigned char)*d)) { alldig = false; break; }
    if (alldig) slug = us + 1;
  }

  static const char* SUBGHZ[] = { "subghz" };
  static const char* RFID[]   = { "rfaudit", "rfid", "nfc", "rf_" };
  static const char* WIFI[]   = { "wifi", "channel", "deauth", "2.4g", "air", "sniffer" };
  static const char* BT[]     = { "bt_", "ble", "bluetooth" };
  static const char* DETECT[] = { "tracker", "camera", "skimmer", "attacker", "detect" };
  static const char* NETWORK[]= { "net", "arp", "dns", "http", "mdns", "ssdp",
                                  "tcp", "trace", "link", "captive", "wol", "scan", "tls" };
  static const char* SYSTEM[] = { "sys", "self", "battery", "em_", "clock", "uno" };

  if (any(slug, SUBGHZ,  1)) return "subghz";
  if (any(slug, RFID,    4)) return "rfid";
  if (any(slug, WIFI,    6)) return "wifi";
  if (any(slug, BT,      3)) return "bluetooth";
  if (any(slug, DETECT,  5)) return "detect";
  if (any(slug, NETWORK, 13)) return "network";
  if (any(slug, SYSTEM,  6)) return "system";
  return "misc";
}
