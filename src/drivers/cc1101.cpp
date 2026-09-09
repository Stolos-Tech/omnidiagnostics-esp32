#include <Arduino.h>
#include <SPI.h>
#include "driver/rmt.h"
#include "cc1101.h"
#include "../kernel/hw_safe.h"
#include "../kernel/subghz_util.h"

// --- Піни (див. cc1101.h) ---
#define PIN_SCK   2
#define PIN_MOSI  15
#define PIN_MISO  38    // спільний з nRF24 (обидва tri-state SO при CS=HIGH)
#define PIN_CS    27
#define PIN_GDO0  39    // input-only -> лише RX/захоплення (async TX тут неможливий)

// --- Регістри CC1101 ---
#define R_IOCFG2   0x00
#define R_IOCFG0   0x02
#define R_FIFOTHR  0x03
#define R_PKTLEN   0x06
#define R_PKTCTRL1 0x07
#define R_PKTCTRL0 0x08
#define R_FSCTRL1  0x0B
#define R_FSCTRL0  0x0C
#define R_FREQ2    0x0D
#define R_FREQ1    0x0E
#define R_FREQ0    0x0F
#define R_MDMCFG4  0x10
#define R_MDMCFG3  0x11
#define R_MDMCFG2  0x12
#define R_MDMCFG1  0x13
#define R_MDMCFG0  0x14
#define R_DEVIATN  0x15
#define R_MCSM0    0x18
#define R_FOCCFG   0x19
#define R_BSCFG    0x1A
#define R_AGCCTRL2 0x1B
#define R_AGCCTRL1 0x1C
#define R_AGCCTRL0 0x1D
#define R_FREND1   0x21
#define R_FREND0   0x22
#define R_FSCAL3   0x23
#define R_FSCAL2   0x24
#define R_FSCAL1   0x25
#define R_FSCAL0   0x26
#define R_TEST2    0x2C
#define R_TEST1    0x2D
#define R_TEST0    0x2E
// Статус-регістри (читати з burst-бітом 0xC0)
#define R_VERSION  0x31
#define R_RSSI     0x34
#define R_MARCSTATE 0x35
// Командні строби
#define S_SRES     0x30
#define S_SRX      0x34
#define S_SIDLE    0x36
#define S_SFRX     0x3A
#define S_SNOP     0x3D
// Заголовкові біти
#define H_READ     0x80
#define H_BURST    0x40

static SPIClass    s_spi(HSPI);
static SPISettings s_set(4000000, MSBFIRST, SPI_MODE0);   // CC1101 SPI до ~6.5МГц; 4М з запасом
static bool        s_inited = false;
static long        s_freq_khz = 433920;

static inline void cs(bool hi) { digitalWrite(PIN_CS, hi ? HIGH : LOW); }

// Переприв'язати HSPI-MISO на пін CC1101 (38) перед пакетом транзакцій (SD/nRF24
// могли перемкнути MISO на свій пін). Дешево — реєстр GPIO-матриці.
static inline void cc1101_claim_bus() { if (s_inited) spiAttachMISO(s_spi.bus(), PIN_MISO); }

static void writeReg(uint8_t addr, uint8_t val) {
  s_spi.beginTransaction(s_set); cs(false);
  s_spi.transfer(addr);
  s_spi.transfer(val);
  cs(true); s_spi.endTransaction();
}

// Статус-регістри 0x30..0x3D читаються ЛИШЕ з burst-бітом (без нього — це строб).
static uint8_t readStatus(uint8_t addr) {
  s_spi.beginTransaction(s_set); cs(false);
  s_spi.transfer(H_READ | H_BURST | addr);
  uint8_t v = s_spi.transfer(0);
  cs(true); s_spi.endTransaction();
  return v;
}

static void strobe(uint8_t s) {
  s_spi.beginTransaction(s_set); cs(false);
  s_spi.transfer(s);
  cs(true); s_spi.endTransaction();
}

// Базовий OOK/ASK RX-конфіг (433/868). Значення — усталена стартова точка для
// прийому простих пультів; за потреби підстроюється на залізі (RX BW/AGC).
static void load_config() {
  writeReg(R_IOCFG2,   0x2E);   // GDO2 high-Z (не юзаємо)
  writeReg(R_IOCFG0,   0x0D);   // GDO0 = асинхронний серійний вихід даних (OOK RX)
  writeReg(R_FIFOTHR,  0x47);
  writeReg(R_PKTLEN,   0xFF);
  writeReg(R_PKTCTRL1, 0x04);
  writeReg(R_PKTCTRL0, 0x30);   // async serial mode, infinite length, no CRC/whitening
  writeReg(R_FSCTRL1,  0x06);
  writeReg(R_FSCTRL0,  0x00);
  writeReg(R_MDMCFG4,  0x87);   // RX BW ~203 кГц
  writeReg(R_MDMCFG3,  0x32);
  writeReg(R_MDMCFG2,  0x30);   // ASK/OOK, без Manchester, без sync-word
  writeReg(R_MDMCFG1,  0x22);
  writeReg(R_MDMCFG0,  0xF8);
  writeReg(R_DEVIATN,  0x15);
  writeReg(R_MCSM0,    0x18);   // авто-калібрування при IDLE->RX/TX
  writeReg(R_FOCCFG,   0x16);
  writeReg(R_BSCFG,    0x6C);
  writeReg(R_AGCCTRL2, 0x03);   // OOK-профіль AGC
  writeReg(R_AGCCTRL1, 0x40);
  writeReg(R_AGCCTRL0, 0x91);
  writeReg(R_FREND1,   0x56);
  writeReg(R_FREND0,   0x11);
  writeReg(R_FSCAL3,   0xE9);
  writeReg(R_FSCAL2,   0x2A);
  writeReg(R_FSCAL1,   0x00);
  writeReg(R_FSCAL0,   0x1F);
  writeReg(R_TEST2,    0x81);
  writeReg(R_TEST1,    0x35);
  writeReg(R_TEST0,    0x09);
}

static void write_freq(long khz) {
  uint8_t f2, f1, f0;
  subghz_freq_to_regs(khz, &f2, &f1, &f0);
  writeReg(R_FREQ2, f2);
  writeReg(R_FREQ1, f1);
  writeReg(R_FREQ0, f0);
}

bool cc1101_present() {
  cc1101_claim_bus();
  uint8_t v = readStatus(R_VERSION);
  return v != 0x00 && v != 0xFF;   // типово 0x14 (клони 0x04/0x17)
}

bool cc1101_begin() {
  if (g_hw_safe) return false;   // safe-режим: не жени CS/SPI (розводка не звірена)
  pinMode(PIN_CS, OUTPUT); cs(true);
  pinMode(PIN_GDO0, INPUT);
  if (!s_inited) { s_spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS); s_inited = true; }
  cc1101_claim_bus();
  // Ручний reset: CS low/high з паузами, потім строб SRES.
  cs(true);  delayMicroseconds(5);
  cs(false); delayMicroseconds(10);
  cs(true);  delayMicroseconds(45);
  strobe(S_SRES);
  delay(2);
  load_config();
  write_freq(s_freq_khz);
  strobe(S_SIDLE);
  return cc1101_present();
}

void cc1101_set_freq_khz(long khz) {
  s_freq_khz = khz;
  cc1101_claim_bus();
  strobe(S_SIDLE);
  write_freq(khz);
  strobe(S_SRX);   // авто-калібрування (MCSM0)
}

int cc1101_probe_rssi_dbm(long khz) {
  cc1101_claim_bus();
  strobe(S_SIDLE);
  write_freq(khz);
  strobe(S_SRX);              // вхід у RX з авто-калібруванням
  delayMicroseconds(1200);   // калібрування (~800мкс) + осідання RSSI
  uint8_t raw = readStatus(R_RSSI);
  return subghz_rssi_dbm(raw);
}

int cc1101_capture_ook(long khz, uint16_t* pulses, int max_pulses, int timeout_ms) {
  if (!pulses || max_pulses <= 0) return 0;
  cc1101_claim_bus();
  strobe(S_SIDLE);
  write_freq(khz);
  strobe(S_SRX);

  // RMT RX на GDO0: 1 тік = 1 мкс (APB 80МГц / 80). idle_threshold завершує кадр
  // після довгої тиші; фільтр відкидає гліч < 30 мкс.
  const rmt_channel_t CH = RMT_CHANNEL_4;
  rmt_config_t cfg = {};
  cfg.rmt_mode = RMT_MODE_RX;
  cfg.channel = CH;
  cfg.gpio_num = (gpio_num_t)PIN_GDO0;
  cfg.clk_div = 80;
  cfg.mem_block_num = 2;
  cfg.rx_config.filter_en = true;
  cfg.rx_config.filter_ticks_thresh = 30;    // <30мкс = шум
  cfg.rx_config.idle_threshold = 12000;       // 12мс тиші = кінець кадру
  if (rmt_config(&cfg) != ESP_OK) return 0;
  if (rmt_driver_install(CH, 2048, 0) != ESP_OK) return 0;

  RingbufHandle_t rb = nullptr;
  rmt_get_ringbuf_handle(CH, &rb);
  rmt_rx_start(CH, true);

  int count = 0;
  uint32_t deadline = millis() + (uint32_t)timeout_ms;
  while (rb && (int32_t)(deadline - millis()) > 0 && count < max_pulses) {
    size_t rx_size = 0;
    rmt_item32_t* items = (rmt_item32_t*)xRingbufferReceive(rb, &rx_size, pdMS_TO_TICKS(40));
    if (!items) continue;
    int n = rx_size / sizeof(rmt_item32_t);
    for (int i = 0; i < n && count < max_pulses; i++) {
      // Кожен item = дві півхвилі (level0/duration0, level1/duration1).
      // Хочемо масив, що стартує з mark(HIGH). Idle GDO0 = LOW, тож перший
      // елемент кадру має level0=1; якщо ні — пропускаємо провідний space.
      if (count == 0 && items[i].level0 == 0) {
        if (items[i].duration1) pulses[count++] = items[i].duration1;
        continue;
      }
      if (items[i].duration0) pulses[count++] = items[i].duration0;
      if (count < max_pulses && items[i].duration1) pulses[count++] = items[i].duration1;
    }
    vRingbufferReturnItem(rb, (void*)items);
    if (count > 0) break;   // маємо кадр -> досить
  }

  rmt_rx_stop(CH);
  rmt_driver_uninstall(CH);
  cc1101_claim_bus();
  strobe(S_SIDLE);
  return count;
}
