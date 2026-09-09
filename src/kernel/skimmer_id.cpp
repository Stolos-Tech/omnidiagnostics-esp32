#include "skimmer_id.h"
#include <string.h>
#include <ctype.h>

static bool istartswith(const char* s, const char* p) {
  while (*p) { if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return false; s++; p++; }
  return true;
}

static bool icontains(const char* s, const char* sub) {
  size_t n = strlen(sub);
  if (n == 0) return true;
  for (; *s; s++) {
    size_t i = 0;
    while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)sub[i])) i++;
    if (i == n) return true;
  }
  return false;
}

bool skimmer_is_suspect_name(const char* name) {
  if (!name || !name[0]) return false;
  // Префікси дефолтних імен дешевих SPP-модулів (часто у скімерах).
  static const char* PFX[] = { "HC-0", "HC-2", "BT04", "JDY-", "SPP", "RNBT", "BOLUTEK", "H-C-20" };
  for (int i = 0; i < (int)(sizeof(PFX) / sizeof(PFX[0])); i++)
    if (istartswith(name, PFX[i])) return true;
  // Підрядки-маркери.
  static const char* SUB[] = { "linvor", "FireFly" };
  for (int i = 0; i < (int)(sizeof(SUB) / sizeof(SUB[0])); i++)
    if (icontains(name, SUB[i])) return true;
  return false;
}
