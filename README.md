# OmniDiagnostics-ESP32

A modular, menu-driven diagnostic firmware for the **LilyGO T-Display V1.1**
(ESP32-WROOM + ST7789 135×240). It provides passive RF environment surveys and
on-device system telemetry through a small finite-state-machine "micro-OS".

> **Scope:** this is a *passive, receive-only* survey and own-device diagnostic
> tool. It scans and displays what is already being broadcast and connects only
> to devices the user explicitly selects. It does **not** implement any active
> radio attacks (no deauthentication, jamming, frame injection, spam, handshake
> cracking, or replay).

## Features

| App | Status | Description |
|-----|--------|-------------|
| 802.11 Auditor | working | Passive 2.4 GHz scan; per-network detail (SSID, BSSID, RSSI, channel, encryption, hidden flag). |
| BLE Auditor | working | Passive BLE advertisement scan (MAC, name, RSSI, iBeacon/Eddystone); connect to a selected device and read its GATT service count. |
| Network (WiFi) | working | Connect to a configured WiFi network in station mode; shows status, IP and RSSI. |
| System Dashboard | working | Battery voltage/%, free heap, die temperature, uptime. |
| RF Sub-1GHz | stub | Passive sub-GHz monitoring; requires an external CC1101 (868 MHz). |
| IR Analyzer | stub | IR capture/replay; requires `IRremoteESP8266` and an IR LED. |

## Hardware

- **Board:** LilyGO T-Display V1.1 (ESP32-WROOM, 4 MB flash, ST7789 135×240).
- **Input:** two on-board buttons (GPIO0 = OK/Back, GPIO35 = Next).
- **Power:** USB-C, or a Li-Po cell via the board's JST connector.

See [docs/HARDWARE.md](docs/HARDWARE.md) for the full pin map and optional
peripherals.

## Quick start

This is a [PlatformIO](https://platformio.org/) project.

```bash
# build
pio run

# flash (close any open serial monitor first to free the port)
pio run -t upload

# serial monitor
pio device monitor
```

The display pin mapping and ST7789 driver are configured in `platformio.ini`
via `build_flags`, so you do **not** need to edit TFT_eSPI's `User_Setup.h`.

Full build, flashing and troubleshooting notes are in
[docs/BUILD.md](docs/BUILD.md).

## Repository layout

```
omnidiagnostics-esp32/
├── platformio.ini         # board, build flags (TFT pins), dependencies
├── include/
│   └── config.h           # pin map, theme colors, input timing, WiFi creds
├── src/
│   ├── main.cpp           # app registration + setup/loop
│   ├── core/              # kernel, input, display, list widget, app base
│   └── apps/              # WiFi, BLE, Network, Dashboard, IR/RF stubs, menu
└── docs/                  # architecture, build, hardware, app reference
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — micro-OS design, app lifecycle, how to add an app.
- [docs/BUILD.md](docs/BUILD.md) — toolchain setup, build/flash, troubleshooting.
- [docs/HARDWARE.md](docs/HARDWARE.md) — pin map, display configuration, optional peripherals.
- [docs/APPS.md](docs/APPS.md) — per-app behaviour and controls.

## Configuration

Edit `include/config.h` to set WiFi credentials for the Network app:

```c
#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"
```

Leave them empty to disable the connect attempt (the app shows "No SSID set").

## Dependencies

Managed automatically by PlatformIO (`lib_deps` in `platformio.ini`):

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — display driver.
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — lightweight BLE stack.

## Continuous integration

A GitHub Actions workflow (`.github/workflows/build.yml`) builds the firmware
with PlatformIO on every push and pull request. After pushing to GitHub you can
add a status badge by uncommenting and editing this line (replace `<owner>`):

```md
<!-- ![build](https://github.com/<owner>/omnidiagnostics-esp32/actions/workflows/build.yml/badge.svg) -->
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Issue and pull-request templates live in
`.github/`. Note the project scope: passive / receive-only and own-device only.

## License

MIT — see [LICENSE](LICENSE).
