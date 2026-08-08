# Controller Help Screen — Design

**Date:** 2026-08-08
**Status:** Architecture approved; the button/chord *labels* below show today's
defaults and will be updated once the owner finishes rethinking button
assignments (in progress 2026-08-08). Everything else is final.

## Goal

Gamepad users navigating AAE from the couch (game list, starting games, coins,
menu, exit) have no way to discover the pad shortcuts. Add a one-page
CONTROLLER GUIDE screen that teaches the existing scheme. **No input mappings
change** — a conventional remap (Back=coin, per-pad starts) was designed,
considered, and rejected by the owner; the current AAE scheme stays as-is.

## The screen

One unified page drawn entirely with the existing vector primitives (VF fonts,
quads, and the beam line renderer) in the 1024x768 VF coordinate space, so it
renders identically on the OpenGL and Vulkan backends with no new texture
plumbing. `xbox_controller.png` in the repo root is the *tracing source only*
— during implementation it is traced once, offline, into a static table of
line segments compiled into the binary; the PNG itself does not ship.

Visual layout (mock: `.superpowers/brainstorm/*/content/final-screen-v3.html`):

- Title `CONTROLLER GUIDE` (cyan), centered controller line art (green, beam
  glow), red leader lines to white button labels.
- Button callouts (current defaults from `inptport.cpp`):
  - `BACK = 1P START`
  - `START = 2P START`
  - `RS CLICK = COIN` (Coin 1)
  - `LS CLICK = COIN 2`
  - `MOVE = LS/DPAD`
  - `A/B/X/Y = FIRE` (game buttons 1-4)
- Chord table (yellow, from `joystick.h`):
  - `LS CLICK + START ... MENU`
  - `LS CLICK + BACK .... EXIT GAME`
  - `START + BACK ....... PAUSE`
- Game-list section: `A = PLAY    B = MENU    Y = THIS SCREEN`
- Footer (gray): `PRESS ANY BUTTON TO CLOSE`

Labels are literal button names for an XInput pad. Users who rebind via JOY
CONFIG will see default names regardless — accepted limitation, noted here
deliberately (the screen documents defaults, it is not a live binding viewer).

## When it appears

1. **First pad detection (one-shot).** New `aae.ini` flag
   `[main] controller_help_shown` (default 0). The screen auto-shows over
   whatever runs first — the GUI frontend or a command-line-launched game —
   only when a gamepad is connected (`num_joysticks > 0`) and the flag is 0.
   Dedicated cabinets with no pad never see it. If the existing first-run
   credit notice is also pending, the credit notice shows and is dismissed
   first; the controller guide appears on a later frame, never stacked.
2. **On demand in the GUI.** Pressing Y (Joy Fire4) in the game-list frontend
   opens it; the GUI footer gains a short `Y=HELP` hint.
3. **On demand anywhere via menu.** New top-level menu entry
   `CONTROLLER HELP`, which also makes it reachable mid-game (LS+Start opens
   the menu).

## Dismissal

Copies the proven first-run notice contract in `menu.cpp`:

- Arm only once no key/button is held (so the opening press never instantly
  dismisses it), then any key or joystick button closes it.
- While active, a `controller_help_active()` gate is exported and checked by
  the same input handlers that honor `first_run_notice_active()`, so the
  dismissing press is swallowed — it must not also launch a game, toggle the
  menu, or insert a coin.
- First-run showing writes `controller_help_shown=1` on dismiss. On-demand
  showings do not touch the flag.

## New module

`aae/aae/controller_help.cpp` / `.h` (kept out of the already-large
`menu.cpp`):

- `controller_help_active()` / `controller_help_open()` /
  `do_the_controller_help()` — same shape as the first-run notice API.
- Static controller trace data (line segments, normalized coordinates) plus
  the callout/label table live in this module.
- Colors reuse the menu palette constants (RGB_WHITE, RGB_YELLOW, panel blue,
  etc.) plus the green used for the controller body.

## Files touched

| File | Change |
|---|---|
| `aae/aae/controller_help.cpp/.h` | new module (screen + trace data + gate) |
| `aae/aae/menu.cpp` | `CONTROLLER HELP` menu entry; call help gate alongside first-run gate |
| `aae/aae/gui/driver_gui.cpp` | Y opens help; `Y=HELP` footer hint |
| `aae/aae/config.cpp/.h` | `controller_help_shown` ini flag |
| `aae/aae/aae_emulator.cpp` | per-frame hook: draw/first-run trigger, input swallow gate |
| `aae/aae.vcxproj` (+filters) | add new files |
| `CHANGELOG.txt` | entry |

No changes to `joystick.h`, `inptport.cpp` defaults, or any chord definitions.

## Verification

- Windows, both renderers (GL and Vulkan): screen draws correctly, glow
  matches menu aesthetics, text legible at 1024x768 and fullscreen.
- First-run: with a pad connected and flag 0 it appears once (after the
  credit notice if that is also pending), dismisses on any button, flag
  written, never reappears. With no pad connected it never appears and the
  flag stays 0 (so a later first pad connect still triggers it).
- Dismiss swallow: closing it with A in the GUI does not launch the selected
  game; closing it with Start in-game does not insert a 2P start.
- On-demand: Y in GUI opens/closes; menu entry works in the GUI and mid-game.
- Keyboard-only session: Y still opens it in the GUI (harmless), no footer
  regression.

## Out of scope (recorded decisions)

- **Input remapping.** A full conventional remap (Back=coin, Start=1P,
  per-pad starts, Back+Start=exit, release-aware taps) was designed and
  rejected: the owner prefers the existing scheme. Do not resurrect without
  being asked.
- **Bumpers (LB/RB).** Confirmed free real estate: the only driver declaring
  buttons 5/6 is the Cinematronics keypad, bound `IP_JOY_NONE` (keyboard
  only). The owner plans to assign the bumpers to something later; nothing in
  this feature may claim them.
- PS4/PS5-specific glyphs (screen shows Xbox names; DualShock/DualSense via
  XInput wrappers read fine as-is).
- WinMM-path chord support (unchanged; chords remain XInput-only).
- Live rebinding display on the help screen.
