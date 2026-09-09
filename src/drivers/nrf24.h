// Мінімальний драйвер nRF24L01+ (PA/LNA) на ESP32 для red-team 2.4GHz-аналізу.
// Register-level (без бібліотеки RF24) — потрібен лише RPD-сканер зайнятості ефіру.
//
// Піни (T-Display, після чистки моторів; strapping-aware — див. пам'ять/аудит пінів):
//   SCK=GPIO2   MOSI=GPIO15   MISO=GPIO38(input-only)   CSN=GPIO25   CE=GPIO12
//   GPIO12 (MTDI strapping) ВИМАГАЄ зовнішній 10k pulldown, щоб не зірвати boot.
//   IRQ не використовується (поллінг). Живлення nRF24 — 3.3V з адаптера AMS1117
//   (годувати 5В з рейки) + рясне decoupling під струмові піки PA (~115mA).
#pragma once
#include <stdint.h>

#define NRF24_CHANNELS 126   // канали 0..125 -> 2400..2525 МГц

// SPI + піни, конфіг PRX для скану. Повертає true, якщо модуль відповів (readback).
bool nrf24_begin();

// Те саме, але в обхід g_hw_safe — лише для діагностики/калібрування (drive перевірено).
bool nrf24_begin_diag();

// Швидка перевірка присутності (запис+readback RF_CH). Дешева, без реконфігу.
bool nrf24_present();

// Діагностика («активна продзвонка»): піднімає SPI НЕЗАЛЕЖНО від safe-режиму й читає
// сирі регістри. out[0..5] = CONFIG(0x00), EN_AA(0x01), SETUP_AW(0x03), RF_SETUP(0x06),
// RF_CH(0x05), а потім RF_CH readback після запису 0x25 (out[5]==0x25 => шина 100% жива).
// Повертає STATUS(0x07). Усі 0x00/0xFF => MISO мовчить (обрив SPI/живлення nRF).
uint8_t nrf24_regdump(uint8_t out[6]);

// Змінити тактову SPI у рантаймі (для свіпу «найвища стабільна частота»). Гц.
void     nrf24_set_spi_hz(uint32_t hz);
uint32_t nrf24_get_spi_hz();

// Час слухання RPD на канал (мкс), тюнабельний для калібрування dwell. Мін 130 (PLL).
void     nrf24_set_dwell_us(uint16_t us);
uint16_t nrf24_get_dwell_us();

// Один прохід сканера: для кожного каналу коротко слухаємо й читаємо RPD (детектор
// носійної, поріг ~-64dBm). +1 у counts[ch] при активності (accumulate, НЕ обнуляє).
// Кілька проходів -> вища роздільність. Повертає к-ть активних каналів цього проходу.
int nrf24_scan_pass(uint8_t* counts, int n);
