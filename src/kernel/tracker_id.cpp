#include "tracker_id.h"

const char* tracker_name(TrackerType t) {
  switch (t) {
    case TRK_APPLE_FINDMY: return "Apple Find My";
    case TRK_TILE:         return "Tile";
    case TRK_SAMSUNG:      return "Samsung SmartTag";
    default:               return "-";
  }
}

TrackerType tracker_classify(int company_id, const uint8_t* payload, int plen,
                             const uint16_t* svc, int n_svc) {
  // Service-UUID сигнатури — найнадійніші.
  if (svc) for (int i = 0; i < n_svc; i++) {
    if (svc[i] == 0xFEED) return TRK_TILE;      // Tile
    if (svc[i] == 0xFD5A) return TRK_SAMSUNG;   // Samsung SmartTag/Find
  }
  // Company ID виробника.
  if (company_id == 0x0157) return TRK_TILE;    // Tile Inc
  // Apple Find My: company 0x004C + перший байт payload 0x12 (Find My «separated»).
  // 0x07/0x10 = nearby/handoff (iPhone/AirPods/Watch) — НЕ трекер, ігноруємо.
  if (company_id == 0x004C && payload && plen >= 1 && payload[0] == 0x12)
    return TRK_APPLE_FINDMY;
  return TRK_NONE;
}
