# Breakout for AAE — Design Spec

**Date:** 2026-07-14
**Author:** Tim Cottrill (with Claude)
**Status:** Approved design, pending implementation plan

## Goal

Add Atari Breakout (1976) to the AAE emulator as a **behavioral simulation** —
readable 2D paddle/ball/brick game logic — rendered through the *same* AAE
raster + mixer plumbing that `drivers/pong.cpp` uses. The target is "close
enough to dial in from reference video," **not** exact gate-level emulation.

Real Breakout is discrete logic (no CPU, no ROMs), like Pong. Unlike Pong,
MAME never shipped a gate-level Breakout sim; only DICE does
(`dice.0.9.src/games/breakout.cpp`, ~1500 lines of netlist `CONNECTION(...)`).
That netlist is exactly the exact-emulation path we are **not** taking. It is
used here only as a **reference** for authentic constants (brick geometry,
overlay colors, sound timing, orientation, gameplay triggers).

## Scope

**In (first version):** Core 1-player game.
- Paddle (analog horizontal), ball, 8 rows of bricks in 4 color bands.
- Authentic scoring, ball speed-ups, paddle-shrink on first top-wall hit.
- 3 balls (lives), score display, clear-the-wall handling, simple
  attract / game-over state.
- Pong-style square-wave sound for paddle / wall / brick events.

**Out (deliberately, per scope decision):** coins/credits, 2-player
alternating, DIP switches (ball count 3/5, bonus credit, cabinet type).
State is to be structured so these are addable later without a rewrite.

## Architecture

One new translation unit: `aae/aae/drivers/breakout.cpp`, structured like
`pong.cpp` at the seams so it drops into the build and menus identically:

- `init_breakout()` / `run_breakout()` / `end_breakout()` triad.
- `AAE_DRIVER_*` block, no CPUs (`AAE_CPU_NONE_ENTRY`), no ROMs.
- Self-registered via `AAE_REGISTER_DRIVER(drv_breakout)`.
- Back buffer via `osd_create_bitmap`; drawing via `plot_pixel`,
  `copybitmap`, `fillbitmap`.
- One 16-bit mixer stream per frame (`mixer_alloc_channel` +
  `stream_start`/`stream_update`/`stream_stop`), gated square waves.

**Key difference from Pong:** no 261-scanline sweep. `run_breakout()` performs
one fixed-timestep 2D update at 60 Hz (paddle, ball as floats, brick-grid
collision), then renders the frame. This is more readable and much easier to
tune from video than a gate-level scanline sweep.

## Orientation, playfield & rendering

- Author the bitmap in the game's **natural portrait coordinates**; set `ROT90`
  in `AAE_DRIVER_VIDEO_CORE` (AAE supports `ORIENTATION_SWAP_XY`; the vertical
  Namco raster games use it) so it displays vertically like reference footage.
- `VIDEO_TYPE_RASTER_COLOR` (Pong is `_BW`; Breakout needs colored rows).
- Layout, top → bottom: score digits; brick wall (8 rows / 4 color bands);
  large empty travel area; paddle sliding horizontally near the bottom; ball
  dies below the paddle. Side walls + top wall bounce the ball.
- Palette from the DICE overlay: red / orange(amber) / green / yellow bricks,
  blue paddle, white ball & walls. (Real machine: B/W tube + physical color
  overlay; we assign pen colors directly for the same visual result.)
- Score digits: adapt Pong's `pong_7seg`, or a compact digit font
  (implementation detail).

## Game logic (the tuning surface)

Classic Atari Breakout rules; constants seeded from DICE, then trimmed against
reference video:

- **Paddle:** analog horizontal control (analog input port, X axis). Serve
  button launches the ball; start button begins a game. Paddle **halves in
  width** the first time the ball hits the top wall.
- **Ball:** rebound angle set by *where* it strikes the paddle (center =
  steep, edges = shallow) — the primary feel knob. Bounces off side walls and
  the top; falling past the paddle loses a ball.
- **Bricks:** 8 rows x N columns. Scoring by band (target: yellow 1 / green 3 /
  orange 5 / red 7). Exact row count, column count, and point values to be
  **confirmed against the DICE overlay geometry + reference video** during
  implementation.
- **Ball speed-ups** at authentic triggers: after the 4th hit, after the 12th
  hit, on reaching the orange row, on reaching the red row.
- **3 balls (lives)**, score display, wall-cleared handling, attract /
  game-over state.

## Sound

Same synthesis model as Pong: gated square waves on one mixer stream, with
distinct tones for paddle bounce, wall bounce, and brick hit. Frequencies
seeded from the DICE 555 / 9602 monoflop timing, then tuned by ear against
video.

## Build & menu wiring

Register identically to Pong (`AAE_REGISTER_DRIVER`) and add the new TU +
game-list entry wherever `pong.cpp` is referenced (build script and/or
`game_list.h`). Exact locations to be found during planning so the game
actually appears in the menu.

## Defaults accepted at design time

1. 8 rows / 4 color bands / 1-3-5-7 scoring (verify exact figures from DICE
   overlay + video during implementation).
2. Paddle-strike position controls rebound angle (main feel knob).
3. Sound = Pong-style gated square waves, not sampled.

## Open items to resolve during implementation (not design blockers)

- Exact brick row/column counts, pixel dimensions, and point values (from DICE
  overlay geometry + video).
- Exact rebound-angle table and ball speed values (tuned from video).
- Score digit rendering choice (7-seg adaptation vs. small font).
- Precise sound frequencies (from DICE timers, tuned by ear).
- Build-system and game-list touch points (located during planning).
