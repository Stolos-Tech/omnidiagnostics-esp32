#include "format_util.h"
#include <stdio.h>

void format_uptime(uint32_t total_sec, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  uint32_t days = total_sec / 86400;
  uint32_t hours = (total_sec % 86400) / 3600;
  uint32_t mins = (total_sec % 3600) / 60;
  uint32_t secs = total_sec % 60;
  if (days > 0) {
    snprintf(out, out_size, "%ud %02uh %02um",
             (unsigned)days, (unsigned)hours, (unsigned)mins);
  } else {
    snprintf(out, out_size, "%uh %02um %02us",
             (unsigned)hours, (unsigned)mins, (unsigned)secs);
  }
}

void format_mmss(uint32_t total_sec, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  uint32_t h = total_sec / 3600, m = (total_sec % 3600) / 60, s = total_sec % 60;
  if (h > 0) snprintf(out, out_size, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  else       snprintf(out, out_size, "%02u:%02u", (unsigned)m, (unsigned)s);
}

void format_bytes(uint32_t bytes, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (bytes < 1024u) {
    snprintf(out, out_size, "%u B", (unsigned)bytes);
  } else if (bytes < 1024u * 1024u) {
    snprintf(out, out_size, "%u KB", (unsigned)(bytes / 1024u));
  } else {
    // МБ із двома знаками після коми через цілочисельну математику
    uint32_t whole = bytes / (1024u * 1024u);
    uint32_t frac = (uint32_t)(((uint64_t)(bytes % (1024u * 1024u)) * 100u) / (1024u * 1024u));
    snprintf(out, out_size, "%u.%02u MB", (unsigned)whole, (unsigned)frac);
  }
}
