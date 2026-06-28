# Hardware

## Board

**LilyGO T-Display V1.1** — ESP32-WROOM module, 4 MB flash, ST7789 IPS display
(135×240, used in landscape as 240×135), two user buttons, USB-C, and a Li-Po
charger with a JST connector.

## Pin map

The values below are defined in `include/config.h` (peripheral pins) and
`platformio.ini` (display pins).

| Function | GPIO | Notes |
|----------|------|-------|
| TFT MOSI | 19 | fixed by board |
| TFT SCLK | 18 | fixed by board |
| TFT CS | 5 | fixed by board |
| TFT DC | 16 | fixed by board |
| TFT RST | 23 | fixed by board |
| TFT backlight | 4 | fixed by board |
| Button OK / Back | 0 | `INPUT_PULLUP`; strapping pin (do not hold at reset) |
| Button Next | 35 | input-only; relies on board pull-up |
| Battery ADC | 34 | ADC1, on-board 1:2 divider |
| IR RX | 13 | TSOP/1838 demodulated input |
| IR TX | 17 | IR LED, driven via transistor (external) |
| CC1101 SCK | 25 | HSPI (external module) |
| CC1101 MOSI | 26 | HSPI |
| CC1101 MISO | 27 | HSPI |
| CC1101 CS | 33 | HSPI |
| CC1101 GDO0 | 32 | async data line |
| Buzzer | 21 | optional |

Free GPIOs for expansion: **22, 2, 15, 12** (12/2/15 are strapping pins) and
**36/37/38/39** (input-only).

## Display configuration

The ST7789 driver and pin assignment are passed to TFT_eSPI through
`build_flags` in `platformio.ini` using `USER_SETUP_LOADED=1`. This keeps the
configuration in the repository and avoids editing `User_Setup.h` inside the
library (which would be lost when the library is reinstalled):

```ini
-D USER_SETUP_LOADED=1
-D ST7789_DRIVER=1
-D TFT_WIDTH=135
-D TFT_HEIGHT=240
-D CGRAM_OFFSET=1
-D TFT_MOSI=19
-D TFT_SCLK=18
-D TFT_CS=5
-D TFT_DC=16
-D TFT_RST=23
-D TFT_BL=4
-D TFT_BACKLIGHT_ON=1
```

The display is initialised in landscape via `setRotation(1)`.

## GPIO notes

- **GPIO0** is a strapping pin. It is fine as a button during normal operation,
  but holding it at reset puts the chip into the serial bootloader.
- **GPIO35–39** are input-only and have no internal pull-ups; external modules
  must provide their own pull-ups, or use the board's.
- **GPIO34/35** belong to ADC1, which (unlike ADC2) is usable while WiFi is
  active. The battery sense pin (34) uses an on-board 2×100k divider, so the
  measured voltage is doubled in firmware.

## Optional peripherals

These are referenced in `config.h`/stubs but are not required for the working
apps:

- **CC1101 (868 MHz)** on a separate HSPI bus for passive sub-GHz monitoring.
  Power it from 3.3 V only.
- **IR receiver (1838) / IR LED** for the IR analyzer. The LED must be driven
  through a transistor, not directly from a GPIO.
- **Active buzzer** on GPIO21 for audible feedback.

When prototyping on a breadboard, power all peripherals from the board's **3V3**
pin (not 5V), and share a common ground.
