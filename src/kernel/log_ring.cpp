#include "log_ring.h"
#include <string.h>

void LogRing::push(const char* s) {
  if (!s) return;
  char* dst = buf_[total_ % CAP];
  size_t i = 0;
  for (; s[i] && i < LINE - 1; i++) dst[i] = s[i];
  dst[i] = '\0';
  total_++;
}

int LogRing::since(uint32_t from, char out[][LINE], int max) const {
  uint32_t earliest = (total_ > (uint32_t)CAP) ? total_ - CAP : 0;
  uint32_t start = from > earliest ? from : earliest;
  int n = 0;
  for (uint32_t i = start; i < total_ && n < max; i++) {
    memcpy(out[n], buf_[i % CAP], LINE);
    n++;
  }
  return n;
}

LogRing& log_ring() {
  static LogRing r;
  return r;
}
