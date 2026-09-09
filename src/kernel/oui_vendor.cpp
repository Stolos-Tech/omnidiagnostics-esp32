#include "oui_vendor.h"
#include <stdio.h>

// Кураторний стартовий список OUI камер-спеціалістів. НЕ вичерпний — розширювати
// за живою базою (напр. wireshark manuf / IEEE OUI). Обрано бренди, що роблять
// переважно камери/DVR -> менше хибних спрацювань, ніж загальні IoT-вендори.
struct CameraOui { uint8_t b[3]; const char* vendor; };
static const CameraOui CAM_OUI[] = {
  {{0x44,0x19,0xB6}, "Hikvision"}, {{0xC0,0x56,0xE3}, "Hikvision"},
  {{0xBC,0xAD,0x28}, "Hikvision"}, {{0x4C,0xBD,0x8F}, "Hikvision"},
  {{0x28,0x57,0xBE}, "Hikvision"}, {{0x54,0xC4,0x15}, "Hikvision"},
  {{0x3C,0xEF,0x8C}, "Dahua"},     {{0x90,0x02,0xA9}, "Dahua"},
  {{0x14,0xA7,0x8B}, "Dahua"},     {{0x08,0xED,0xED}, "Dahua"},
  {{0xE4,0x24,0x6C}, "Dahua"},
  {{0xEC,0x71,0xDB}, "Reolink"},
  {{0x00,0x62,0x6E}, "Foscam"},    {{0xE8,0xAB,0xFA}, "Foscam"},
  {{0x2C,0xAA,0x8E}, "Wyze"},      {{0xD0,0x3F,0x27}, "Wyze"},
  {{0x9C,0x8E,0xCD}, "Amcrest"},
  {{0x00,0x40,0x8C}, "Axis"},      {{0xAC,0xCC,0x8E}, "Axis"},
  {{0x00,0x02,0xD1}, "Vivotek"},
};
static const int CAM_OUI_N = sizeof(CAM_OUI) / sizeof(CAM_OUI[0]);

const char* oui_camera_vendor(uint8_t b0, uint8_t b1, uint8_t b2) {
  for (int i = 0; i < CAM_OUI_N; i++)
    if (CAM_OUI[i].b[0] == b0 && CAM_OUI[i].b[1] == b1 && CAM_OUI[i].b[2] == b2)
      return CAM_OUI[i].vendor;
  return nullptr;
}

bool oui_parse3(const char* bssid, uint8_t* b0, uint8_t* b1, uint8_t* b2) {
  if (!bssid) return false;
  unsigned x0, x1, x2;
  if (sscanf(bssid, "%x:%x:%x", &x0, &x1, &x2) != 3) return false;
  if (x0 > 255 || x1 > 255 || x2 > 255) return false;
  if (b0) *b0 = (uint8_t)x0;
  if (b1) *b1 = (uint8_t)x1;
  if (b2) *b2 = (uint8_t)x2;
  return true;
}
