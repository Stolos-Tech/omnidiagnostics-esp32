// Чиста логіка класифікації BLE-трекерів за сигнатурою реклами (без апаратних
// включень -> native-тести). Для контрсурвейлансу: виявити чужий AirTag/Tile/
// SmartTag, що «їде» з тобою.
#pragma once
#include <stdint.h>

enum TrackerType : uint8_t {
  TRK_NONE = 0,
  TRK_APPLE_FINDMY,   // Apple Find My / AirTag (company 0x004C, тип payload 0x12)
  TRK_TILE,           // Tile (service 0xFEED / company 0x0157)
  TRK_SAMSUNG,        // Samsung SmartTag (service 0xFD5A)
  TRK_COUNT
};

// Людська назва типу трекера.
const char* tracker_name(TrackerType t);

// Класифікує BLE-рекламу за сигнатурою відомих трекерів.
//   company_id — ID виробника з manufacturer data (-1 якщо нема);
//   payload/plen — байти ПІСЛЯ 2-байтного company_id;
//   svc/n_svc — 16-бітні service (і service-data) UUID з реклами.
// Повертає TRK_NONE, якщо не схоже на трекер.
TrackerType tracker_classify(int company_id, const uint8_t* payload, int plen,
                             const uint16_t* svc, int n_svc);
