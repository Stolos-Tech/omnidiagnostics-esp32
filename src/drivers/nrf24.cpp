#include <Arduino.h>
#include <SPI.h>
#include "nrf24.h"
#include "../kernel/hw_safe.h"

// Піни — див. nrf24.h (strapping-aware).
#define PIN_SCK   2    // out (SCK idle LOW -> boot-safe) — СПІЛЬНИЙ з SD
#define PIN_MOSI  15   // out (внутр. pullup HIGH -> потрібний стан GPIO15 при boot) — СПІЛЬНИЙ з SD
#define PIN_MISO  21   // MISO перенесено 38->21 (GPIO38 виявився мертвим на цій платі). SD знято,
                       // тож 21 вільний і перевірено живий. Якщо SD повернеться — рознести піни.
#define PIN_CSN   25   // out (звільнений свіч S3)
#define PIN_CE    12   // out (strapping MTDI -> зовнішній 10k pulldown ОБОВ'ЯЗКОВО)

// Регістри nRF24L01+
#define R_CONFIG    0x00
#define R_EN_AA     0x01
#define R_EN_RXADDR 0x02
#define R_RF_CH     0x05
#define R_RF_SETUP  0x06
#define R_STATUS    0x07
#define R_RPD       0x09
// Команди
#define C_R_REGISTER 0x00
#define C_W_REGISTER 0x20
#define C_FLUSH_RX   0xE2
#define C_NOP        0xFF

static SPIClass    s_spi(HSPI);   // окрема від VSPI дисплея шина
// Тактова SPI. 8 МГц базово (datasheet nRF24L01+ max = 10 МГц; 8 = стабільно з запасом).
// Свіп 1..10 МГц після заміни SCK/MOSI-джамперів + окремої землі nRF дав 12/12 на ВСІХ
// частотах включно з 10 МГц — тобто ранній «дзвін 0xC5» був суто поганим джгутом, не тактовою.
// Змінна в рантаймі (nrf24_set_spi_hz / ?hz=) — 10 МГц доступна, але 8 лишаю дефолтом за margin.
static uint32_t   s_hz  = 8000000;
static SPISettings s_set(s_hz, MSBFIRST, SPI_MODE0);

void nrf24_set_spi_hz(uint32_t hz) { s_hz = hz; s_set = SPISettings(hz, MSBFIRST, SPI_MODE0); }
uint32_t nrf24_get_spi_hz() { return s_hz; }

// Час слухання RPD на канал (мкс). PLL-осідання ~130мкс + слухання. Тюнабельний для
// калібрування: довше = надійніше ловить бурстовий трафік (WiFi/BLE), але повільніший свіп.
static uint16_t s_dwell_us = 300;
void nrf24_set_dwell_us(uint16_t us) { s_dwell_us = us < 130 ? 130 : us; }
uint16_t nrf24_get_dwell_us() { return s_dwell_us; }
static bool        s_inited = false;

static inline void csn(bool hi) { digitalWrite(PIN_CSN, hi ? HIGH : LOW); }
static inline void ce(bool hi)  { digitalWrite(PIN_CE,  hi ? HIGH : LOW); }

// Переприв'язати HSPI-MISO на пін nRF24 (38). Викликати перед пакетом транзакцій — бо SD
// міг перемкнути MISO на свій 21. Дешево (реєстр GPIO-матриці).
static inline void nrf24_claim_bus() { if (s_inited) spiAttachMISO(s_spi.bus(), PIN_MISO); }

static uint8_t writeReg(uint8_t reg, uint8_t val) {
  s_spi.beginTransaction(s_set); csn(false);
  uint8_t st = s_spi.transfer(C_W_REGISTER | (reg & 0x1F));
  s_spi.transfer(val);
  csn(true); s_spi.endTransaction();
  return st;
}

static uint8_t readReg(uint8_t reg) {
  s_spi.beginTransaction(s_set); csn(false);
  s_spi.transfer(C_R_REGISTER | (reg & 0x1F));
  uint8_t v = s_spi.transfer(C_NOP);
  csn(true); s_spi.endTransaction();
  return v;
}

static void cmd(uint8_t c) {
  s_spi.beginTransaction(s_set); csn(false);
  s_spi.transfer(c);
  csn(true); s_spi.endTransaction();
}

bool nrf24_present() {
  nrf24_claim_bus();
  writeReg(R_RF_CH, 0x25);
  return readReg(R_RF_CH) == 0x25;   // readback співпав -> модуль на шині
}

static bool nrf24_begin_impl() {
  pinMode(PIN_CSN, OUTPUT); csn(true);
  pinMode(PIN_CE, OUTPUT);  ce(false);
  if (!s_inited) { s_spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN); s_inited = true; }
  nrf24_claim_bus();   // гарантувати MISO=38 (SD міг перехопити на 21)
  delay(5);
  writeReg(R_CONFIG, 0x00);      // усе off спершу
  delay(2);
  writeReg(R_EN_AA, 0x00);       // без auto-ack (пасивний слухач)
  writeReg(R_EN_RXADDR, 0x01);   // pipe0 увімкнено
  writeReg(R_RF_SETUP, 0x06);    // 1Mbps, 0dBm — для скану вужча смуга = чіткіші канали
  writeReg(R_STATUS, 0x70);      // очистити RX_DR/TX_DS/MAX_RT
  cmd(C_FLUSH_RX);
  writeReg(R_CONFIG, 0x03);      // PWR_UP | PRIM_RX
  delay(2);
  return nrf24_present();
}

bool nrf24_begin() {
  if (g_hw_safe) return false;   // safe-режим: не жени CSN/CE/SPI (розводка не звірена)
  return nrf24_begin_impl();
}

// Діагностичний ініт в обхід safe (для калібрування dwell; drive безпечний — КЗ-скан пройдено).
bool nrf24_begin_diag() { return nrf24_begin_impl(); }

uint8_t nrf24_regdump(uint8_t out[6]) {
  // Діагностика: піднімаємо SPI незалежно від g_hw_safe (КЗ-скан підтвердив безпеку драйву).
  pinMode(PIN_CSN, OUTPUT); csn(true);
  pinMode(PIN_CE, OUTPUT);  ce(false);
  if (!s_inited) { s_spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN); s_inited = true; }
  nrf24_claim_bus();   // MISO=38 (SD міг перехопити на 21)
  delay(2);
  out[0] = readReg(R_CONFIG);     // 0x00 — після ресету 0x08
  out[1] = readReg(R_EN_AA);      // 0x01 — після ресету 0x3F
  out[2] = readReg(0x03);         // SETUP_AW — після ресету 0x03
  out[3] = readReg(R_RF_SETUP);   // 0x06 — після ресету 0x0E
  out[4] = readReg(R_RF_CH);      // 0x05
  writeReg(R_RF_CH, 0x25); out[5] = readReg(R_RF_CH);   // write+readback: 0x25 => шина жива
  return readReg(R_STATUS);
}

int nrf24_scan_pass(uint8_t* counts, int n) {
  if (!counts || n <= 0) return 0;
  if (n > NRF24_CHANNELS) n = NRF24_CHANNELS;
  nrf24_claim_bus();   // MISO=38 на весь прохід (SD між скан-проходами не лізе)
  int hits = 0;
  for (int ch = 0; ch < n; ch++) {
    writeReg(R_RF_CH, (uint8_t)ch);
    ce(true);
    delayMicroseconds(s_dwell_us);  // осідання PLL (~130мкс) + слухання (тюнабельне, дефолт 300);
                                    // довше -> надійніше ловить бурстовий трафік; RPD-поріг -64dBm
    ce(false);
    if (readReg(R_RPD) & 0x01) { if (counts[ch] < 255) counts[ch]++; hits++; }
  }
  return hits;
}
