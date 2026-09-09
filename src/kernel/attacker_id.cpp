#include "attacker_id.h"
#include <string.h>

bool attacker_is_pwnagotchi_ssid(const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  // pwnagotchi-beacon: SSID — фрагмент JSON, заголовок починається з '{' і містить лапки.
  return ssid[0] == '{' && strchr(ssid, '"') != nullptr;
}
