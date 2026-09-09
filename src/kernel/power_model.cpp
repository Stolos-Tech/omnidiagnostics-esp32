#include "power_model.h"

// Типові струми складників (мА). Коментарі — джерело/логіка. Калібрувати шунтом пізніше.
static const int MA_BASE       = 40;   // ESP32 @80MHz, modem idle
static const int MA_BL_MAX     = 32;   // TFT LED-підсвітка на повній яскравості
static const int MA_WIFI_STA   = 70;   // середнє STA (піки TX вищі)
static const int MA_AP         = 55;   // SoftAP активний
static const int MA_BT         = 90;   // Bluetooth-радіо
static const int MA_NRF24      = 115;  // nRF24 PA/LNA (пік)
static const int MA_CC1101     = 30;   // CC1101 RX
static const int MA_SD_WRITE   = 80;   // SD під час запису (транзієнт)

PowerBreakdown power_model_breakdown(const PowerFlags& f) {
  PowerBreakdown b;
  b.base      = MA_BASE;
  int bl = f.backlight; if (bl < 0) bl = 0; if (bl > 255) bl = 255;
  b.backlight = MA_BL_MAX * bl / 255;
  b.wifi      = (f.wifi_sta ? MA_WIFI_STA : 0) + (f.soft_ap ? MA_AP : 0);
  b.radios    = (f.nrf24 ? MA_NRF24 : 0) + (f.cc1101 ? MA_CC1101 : 0);
  b.sd        = f.sd_write ? MA_SD_WRITE : 0;
  b.bt        = f.bt ? MA_BT : 0;
  b.total     = b.base + b.backlight + b.wifi + b.radios + b.sd + b.bt;
  return b;
}

int power_model_estimate_ma(const PowerFlags& f) { return power_model_breakdown(f).total; }
