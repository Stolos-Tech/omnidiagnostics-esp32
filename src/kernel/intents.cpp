#include "intents.h"
#include <string.h>

static char s_url[80] = "";
static bool s_pending = false;

void intent_open_url(const char* url) {
  if (!url) return;
  size_t i = 0;
  for (; url[i] && i < sizeof(s_url) - 1; i++) s_url[i] = url[i];
  s_url[i] = '\0';
  s_pending = true;
}

bool intent_take_url(char* out, size_t out_size) {
  if (!s_pending) return false;
  s_pending = false;
  if (out && out_size > 0) {
    size_t i = 0;
    for (; s_url[i] && i < out_size - 1; i++) out[i] = s_url[i];
    out[i] = '\0';
  }
  return true;
}
