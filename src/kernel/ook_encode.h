// Чистий OOK-енкодер (EV1527/PT2262) — інверсія ook_decode. Кодує code у масив
// тривалостей mark/space (мкс, починаючи з mark) для передачі через CC1101 (TX).
// БЕЗ апаратних включень -> native-тести (round-trip: encode -> decode = той самий code).
//
// Кадр: для кожного біта MSB->LSB  bit1 = HIGH 3·Te + LOW 1·Te; bit0 = HIGH 1·Te + LOW 3·Te.
// Наприкінці кадру — синхро (HIGH 1·Te + LOW 31·Te). Реальні пульти шлють кілька повторів
// поспіль (repeats) — приймач синхронізується на паузі й читає наступний кадр.
#pragma once
#include <stdint.h>

// Заповнює out[] тривалостями (мкс) для `repeats` кадрів. Повертає к-ть елементів,
// або 0 при невалідних аргументах / переповненні out_cap.
// need = repeats * (bits*2 + 2).
int ook_encode(uint32_t code, int bits, int te_us, int repeats, uint16_t* out, int out_cap);
