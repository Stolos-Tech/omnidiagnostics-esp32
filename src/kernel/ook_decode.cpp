#include "ook_decode.h"

// Оцінка Te: найкоротший «справжній» імпульс (>=80 мкс, щоб відкинути гліч-шум).
static int estimate_te(const uint16_t* p, int n) {
  int te = 1 << 30;
  for (int i = 0; i < n; i++) {
    int d = p[i];
    if (d >= 80 && d < te) te = d;
  }
  return (te == (1 << 30)) ? -1 : te;
}

OokDecoded ook_decode(const uint16_t* p, int n) {
  OokDecoded r;
  r.valid = false; r.proto = "?"; r.code = 0; r.bits = 0; r.te_us = 0;
  if (!p || n < 40) return r;   // потрібні бодай ~24 пари

  int te = estimate_te(p, n);
  if (te < 80) return r;
  r.te_us = te;

  // Класифікаційні межі (мкс):
  const int shortLo = te * 6 / 10,  shortHi = te * 16 / 10;   // ~0.6..1.6 Te
  const int longLo  = te * 22 / 10, longHi  = te * 45 / 10;   // ~2.2..4.5 Te
  const int syncMin = te * 10;                                // синхро-пауза >= 10 Te

  // Синхро — це довга ПАУЗА (space, непарний індекс, бо масив стартує з mark).
  int sync = -1;
  for (int i = 1; i < n; i += 2) {
    if (p[i] >= syncMin) { sync = i; break; }
  }
  if (sync < 0) return r;

  // Декодуємо 24 пари (mark,space) одразу ПІСЛЯ синхро-паузи.
  int idx = sync + 1;   // перший mark кадру
  uint32_t code = 0;
  int bits = 0;
  for (; bits < 24 && idx + 1 < n; bits++) {
    int mk = p[idx], sp = p[idx + 1];
    idx += 2;
    int b;
    if (mk >= shortLo && mk <= shortHi && sp >= longLo && sp <= longHi) b = 0;       // 1Te / 3Te
    else if (mk >= longLo && mk <= longHi && sp >= shortLo && sp <= shortHi) b = 1;  // 3Te / 1Te
    else break;   // не валідний біт (напр. наступна синхро) -> кінець кадру
    code = (code << 1) | (uint32_t)b;
  }
  if (bits < 24) return r;   // вимагаємо повний 24-бітний кадр

  r.valid = true;
  r.code = code;
  r.bits = bits;
  r.proto = "EV1527/PT2262";
  return r;
}
