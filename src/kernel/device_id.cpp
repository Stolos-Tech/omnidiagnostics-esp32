#include "device_id.h"
#include <string.h>

struct OuiEntry { uint8_t oui[3]; const char* vendor; char cat; };

// Курована таблиця найпоширеніших вендорів (2-4 префікси на бренд). Не повна —
// мета покрити типові домашні пристрої, не всі можливі. Реальні IEEE-OUI.
static const OuiEntry OUI_TABLE[] = {
  // Espressif (ESP32/ESP8266) — щоб упізнавати інші плати в мережі
  {{0x24,0x0A,0xC4},"Espressif",DC_IOT}, {{0x24,0x6F,0x28},"Espressif",DC_IOT},
  {{0x30,0xAE,0xA4},"Espressif",DC_IOT}, {{0x7C,0x9E,0xBD},"Espressif",DC_IOT},
  {{0xA4,0xCF,0x12},"Espressif",DC_IOT}, {{0xC4,0x4F,0x33},"Espressif",DC_IOT},
  {{0xEC,0xFA,0xBC},"Espressif",DC_IOT},
  // Apple
  {{0xA4,0x83,0xE7},"Apple",DC_PHONE}, {{0xF0,0x18,0x98},"Apple",DC_PHONE},
  {{0xF4,0xF1,0x5A},"Apple",DC_PHONE}, {{0x3C,0x15,0xC2},"Apple",DC_PHONE},
  {{0xAC,0xBC,0x32},"Apple",DC_PHONE}, {{0xDC,0x2B,0x2A},"Apple",DC_PHONE},
  {{0x68,0xAB,0xBC},"Apple",DC_PHONE},
  // Samsung
  {{0x34,0x23,0xBA},"Samsung",DC_PHONE}, {{0x5C,0x0A,0x5B},"Samsung",DC_PHONE},
  {{0x8C,0x77,0x12},"Samsung",DC_PHONE}, {{0xB4,0x07,0xF9},"Samsung",DC_PHONE},
  {{0xE8,0x50,0x8B},"Samsung",DC_PHONE},
  // Xiaomi
  {{0x0C,0x1D,0xAF},"Xiaomi",DC_PHONE}, {{0x28,0x6C,0x07},"Xiaomi",DC_PHONE},
  {{0x64,0x09,0x80},"Xiaomi",DC_PHONE}, {{0xF0,0xB4,0x29},"Xiaomi",DC_PHONE},
  {{0xFC,0x64,0xBA},"Xiaomi",DC_PHONE},
  // Google
  {{0x3C,0x5A,0xB4},"Google",DC_PHONE}, {{0x94,0xEB,0x2C},"Google",DC_PHONE},
  {{0xF4,0xF5,0xD8},"Google",DC_CAST}, {{0xDA,0xA1,0x19},"Google",DC_PHONE},
  // Huawei
  {{0x04,0xBD,0x70},"Huawei",DC_PHONE}, {{0x28,0x31,0x52},"Huawei",DC_PHONE},
  {{0x70,0x72,0x3C},"Huawei",DC_PHONE}, {{0xAC,0xE2,0x15},"Huawei",DC_PHONE},
  // OnePlus / Realme / Oppo
  {{0x94,0x65,0x2D},"OnePlus",DC_PHONE}, {{0xC0,0xEE,0xFB},"OnePlus",DC_PHONE},
  // TP-Link (роутери/IoT)
  {{0x50,0xC7,0xBF},"TP-Link",DC_ROUTER}, {{0xAC,0x84,0xC6},"TP-Link",DC_ROUTER},
  {{0xC0,0x06,0xC3},"TP-Link",DC_ROUTER}, {{0x50,0xD4,0xF7},"TP-Link",DC_ROUTER},
  // ASUS (роутери)
  {{0x2C,0x56,0xDC},"ASUS",DC_ROUTER}, {{0x38,0xD5,0x47},"ASUS",DC_ROUTER},
  {{0xAC,0x9E,0x17},"ASUS",DC_ROUTER}, {{0xBC,0xEE,0x7B},"ASUS",DC_ROUTER},
  {{0xD8,0x50,0xE6},"ASUS",DC_ROUTER}, {{0x50,0x46,0x5D},"ASUS",DC_ROUTER},
  // Netgear
  {{0x28,0xC6,0x8E},"Netgear",DC_ROUTER}, {{0xA0,0x63,0x91},"Netgear",DC_ROUTER},
  {{0xC0,0x3F,0x0E},"Netgear",DC_ROUTER}, {{0x9C,0x3D,0xCF},"Netgear",DC_ROUTER},
  // D-Link
  {{0x14,0xD6,0x4D},"D-Link",DC_ROUTER}, {{0x28,0x10,0x7B},"D-Link",DC_ROUTER},
  {{0xC0,0xA0,0xBB},"D-Link",DC_ROUTER},
  // MikroTik / Ubiquiti
  {{0x4C,0x5E,0x0C},"MikroTik",DC_ROUTER}, {{0x64,0xD1,0x54},"MikroTik",DC_ROUTER},
  {{0xCC,0x2D,0xE0},"MikroTik",DC_ROUTER}, {{0x24,0xA4,0x3C},"Ubiquiti",DC_ROUTER},
  {{0x78,0x8A,0x20},"Ubiquiti",DC_ROUTER}, {{0xF0,0x9F,0xC2},"Ubiquiti",DC_ROUTER},
  // Intel / Realtek (мережеві карти ПК)
  {{0x3C,0xA9,0xF4},"Intel",DC_COMPUTER}, {{0x7C,0x5C,0xF8},"Intel",DC_COMPUTER},
  {{0xA0,0x88,0x69},"Intel",DC_COMPUTER}, {{0xDC,0x53,0x60},"Intel",DC_COMPUTER},
  {{0x00,0xE0,0x4C},"Realtek",DC_COMPUTER},
  // Raspberry Pi
  {{0xB8,0x27,0xEB},"Raspberry Pi",DC_COMPUTER}, {{0xDC,0xA6,0x32},"Raspberry Pi",DC_COMPUTER},
  {{0xE4,0x5F,0x01},"Raspberry Pi",DC_COMPUTER}, {{0x2C,0xCF,0x67},"Raspberry Pi",DC_COMPUTER},
  // Amazon (Echo/Fire)
  {{0x34,0xD2,0x70},"Amazon",DC_CAST}, {{0x68,0x37,0xE9},"Amazon",DC_CAST},
  {{0xAC,0x63,0xBE},"Amazon",DC_CAST}, {{0xFC,0x65,0xDE},"Amazon",DC_CAST},
  // Sonos
  {{0x34,0x7E,0x5C},"Sonos",DC_CAST}, {{0x48,0xA6,0xB8},"Sonos",DC_CAST},
  {{0xB8,0xE9,0x37},"Sonos",DC_CAST},
  // Hikvision / Dahua (камери)
  {{0x44,0x19,0xB6},"Hikvision",DC_CAMERA}, {{0xC0,0x56,0xE3},"Hikvision",DC_CAMERA},
  {{0xBC,0xAD,0x28},"Hikvision",DC_CAMERA}, {{0x3C,0xEF,0x8C},"Dahua",DC_CAMERA},
  {{0x90,0x02,0xA9},"Dahua",DC_CAMERA},
  // HP / Canon / Epson (принтери)
  {{0x3C,0xD9,0x2B},"HP",DC_PRINTER}, {{0x94,0x57,0xA5},"HP",DC_PRINTER},
  {{0xD0,0xBF,0x9C},"HP",DC_PRINTER}, {{0x2C,0x9E,0xFC},"Canon",DC_PRINTER},
  {{0x88,0x87,0x17},"Canon",DC_PRINTER},
  // Tuya / Sonoff (розумний дім)
  {{0x18,0xFE,0x34},"Sonoff/Itead",DC_IOT}, {{0xD8,0xF1,0x5B},"Espressif/Sonoff",DC_IOT},
};
static const int OUI_COUNT = sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]);

static const OuiEntry* lookup(const uint8_t oui[3]) {
  if (!oui) return nullptr;
  for (int i = 0; i < OUI_COUNT; i++)
    if (memcmp(OUI_TABLE[i].oui, oui, 3) == 0) return &OUI_TABLE[i];
  return nullptr;
}

const char* net_oui_vendor(const uint8_t oui[3]) {
  const OuiEntry* e = lookup(oui);
  return e ? e->vendor : "";
}

char net_oui_category(const uint8_t oui[3]) {
  const OuiEntry* e = lookup(oui);
  return e ? e->cat : (char)DC_UNKNOWN;
}

static bool has_port(const uint16_t* p, int n, uint16_t want) {
  for (int i = 0; i < n; i++) if (p[i] == want) return true;
  return false;
}

bool net_mac_is_local(const uint8_t mac[6]) {
  return mac && (mac[0] & 0x02) != 0;
}

const char* net_device_type(bool is_gateway, char cat, bool mac_local,
                            const uint16_t* ports, int n) {
  // 1) За портами (найнадійніше — сервіс однозначно вказує роль).
  if (has_port(ports, n, 9100) || has_port(ports, n, 515) || has_port(ports, n, 631))
    return "Printer";
  if (has_port(ports, n, 554) || has_port(ports, n, 37777) || has_port(ports, n, 8000))
    return "IP Camera";
  if (has_port(ports, n, 62078)) return "iPhone/iPad";
  if (has_port(ports, n, 8009)) return "Chromecast/TV";
  if (has_port(ports, n, 32400)) return "Plex Server";
  if (has_port(ports, n, 445) || has_port(ports, n, 3389) || has_port(ports, n, 139))
    return "PC (Windows)";
  if (has_port(ports, n, 548) || has_port(ports, n, 5000)) return "NAS/Server";
  if (has_port(ports, n, 1883) || has_port(ports, n, 8883)) return "IoT/MQTT";

  // 2) Шлюз (навіть якщо тільки 80/443 — це роутер).
  if (is_gateway) return "Router/Gateway";

  // 3) За категорією вендора.
  switch (cat) {
    case DC_ROUTER:  return "Router/AP";
    case DC_PHONE:   return "Phone/Tablet";
    case DC_COMPUTER:return "Computer";
    case DC_IOT:     return "IoT/MCU";
    case DC_PRINTER: return "Printer";
    case DC_CAMERA:  return "IP Camera";
    case DC_CAST:    return "Speaker/Cast";
    default: break;
  }

  // 4) Рандомізований (локально-адмін.) MAC при невідомому вендорі — майже
  //    завжди телефон/планшет (iOS/Android рандомізують MAC на приватних мережах).
  if (mac_local) return "Phone (rand MAC)";

  // 5) Загальні натяки за портами.
  if (has_port(ports, n, 22)) return "Linux/Unix host";
  if (has_port(ports, n, 80) || has_port(ports, n, 443)) return "Web device";
  return "Unknown";
}
