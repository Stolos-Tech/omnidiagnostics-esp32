# APEX Aerospace // Mission Control UI — extracted design system

Reference for the esp32-os web UI redesign. Source: Behance gallery 249223385
(Gary Gurman). 15 screens saved as `apex_01.webp` … `apex_15.webp` (1400px).
Studied live 2026-08-20. Behance is an image gallery — these are the design screens,
not source HTML/CSS.

## Vibe
Dark HUD / sci-fi mission-control OS. "Powerful without clutter." Every screen reads
like part of one larger system. Near-black, data-dense, restrained color.

## Layout architecture
- **Left vertical nav rail**: thin strip, stage names as ROTATED vertical text
  (PRE-FLIGHT / ASCENT / ORBIT), active segment tinted/highlighted.
- **Top bar**: angular logo mark + coded system line (`ASCENT-OS // BUILD 4.2.1`,
  `KSC PAD 39A | LAT.. | LON..`), a `SECURE CHANNEL / AES-256 ACTIVE` badge (green),
  dot-matrix indicator, top-right action pill (`AUTO-SEQ`).
- **Hero metric**: huge tabular-numeral value (`00:10:42`.88 — decimals dimmed/smaller),
  small amber label above (`T-MINUS HOLD`), mono sub-line below (`UTC.. | WEATHER: GO`).
- **Framed panels**: header row = title (mono uppercase) + right-aligned status
  (`SYSTEM READINESS` ......... `36% CLEARED` in green).
- **Micro footer**: tiny mono coded line (`FRAME 07 | LAUNCH DIRECTOR / T+04:21:18 / NORTH STAR = LOCKED`).

## Signature components (adopt these)
1. **Status row = label + dotted leader + bracketed tag**:
   `AVIONICS ........ [ GO ]`  `RANGE ........ [ HOLD ]`  `THERMAL .... [ WARN ]`.
   -> maps directly onto our mirrored KEY:VALUE item lists (net_info, self_test,
      http_sec, uno sensors, wifi security).
2. **Bracketed status tags** `[ GO ]/[ HOLD ]/[ WARN ]/[ N/A ]`, colored:
   GO/OK/NOMINAL/UP = green; HOLD/WARN/WAITING/STANDBY/N-A = amber; FAIL/ANOMALY = red.
3. **Big tabular-numeral hero** for the one key metric of a screen (green when nominal).
4. **Segmented bar meters**: label left, % right, thin segmented bar (CORE PROCESSOR 42%).
5. **Vertical stepper** with triangle state icons (done=green, waiting=amber, pending=dim).
6. **Coded IDs / dotted leaders everywhere**; monospace for ALL labels + data.
7. **Numbered callout badges** on schematics (circular, amber when anomalous).

## Color system
- bg near-black (#0a0a0c-ish), surfaces slightly lighter, hairline borders.
- **green = primary/OK/live data** (dominant accent), **amber = warn/hold/anomaly**,
  **red = critical only** (gradient extreme). White = neutral text. Very sparing color.
- NOTE: user wants our palette pastel cyan/violet — so we KEEP cyan/violet as the UI
  accent, but use APEX's green(GO)/amber(WARN)/red(BAD) semantics for STATUS tags
  (we already have --good/--warn/--bad). Blend, don't go full APEX-green.

## Typography
- Monospace for labels, data, status, coded lines (we use --mono).
- Huge bold sans display for wordmark/hero numbers; tabular-nums for all figures.
- Small UPPERCASE tracked labels.

## Applied to esp32-os so far
- HUD corner-frames on the current-page bar; mono coded eyebrow + `▸` caret.
- `[ TOOLS · MONITORS ]` coded section label; mono uppercase card headers.
- NEXT: render mirrored info-rows as `label ···· [ TAG ]` status rows (the signature pattern).
