# Contributing

Thanks for your interest in improving OmniDiagnostics-ESP32.

## Scope

This project is a **passive, receive-only** survey and own-device diagnostic
tool. Contributions that add active radio attacks — deauthentication, jamming,
frame injection, beacon/advertisement spam, handshake capture for cracking,
sub-GHz replay/brute-force, or similar — are out of scope and will not be
merged. Passive scanning, decoding, monitoring, UI/UX, refactoring, docs and
own-device features are all welcome.

## Development setup

This is a [PlatformIO](https://platformio.org/) project. See
[docs/BUILD.md](docs/BUILD.md) for the toolchain and flashing steps.

```bash
pio run            # build
pio run -t upload  # flash (close the serial monitor first)
```

## Project layout & design

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before larger changes. The
"Adding a new app" section there is the quickest way to extend the firmware.

## Code style

- C++ with the existing 4-space indentation and brace style.
- **Comments and identifiers in English.**
- Keep each app self-contained: acquire resources in `onEnter()` and release
  them in `onExit()` (especially the radio).
- Set the `dirty` flag when the screen needs to redraw; do not draw every loop.
- Avoid blocking calls (`delay()`) in input/UI paths.

## Commits & pull requests

- Keep commits focused; write clear, imperative commit messages
  (e.g. `Add channel hopping to WiFi scan`).
- Describe what changed and how you tested it on hardware. Note the board
  revision and any wired peripherals.
- One logical change per pull request where practical.

## Reporting issues

Use the issue templates. For bugs, include the board revision, build output,
and serial monitor log if relevant.
