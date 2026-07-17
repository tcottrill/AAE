# Breakout for AAE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Atari Breakout (1976) to AAE as a readable behavioral simulation that renders through the same raster + mixer plumbing as `drivers/pong.cpp`, tunable from reference video.

**Architecture:** One self-registering driver TU, `aae/aae/drivers/breakout.cpp`, with no CPUs and no ROMs. `run_breakout()` runs one fixed-timestep 2D update per 60 Hz frame (paddle, float-position ball, brick-grid collision), then draws a back-buffer bitmap and pushes one gated-square-wave audio frame. Portrait display via `ROT90` (bitmap authored landscape-in-memory like galaga/digdug).

**Tech Stack:** C++ (Visual Studio, `aae.vcxproj`), AAE raster bitmap API (`osd_create_bitmap`/`plot_pixel`/`copybitmap`/`fillbitmap`), AAE mixer (`mixer_alloc_channel`/`stream_*`), AAE driver macros (`AAE_DRIVER_*`), MAME-style input ports.

---

## Methodology note (read before starting)

This codebase has **no driver-level unit-test framework** — `pong.cpp` and every
other driver are verified by building and running the emulator. Formal TDD does
not apply here. Each task below is therefore a **coherent, independently
verifiable milestone**: the verification step is *build the project, launch the
game, and observe a named behavior*. Physics/collision live in small pure
helper functions so the logic is inspectable and cheap to reason about.

**Build command (all tasks, from `C:\Source2026\AAE_publish\aae`):**

```
msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

(Or build in Visual Studio with Ctrl+Shift+B. Substitute `Release` / `Win32`
to match how you normally run AAE.)

**Run:** launch the AAE build, open the game menu, select **Breakout**.

**Reference material (do not port, read for constants):** the DICE gate-level
netlist at `C:\Source2026\dice.0.9.src\games\breakout.cpp` — use it to confirm
overlay colors (red/orange/green/yellow bricks, blue paddle), portrait
orientation, and gameplay triggers. The design spec is
`docs/superpowers/specs/2026-07-14-breakout-aae-design.md`.

**All pixel geometry, speeds, and angles below are deliberate starting values.**
They are marked "TUNE" where you are expected to adjust them against video. They
are concrete (not placeholders) so the game runs immediately.

---

## File structure

- **Create:** `aae/aae/drivers/breakout.cpp` — the entire driver (state,
  rendering, physics, sound, palette, input ports, driver registration). Single
  file, matching how `pong.cpp` is organized.
- **Modify:** `aae/aae.vcxproj` (add `<ClCompile>` entry next to `pong.cpp` at
  line 324) — so the TU is compiled.
- **Modify:** `aae/aae.vcxproj.filters` (add matching entry next to `pong.cpp`
  at line ~373) — so it appears under the right filter in the IDE.

No other files change: the game menu builds itself from `AAE_REGISTER_DRIVER`.

---

## Shared conventions used throughout

**Coordinate system (game logic):**
- `d` = *depth* = vertical position on screen, `0` = top … `BM_W-1` = bottom
  (near the paddle). This maps to the bitmap's **width** axis.
- `x` = *lateral* = horizontal position on screen, `0` = left … `BM_H-1` =
  right. This maps to the bitmap's **height** axis.
- The bitmap is authored landscape-in-memory (`BM_W` wide × `BM_H` tall) and the
  driver sets `ROT90`, so memory-column `d` becomes the on-screen vertical axis.
  This is exactly the galaga/digdug convention (`osd_create_bitmap(36*8, 28*8)`
  + `ORIENTATION_ROTATE_90`).
- All drawing goes through one helper, `px(d, x, pen)`, so no other code needs
  to know about the axis swap.

**Layout constants (TUNE):**

```cpp
#define BM_W        256     // memory width  = on-screen vertical (depth) axis
#define BM_H        224     // memory height = on-screen horizontal (lateral) axis

#define WALL        8       // side/top wall thickness (pixels)
#define PLAY_L      (WALL)          // leftmost playable lateral (8)
#define PLAY_R      (BM_H - WALL)   // one past rightmost playable lateral (216)

#define SCORE_D     2       // score digits top edge (depth)
#define TOP_WALL_D  24      // top bounce surface (ball reflects when its top reaches here)

#define BRICK_ROWS  8
#define BRICK_COLS  13
#define BRICK_W     16      // (PLAY_R - PLAY_L) / BRICK_COLS = 208/13 = 16
#define BRICK_H     8
#define BRICK_TOP_D (TOP_WALL_D)          // first brick row starts just below the top wall (24)
// brick field occupies depth 24..(24+8*8-1)=87

#define PADDLE_D    236     // paddle top edge (depth)
#define PADDLE_TH   4       // paddle thickness (depth)
#define PADDLE_FULL 32      // full paddle width (lateral)
#define PADDLE_HALF 16      // shrunk paddle width after first top-wall hit
#define BALL_SZ     4       // ball is BALL_SZ x BALL_SZ pixels
#define LOSE_D      248     // ball below this depth = lost
```

**Palette / pens (index → color):**

```
0 = black (background)   1 = white (walls, ball, digits)   2 = red
3 = orange               4 = green                          5 = yellow
6 = blue (paddle)
```

Brick color by row (row 0 = top): rows 0-1 red (7 pts), rows 2-3 orange (5),
rows 4-5 green (3), rows 6-7 yellow (1). (TUNE: confirm row/point mapping from
video.)

---

## Task 1: Compiling, self-registering skeleton with palette

Goal: Breakout appears in the menu and shows a blank portrait field in the
correct orientation and background color.

**Files:**
- Create: `aae/aae/drivers/breakout.cpp`
- Modify: `aae/aae.vcxproj:324`
- Modify: `aae/aae.vcxproj.filters` (near line 373)

- [ ] **Step 1: Create the skeleton driver**

Create `aae/aae/drivers/breakout.cpp` with the full contents below. This
compiles, registers, allocates a portrait bitmap, clears it each frame, and
does nothing else yet.

```cpp
//==========================================================================
// Breakout (Atari 1976) -- behavioral simulation for AAE.
//
// Not a gate-level port. Real Breakout is discrete logic (no CPU, no ROMs);
// this driver reproduces the *gameplay* with clean 2D physics rendered through
// the same AAE raster + mixer plumbing that drivers/pong.cpp uses. The DICE
// netlist (dice.0.9.src/games/breakout.cpp) is the reference for authentic
// colors, orientation, and gameplay triggers -- none of its logic is ported.
//==========================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"

//--- Layout constants (TUNE) ----------------------------------------------
#define BM_W        256
#define BM_H        224
#define WALL        8
#define PLAY_L      (WALL)
#define PLAY_R      (BM_H - WALL)
#define SCORE_D     2
#define TOP_WALL_D  24
#define BRICK_ROWS  8
#define BRICK_COLS  13
#define BRICK_W     16
#define BRICK_H     8
#define BRICK_TOP_D (TOP_WALL_D)
#define PADDLE_D    236
#define PADDLE_TH   4
#define PADDLE_FULL 32
#define PADDLE_HALF 16
#define BALL_SZ     4
#define LOSE_D      248
#define BREAKOUT_FPS 60

//--- Draw helper: game (depth,lateral) -> rotated bitmap ------------------
static inline void px(int d, int x, int pen)
{
    if ((unsigned)d < (unsigned)BM_W && (unsigned)x < (unsigned)BM_H)
        plot_pixel(tmpbitmap, d, x, pen);
}

static void fill_rect(int d0, int x0, int d1, int x1, int pen)
{
    for (int d = d0; d < d1; d++)
        for (int x = x0; x < x1; x++)
            px(d, x, pen);
}

//==========================================================================
// Video
//==========================================================================
static int breakout_vh_start(void)
{
    tmpbitmap = osd_create_bitmap(BM_W, BM_H);
    if (!tmpbitmap)
        return 1;
    osd_clearbitmap(tmpbitmap);
    return 0;
}

static void breakout_vh_stop(void)
{
    if (tmpbitmap) { osd_free_bitmap(tmpbitmap); tmpbitmap = nullptr; }
}

static void breakout_draw(void)
{
    fillbitmap(tmpbitmap, Machine->pens[0], &Machine->drv->visible_area);

    // Placeholder so we can see orientation: draw the side + top walls.
    fill_rect(TOP_WALL_D - WALL, 0, TOP_WALL_D, BM_H, Machine->pens[1]); // top wall
    fill_rect(0, 0, BM_W, WALL, Machine->pens[1]);                        // left wall
    fill_rect(0, PLAY_R, BM_W, BM_H, Machine->pens[1]);                   // right wall

    copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0,
        &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}

//==========================================================================
// Palette
//==========================================================================
static const unsigned char breakout_palette[] =
{
    0x00,0x00,0x00,  // 0 black
    0xff,0xff,0xff,  // 1 white
    0xd8,0x30,0x20,  // 2 red
    0xe0,0x80,0x20,  // 3 orange
    0x30,0xc0,0x40,  // 4 green
    0xe0,0xe0,0x30,  // 5 yellow
    0x30,0x60,0xe0,  // 6 blue
};

static void breakout_init_palette(unsigned char* palette,
    unsigned char* colortable, const unsigned char* color_prom)
{
    memcpy(palette, breakout_palette, sizeof(breakout_palette));
}

//==========================================================================
// Driver glue
//==========================================================================
int init_breakout(void)
{
    LOG_INFO("INIT: Breakout Driver Init (behavioral simulation, no CPU)");
    if (breakout_vh_start())
        return 1;
    return 0;
}

void run_breakout(void)
{
    breakout_draw();
}

void end_breakout(void)
{
    breakout_vh_stop();
}

INPUT_PORTS_START(breakout)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON1)   // serve
PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN1")   // paddle: lateral position, PLAY_L..PLAY_R
PORT_ANALOG(0xff, (PLAY_L + PLAY_R) / 2, IPT_AD_STICK_X, 100, 5, PLAY_L, PLAY_R - 1)
INPUT_PORTS_END

AAE_DRIVER_BEGIN(drv_breakout, "breakout", "Breakout")
AAE_DRIVER_ROM(nullptr)
AAE_DRIVER_FUNCS(&init_breakout, &run_breakout, &end_breakout)
AAE_DRIVER_INPUT(input_ports_breakout)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_RASTER_COLOR, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(BM_W, BM_H, 0, BM_W - 1, 0, BM_H - 1)
AAE_DRIVER_RASTER(0, 7, 7, breakout_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_breakout)
```

- [ ] **Step 2: Wire the TU into the build**

In `aae/aae.vcxproj`, after line 324 (`<ClCompile Include="aae\drivers\pong.cpp" />`), add:

```xml
    <ClCompile Include="aae\drivers\breakout.cpp" />
```

In `aae/aae.vcxproj.filters`, find the `pong.cpp` entry (near line 373) and add
a sibling entry immediately after it, copying whatever `<Filter>` child the
`pong.cpp` element uses. Example (match the pong entry's filter text exactly):

```xml
    <ClCompile Include="aae\drivers\breakout.cpp">
      <Filter>Source Files\drivers</Filter>
    </ClCompile>
```

- [ ] **Step 3: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds with no errors; `breakout.cpp` compiles.

- [ ] **Step 4: Run and observe**

Launch AAE, open the menu. Expected: **Breakout** appears in the list. Select
it. Expected: a **portrait** (taller-than-wide) field, black background, with a
white top wall and white left/right side walls in the correct orientation
(walls vertical on the sides, one across the top). If it displays landscape or
mirrored/upside-down, switch `ORIENTATION_ROTATE_90` to `ORIENTATION_ROTATE_270`
and rebuild — that is the only orientation knob.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/drivers/breakout.cpp aae/aae.vcxproj aae/aae.vcxproj.filters
git commit -m "feat(breakout): compiling skeleton driver with portrait field and palette"
```

---

## Task 2: Static playfield — bricks, paddle, and score digits

Goal: draw the full static scene (colored brick wall, blue paddle centered,
score `000`) so every visual element and color is confirmed before anything
moves.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Add game-state variables and the digit font**

Add near the top of the file, after the draw helpers:

```cpp
//--- Game state -----------------------------------------------------------
static int  bricks[BRICK_ROWS][BRICK_COLS]; // 1 = present, 0 = cleared
static int  score;
static int  paddle_x;      // lateral of paddle left edge
static int  paddle_w;      // current paddle width (PADDLE_FULL or PADDLE_HALF)

// row -> pen and points (row 0 = top). TUNE against video.
static const int row_pen[BRICK_ROWS]    = { 2,2, 3,3, 4,4, 5,5 };
static const int row_points[BRICK_ROWS] = { 7,7, 5,5, 3,3, 1,1 };

//--- 3x5 digit font, one byte per row, low 3 bits = pixels (msb = left) ----
static const unsigned char digit_font[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b010,0b010,0b010}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

// draw one digit with each font pixel scaled to sc x sc, top-left at (d,x)
static void draw_digit(int d, int x, int n, int sc, int pen)
{
    n %= 10;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (digit_font[n][r] & (1 << (2 - c)))
                fill_rect(d + r * sc, x + c * sc, d + r * sc + sc, x + c * sc + sc, pen);
}

// draw `value` right-aligned ending at lateral x_right, top at depth d
static void draw_number(int d, int x_right, int value, int sc, int pen)
{
    int glyph_w = 3 * sc + sc;      // 3 columns + 1 column spacing
    int x = x_right - glyph_w;
    do {
        draw_digit(d, x, value % 10, sc, pen);
        value /= 10;
        x -= glyph_w;
    } while (value > 0 && x >= 0);
}
```

- [ ] **Step 2: Add a brick-wall initializer and call it from init**

Add:

```cpp
static void reset_wall(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            bricks[r][c] = 1;
}
```

In `init_breakout()`, after `breakout_vh_start()` succeeds, add:

```cpp
    score = 0;
    paddle_w = PADDLE_FULL;
    paddle_x = (PLAY_L + PLAY_R) / 2 - paddle_w / 2;
    reset_wall();
```

- [ ] **Step 3: Replace `breakout_draw()` with the full static scene**

Replace the body of `breakout_draw()` (keep the `fillbitmap` clear and the final
`copybitmap`) with:

```cpp
static void draw_bricks(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
    {
        int d0 = BRICK_TOP_D + r * BRICK_H;
        for (int c = 0; c < BRICK_COLS; c++)
        {
            if (!bricks[r][c]) continue;
            int x0 = PLAY_L + c * BRICK_W;
            // 1px gap on right/bottom so the wall reads as separate bricks
            fill_rect(d0, x0, d0 + BRICK_H - 1, x0 + BRICK_W - 1, Machine->pens[row_pen[r]]);
        }
    }
}

static void draw_paddle(void)
{
    fill_rect(PADDLE_D, paddle_x, PADDLE_D + PADDLE_TH, paddle_x + paddle_w,
        Machine->pens[6]);
}

static void breakout_draw(void)
{
    fillbitmap(tmpbitmap, Machine->pens[0], &Machine->drv->visible_area);

    // walls
    fill_rect(TOP_WALL_D - WALL, 0, TOP_WALL_D, BM_H, Machine->pens[1]);
    fill_rect(0, 0, BM_W, WALL, Machine->pens[1]);
    fill_rect(0, PLAY_R, BM_W, BM_H, Machine->pens[1]);

    draw_number(SCORE_D, (BM_H / 2), score, 3, Machine->pens[1]);
    draw_bricks();
    draw_paddle();

    copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0,
        &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}
```

Because `draw_bricks`/`draw_paddle`/`breakout_draw` reference each other, place
`draw_bricks` and `draw_paddle` **above** `breakout_draw` in the file.

- [ ] **Step 4: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 5: Run and observe**

Launch Breakout. Expected: 8 rows of bricks near the top in four color bands —
**red (top), orange, green, yellow (bottom)** — a blue paddle centered lower on
the field, and a `000` score at the top. Colors and layout should match your
reference footage. Adjust the layout `#define`s if proportions look off (TUNE).

- [ ] **Step 6: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): static playfield -- colored brick wall, paddle, score digits"
```

---

## Task 3: Paddle control

Goal: the analog control moves the paddle laterally, clamped between the walls.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Read the paddle input each frame**

Add this helper above `run_breakout()`:

```cpp
static void update_paddle(void)
{
    int pos = (int)readinputportbytag("IN1");   // center of paddle, lateral
    paddle_x = pos - paddle_w / 2;
    if (paddle_x < PLAY_L)            paddle_x = PLAY_L;
    if (paddle_x > PLAY_R - paddle_w) paddle_x = PLAY_R - paddle_w;
}
```

- [ ] **Step 2: Call it from the frame loop**

Change `run_breakout()` to:

```cpp
void run_breakout(void)
{
    update_paddle();
    breakout_draw();
}
```

- [ ] **Step 3: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 4: Run and observe**

Launch Breakout. Move the paddle control (mouse/analog per your AAE input
config). Expected: the blue paddle slides left/right and **stops flush against
each side wall** without overlapping it. If the direction is inverted, that is
an input-mapping preference, not a bug here.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): analog paddle control with wall clamping"
```

---

## Task 4: Ball, serve state machine, and wall/paddle bounce

Goal: pressing Start begins a game; the ball rests on the paddle; pressing Serve
launches it; it bounces off the side and top walls and off the paddle, and is
lost below the paddle.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Add ball state and the game-phase enum**

Add to the game-state block:

```cpp
enum { ST_ATTRACT, ST_SERVE_WAIT, ST_PLAYING, ST_GAMEOVER };
static int   state;
static int   lives;
static float ball_d, ball_x;    // ball top-left (depth, lateral), float
static float vel_d, vel_x;      // velocity per frame
static float ball_speed;        // current speed magnitude (px/frame)
static int   prev_start, prev_serve;

#define BALL_BASE_SPEED 1.6f    // TUNE
```

- [ ] **Step 2: Add serve / edge-detect / reset helpers**

```cpp
static void place_ball_on_paddle(void)
{
    ball_x = paddle_x + paddle_w / 2 - BALL_SZ / 2;
    ball_d = PADDLE_D - BALL_SZ;
    vel_d = vel_x = 0.0f;
}

static void serve_ball(void)
{
    ball_speed = BALL_BASE_SPEED;
    vel_d = -ball_speed;                 // upward (toward bricks)
    vel_x = (paddle_x & 1) ? 0.4f : -0.4f; // slight sideways kick; TUNE
    state = ST_PLAYING;
}

static void start_new_game(void)
{
    score = 0;
    lives = 3;                 // TUNE (real machine: 3 or 5 via DIP; fixed 3 here)
    paddle_w = PADDLE_FULL;
    reset_wall();
    place_ball_on_paddle();
    state = ST_SERVE_WAIT;
}

// returns 1 on the rising edge of button `cur` given previous `prev`
static int rising(int cur, int* prev)
{
    int edge = (cur && !*prev);
    *prev = cur;
    return edge;
}
```

- [ ] **Step 3: Add the ball-physics update**

Add `update_ball()` (wall + paddle bounce only; brick collision comes in
Task 5):

```cpp
static void update_ball(void)
{
    ball_d += vel_d;
    ball_x += vel_x;

    // side walls
    if (ball_x < PLAY_L)               { ball_x = PLAY_L;               vel_x = -vel_x; }
    if (ball_x > PLAY_R - BALL_SZ)     { ball_x = PLAY_R - BALL_SZ;     vel_x = -vel_x; }

    // top wall
    if (ball_d < TOP_WALL_D)           { ball_d = TOP_WALL_D;           vel_d = -vel_d; }

    // paddle
    if (vel_d > 0 &&
        ball_d + BALL_SZ >= PADDLE_D &&
        ball_d + BALL_SZ <= PADDLE_D + PADDLE_TH &&
        ball_x + BALL_SZ > paddle_x &&
        ball_x < paddle_x + paddle_w)
    {
        ball_d = PADDLE_D - BALL_SZ;
        vel_d = -fabsf(vel_d);          // bounce up; angle refined in Task 6
    }

    // lost
    if (ball_d > LOSE_D)
    {
        if (--lives <= 0) { state = ST_GAMEOVER; }
        else { place_ball_on_paddle(); state = ST_SERVE_WAIT; }
    }
}
```

Add `#include <math.h>` at the top with the other includes (for `fabsf`).

- [ ] **Step 4: Drive the state machine from `run_breakout()`**

Replace `run_breakout()`:

```cpp
void run_breakout(void)
{
    int start = (int)(readinputportbytag("IN0") & 1);
    int serve = (int)(readinputportbytag("IN0") & 2);

    switch (state)
    {
    case ST_ATTRACT:
    case ST_GAMEOVER:
        if (rising(start, &prev_start)) start_new_game();
        break;
    case ST_SERVE_WAIT:
        update_paddle();
        place_ball_on_paddle();          // ball tracks paddle until served
        if (rising(serve, &prev_serve)) serve_ball();
        break;
    case ST_PLAYING:
        update_paddle();
        update_ball();
        break;
    }
    // keep edge trackers fresh in states that didn't read them
    prev_start = start; prev_serve = serve;

    breakout_draw();
}
```

Initialize the new state in `init_breakout()` (after `reset_wall()`):

```cpp
    lives = 3;
    state = ST_ATTRACT;
    prev_start = prev_serve = 0;
    place_ball_on_paddle();
```

- [ ] **Step 5: Draw the ball**

Add a `draw_ball()` above `breakout_draw()` and call it from `breakout_draw()`
(after `draw_paddle()`), but only when a ball is in play or waiting:

```cpp
static void draw_ball(void)
{
    if (state == ST_PLAYING || state == ST_SERVE_WAIT)
        fill_rect((int)ball_d, (int)ball_x,
                  (int)ball_d + BALL_SZ, (int)ball_x + BALL_SZ, Machine->pens[1]);
}
```

- [ ] **Step 6: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 7: Run and observe**

Launch Breakout. Press Start. Expected: the ball sits on the paddle and tracks
it. Press Serve. Expected: the ball launches upward, bounces off the top and
side walls and off the paddle, and — when it falls past the paddle — a life is
consumed and it re-serves; after 3 losses it stops (game over). Bricks do not
react yet (that's Task 5).

- [ ] **Step 8: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): ball, serve state machine, wall and paddle bounce"
```

---

## Task 5: Brick collision, scoring, and score display

Goal: the ball destroys bricks it touches, the score increases by the row value,
and the wall visibly empties.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Add a brick-hit test/response helper**

Add above `update_ball()`:

```cpp
// Map a pixel (depth,lateral) to a brick cell; return 1 and set r,c if the
// pixel lies on a present brick.
static int brick_at(int d, int x, int* r, int* c)
{
    if (d < BRICK_TOP_D || d >= BRICK_TOP_D + BRICK_ROWS * BRICK_H) return 0;
    if (x < PLAY_L || x >= PLAY_R) return 0;
    int rr = (d - BRICK_TOP_D) / BRICK_H;
    int cc = (x - PLAY_L) / BRICK_W;
    if (rr < 0 || rr >= BRICK_ROWS || cc < 0 || cc >= BRICK_COLS) return 0;
    if (!bricks[rr][cc]) return 0;
    *r = rr; *c = cc;
    return 1;
}

// Test the ball's leading edge against the wall; on a hit, clear the brick,
// score it, reflect vertically, and return 1.
static int hit_bricks(void)
{
    int r, c;
    int cd = (int)(ball_d + BALL_SZ / 2);      // ball center depth
    int lead = (vel_d < 0) ? (int)ball_d : (int)(ball_d + BALL_SZ); // leading edge
    int cx = (int)(ball_x + BALL_SZ / 2);

    if (brick_at(lead, cx, &r, &c) || brick_at(cd, cx, &r, &c))
    {
        bricks[r][c] = 0;
        score += row_points[r];
        vel_d = -vel_d;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Call `hit_bricks()` from `update_ball()`**

In `update_ball()`, immediately after the position update
(`ball_d += vel_d; ball_x += vel_x;`) and before the wall checks, add:

```cpp
    hit_bricks();
```

- [ ] **Step 3: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 4: Run and observe**

Launch, Start, Serve. Expected: when the ball reaches the wall it **removes the
brick it touches**, bounces back down, and the **score increases** by 7/5/3/1
depending on the row color (red highest). The score digits at the top update.
Play a few balls and confirm bricks across the whole width can be cleared.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): brick collision, per-row scoring, live score display"
```

---

## Task 6: Gameplay nuances — angle control, speed-ups, paddle shrink, second screen

Goal: make it *play* like Breakout. Paddle-strike position sets the rebound
angle; the ball speeds up at the authentic triggers; the paddle halves after the
first top-wall hit; clearing the wall gives a second screen, after which the
game ends.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Add nuance state**

Add to the game-state block:

```cpp
static int hit_count;        // paddle hits this ball (for 4th/12th speed-ups)
static int reached_orange;   // one-shot speed-up flags
static int reached_red;
static int topwall_hit;      // paddle already shrunk this game?
static int screen_num;       // 0 = first wall, 1 = second wall

// speed schedule (TUNE)
#define SPEED_STEP   0.5f
#define SPEED_MAX    3.2f
```

Reset them in `start_new_game()` (and reset `hit_count`, keep `screen_num`
handling): add at the end of `start_new_game()`:

```cpp
    hit_count = 0;
    reached_orange = reached_red = 0;
    topwall_hit = 0;
    screen_num = 0;
```

Also reset `hit_count = 0;` inside `place_ball_on_paddle()` so each new ball
starts fresh.

- [ ] **Step 2: Angle-from-paddle rebound**

Replace the paddle-bounce block in `update_ball()` with:

```cpp
    if (vel_d > 0 &&
        ball_d + BALL_SZ >= PADDLE_D &&
        ball_d + BALL_SZ <= PADDLE_D + PADDLE_TH &&
        ball_x + BALL_SZ > paddle_x &&
        ball_x < paddle_x + paddle_w)
    {
        ball_d = PADDLE_D - BALL_SZ;

        // offset -1.0 (left edge) .. +1.0 (right edge) of paddle center
        float center = paddle_x + paddle_w / 2.0f;
        float off = ((ball_x + BALL_SZ / 2.0f) - center) / (paddle_w / 2.0f);
        if (off < -1.0f) off = -1.0f;
        if (off >  1.0f) off =  1.0f;

        // steeper (more vertical) at center, shallow at edges. TUNE the 0.85.
        vel_x = off * ball_speed * 0.85f;
        float vd2 = ball_speed * ball_speed - vel_x * vel_x;
        vel_d = -sqrtf(vd2 > 0.01f ? vd2 : 0.01f);   // always upward

        if (++hit_count == 4 || hit_count == 12)      // authentic speed-ups
        {
            ball_speed = fminf(ball_speed + SPEED_STEP, SPEED_MAX);
        }
    }
```

- [ ] **Step 3: Paddle shrink on first top-wall hit**

Replace the top-wall block in `update_ball()` with:

```cpp
    if (ball_d < TOP_WALL_D)
    {
        ball_d = TOP_WALL_D;
        vel_d = -vel_d;
        if (!topwall_hit) { topwall_hit = 1; paddle_w = PADDLE_HALF; }
    }
```

Note: after shrinking, `update_paddle()` already re-clamps using `paddle_w`, so
nothing else is needed.

- [ ] **Step 4: Row-based speed-ups when a brick is destroyed**

In `hit_bricks()`, after `score += row_points[r];`, add:

```cpp
        // rows 2-3 = orange, rows 0-1 = red (row 0 = top). One-shot each.
        if (!reached_orange && r <= 3) { reached_orange = 1; ball_speed = fminf(ball_speed + SPEED_STEP, SPEED_MAX); }
        if (!reached_red    && r <= 1) { reached_red    = 1; ball_speed = fminf(ball_speed + SPEED_STEP, SPEED_MAX); }
```

Because `hit_bricks()` reflects with `vel_d = -vel_d` (keeping magnitude), the
new `ball_speed` takes effect on the next paddle bounce. That matches the feel
of the original closely enough (TUNE: if you want the speed to jump immediately,
rescale `vel_d`/`vel_x` to the new `ball_speed` here).

- [ ] **Step 5: Wall-cleared → second screen → game over**

Add a helper and call it at the end of `update_ball()`:

```cpp
static int wall_empty(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            if (bricks[r][c]) return 0;
    return 1;
}
```

At the very end of `update_ball()` (after the "lost" check), add:

```cpp
    if (state == ST_PLAYING && wall_empty())
    {
        if (screen_num == 0)
        {
            screen_num = 1;
            reset_wall();
            reached_orange = reached_red = 0;   // second wall can speed up again
            place_ball_on_paddle();
            state = ST_SERVE_WAIT;
        }
        else
        {
            state = ST_GAMEOVER;                // two screens cleared: game ends
        }
    }
```

- [ ] **Step 6: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 7: Run and observe (tune against video here)**

Launch and play. Confirm each behavior:
- Hitting the ball with the **paddle edge** sends it out at a **shallow angle**;
  hitting with the **center** sends it **steep/vertical**.
- Ball **speeds up** noticeably after several paddle hits and again once you
  start breaking into the **orange** and **red** rows.
- The **first time the ball touches the top wall the paddle halves** in width.
- **Clearing the whole wall** re-fills it once (second screen); clearing it
  again ends the game.
This is the main tuning task — adjust `BALL_BASE_SPEED`, `SPEED_STEP`,
`SPEED_MAX`, and the `0.85f` angle factor until it matches your reference
footage.

- [ ] **Step 8: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): rebound angle, speed-ups, paddle shrink, second screen"
```

---

## Task 7: Sound

Goal: paddle bounce, wall bounce, and brick hit each make a short tone, reusing
Pong's proven synthesis. Frequencies are seeded from Pong (which you confirmed
sounds right) and tuned by ear.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Add the sound module (adapted from `pong_sh_*`)**

Add this block (it mirrors `pong.cpp`'s three-square-wave mixer, with three
Breakout event gates). Place it above the driver-glue section:

```cpp
//==========================================================================
// Sound: three gated square waves on one mixer stream (adapted from pong.cpp)
//==========================================================================
static int      bo_channel = -1;
static int16_t* bo_frame_buf = nullptr;
static int      bo_frame_len = 0;
static double   bo_phase[3] = { 0, 0, 0 };

// event gates: set to a small positive frame count when the event fires
static int bo_paddle_snd = 0;   // paddle bounce
static int bo_wall_snd   = 0;   // wall bounce
static int bo_brick_snd  = 0;   // brick hit

// tone frequencies (Hz). TUNE by ear against video / against pong's tones.
static const double bo_freq[3] = { 240.0, 180.0, 480.0 };  // paddle, wall, brick

static int bo_sh_start(void)
{
    bo_frame_len = config.samplerate / BREAKOUT_FPS;
    bo_channel = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
    if (bo_channel < 0) { LOG_INFO("Breakout: no free mixer channel"); return 1; }
    bo_frame_buf = (int16_t*)malloc(bo_frame_len * sizeof(int16_t));
    if (!bo_frame_buf) return 1;
    memset(bo_frame_buf, 0, bo_frame_len * sizeof(int16_t));
    stream_start(bo_channel, 0, 16, BREAKOUT_FPS, /*stereo=*/false);
    return 0;
}

static void bo_sh_stop(void)
{
    if (bo_channel >= 0) stream_stop(bo_channel, 0);
    bo_channel = -1;
    free(bo_frame_buf);
    bo_frame_buf = nullptr;
}

static void bo_sh_update(void)
{
    const int gate[3] = { bo_paddle_snd, bo_wall_snd, bo_brick_snd };
    const int amp = 6000;
    if (bo_channel < 0) return;

    for (int s = 0; s < bo_frame_len; s++)
    {
        int v = 0;
        for (int t = 0; t < 3; t++)
        {
            bo_phase[t] += bo_freq[t] / (double)config.samplerate;
            if (bo_phase[t] >= 1.0) bo_phase[t] -= 1.0;
            if (gate[t]) v += (bo_phase[t] < 0.5) ? amp : -amp;
        }
        bo_frame_buf[s] = (int16_t)v;
    }
    stream_update(bo_channel, bo_frame_buf);

    // gates are one-shot: each is a short countdown of frames
    if (bo_paddle_snd > 0) bo_paddle_snd--;
    if (bo_wall_snd   > 0) bo_wall_snd--;
    if (bo_brick_snd  > 0) bo_brick_snd--;
}
```

- [ ] **Step 2: Trigger the gates from physics**

- In `update_ball()`, side-wall and top-wall bounces: after each reflection set
  `bo_wall_snd = 2;`
- In the paddle-bounce block: set `bo_paddle_snd = 2;`
- In `hit_bricks()` on a hit: set `bo_brick_snd = 2;`

(2 frames ≈ 33 ms tone; TUNE the count for length.)

- [ ] **Step 3: Wire start/stop/update into the driver**

In `init_breakout()`, after video start succeeds, add `bo_sh_start();`
In `end_breakout()`, add `bo_sh_stop();` before `breakout_vh_stop();`
At the end of `run_breakout()`, after `breakout_draw();`, add `bo_sh_update();`

- [ ] **Step 4: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 5: Run and observe**

Launch and play. Expected: distinct short tones on paddle bounce, wall bounce,
and brick hit. TUNE `bo_freq[]` and the gate frame counts by ear until they feel
like the reference. (If Pong's exact tones are wanted, copy Pong's `HIT_CLOCK` /
`VBLANK_CLOCK` / `SCORE_CLOCK` values into `bo_freq[]`.)

- [ ] **Step 6: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): paddle/wall/brick sound via gated square waves"
```

---

## Task 8: Attract-mode polish and final tuning pass

Goal: the game reads well before Start is pressed, and all tuning constants are
grouped for easy adjustment against video.

**Files:**
- Modify: `aae/aae/drivers/breakout.cpp`

- [ ] **Step 1: Draw a static wall + centered paddle in attract**

In `breakout_draw()`, the brick wall and paddle already draw in all states,
which is fine for attract. Ensure the score shows `0` and no ball is drawn in
`ST_ATTRACT`/`ST_GAMEOVER` (already handled by `draw_ball()`'s state check).
Optionally, in `ST_GAMEOVER`, keep the final score visible (already true — the
`score` variable is not reset until the next `start_new_game()`).

- [ ] **Step 2: Group the tuning constants**

Confirm every value marked **TUNE** in this plan is a `#define` or a single
named `static const` near the top of the file, so a tuning pass against video
touches only that block. If any are inline literals, hoist them to named
constants now. No behavior change.

- [ ] **Step 3: Build**

Run: `msbuild aae.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
Expected: builds clean.

- [ ] **Step 4: Full playthrough against reference video**

Play a complete game start-to-finish alongside a Breakout gameplay video.
Adjust the tuning block until layout, ball feel, speed progression, and sounds
match to your satisfaction. This is expected, iterative, and the whole point of
the "simulation, close enough" approach.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/drivers/breakout.cpp
git commit -m "feat(breakout): attract polish and grouped tuning constants"
```

---

## Self-review notes (traceability to the spec)

- **Behavioral sim, Pong-style plumbing** → Tasks 1, 2, 7 (same bitmap/mixer/
  macro patterns as `pong.cpp`).
- **Portrait orientation** → Task 1 (`ROT90`, galaga/digdug convention, single
  `px()` axis-swap helper).
- **8 rows / 4 color bands / 1-3-5-7 scoring** → Task 2 (`row_pen`,
  `row_points`), Task 5 (scoring).
- **Analog horizontal paddle, serve/start** → Task 3, Task 4 (input ports in
  Task 1).
- **Paddle-strike rebound angle (main feel knob)** → Task 6 Step 2.
- **Ball speed-ups (4th/12th hit, orange, red)** → Task 6 Steps 2, 4.
- **Paddle shrink on first top-wall hit** → Task 6 Step 3.
- **3 balls, wall-clear → second screen → game over** → Task 4, Task 6 Step 5.
- **Sound = gated square waves** → Task 7.
- **Build + menu wiring** → Task 1 Step 2 (`AAE_REGISTER_DRIVER` auto-populates
  the menu; only `.vcxproj`/`.filters` compile entries are manual).
- **Out of scope (coins, 2-player, DIPs)** → intentionally absent; state
  variables (`screen_num`, `lives`, fixed 3 balls) are structured to extend
  later.
```
