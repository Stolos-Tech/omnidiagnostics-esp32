#pragma once
#include <stdint.h>

// =====================================================================
//  OmniDiagnostics-ESP32 :: global configuration
//  Board: LilyGO T-Display V1.1
// =====================================================================

// ---- Buttons (active LOW) ----
#define PIN_BTN_OK    0    // OK / Back. Has an internal pull-up.
                           //   NOTE: strapping pin - do not hold it pressed
                           //   during reset or the board enters the bootloader.
#define PIN_BTN_NEXT  35   // Next / Scroll. Input-only pin, no internal
                           //   pull-up - relies on the board's external pull-up.

// ---- Power / monitoring ----
#define PIN_VBAT      34   // ADC1, on-board 1:2 divider (2x100k) on T-Display

// ---- IR (APP_IR_PROTOCOL_DEBUGGER) ----
#define PIN_IR_RX     13   // TSOP/1838 demodulated input
#define PIN_IR_TX     17   // IR LED (external; drive through transistor + resistor)

// ---- CC1101 sub-GHz (APP_RF_SPECTRAL_ANALYZER), separate HSPI bus ----
#define PIN_CC1101_SCK   25
#define PIN_CC1101_MOSI  26
#define PIN_CC1101_MISO  27
#define PIN_CC1101_CS    33
#define PIN_CC1101_GDO0  32

// ---- UX (optional; set to 0 to disable) ----
#define ENABLE_BUZZER 1
#define PIN_BUZZER    21
// Free GPIOs for expansion: 22, 2, 15, 12 (strap), 36/37/38/39 (input-only)

// ---- UI theme (amber on black), RGB565 ----
#define COL_BG      0x0000   // black
#define COL_ACCENT  0xFD20   // amber (~255,165,0)
#define COL_TEXT    0xFFFF   // white
#define COL_DIM     0x8410   // grey
#define COL_OK      0x07E0   // green
#define COL_WARN    0xF800   // red

// ---- Input behaviour ----
#define LONGPRESS_MS  600    // long-press threshold (OK held = Back)
#define DEBOUNCE_MS   25

// ---- WiFi station (Network app) ----
// Set your network here. Leave empty and the app simply shows "No SSID set".
#define WIFI_SSID  ""
#define WIFI_PASS  ""
