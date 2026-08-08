# Gamepad Normalization — Sony Pad Remap + Canonical Chord Detection

**Date:** 2026-08-08
**Status:** Approved in discussion; pending owner review of this document.
**Follows:** commit 33ad07e (combo gate fixed for hybrid driver mode).

## Background

The hybrid DirectInput8+XInput driver (July 2026 multi-HID update) serves
generic HID sticks via DirectInput and Xbox-family pads via XInput. A
DualSense connects through the DirectInput side, which reports Sony's raw HID
button order — so its buttons land in the wrong places, and chord detection
(which reads XInput's private cache) can never see it at all.

Industry-standard solution (SDL GameController model, used by effectively
every sub-AAA engine): one canonical abstract pad in Xbox ordering, plus
per-device mapping tables keyed by VID/PID. AAE's `joy[]` XInput layout
(joystick.h lines 57–67) already *is* the canonical pad; this work adds the
mapping layer and makes chords read canonical state.

## Part A — Sony remap in the DirectInput path

**Detection.** In `dinput::enum_cb` (Joystick.cpp — VID/PID are already
extracted at line ~1057), match a small built-in table:

| VID:PID | Device |
|---|---|
| 054C:0CE6 | DualSense |
| 054C:0DF2 | DualSense Edge |
| 054C:05C4 | DualShock 4 (v1) |
| 054C:09CC | DualShock 4 (v2) |

A match sets `Device.pad_map = PadMap::Sony` and marks the device
gamepad-class. Unknown VID/PID keeps today's raw passthrough (`PadMap::None`)
— Ultimarc sticks and other cab hardware are untouched.

**Canonical fill.** For mapped devices, `dinput::poll()` fills `joy[]` to the
exact XInput contract instead of the raw layout:

- `stick[0]` = left stick (`lX`/`lY`, signed −128..127) with the hat (d-pad)
  overriding the digital flags when pressed — same merge the XInput path does.
- `stick[1]` = right stick: `lZ` → X, `lRz` → Y, range switched to signed
  −128..127 (`set_axis_range` currently makes them unsigned extras).
- Buttons reordered raw → canonical:

| Canonical | Meaning | Sony raw |
|---|---|---|
| 0 | A | 1 (Cross) |
| 1 | B | 2 (Circle) |
| 2 | X | 0 (Square) |
| 3 | Y | 3 (Triangle) |
| 4 | LB | 4 (L1) |
| 5 | RB | 5 (R1) |
| 6 | Back | 8 (Create) |
| 7 | Start | 9 (Options) |
| 8 | LStick | 10 (L3) |
| 9 | RStick | 11 (R3) |
| 10–13 | DPad U/D/L/R | hat POV |
| 14 | LT digital | 6 (L2) |
| 15 | RT digital | 7 (R2) |

- PS, touchpad-click, and mute buttons: intentionally unmapped (outside the
  canonical 16).
- Analog trigger axes (`lRx`/`lRy`): exposed only if/exactly as the XInput
  path exposes analog triggers; if XInput exposes none, Sony exposes none.
  Parity, not invention.
- Friendly name, GUID string, and INPUT DEVICES / per-player assignment
  behavior are unchanged — the remap only reorders what fills `joy[]`.

**Result:** every default binding works identically on a DualSense — Cross
launches games, Create = 1P Start, Options = 2P Start, R3 = coin — and the
CONTROLLER GUIDE screen's one diagram is truthful for both pad families
(positions identical; Xbox names shown).

## Part B — chord detection reads canonical state

`joystick_check_combo()` (Joystick.cpp:1339) currently gates on the driver
mode and reads `xinput::get_cached_buttons()` — XInput pads only, and a
Windows-only dead end for the Linux port.

Rewrite:

- Add a per-device **gamepad-class flag**: true for every XInput slot and for
  DI devices matched by the Part A table; false for raw DI devices and WinMM.
- Synthesize the chord mask from canonical `joy[i]` buttons:
  START=btn7, BACK=btn6, L/R-THUMB=btn8/9, L/R-SHOULDER=btn4/5,
  A/B/X/Y=btn0–3, DPAD=btn10–13. No XInput cache, no driver-mode gate.
- **Scan all gamepad-class devices**, each with its own hold-frame counters
  (the state arrays are already per-device-sized). A chord fires when any pad
  edge-triggers it. The `player` parameter is retained for signature
  compatibility but no longer selects a device — all call sites pass 0 and
  the chords are global system shortcuts.
- Non-gamepad devices are never scanned, so an Ultimarc stick whose raw
  buttons 6/7 happen to be wired to fire buttons can never phantom-trigger
  menu/exit/pause. This is the guard that makes Hybrid+cabinet+couch coexist.
- The 2-frame confirm and released-before-refire behavior are preserved
  as-is.

This supersedes the 33ad07e gate fix (which stays, harmlessly, for
`joystick_using_xinput()`'s other meaning as a driver query). Because the new
logic is pure `joy[]` state, the Linux evdev backend inherits working chords
by tagging its pads gamepad-class — a follow-up verification item for the
Linux hardware pass, not part of this change.

## Files touched

| File | Change |
|---|---|
| `aae/system/input/Joystick.cpp` | Sony VID/PID table, canonical fill in DI poll, gamepad-class flag, chord rewrite |
| `aae/system/input/joystick.h` | layout/combo doc updates; note canonical contract applies to mapped DI pads |
| `aae/inputtest/inputtest_main.cpp` | exercise Sony remap table + canonical chord synthesis |
| `CHANGELOG.txt` | entry |

No changes to `inptport.cpp` defaults, menus, or drivers.

## Verification

- **Xbox pad alone:** all three chords, all bindings — no regression.
- **DualSense alone, USB and Bluetooth** (BT enumeration can differ — verify
  both): left stick + d-pad navigate, Cross launches, Create/Options/R3 =
  1P/2P/coin, all three chords work. `systemlog.txt` shows the device
  registered with the Sony map applied.
- **Xbox + DualSense together:** both pads can fire chords; per-player
  assignment in INPUT DEVICES still routes game input correctly.
- **Ultimarc + pad (owner has hardware queued):** stick plays games; mashing
  its buttons never opens menu/exit/pause; pad chords still work even though
  the DI stick occupies `joy[0]` and shifts the pad's index up.
- **No devices / WinMM-only:** unchanged behavior, chords inert.

## Part C — Linux evdev parity (SteamOS / Steam controllers) — added 2026-08-08

The owner's primary target is SteamOS (Deck + Steam Machine Flatpak). Steam
Input consumes every physical controller there — Deck built-ins, the new
Steam Controller, DualSense, Xbox — and re-presents it to the game as a
virtual Xbox 360 pad over uinput/evdev. The evdev backend already fills
`joy[]` in the canonical layout (BTN_SOUTH-family → canonical slots), so
Steam-brand support means finishing Part B on Linux:

- Tag evdev devices that advertise gamepad capability as `is_gamepad`;
  generic HID sticks stay untagged (same Ultimarc guard as Windows).
- Replace `evdev_joystick.cpp`'s private player-0-only
  `joystick_check_combo` (and its stale driver comments) with the shared
  canonical implementation, so chords work from any pad, identically to
  Windows.
- Verify with the existing Phase-3c uinput harness (`aae_uinput_test` +
  `aae_inputtest` under WSL): a fabricated X360-style virtual pad is
  exactly what Steam Input presents on SteamOS.

Non-goal: driving the Steam Controller natively without Steam running (its
lizard mode presents as keyboard/mouse; Steam is always present on SteamOS).

## Out of scope

- `gamecontrollerdb.txt` (community mapping file) parsing — the built-in
  table covers the couch; the file format can layer on later if an exotic pad
  ever matters.
- Switch Pro / other non-Sony pads (raw DI order varies; add table entries
  when hardware exists to verify).
- Rumble, LEDs, touchpad, motion.
- PS-glyph labels on the CONTROLLER GUIDE screen (positions match; Xbox
  names shown; dual-labeling is a possible later polish).
- Native (non-Steam-Input) Steam Controller hidraw support.
