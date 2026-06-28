# Apps

Navigation is the same everywhere: **NEXT** scrolls, **OK** (short) selects,
**OK** (held) goes back one level. The active row is highlighted; a scrollbar
appears when a list is longer than the viewport.

## Main menu

Lists the registered apps. NEXT moves the highlight, OK opens the selected app.

## 802.11 Auditor

Passive 2.4 GHz survey (receive only).

- On entry it starts an asynchronous scan (hidden networks included) and shows
  `Scanning...` until results arrive.
- The **list** view shows a `Rescan` row followed by the discovered SSIDs
  (hidden ones as `<hidden>`). The footer shows the highlighted network's RSSI
  and encryption.
- **OK** on a network opens the **detail** view: SSID, BSSID, RSSI with a signal
  bar, encryption type, channel, band and hidden flag. In detail view, NEXT
  steps through networks and OK (or hold) returns to the list.
- **OK** on the `Rescan` row repeats the scan.

The radio is released on exit.

## BLE Auditor

Passive BLE advertisement scan, with the option to connect to a chosen device.

- **Scan** view lists nearby devices (name or MAC), with the highlighted
  device's RSSI and type (`BLE` / `iBCN` iBeacon / `EDDY` Eddystone) in the
  footer. Scanning is passive (no scan requests). OK clears the list.
- **OK** on a device connects to it (GATT client), shows `Connecting...`, then
  reports `Connected` with the number of services discovered, or
  `Connect failed`. Service discovery is read-only.
- **OK** held disconnects and returns to scanning.

The BLE stack is de-initialised on exit.

## Network (WiFi)

Connects to a configured network in station mode.

- Reads `WIFI_SSID` / `WIFI_PASS` from `config.h`. If the SSID is empty it shows
  `No SSID set`.
- Displays connection status, and once connected the assigned IP and RSSI.
- **OK** reconnects; **OK** held returns to the menu. The connection is dropped
  on exit.

## System Dashboard

Live device telemetry, refreshed once per second: battery voltage and percent,
free heap, internal die temperature (approximate, uncalibrated) and uptime.

> Battery readings are only meaningful with a Li-Po connected; on USB-only power
> the value is not representative.

## RF Sub-1GHz (stub)

Placeholder for passive sub-GHz monitoring. Requires an external CC1101
(868 MHz) on the HSPI bus (see [HARDWARE.md](HARDWARE.md)). The screen shows the
required wiring until the module is implemented.

## IR Analyzer (stub)

Placeholder for IR capture/replay. Requires the `IRremoteESP8266` library and an
IR LED on the TX pin; the receiver pin is already reserved. The screen shows the
required setup until implemented.
