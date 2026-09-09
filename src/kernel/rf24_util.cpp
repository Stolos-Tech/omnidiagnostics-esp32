#include "rf24_util.h"

int rf24_channel_mhz(int ch) {
  if (ch < 0) ch = 0;
  if (ch > 125) ch = 125;
  return 2400 + ch;
}

int rf24_wifi_channel(int ch) {
  int mhz = rf24_channel_mhz(ch);
  int best = 0, bestd = 12;   // поріг <12 МГц від центру найближчого WiFi-каналу
  for (int wc = 1; wc <= 14; wc++) {
    int center = (wc == 14) ? 2484 : 2412 + (wc - 1) * 5;
    int d = mhz - center; if (d < 0) d = -d;
    if (d < bestd) { bestd = d; best = wc; }
  }
  return best;
}

int rf24_peak_index(const uint8_t* counts, int n) {
  if (!counts || n <= 0) return -1;
  int bi = 0; uint8_t bv = counts[0];
  for (int i = 1; i < n; i++) if (counts[i] > bv) { bv = counts[i]; bi = i; }
  return bi;
}

int rf24_bar_height(int count, int maxv, int h) {
  if (maxv <= 0 || h <= 0 || count <= 0) return 0;
  if (count > maxv) count = maxv;
  int v = count * h / maxv;
  if (v > h) v = h;
  return v;
}
