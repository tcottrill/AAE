# OSD Contract (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define a real MAME-style `src/emu` ↔ `src/osd` boundary in AAE — one contract header, no `windows.h` or audio-mixer dependency in the emulation core, and a clean vector seam — with zero change in emulator behavior.

**Architecture:** Five independent refactors, each verified by the build plus a permanent `#error` leak guard. `osdepend.h` becomes the single contract header declaring all 29 `osd_*` functions. `rawinput.h` splits into a platform-neutral `sys_input.h` (with an `AaeKey` enum replacing the `KEY_*` macros that collide with Linux) and a Win32-private `rawinput_win32.h`. `framework.h` and `mixer.h` are removed from the core's master header. `emu_vector_draw.h` splits along the emu/render line, exposing `add_line()` as the seam a Teensy DAC backend will implement.

**Tech Stack:** C++17 / MSVC 2022, MSBuild, x64 only. **No test framework** — the tests are the build plus `#error` leak guards, following the idiom established by `docs/superpowers/plans/2026-07-11-gl-header-scrub.md`.

**Spec:** `docs/superpowers/specs/2026-07-28-osd-contract-design.md`

---

## Conventions for every task

**The build command ("the build"):**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

**Pass state:** exit code 0, and exactly these **six** pre-existing warning lines (measured on this branch 2026-07-28, before any Phase 1 change):

```
cpu_i8085.cpp(452,11): warning C4101: 'temp32': unreferenced local variable
foodf.cpp(76,2):       warning C4333: '>>': right shift by too large amount, data loss
pacman.cpp(104,16):    warning C4018: '<': signed/unsigned mismatch
pacman.cpp(716,16):    warning C4018: '<': signed/unsigned mismatch
phoenix.cpp(392,12):   warning C4018: '<': signed/unsigned mismatch
gaplus_video.cpp(23,16): warning C4018: '<': signed/unsigned mismatch
```

Six *lines* across five *files* — `pacman.cpp` contributes two. Any **seventh** warning line is a regression. Do not attempt to fix these six; they are out of scope.

**Do NOT edit `aae/aae.vcxproj`.** It is tangled with driver work in progress. New headers do not need vcxproj entries to compile. New `.cpp` files would — this plan creates none.

**x86/Win32 configurations are known-broken** and are not a gate. Only `x64` matters.

**TDD adaptation.** There is no unit-test framework here. Each task's "failing test" is a compile-time `#error` guard that proves the boundary is currently violated. Sequence is always: add the guard → build → observe the **expected failure** → do the refactor → build → observe **pass**.

**GUARD PLACEMENT — read this before writing any guard.** A guard goes **immediately after the file's own `#include` block**, never before it. The preprocessor is a single forward pass: a guard placed above the includes is evaluated before any header has been read, so the macro it tests cannot possibly be defined yet and the guard silently passes no matter what. It would be a test that can never fail — worse than no test.

Placed *after* the includes, the guard asserts the thing we actually care about: **"nothing I include drags in the forbidden dependency."** That is exactly the boundary property each task establishes.

For a **header**, the same rule applies to that header's own includes — put the guard after them, at the point where the header has finished declaring its dependencies.

A guard that passes the moment you correctly place it means the boundary is already clean and the task has nothing to prove — stop and report that, do not proceed as if it failed.

---

## File Structure

| file | status | responsibility |
|---|---|---|
| `aae/aae/osdepend.h` | modify | **The contract.** All 29 `osd_*` declarations, `struct osd_bitmap`, existing `OSD_KEY_*`/`OSD_JOY_*` logical codes. Platform-neutral. |
| `aae/system/input/sys_input.h` | **create** | Platform-neutral input API: `AaeKey` enum, `key[256]`, `mouse_b`, queries, callbacks, multi-HID `*_Ex`. No `windows.h`. |
| `aae/system/input/rawinput_win32.h` | **create** | Win32-only raw-input plumbing. Included by `winmain.cpp` alone. |
| `aae/system/input/rawinput.h` | delete | Replaced by the two above. |
| `aae/system/input/rawinput.cpp` | modify | Includes both new headers; implementation unchanged. |
| `aae/aae/vidhrdwr/emu_vector_draw.h` | modify | Emu-side vector seam only: `add_line`, `add_tex`, `cache_clear`, `set_texture_id`, `vec_colors`, `colors`. |
| `aae/aae/aae_video/vector_draw_gl.h` | **create** | Render-side: `draw_textured_shots`, `modulate_color`, `txdata`. |
| `aae/aae/aae_mame_driver.h` | modify | Drop `framework.h` (line 19) and `mixer.h` (line 31). |
| `aae/aae/vidhrdwr/osd_video.h` | modify | Drop dead includes at lines 61–63. |
| `aae/aae/os_input.h`, `fileio/mame_fileio.h`, `vidhrdwr/old_mame_raster.h`, `led_service_handler.h` | modify | Remove their `osd_*` declarations; include `osdepend.h`. |
| 19 audio-using `.cpp` files | modify | Gain their own `#include "mixer.h"`. |

---

## Task 0: Establish the baseline

No code change. This exists so that every later "did I break it?" has an answer.

**Files:** none

- [ ] **Step 1: Confirm a clean tree for the files we will touch**

```bash
git status --short aae/ | head -20
```

Expected: `aae/aae.vcxproj`, `aae/aae/drivers/bwidow.cpp`, `aae/aae/led_service_handler.cpp` show as modified (pre-existing work, leave alone). Nothing else under `aae/` should be dirty. If other files are dirty, stop and ask before proceeding.

- [ ] **Step 2: Run the build**

Run the build command from *Conventions* above.
Expected: exit code 0, exactly the six known warning lines listed in *Conventions*.

**STATUS: Task 0 was completed by the controller on 2026-07-28.** Baseline confirmed: exit code 0, six warning lines across five files, on branch `refactor/osd-contract-phase1`. The carried-over WIP files (`aae.vcxproj`, `drivers/bwidow.cpp`, `led_service_handler.cpp` modified; `drivers/tempest_with_random_checks.cpp`, `sndhrdwr/generic.{cpp,h}` untracked) are pre-existing and must be left alone.

If a later build shows a seventh warning, that is a regression introduced by the task in progress.

---

## Task 1: `AaeKey` enum replaces the `KEY_*` macros

The `KEY_*` macros collide with Linux's `<linux/input-event-codes.h>`, which defines the same identifiers with different values (`KEY_A` is 30 there, `0x41` here). Macros leak into every header downstream; an enum cannot. **Values are unchanged — this is a rename, not a renumber.**

**Files:**
- Modify: `aae/system/input/rawinput.h` (replace lines 390–510, the `KEY_*` block)
- Modify: `aae/aae/acommon.cpp:128-131`
- Modify: `aae/aae/os_input.cpp:126,127,133,192,193,194`

**Note on scope:** `aae/aae/osdepend.h:151-154` mention `KEY_RCONTROL` etc. **inside comments only** — do not change them, they are historical notes. `aae/system/input/rawinput.cpp` lines 219/231/242/282 use `KEY_READ`, which is the **Win32 registry access constant from `<winreg.h>`**, completely unrelated to this set — do not touch it.

- [ ] **Step 1: Write the failing guard**

Add to `aae/aae/acommon.cpp` **immediately after its last `#include`** (see *Guard placement* in Conventions — above the includes it can never fire):

```c
// Boundary guard: nothing acommon.cpp includes may still define the legacy
// KEY_* macros, which collide with <linux/input-event-codes.h>.
#ifdef KEY_A
#error "legacy KEY_* macros still present - port this file to AaeKey"
#endif
```

- [ ] **Step 2: Build and confirm it fails**

Run the build.
Expected: **FAIL** — `error C1189: #error: "legacy KEY_* macros still present - port this file to AaeKey"` in `acommon.cpp`. This proves the guard is live and the boundary is genuinely violated today.

- [ ] **Step 3: Replace the macro block with the enum**

In `aae/system/input/rawinput.h`, delete everything from `#define KEY_A 0x41` through `#define KEY_MAX 0xEF` (lines 390–510) and put this in its place:

```c
// ---------------------------------------------------------------------------
// AaeKey - AAE's canonical physical key codes.
//
// Values are historical Windows VK codes and MUST NOT change: they are what
// gets written to default.cfg / per-game .cfg via writeword() in inptport.cpp,
// and what menu.cpp's key_names[] table is indexed by.
//
// This is an UNSCOPED enum on purpose - the values implicitly convert to int,
// so key[AAEKEY_A], IsKeyDown(AAEKEY_ESC) and the cfg write path all work with
// no casts. It is an enum rather than macros because macros leak into every
// downstream header; <linux/input-event-codes.h> defines KEY_A as 30, and the
// two cannot coexist in one translation unit.
//
// Backends translate their native codes to these (Win32: 1:1 with VK codes;
// Linux: an evdev -> AaeKey table in the evdev backend).
//
// Several enumerators deliberately share a value (AAEKEY_TILDE/BACKQUOTE,
// AAEKEY_COLON/SEMICOLON, AAEKEY_BACKSLASH/BACKSLASH2, AAEKEY_ALT/LMENU,
// AAEKEY_AT/SCRLOCK, AAEKEY_CIRCUMFLEX/NUMLOCK). These duplicates are
// pre-existing and intentional - do not "fix" them.
// ---------------------------------------------------------------------------
enum AaeKey {
    AAEKEY_A = 0x41, AAEKEY_B = 0x42, AAEKEY_C = 0x43, AAEKEY_D = 0x44,
    AAEKEY_E = 0x45, AAEKEY_F = 0x46, AAEKEY_G = 0x47, AAEKEY_H = 0x48,
    AAEKEY_I = 0x49, AAEKEY_J = 0x4a, AAEKEY_K = 0x4b, AAEKEY_L = 0x4c,
    AAEKEY_M = 0x4d, AAEKEY_N = 0x4e, AAEKEY_O = 0x4f, AAEKEY_P = 0x50,
    AAEKEY_Q = 0x51, AAEKEY_R = 0x52, AAEKEY_S = 0x53, AAEKEY_T = 0x54,
    AAEKEY_U = 0x55, AAEKEY_V = 0x56, AAEKEY_W = 0x57, AAEKEY_X = 0x58,
    AAEKEY_Y = 0x59, AAEKEY_Z = 0x5a,

    AAEKEY_0 = 0x30, AAEKEY_1 = 0x31, AAEKEY_2 = 0x32, AAEKEY_3 = 0x33,
    AAEKEY_4 = 0x34, AAEKEY_5 = 0x35, AAEKEY_6 = 0x36, AAEKEY_7 = 0x37,
    AAEKEY_8 = 0x38, AAEKEY_9 = 0x39,

    AAEKEY_0_PAD = 0x60, AAEKEY_1_PAD = 0x61, AAEKEY_2_PAD = 0x62,
    AAEKEY_3_PAD = 0x63, AAEKEY_4_PAD = 0x64, AAEKEY_5_PAD = 0x65,
    AAEKEY_6_PAD = 0x66, AAEKEY_7_PAD = 0x67, AAEKEY_8_PAD = 0x68,
    AAEKEY_9_PAD = 0x69,

    AAEKEY_F1 = 0x70, AAEKEY_F2 = 0x71, AAEKEY_F3  = 0x72, AAEKEY_F4  = 0x73,
    AAEKEY_F5 = 0x74, AAEKEY_F6 = 0x75, AAEKEY_F7  = 0x76, AAEKEY_F8  = 0x77,
    AAEKEY_F9 = 0x78, AAEKEY_F10 = 0x79, AAEKEY_F11 = 0x7a, AAEKEY_F12 = 0x7b,

    AAEKEY_ESC        = 0x1b,
    AAEKEY_TILDE      = 0xc0,
    AAEKEY_MINUS      = 0xbd,
    AAEKEY_EQUALS     = 0xbb,
    AAEKEY_BACKSPACE  = 0x08,
    AAEKEY_TAB        = 0x09,
    AAEKEY_OPENBRACE  = 0xdb,
    AAEKEY_CLOSEBRACE = 0xdd,
    AAEKEY_ENTER      = 0x0d,
    AAEKEY_COLON      = 0xba,
    AAEKEY_QUOTE      = 0xde,
    AAEKEY_BACKSLASH  = 0xdc,
    AAEKEY_BACKSLASH2 = 0xdc,
    AAEKEY_COMMA      = 0xbc,
    AAEKEY_STOP       = 0xbe,
    AAEKEY_SLASH      = 0xbf,
    AAEKEY_SPACE      = 0x20,

    AAEKEY_INSERT = 0x2d, AAEKEY_DEL  = 0x2e, AAEKEY_HOME = 0x24,
    AAEKEY_END    = 0x23, AAEKEY_PGUP = 0x21, AAEKEY_PGDN = 0x22,
    AAEKEY_LEFT   = 0x25, AAEKEY_RIGHT = 0x27,
    AAEKEY_UP     = 0x26, AAEKEY_DOWN  = 0x28,

    AAEKEY_SLASH_PAD  = 0x6f,
    AAEKEY_ASTERISK   = 0x6a,
    AAEKEY_MINUS_PAD  = 0x6d,
    AAEKEY_PLUS_PAD   = 0x6b,
    AAEKEY_DEL_PAD    = 0x6e,
    AAEKEY_ENTER_PAD  = 0x6c,
    AAEKEY_PRTSCR     = 0x2c,
    AAEKEY_PAUSE      = 0x13,

    AAEKEY_ABNT_C1    = 0xc1,
    AAEKEY_YEN        = 0x7d,
    AAEKEY_KANA       = 0x15,
    AAEKEY_CONVERT    = 0x79,
    AAEKEY_NOCONVERT  = 0x7b,
    AAEKEY_AT         = 0x91,
    AAEKEY_CIRCUMFLEX = 0x90,
    AAEKEY_COLON2     = 0x92,
    AAEKEY_KANJI      = 0x94,
    AAEKEY_EQUALS_PAD = 0x00,
    AAEKEY_BACKQUOTE  = 0xc0,
    AAEKEY_SEMICOLON  = 0xba,

    AAEKEY_LSHIFT   = 0xa0, AAEKEY_RSHIFT   = 0xa1,
    AAEKEY_LCONTROL = 0xa2, AAEKEY_RCONTROL = 0xa3,
    AAEKEY_ALT      = 0xa4, AAEKEY_LMENU    = 0xa4,
    AAEKEY_RMENU    = 0xa5, AAEKEY_ALTGR    = 0xa5,
    AAEKEY_LWIN     = 0x5b, AAEKEY_RWIN     = 0x5c,
    AAEKEY_MENU     = 0x12,
    AAEKEY_SCRLOCK  = 0x91,
    AAEKEY_NUMLOCK  = 0x90,
    AAEKEY_CAPSLOCK = 0x14,

    AAEKEY_MAX = 0xef
};
```

- [ ] **Step 4: Update the 4 call sites in `acommon.cpp`**

Lines 128–131 become:

```c
		if (key[AAEKEY_RIGHT]) { game_rect_right += (shift ? -STEP : +STEP); moved = RIGHT; }
		else if (key[AAEKEY_LEFT]) { game_rect_left += (shift ? +STEP : -STEP); moved = LEFT; }
		else if (key[AAEKEY_DOWN]) { game_rect_bottom += (shift ? -STEP : +STEP); moved = BOTTOM; }
		else if (key[AAEKEY_UP]) { game_rect_top += (shift ? +STEP : -STEP); moved = TOP; }
```

- [ ] **Step 5: Update the 6 call sites in `os_input.cpp`**

Line 126–127:

```c
	if (keycode == OSD_KEY_RCONTROL) keycode = AAEKEY_RCONTROL;
	if (keycode == OSD_KEY_ALTGR) keycode = AAEKEY_ALTGR;
```

Line 133:

```c
		keycode = AAEKEY_PAUSE;
```

Lines 192–194:

```c
	if (keycode == OSD_KEY_RCONTROL) keycode = AAEKEY_RCONTROL;
	if (keycode == OSD_KEY_ALTGR) keycode = AAEKEY_ALTGR;
	if (keycode == OSD_KEY_PAUSE) keycode = AAEKEY_PAUSE;
```

- [ ] **Step 6: Build and confirm it passes**

Run the build.
Expected: exit code 0, the six baseline warnings, and the `#ifdef KEY_A` guard in `acommon.cpp` no longer trips.

If you get `error C2065: 'KEY_xxx': undeclared identifier` in a file not listed above, that file had a `KEY_*` use the inventory missed — rename it to `AAEKEY_*` and note it in the commit message.

- [ ] **Step 7: Commit**

```bash
git add aae/system/input/rawinput.h aae/aae/acommon.cpp aae/aae/os_input.cpp
git commit -m "refactor(input): replace KEY_* macros with AaeKey enum

Values unchanged - this is a rename, not a renumber. The KEY_* macros
collide with <linux/input-event-codes.h> (KEY_A is 30 there, 0x41 here)
and, being macros, leaked into every downstream header. An unscoped enum
converts implicitly to int so all call sites work unchanged."
```

---

## Task 2: Split `rawinput.h` into neutral + Win32 headers

`rawinput.h` opens with `#include <windows.h>` and is included by 9 files, three of them drivers. Its public API speaks `HRESULT`, `HWND`, `WPARAM`, `LPARAM`, `INT`, `LONG`.

**Files:**
- Create: `aae/system/input/sys_input.h`
- Create: `aae/system/input/rawinput_win32.h`
- Delete: `aae/system/input/rawinput.h`
- Modify: `aae/system/input/rawinput.cpp`, `aae/system/window/winmain.cpp`, `aae/aae/acommon.cpp`, `aae/aae/menu.cpp`, `aae/aae/os_input.cpp`, `aae/aae/drivers/clowns.cpp`, `aae/aae/drivers/invaders.cpp`, `aae/aae/drivers/yiear.cpp`

- [ ] **Step 1: Write the failing guard**

Add to `aae/aae/drivers/invaders.cpp` **immediately after its last `#include`** (see *Guard placement* in Conventions):

```c
// Boundary guard: nothing driver code includes may drag in the Win32 API.
#ifdef _WINDOWS_
#error "windows.h leaked into driver code"
#endif
```

- [ ] **Step 2: Build and confirm it fails**

Run the build.
Expected: **FAIL** with `#error: "windows.h leaked into driver code"` in `invaders.cpp`.

Note: this guard will still fail after this task, because `aae_mame_driver.h` also pulls `windows.h` (Task 5 closes that). To see *this* task's progress, temporarily comment the guard out after Step 5 and confirm the build is otherwise clean; re-enable it and leave it failing until Task 5. Record in the commit message that the guard is expected-red until Task 5.

- [ ] **Step 3: Create `aae/system/input/sys_input.h`**

Move into this file, verbatim from `rawinput.h`: the `bset`/`bclr`/`toUpper` macros, the complete `AaeKey` enum from Task 1, `enum RI_Modifiers`, the callback typedefs, `RI_MAX_MICE`/`RI_MAX_KBDS`, and all the query/multi-HID declarations — with Windows types replaced:

```c
#pragma once
// ===========================================================================
// sys_input.h - platform-neutral input API.
//
// Every consumer outside the platform backend includes THIS, never
// rawinput_win32.h. Nothing here may reference windows.h.
// ===========================================================================
#include <cstdint>

#ifdef _WINDOWS_
#error "sys_input.h must stay platform-neutral - do not include windows.h before it"
#endif

#define bset(p,m) ((p) |= (m))
#define bclr(p,m) ((p) &= ~(m))
#define toUpper(ch) ((ch >= 'a' && ch <='z') ? ch & 0x5f : ch)

// MOVE OPERATION, not a re-type: cut the entire `enum AaeKey { ... };` block
// (with its comment header) out of aae/system/input/rawinput.h where Task 1
// Step 3 put it, and paste it here unchanged. rawinput.h is deleted in Step 5
// of this task, so the enum must live here afterward. Do not retype it - a
// single transposed hex digit silently rebinds a key.

enum RI_Modifiers {
    RI_MOD_SHIFT   = 0x01,
    RI_MOD_CONTROL = 0x02,
    RI_MOD_ALT     = 0x04,
    RI_MOD_SUPER   = 0x08
};

#define RI_MAX_MICE 8
#define RI_MAX_KBDS 8

typedef void (*KeyCallback)(int key, int scancode, int action, int mods);
typedef void (*MouseButtonCallback)(int button, int action, int mods);
typedef void (*CursorPositionCallback)(double xpos, double ypos);

void SetKeyCallback(KeyCallback callback);
void SetMouseButtonCallback(MouseButtonCallback callback);
void SetCursorPositionCallback(CursorPositionCallback callback);

int  GetModifierFlags();

// Allegro-compatible C-style keystate buffers.
extern int mouse_b;
extern unsigned char key[256];

// Keyboard queries.
int  isKeyHeld(int keycode);
bool IsKeyDown(int keycode);
bool IsKeyUp(int keycode);
void test_clr();

// Mouse.
void set_mouse_mickey_scale(float scale);
void get_mouse_win(int* mickeyx, int* mickeyy);
void get_mouse_mickeys(int* mickeyx, int* mickeyy);

int32_t GetMouseX();
int32_t GetMouseY();
int32_t GetMouseWheel();
void    SetMouseX(int32_t x);
void    SetMouseY(int32_t y);
void    SetMouseWheel(int32_t wheel);
int32_t GetMouseXChange();
int32_t GetMouseYChange();
int32_t GetMouseWheelChange();

bool IsMouseLButtonDown(); bool IsMouseLButtonUp();
bool IsMouseRButtonDown(); bool IsMouseRButtonUp();
bool IsMouseMButtonDown(); bool IsMouseMButtonUp();

// Multiple mice (per-device).
int         RawInput_GetMouseCount();
const char* RawInput_GetMouseName(int index);
void        get_mouse_mickeys_ex(int index, int* mickeyx, int* mickeyy);
int         RawInput_GetMouseButtons(int index);
int         RawInput_MouseSeenInput(int index);
const char* RawInput_GetMousePath(int index);
int         RawInput_FindMouseByPath(const char* path);

// Multiple keyboards (per-device).
int         RawInput_GetKeyboardCount();
const char* RawInput_GetKeyboardName(int index);
const char* RawInput_GetKeyboardPath(int index);
int         RawInput_FindKeyboardByPath(const char* path);
int         RawInput_KeyboardSeenInput(int index);
int         RawInput_IsKeyDownEx(int index, int keycode);

// Lifecycle that is meaningful on every backend. Initialize/Shutdown are
// backend-specific and live in rawinput_win32.h.
void RawInput_SetPaused(bool paused);
```

**Important:** copy the full 375-line usage-guide comment block from the head of `rawinput.h` into `sys_input.h` as well. It is the only documentation of the threading model and the multi-HID semantics; losing it is a real regression.

- [ ] **Step 4: Create `aae/system/input/rawinput_win32.h`**

```c
#pragma once
// ===========================================================================
// rawinput_win32.h - Win32 Raw Input plumbing.
//
// PRIVATE to the Win32 backend. Only winmain.cpp and rawinput.cpp include
// this. Everything else uses sys_input.h.
// ===========================================================================
#include <windows.h>
#include "sys_input.h"

// Registers keyboard (HID usage 0x06) and mouse (0x02) for Raw Input with
// RIDEV_INPUTSINK, zeroes state, starts the worker thread.
// Returns S_OK on success, E_FAIL if registration fails.
HRESULT RawInput_Initialize(HWND hWnd);

// Call from WndProc on WM_INPUT.
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

// Stops the worker thread and joins it. Safe if never initialized.
void RawInput_Shutdown();
```

- [ ] **Step 5: Repoint the includes**

- `aae/system/input/rawinput.cpp`: replace `#include "rawinput.h"` with `#include "rawinput_win32.h"`.
- `aae/system/window/winmain.cpp`: replace `#include "rawinput.h"` with `#include "rawinput_win32.h"`.
- `aae/aae/acommon.cpp:24`, `aae/aae/menu.cpp:16`, `aae/aae/os_input.cpp:18`, `aae/aae/drivers/clowns.cpp:5`, `aae/aae/drivers/invaders.cpp:18`, `aae/aae/drivers/yiear.cpp:17`: replace `#include "rawinput.h"` with `#include "sys_input.h"`.
- Delete `aae/system/input/rawinput.h`.

`rawinput.cpp`'s definitions of `GetMouseX()` etc. must change their return type from `LONG` to `int32_t` to match the new declarations. On MSVC x64 `LONG` **is** `int32_t`, so this is a signature-text change only, with no behavior change.

- [ ] **Step 6: Build and confirm it passes**

Run the build.
Expected: exit code 0 apart from the intentionally-red `_WINDOWS_` guard in `invaders.cpp`. Comment that guard out to confirm, then restore it.

If you see `error C2065: 'HWND': undeclared identifier` in one of the six neutral consumers, that file was relying on `rawinput.h` for `windows.h`. Check whether it genuinely needs Win32 — if so add `#include "framework.h"` to that file explicitly and note it; do not put `windows.h` back into `sys_input.h`.

- [ ] **Step 7: Commit**

```bash
git add aae/system/input/ aae/system/window/winmain.cpp aae/aae/acommon.cpp aae/aae/menu.cpp aae/aae/os_input.cpp aae/aae/drivers/clowns.cpp aae/aae/drivers/invaders.cpp aae/aae/drivers/yiear.cpp
git commit -m "refactor(input): split rawinput.h into sys_input.h + rawinput_win32.h

Neutral consumers (3 drivers, menu, acommon, os_input) no longer see
windows.h through the input header. LONG/INT in the public API become
int32_t/int - identical types on MSVC x64.

The _WINDOWS_ guard added to invaders.cpp is expected-RED until Task 5
closes the aae_mame_driver.h door."
```

---

## Task 3: `osdepend.h` becomes the contract header

The 29 `osd_*` functions are spread across five headers and `osdepend.h` declares none of them. A new target has no single place to learn what it must implement.

**Files:**
- Modify: `aae/aae/osdepend.h`
- Modify: `aae/aae/os_input.h`, `aae/aae/fileio/mame_fileio.h`, `aae/aae/vidhrdwr/osd_video.h`, `aae/aae/vidhrdwr/old_mame_raster.h`, `aae/aae/led_service_handler.h`

- [ ] **Step 1: Append the contract to `osdepend.h`**

Keep the existing `OSD_KEY_*` / `OSD_JOY_*` defines exactly as they are — they are the *logical* input names 30 driver-side files use, they do not collide on Linux, and they are out of scope. Append:

```c
// ===========================================================================
// THE OSD CONTRACT
//
// Every function below must be provided by the platform backend. A new
// target implements these and nothing else. Groups are marked with which
// targets must supply a real implementation vs. a stub.
//
// This header must stay platform-neutral: no windows.h, no GL, no STL
// beyond <cstdint>.
// ===========================================================================
#include <cstdint>

struct osd_bitmap;   // full definition lives in vidhrdwr/old_mame_raster.h

// --- INPUT ------------------------------------- win: yes  linux: yes  teensy: yes
int         osd_key_pressed(int keycode);
int         osd_key_pressed_for(int player, int keycode);
int         osd_key_pressed_memory(int keycode);
int         osd_key_pressed_memory_repeat(int keycode, int speed);
int         osd_read_key_immediate(void);
const char* osd_key_name(int keycode);
const char* osd_joy_name(int joycode);
void        osd_poll_joysticks(void);
int         osd_joy_pressed(int joycode);
void        osd_analogjoy_read(int player, int* analog_x, int* analog_y);
void        osd_trak_read(int player, int* deltax, int* deltay);

// --- FILE I/O ---------------------------------- win: yes  linux: yes  teensy: yes (SD/flash)
void*        osd_fopen(const char* gamename, const char* filename, int filetype, int write);
int          osd_fread(void* file, void* buffer, int length);
int          osd_fread_swap(void* file, void* buffer, int length);
int          osd_fread_scatter(void* file, void* buffer, int length, int increment);
int          osd_fwrite(void* file, const void* buffer, int length);
int          osd_fseek(void* file, int offset, int whence);
unsigned int osd_fcrc(void* file);
void         osd_fclose(void* file);

// --- VIDEO / PALETTE --------------------------- win: yes  linux: yes  teensy: STUB
// Teensy is vector-only and has no framebuffer; these may be no-ops there.
void osd_modify_pen(int pen, unsigned char r, unsigned char g, unsigned char b);
void osd_get_pen(int pen, unsigned char* r, unsigned char* g, unsigned char* b);
struct osd_bitmap* osd_create_display(int width, int height,
                                      unsigned int totalcolors,
                                      const unsigned char* palette,
                                      unsigned char* pens, int attributes);
struct osd_bitmap* osd_create_bitmap(int width, int height);
void osd_free_bitmap(struct osd_bitmap* bitmap);
void osd_clearbitmap(struct osd_bitmap* bitmap);

// --- LEDs -------------------------------------- win: yes  linux: STUB  teensy: STUB
void osd_led_service_start();
void osd_led_service_stop();
void osd_set_leds(int state);
int  osd_get_leds();
```

- [ ] **Step 2: Remove the duplicate declarations from their old homes**

Delete these declaration lines and add `#include "osdepend.h"` to each file:

| file | delete lines |
|---|---|
| `aae/aae/os_input.h` | 26, 27, 30–38 |
| `aae/aae/fileio/mame_fileio.h` | 68–75 (keep the macro aliases at 44–47) |
| `aae/aae/vidhrdwr/osd_video.h` | 98, 99, 105–109 |
| `aae/aae/vidhrdwr/old_mame_raster.h` | 117–119 |
| `aae/aae/led_service_handler.h` | 2, 3, 5, 6 |

Each file keeps all of its non-`osd_` content unchanged.

- [ ] **Step 3: Build and confirm it passes**

Run the build.
Expected: exit code 0, the six baseline warnings (plus the still-red `invaders.cpp` guard from Task 2).

If you get `error C2371: redefinition; different basic types`, a signature in `osdepend.h` does not match the original — go back to the original header, copy the signature character-for-character, and retry. Do **not** change the implementation to match your guess.

- [ ] **Step 4: Commit**

```bash
git add aae/aae/osdepend.h aae/aae/os_input.h aae/aae/fileio/mame_fileio.h aae/aae/vidhrdwr/osd_video.h aae/aae/vidhrdwr/old_mame_raster.h aae/aae/led_service_handler.h
git commit -m "refactor(osd): make osdepend.h the single OSD contract header

All 29 osd_* functions now declared in one place, grouped by capability
and annotated with which targets must implement vs. stub each group.
Previously spread across five unrelated headers with osdepend.h declaring
none of them."
```

---

## Task 4: Remove `mixer.h` from `aae_mame_driver.h`

`aae_mame_driver.h:31` puts the XAudio2 mixer — and with it `<xaudio2.h>`, `<thread>`, `<condition_variable>`, `<atomic>` — into all 109 consumers. Teensy has no XAudio2.

**Files:**
- Modify: `aae/aae/aae_mame_driver.h` (delete line 31)
- Modify: the 19 files listed below

- [ ] **Step 1: Write the failing guard**

Add to `aae/aae/cpu_code/cpu_6502.cpp` **immediately after its last `#include`** (see *Guard placement* in Conventions):

```c
// Boundary guard: nothing a CPU core includes may drag in the audio mixer.
#ifdef _XAUDIO2_INCLUDED_
#error "xaudio2 leaked into a CPU core"
#endif
```

Verify `_XAUDIO2_INCLUDED_` is the macro `<xaudio2.h>` actually defines before relying on it — open the SDK header and check. If it defines something else, use that macro name and note the correction in your report.

- [ ] **Step 2: Build and confirm it fails**

Run the build.
Expected: **FAIL** with `#error: "xaudio2 leaked into a CPU core"`.

If it *passes*, `cpu_6502.cpp` does not include `aae_mame_driver.h`; instead put the guard in `aae/aae/memory.cpp` and repeat.

- [ ] **Step 3: Delete line 31 of `aae_mame_driver.h`**

Remove `#include "mixer.h"`.

- [ ] **Step 4: Add the include to the 19 files that need it**

Add `#include "mixer.h"` to each, in the existing include block:

`aae/aae/acommon.cpp`, `aae/aae/gui/driver_gui.cpp`, `aae/aae/sndhrdwr/tms5220.cpp`, `aae/aae/sndhrdwr/cinematronics_sound.cpp`, `aae/aae/machine/bosconian_machine.cpp`, `aae/aae/drivers/invaders.cpp`, `aae/aae/drivers/segag80.cpp`, `aae/aae/drivers/dkong.cpp`, `aae/aae/drivers/gaplus.cpp`, `aae/aae/drivers/bzone.cpp`, `aae/aae/drivers/asteroid.cpp`, `aae/aae/drivers/llander.cpp`, `aae/aae/drivers/rallyx.cpp`, `aae/aae/drivers/vicdual.cpp`, `aae/aae/drivers/xevious.cpp`, `aae/aae/drivers/yiear.cpp`, `aae/aae/drivers/clowns.cpp`, `aae/aae/drivers/galaga.cpp`, `aae/aae/drivers/galaxian_driver.cpp`

`rallyx.cpp` reaches the mixer through local `PLAY`/`STOP` macros at lines 39–40 rather than direct calls — it still needs the include.

- [ ] **Step 5: Build and confirm it passes**

Run the build.
Expected: exit code 0 (plus the still-red `invaders.cpp` guard), the six baseline warnings, and the `_XAUDIO2_INCLUDED_` guard clean.

Expect additional files to fail with `error C3861: 'sample_start': identifier not found`. The inventory was grep-based and could not prove every call site. For each, add `#include "mixer.h"` and list it in the commit message — that list is real data about how far audio had spread.

- [ ] **Step 6: Commit**

```bash
git add aae/aae/aae_mame_driver.h aae/aae/acommon.cpp aae/aae/gui/driver_gui.cpp aae/aae/sndhrdwr/ aae/aae/machine/bosconian_machine.cpp aae/aae/drivers/ aae/aae/cpu_code/cpu_6502.cpp
git commit -m "refactor(core): remove mixer.h from aae_mame_driver.h

19 files now include it directly. This keeps <xaudio2.h>, <thread>,
<condition_variable> and <atomic> out of all 109 consumers of the core's
master header - required for the Teensy target, which has no XAudio2."
```

---

## Task 5: Close both `framework.h` doors

`windows.h` reaches the core through **two** includes: `aae_mame_driver.h:19` and `osd_video.h:61` (the latter pulled in unconditionally by `aae_mame_driver.h:36`). Removing either alone is a no-op.

Verified 2026-07-28: `osd_video.h` uses **zero** framework.h symbols, and its lines 62–63 are dead too — the `// RunningMachine, Machine, MAX_GFX_ELEMENTS, MAX_PENS` comment is stale, none of those names appear in the file.

**Files:**
- Modify: `aae/aae/vidhrdwr/osd_video.h` (delete lines 61–63)
- Modify: `aae/aae/aae_mame_driver.h` (delete line 19)
- Modify: `aae/aae/fileio/texture_handler.cpp`

- [ ] **Step 1: The guard is already written**

The `#ifdef _WINDOWS_` guard added to `invaders.cpp` in Task 2 Step 1 has been red since then. That is this task's failing test. Confirm it still fails:

Run the build.
Expected: **FAIL** with `#error: "windows.h leaked into driver code"`.

- [ ] **Step 2: Delete lines 61–63 of `osd_video.h`**

Remove all three:

```c
#include "framework.h"
#include "aae_mame_driver.h"   // RunningMachine, Machine, MAX_GFX_ELEMENTS, MAX_PENS, etc.
#include "old_mame_raster.h"   // GfxElement, GfxLayout, gfxdecodeinfo, decode/free helpers
```

This also breaks the `aae_mame_driver.h` ↔ `osd_video.h` include cycle. `osd_bitmap` is already forward-declared via `osdepend.h` from Task 3, so the pointer uses at lines 105/108 still compile.

**Keep** `aae_mame_driver.h:36`'s `#include "osd_video.h"` — it is the legitimate re-export point for seven files that use `osd_get_pen` / `osd_modify_pen` / `palette_recalc` / `vh_open` without including `osd_video.h` themselves (`aae_video/opengl_renderer.cpp`, drivers `centiped`, `circus`, `foodf`, `milliped`, `missile`, `warlord`).

- [ ] **Step 3: Delete line 19 of `aae_mame_driver.h`**

Remove `#include "framework.h"`.

- [ ] **Step 4: Give `texture_handler.cpp` its own include**

`aae/aae/fileio/texture_handler.cpp:222` calls `win_get_window()` and reached it only through the backdoor. Add to its include block:

```c
#include "framework.h"   // win_get_window()
```

- [ ] **Step 5: Build and expect fallout**

Run the build.

This is the step with real cascade risk. Two failure classes, both with a mechanical fix:

1. `error C2065: 'HWND': undeclared identifier` (or `RECT`, `DWORD`, `win_get_window`, `allegro_message`, `osMessage`, `GetClientWidth`) — that file used Win32 via the backdoor. Add `#include "framework.h"` to that **`.cpp`**. If it is a `.h`, stop and reconsider: a core header needing Win32 is exactly what this phase is removing, and it belongs behind the contract instead.
2. `error C2065: 'MAX_PENS'` / `'GfxElement'` / `'RunningMachine'` — that file relied on `osd_video.h` to transitively supply `aae_mame_driver.h` or `old_mame_raster.h`. Add the specific header that defines the symbol: `MAX_GFX_ELEMENTS`/`MAX_PENS`/`RunningMachine`/`Machine` → `aae_mame_driver.h`; `GfxElement`/`GfxLayout`/`GfxDecodeInfo` → `vidhrdwr/old_mame_raster.h`.

Keep fixing until the build is green. Record every file you touched — that list is the true measure of how far the leak had spread.

- [ ] **Step 6: Verify the guard is now green**

Run the build.
Expected: exit code 0, the six baseline warnings, and `invaders.cpp`'s `#ifdef _WINDOWS_` guard passing for the first time.

- [ ] **Step 7: Add guards to lock the boundary**

Add this block to `aae/aae/cpu_code/cpu_6502.cpp`, `aae/aae/memory.cpp` and `aae/aae/drivers/asteroid.cpp`, in each case **immediately after that file's last `#include`** (see *Guard placement* in Conventions):

```c
// Boundary guard: nothing the emulation core includes may drag in the Win32 API.
#ifdef _WINDOWS_
#error "windows.h leaked into the emulation core"
#endif
```

Run the build. Expected: exit code 0. Any file that fails here is still leaking — fix it now, while you have the context.

- [ ] **Step 8: Commit**

```bash
git add aae/aae/vidhrdwr/osd_video.h aae/aae/aae_mame_driver.h aae/aae/fileio/texture_handler.cpp aae/aae/cpu_code/cpu_6502.cpp aae/aae/memory.cpp aae/aae/drivers/asteroid.cpp aae/aae/drivers/invaders.cpp
git commit -m "refactor(core): remove windows.h from the emulation core

Closes both doors: aae_mame_driver.h:19 and osd_video.h:61. Removing
either alone was a no-op because aae_mame_driver.h:36 pulls osd_video.h
unconditionally. Also drops osd_video.h's two dead includes, breaking the
aae_mame_driver.h <-> osd_video.h cycle.

Permanent #ifdef _WINDOWS_ guards in four core translation units make this
a build-time regression test."
```

---

## Task 6: Split `emu_vector_draw.h` along the emu/render line

The header mixes the emulation seam (`add_line`, `add_tex`) with renderer concerns (`draw_textured_shots(const mat4&)`, `txdata`, `modulate_color`). After this task, `add_line()` is the clean seam a Teensy DAC backend implements.

**Files:**
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.h`
- Create: `aae/aae/aae_video/vector_draw_gl.h`
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.cpp`, `aae/aae/aae_video/vector_draw.cpp`, `aae/aae/aae_video/opengl_renderer.cpp`, `aae/aae/aae_video/shader_definitions.h`

- [ ] **Step 1: Write the failing guard**

Add to `aae/aae/vidhrdwr/emu_vector_draw.h` **immediately after that header's own `#include` block** — i.e. below its current `#include "MathUtils.h"` at line 22, not up by `#pragma once` (see *Guard placement* in Conventions):

```c
// Boundary guard: the emu-side vector header must not drag in render math.
// Only colordefs.h and render_types.h belong here.
#ifdef MATHUTILS_H
#error "MathUtils.h reached the emu-side vector header"
#endif
```

Use `MATHUTILS_H` (the include guard at `aae/system/math/MathUtils.h:47`) rather than `_WINDOWS_` here: after Task 5 the Windows leak is already closed, so a `_WINDOWS_` guard would pass immediately and prove nothing. `MathUtils.h` is the coupling that genuinely still exists.

- [ ] **Step 2: Build and confirm it fails**

Run the build.
Expected: **FAIL** with `#error: "MathUtils.h reached the emu-side vector header"` — because `emu_vector_draw.h:22` includes `MathUtils.h` today for `draw_textured_shots`.

- [ ] **Step 3: Rewrite `emu_vector_draw.h` as emu-side only**

```c
#pragma once
#ifndef EMU_VECTOR_DRAW_H
#define EMU_VECTOR_DRAW_H

// ===========================================================================
// emu_vector_draw.h - the emulation-side vector seam.
//
// Drivers and vector generators (DVG/AVG/CCPU) call add_line/add_tex. The
// backend decides what that means: the GL/VK backend queues vertices; a
// Teensy backend drives X/Y DACs directly. Nothing render-specific may be
// declared here.
// ===========================================================================

#include "colordefs.h"      // rgb_t
#include "render_types.h"   // rtex_t - already backend-neutral (plain uint32_t)

typedef struct colorsarray { int r, g, b; } colors;
extern colors vec_colors[256];

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
void add_tex(float ex, float ey, int intensity, rgb_t col);
void cache_clear();
void set_texture_id(rtex_t* id);

#endif
```

Removed: `#include "aae_mame_driver.h"`, `#include "MathUtils.h"`, `class fpoint` (dead — zero uses anywhere in the tree), `class txdata`, `draw_textured_shots`, `modulate_color`, `cache_tex_color`, `cache_texpoint`.

- [ ] **Step 4: Create `aae/aae/aae_video/vector_draw_gl.h`**

```c
#pragma once
// ===========================================================================
// vector_draw_gl.h - render-side vector helpers.
//
// Split out of emu_vector_draw.h: these are renderer concerns and must not
// be visible to drivers or vector generators.
// ===========================================================================
#include "colordefs.h"    // rgb_t
#include "MathUtils.h"    // aae::math::mat4

class txdata
{
public:
    float x, y;
    float tx, ty;
    rgb_t color;

    txdata() : x(0), y(0), tx(0), ty(0), color(0) {}
    txdata(float x, float y, float tx, float ty, rgb_t color)
        : x(x), y(y), tx(tx), ty(ty), color(color) {}
};

rgb_t modulate_color(rgb_t col, int intensity, int gain);
void  draw_textured_shots(const aae::math::mat4& proj);
```

- [ ] **Step 5: Make the two cache helpers file-static**

In `aae/aae/vidhrdwr/emu_vector_draw.cpp`, add `#include "vector_draw_gl.h"` to the include block, and change the definitions at lines 58 and 69 to `static`:

```c
static rgb_t cache_tex_color(int intensity, rgb_t col)
```

```c
static void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
```

Both are called only from within this file (`cache_texpoint` from `add_tex` at lines 94–101, `cache_tex_color` from `cache_texpoint` at line 71). `modulate_color` at line 42 stays non-static — `vector_draw.cpp` uses it.

- [ ] **Step 6: Repoint the render-side consumers**

Add `#include "vector_draw_gl.h"` to `aae/aae/aae_video/vector_draw.cpp` (uses `modulate_color`), `aae/aae/aae_video/opengl_renderer.cpp` (uses `draw_textured_shots`), and `aae/aae/aae_video/shader_definitions.h` (references `draw_textured_shots`).

- [ ] **Step 7: Build and confirm it passes**

Run the build.
Expected: exit code 0, the six baseline warnings.

If you get `error C2065: 'txdata': undeclared identifier` in `emu_vector_draw.cpp`, Step 5's include was not added — `texlist` at line 35 is `std::vector<txdata>` and needs the new header.

- [ ] **Step 8: Commit**

```bash
git add aae/aae/vidhrdwr/emu_vector_draw.h aae/aae/vidhrdwr/emu_vector_draw.cpp aae/aae/aae_video/vector_draw_gl.h aae/aae/aae_video/vector_draw.cpp aae/aae/aae_video/opengl_renderer.cpp aae/aae/aae_video/shader_definitions.h
git commit -m "refactor(vector): split emu_vector_draw.h along the emu/render line

add_line/add_tex/cache_clear/set_texture_id stay emu-side; txdata,
modulate_color and draw_textured_shots move to a new render-side header.
cache_tex_color/cache_texpoint become file-static (no external users).
Deletes the dead fpoint class.

add_line() is now the seam a Teensy DAC backend implements."
```

---

## Task 7: Runtime verification

The build proves it compiles. This proves it still works. **Do not skip this** — every task above is a refactor whose whole claim is "no behavior change", and only this step tests that claim.

**Files:** none

- [ ] **Step 1: Launch the vector path**

```bash
./x64/Release/aae.exe asteroid
```

Verify: boots to attract mode; vectors are drawn with correct brightness and glow; coin (5) and start (1) work; the ship moves, fires, and thrusts; sound plays.

- [ ] **Step 2: Launch the raster path**

```bash
./x64/Release/aae.exe pacman
```

Verify: boots to attract mode; sprites and maze render correctly with the right colors — this exercises the `osd_video.h` / palette path touched in Tasks 3 and 5.

- [ ] **Step 3: Launch the sample-heavy vector path**

```bash
./x64/Release/aae.exe bzone
```

Verify: boots; **all sound effects play** (engine idle, shot, explosion) — this is the specific regression Task 4 could cause; vectors render.

- [ ] **Step 4: Verify the input paths**

In any running game:
- Press Tab (or your bound menu key) to open the menu — exercises `menu.cpp`, retargeted in Task 2.
- Open **KEY CONFIG (GLOBAL)**, rebind one UI key, confirm the new binding works and the old one no longer does. This exercises the `writeword()` cfg path against the renamed `AaeKey` values from Task 1.
- Open **INPUT DEVICES** and confirm attached keyboards and mice are still listed with friendly names — this is the multi-HID API surface Task 2 moved.

- [ ] **Step 5: Confirm the guards are permanent**

```bash
grep -rn "_WINDOWS_\|_XAUDIO2_INCLUDED_" --include=*.cpp --include=*.h aae/aae/ | grep -A1 error
```

Expected: the guard blocks from Tasks 2, 4, 5 and 6 are all present. These are the regression test — leave them in.

- [ ] **Step 6: Commit any fixes**

If any verification step failed, fix it, then:

```bash
git add -A aae/
git commit -m "fix: <specific issue found in runtime verification>"
```

If everything passed, there is nothing to commit — say so explicitly rather than making an empty commit.

---

## Done criteria

- [ ] The build passes at x64 Release with exactly the five pre-existing warnings
- [ ] `#ifdef _WINDOWS_` guards pass in `cpu_6502.cpp`, `memory.cpp`, `asteroid.cpp`, `invaders.cpp`
- [ ] `#ifdef _XAUDIO2_INCLUDED_` guard passes in a CPU core
- [ ] `osdepend.h` declares all 29 `osd_*` functions and nothing else declares them
- [ ] `sys_input.h` exists, contains no `windows.h`, and is what all six neutral consumers include
- [ ] `emu_vector_draw.h` includes only `colordefs.h` and `render_types.h`
- [ ] asteroid, pacman and bzone all run correctly with sound and input
- [ ] `aae/aae.vcxproj` is unmodified

## Explicitly NOT in this plan

Moving files into `src/emu` / `src/osd` directories; CMake; any Linux, evdev, ALSA, Vulkan or Teensy code; removing `std::function`/`std::string`/`std::vector` from the CPU and timer cores; merging the `AaeKey` and `OSD_KEY_*` numbering sets. All are Phase 2+ per the spec.
