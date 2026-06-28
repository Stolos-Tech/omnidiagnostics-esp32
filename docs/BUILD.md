# Build & Flash

## Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/) with the
  [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension,
  **or** the PlatformIO Core CLI.
- A LilyGO T-Display V1.1 and a **data-capable** USB-C cable.
- USB-serial driver for the on-board bridge (CH9102F). Most Linux/macOS systems
  enumerate it automatically; Windows may need the vendor driver.

PlatformIO downloads the ESP32 platform and all libraries on the first build, so
the initial build needs network access and takes several minutes.

## Build

CLI:

```bash
pio run
```

IDE: open the project folder (the one containing `platformio.ini`) and use the
**Build** action in the PlatformIO toolbar.

A successful build ends with `[SUCCESS]`.

> A `TOUCH_CS pin not defined` warning from TFT_eSPI is expected — the T-Display
> has no touch panel — and can be ignored.

## Flash

```bash
pio run -t upload
```

If upload stalls at `Connecting...`, hold the **BOOT** button (GPIO0) while the
upload starts, then release it. Most boards auto-reset and do not need this.

## Serial monitor

```bash
pio device monitor   # 115200 baud
```

## Partition table

`platformio.ini` sets `board_build.partitions = min_spiffs.csv`. The default
table is too small once WiFi, BLE (NimBLE) and TFT_eSPI are all linked; do not
revert this line or the image may overflow the app partition.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| Port not listed | Use a data USB-C cable, not charge-only; install the CH9102 driver. |
| `Could not open <port>: ... busy` | A serial monitor is holding the port — stop it (Ctrl-C) and retry. |
| Upload stalls at `Connecting...` | Hold BOOT (GPIO0) during the upload handshake. |
| Wrong/`/dev/ttyS*` port selected | Set `upload_port` / `monitor_port` in `platformio.ini` to the real device (e.g. `/dev/ttyACM0`). On Linux, confirm with `dmesg \| tail` after plugging in. |
| `region ... overflowed` at link time | Confirm the `min_spiffs.csv` partition line is present. |
| Blank screen, backlight on | Confirm the board is a T-Display V1.1 and the TFT `build_flags` match the panel. |

## Linux serial permissions

If the port appears but cannot be opened, add your user to the `dialout` group
and install the PlatformIO udev rules:

```bash
sudo usermod -aG dialout "$USER"   # then log out and back in
```

See the PlatformIO udev-rules documentation if the device is not accessible.
