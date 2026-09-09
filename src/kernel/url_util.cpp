#include "url_util.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static bool starts_with_ci(const char* s, const char* prefix) {
  size_t n = strlen(prefix);
  for (size_t i = 0; i < n; i++)
    if (tolower((unsigned char)s[i]) != prefix[i]) return false;
  return true;
}

bool http_normalize_url(const char* in, char* out, size_t out_size) {
  if (!in || !out || out_size == 0) return false;

  // пропустити провідні пробіли
  while (*in == ' ' || *in == '\t') in++;
  if (*in == '\0') return false;

  bool has_scheme = starts_with_ci(in, "http://") || starts_with_ci(in, "https://");
  int written;
  if (has_scheme) {
    written = snprintf(out, out_size, "%s", in);
  } else {
    written = snprintf(out, out_size, "http://%s", in);
  }
  // snprintf повертає бажану довжину; якщо >= out_size — усічено
  return written > 0 && (size_t)written < out_size;
}
