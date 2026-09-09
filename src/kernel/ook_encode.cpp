#include "ook_encode.h"

int ook_encode(uint32_t code, int bits, int te_us, int repeats, uint16_t* out, int out_cap) {
  if (!out || bits <= 0 || bits > 32 || te_us <= 0 || repeats < 1) return 0;
  // Синхро-пауза 31·Te має влазити в uint16_t.
  if ((long)te_us * 31 > 65535) return 0;
  const int per = bits * 2 + 2;              // кожен біт: mark+space; + синхро mark+space
  if ((long)per * repeats > out_cap) return 0;

  const uint16_t oneTe   = (uint16_t)(te_us);
  const uint16_t threeTe = (uint16_t)(te_us * 3);
  const uint16_t syncGap = (uint16_t)(te_us * 31);

  int i = 0;
  for (int r = 0; r < repeats; r++) {
    for (int b = bits - 1; b >= 0; b--) {
      if ((code >> b) & 1u) { out[i++] = threeTe; out[i++] = oneTe; }   // bit 1: 3Te / 1Te
      else                  { out[i++] = oneTe;   out[i++] = threeTe; } // bit 0: 1Te / 3Te
    }
    out[i++] = oneTe;     // синхро mark
    out[i++] = syncGap;   // синхро довга пауза
  }
  return i;
}
