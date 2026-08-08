# Gamepad Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sony pads (DualSense/DualShock 4) present the canonical XInput button layout through the DirectInput path, and system chords (LS+Start etc.) work on every gamepad-class device while never firing from raw sticks like the Ultimarc.

**Architecture:** A new OS-agnostic pure module (`pad_map.h/.cpp`) holds the Sony VID/PID table, the raw→canonical button reorder, chord-mask synthesis, and the combo hold-counter step — all unit-testable with g++ under WSL, no Windows headers. `Joystick.cpp` consumes it: `dinput::enum_cb`/`poll` gain a Sony branch that fills `joy[]` to the exact XInput contract, and `joystick_check_combo()` is rewritten to scan canonical `joy[]` state across devices flagged gamepad-class instead of reading XInput's private cache.

**Tech Stack:** C++17, MSVC (aae.sln Release|x64), WSL g++ 15.2 for the pure-logic tests, DirectInput8/XInput.

**Spec:** `docs/superpowers/specs/2026-08-08-gamepad-normalization-design.md`

## File Structure

- Create `aae/system/input/pad_map.h` — canonical button indices, pure API (no Windows types).
- Create `aae/system/input/pad_map.cpp` — implementation of the pure API.
- Create `aae/system/input/tests/pad_map_tests.cpp` — assert-based test runner (built ad hoc with WSL g++; not part of aae.sln). This supersedes the spec's "modify inputtest_main.cpp" row: that target links the Linux evdev backend only and cannot exercise Windows DirectInput code; the portable logic is tested here instead.
- Modify `aae/system/input/joystick.h` — add `is_gamepad` to `JOYSTICK_INFO` (line ~328); update layout/combo doc comments.
- Modify `aae/system/input/Joystick.cpp` — flag wiring, Sony DI branch, chord rewrite.
- Modify `aae/aae.vcxproj` (+ `.filters` if Joystick.cpp is listed there) — add pad_map.cpp/h.
- Modify `CHANGELOG.txt` — one entry at the end of the current (uncommitted) section. NOTE: this file already has uncommitted owner edits — never `git add CHANGELOG.txt` in this plan's commits; leave the edit in the working tree.

Manual-hardware checkpoints (owner has Xbox pad + DualSense + Ultra-Stik attached) are explicit steps — do not skip them, do not claim them done yourself.

---

### Task 1: `pad_map` pure module with tests (TDD)

**Files:**
- Create: `aae/system/input/pad_map.h`
- Create: `aae/system/input/tests/pad_map_tests.cpp`
- Create: `aae/system/input/pad_map.cpp`

- [ ] **Step 1: Write the header** (API only — implementation comes after the failing test)

`aae/system/input/pad_map.h`:

```cpp
#pragma once
//==============================================================================
// pad_map -- pure gamepad-normalization logic shared by the OS joystick
// backends. No OS headers: unit-tested standalone (tests/pad_map_tests.cpp).
//
// "Canonical" means the XInput layout documented in joystick.h lines 57-67;
// every gamepad-class device fills joy[] in this order regardless of how the
// OS reports it.
//==============================================================================
#include <stdint.h>

#include "joystick.h"   // AAE_JOYBTN_* chord bits (Win32-free header)

// Canonical pad button indices (XInput ordering).
enum {
    PAD_BTN_A = 0, PAD_BTN_B, PAD_BTN_X, PAD_BTN_Y,
    PAD_BTN_LB, PAD_BTN_RB, PAD_BTN_BACK, PAD_BTN_START,
    PAD_BTN_LTHUMB, PAD_BTN_RTHUMB,
    PAD_BTN_DPAD_UP, PAD_BTN_DPAD_DOWN, PAD_BTN_DPAD_LEFT, PAD_BTN_DPAD_RIGHT,
    PAD_BTN_LT, PAD_BTN_RT,
    PAD_BTN_COUNT
};

// Number of raw Sony HID buttons the remap consumes (DualSense reports 15;
// the DS4's 14 are a prefix of the same order).
constexpr int PAD_SONY_RAW_BUTTONS = 15;

// True for Sony pads served by the DirectInput path: DualSense (0CE6),
// DualSense Edge (0DF2), DualShock 4 v1/v2 (05C4/09CC).
bool pad_map_is_sony(uint16_t vid, uint16_t pid);

// Reorder Sony raw DirectInput rgbButtons order into canonical slots.
// raw[i] nonzero = pressed. Writes all PAD_BTN_COUNT entries (0/1); the
// d-pad slots (10..13) are always 0 here -- the d-pad arrives via the POV
// hat and is filled by the caller. PS / touchpad / mute are dropped.
void pad_map_sony_buttons(const uint8_t raw[PAD_SONY_RAW_BUTTONS],
                          uint8_t canonical[PAD_BTN_COUNT]);

// Chord mask (AAE_JOYBTN_* bits) from canonical button states.
uint16_t pad_map_chord_mask(const uint8_t canonical[PAD_BTN_COUNT]);

// Combos must be held this many consecutive polls to fire.
constexpr int PAD_COMBO_CONFIRM_FRAMES = 2;

// Advance one (device, combo) hold counter. Returns true exactly once, on
// the poll where the count reaches PAD_COMBO_CONFIRM_FRAMES; the combo must
// be fully released before it can fire again.
bool pad_map_combo_step(bool held, int* holdFrames);
```

- [ ] **Step 2: Write the failing tests**

`aae/system/input/tests/pad_map_tests.cpp`:

```cpp
// Standalone assert-runner for pad_map. Build & run (from repo root, WSL):
//   g++ -std=c++17 -I aae/system/input aae/system/input/tests/pad_map_tests.cpp \
//       aae/system/input/pad_map.cpp -o /tmp/pad_map_tests && /tmp/pad_map_tests
#include "pad_map.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
} while (0)

// Sony raw rgbButtons order (DirectInput): 0=Square 1=Cross 2=Circle
// 3=Triangle 4=L1 5=R1 6=L2 7=R2 8=Create 9=Options 10=L3 11=R3 12=PS
// 13=Touchpad 14=Mute
enum { RAW_SQUARE = 0, RAW_CROSS, RAW_CIRCLE, RAW_TRIANGLE, RAW_L1, RAW_R1,
       RAW_L2, RAW_R2, RAW_CREATE, RAW_OPTIONS, RAW_L3, RAW_R3, RAW_PS,
       RAW_TOUCHPAD, RAW_MUTE };

static void test_is_sony()
{
    CHECK(pad_map_is_sony(0x054C, 0x0CE6));   // DualSense
    CHECK(pad_map_is_sony(0x054C, 0x0DF2));   // DualSense Edge
    CHECK(pad_map_is_sony(0x054C, 0x05C4));   // DualShock 4 v1
    CHECK(pad_map_is_sony(0x054C, 0x09CC));   // DualShock 4 v2
    CHECK(!pad_map_is_sony(0x054C, 0x0BA0));  // Sony VID, unknown PID (DS4 dongle)
    CHECK(!pad_map_is_sony(0xD209, 0x0501));  // Ultimarc
    CHECK(!pad_map_is_sony(0x045E, 0x02FF));  // Microsoft
}

static void test_sony_buttons()
{
    uint8_t raw[PAD_SONY_RAW_BUTTONS];
    uint8_t out[PAD_BTN_COUNT];

    // Every raw button lands in its canonical slot.
    static const struct { int raw; int canon; } pairs[] = {
        { RAW_CROSS,    PAD_BTN_A },      { RAW_CIRCLE,  PAD_BTN_B },
        { RAW_SQUARE,   PAD_BTN_X },      { RAW_TRIANGLE,PAD_BTN_Y },
        { RAW_L1,       PAD_BTN_LB },     { RAW_R1,      PAD_BTN_RB },
        { RAW_CREATE,   PAD_BTN_BACK },   { RAW_OPTIONS, PAD_BTN_START },
        { RAW_L3,       PAD_BTN_LTHUMB }, { RAW_R3,      PAD_BTN_RTHUMB },
        { RAW_L2,       PAD_BTN_LT },     { RAW_R2,      PAD_BTN_RT },
    };
    for (const auto& p : pairs) {
        std::memset(raw, 0, sizeof(raw));
        raw[p.raw] = 0x80;
        pad_map_sony_buttons(raw, out);
        for (int b = 0; b < PAD_BTN_COUNT; ++b)
            CHECK(out[b] == (b == p.canon ? 1 : 0));
    }

    // PS, touchpad, mute are dropped entirely.
    std::memset(raw, 0, sizeof(raw));
    raw[RAW_PS] = raw[RAW_TOUCHPAD] = raw[RAW_MUTE] = 0x80;
    pad_map_sony_buttons(raw, out);
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        CHECK(out[b] == 0);
}

static void test_chord_mask()
{
    uint8_t c[PAD_BTN_COUNT] = {};
    CHECK(pad_map_chord_mask(c) == 0);

    c[PAD_BTN_BACK] = 1; c[PAD_BTN_START] = 1;
    CHECK(pad_map_chord_mask(c) == (AAE_JOYBTN_BACK | AAE_JOYBTN_START));

    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_LTHUMB] = 1; c[PAD_BTN_START] = 1;
    CHECK(pad_map_chord_mask(c) == JOY_COMBO_MENU);

    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_A] = 1; c[PAD_BTN_DPAD_LEFT] = 1;
    CHECK(pad_map_chord_mask(c) == (AAE_JOYBTN_A | AAE_JOYBTN_DPAD_LEFT));

    // LT/RT have no chord bits; they must not leak into the mask.
    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_LT] = 1; c[PAD_BTN_RT] = 1;
    CHECK(pad_map_chord_mask(c) == 0);
}

static void test_combo_step()
{
    int frames = 0;
    CHECK(!pad_map_combo_step(true,  &frames));  // frame 1: arming
    CHECK( pad_map_combo_step(true,  &frames));  // frame 2: fires
    CHECK(!pad_map_combo_step(true,  &frames));  // still held: no refire
    CHECK(!pad_map_combo_step(false, &frames));  // released
    CHECK(!pad_map_combo_step(true,  &frames));  // re-pressed: arming again
    CHECK( pad_map_combo_step(true,  &frames));  // fires again
}

int main()
{
    test_is_sony();
    test_sony_buttons();
    test_chord_mask();
    test_combo_step();
    if (g_failures) { std::printf("%d FAILURE(S)\n", g_failures); return 1; }
    std::printf("pad_map_tests: all passed\n");
    return 0;
}
```

- [ ] **Step 3: Run tests, verify they fail**

Run (Git Bash, repo root):
```bash
wsl -e bash -lc "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -I aae/system/input aae/system/input/tests/pad_map_tests.cpp aae/system/input/pad_map.cpp -o /tmp/pad_map_tests && /tmp/pad_map_tests"
```
Expected: FAIL — `pad_map.cpp: No such file or directory` (a compile failure is the failing state here).

If `joystick.h` drags in anything non-portable, fix the include, don't work around it — the header is contractually Win32-free (commit 7674d4f).

- [ ] **Step 4: Implement**

`aae/system/input/pad_map.cpp`:

```cpp
#include "pad_map.h"

bool pad_map_is_sony(uint16_t vid, uint16_t pid)
{
    if (vid != 0x054C) return false;
    switch (pid) {
    case 0x0CE6:  // DualSense
    case 0x0DF2:  // DualSense Edge
    case 0x05C4:  // DualShock 4 v1
    case 0x09CC:  // DualShock 4 v2
        return true;
    default:
        return false;
    }
}

void pad_map_sony_buttons(const uint8_t raw[PAD_SONY_RAW_BUTTONS],
                          uint8_t canonical[PAD_BTN_COUNT])
{
    // canonical slot -> raw Sony index; -1 = no raw source (d-pad slots,
    // which the caller fills from the POV hat).
    static const int SRC[PAD_BTN_COUNT] = {
        1,   // A      <- Cross
        2,   // B      <- Circle
        0,   // X      <- Square
        3,   // Y      <- Triangle
        4,   // LB     <- L1
        5,   // RB     <- R1
        8,   // BACK   <- Create
        9,   // START  <- Options
        10,  // LTHUMB <- L3
        11,  // RTHUMB <- R3
        -1, -1, -1, -1,   // d-pad: from POV hat
        6,   // LT     <- L2 digital
        7,   // RT     <- R2 digital
    };
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        canonical[b] = (SRC[b] >= 0 && raw[SRC[b]]) ? 1 : 0;
}

uint16_t pad_map_chord_mask(const uint8_t canonical[PAD_BTN_COUNT])
{
    // canonical index -> AAE_JOYBTN_* bit; LT/RT have no XInput button bit.
    static const uint16_t BIT[PAD_BTN_COUNT] = {
        AAE_JOYBTN_A, AAE_JOYBTN_B, AAE_JOYBTN_X, AAE_JOYBTN_Y,
        AAE_JOYBTN_LEFT_SHOULDER, AAE_JOYBTN_RIGHT_SHOULDER,
        AAE_JOYBTN_BACK, AAE_JOYBTN_START,
        AAE_JOYBTN_LEFT_THUMB, AAE_JOYBTN_RIGHT_THUMB,
        AAE_JOYBTN_DPAD_UP, AAE_JOYBTN_DPAD_DOWN,
        AAE_JOYBTN_DPAD_LEFT, AAE_JOYBTN_DPAD_RIGHT,
        0, 0,
    };
    uint16_t mask = 0;
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        if (canonical[b]) mask |= BIT[b];
    return mask;
}

bool pad_map_combo_step(bool held, int* holdFrames)
{
    if (!held) { *holdFrames = 0; return false; }
    ++*holdFrames;
    return *holdFrames == PAD_COMBO_CONFIRM_FRAMES;
}
```

Note: the test references `AAE_JOYBTN_DPAD_*` — confirm those names in `joystick.h` (they sit just above `AAE_JOYBTN_START`, XInput values 0x0001–0x0008). If the header spells them differently, match the header, not this plan.

- [ ] **Step 5: Run tests, verify all pass**

Same command as Step 3. Expected: `pad_map_tests: all passed`, exit 0.

- [ ] **Step 6: Commit**

```bash
git add aae/system/input/pad_map.h aae/system/input/pad_map.cpp aae/system/input/tests/pad_map_tests.cpp
git commit -m "feat(input): pad_map pure module - Sony remap table, chord mask, combo step"
```

---

### Task 2: Wire pad_map into the MSVC build

**Files:**
- Modify: `aae/aae.vcxproj` (and `aae/aae.vcxproj.filters` if it lists Joystick.cpp)

- [ ] **Step 1: Find where Joystick.cpp is declared**

Run: `grep -n "Joystick.cpp" aae/aae.vcxproj aae/aae.vcxproj.filters`
Add `<ClCompile Include="..\system\input\pad_map.cpp" />` and `<ClInclude Include="..\system\input\pad_map.h" />` beside the Joystick.cpp entries, matching the exact relative-path style used there (adjust `..\` to whatever prefix Joystick.cpp uses). Mirror into the same filter group in the `.filters` file.

- [ ] **Step 2: Build Release x64, verify green**

Run (PowerShell):
```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild C:\Source2026\AAE_publish\aae.sln /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
```
Expected: `aae.vcxproj -> C:\Source2026\AAE_publish\x64\Release\aae.exe`, zero errors.

- [ ] **Step 3: Commit**

```bash
git add aae/aae.vcxproj aae/aae.vcxproj.filters
git commit -m "build: add pad_map to the aae project"
```

---

### Task 3: `is_gamepad` flag on `JOYSTICK_INFO`

**Files:**
- Modify: `aae/system/input/joystick.h:328-334` (struct) and the layout doc block (lines ~57-67)
- Modify: `aae/system/input/Joystick.cpp` — `clear_all_joystick_state()` (~line 139), `reset_single_joystick()` (~line 168), `xinput::setup_descriptor()` (~line 284)

- [ ] **Step 1: Add the field**

In `joystick.h`, change `JOYSTICK_INFO` to:

```c
// Information about an entire joystick
typedef struct JOYSTICK_INFO {
    int flags;
    int num_sticks;
    int num_buttons;
    // 1 = this device fills the canonical XInput-order layout above
    // (XInput pads; DirectInput pads matched by pad_map). Chord detection
    // scans ONLY these devices. Raw DI sticks (Ultimarc etc.) stay 0 so
    // their arbitrary high buttons can never fire menu/exit/pause.
    int is_gamepad;
    JOYSTICK_STICK_INFO stick[MAX_JOYSTICK_STICKS];
    JOYSTICK_BUTTON_INFO button[MAX_JOYSTICK_BUTTONS];
} JOYSTICK_INFO;
```

- [ ] **Step 2: Clear it in both reset paths**

In `clear_all_joystick_state()` and `reset_single_joystick()` add, next to the existing `num_buttons` clears:

```c
	joy[i].is_gamepad = 0;      // clear_all_joystick_state (loop var i)
	joy[index].is_gamepad = 0;  // reset_single_joystick
```

- [ ] **Step 3: Set it for XInput pads**

In `xinput::setup_descriptor()` (line ~284), after `j.num_buttons = 16;` add:

```c
	j.is_gamepad = 1;
```

- [ ] **Step 4: Build (same MSBuild command as Task 2 Step 2), expect green.**

Also check the Linux side still compiles when convenient — `evdev_joystick.cpp` fills the same struct; a new zero-default int field must not break it (it is aggregate-zeroed globals, no designated initializers of JOYSTICK_INFO exist — verify with `grep -rn "JOYSTICK_INFO" aae/system/input/linux/`).

- [ ] **Step 5: Commit**

```bash
git add aae/system/input/joystick.h aae/system/input/Joystick.cpp
git commit -m "feat(input): tag canonical-layout devices with is_gamepad"
```

---

### Task 4: Chord detection reads canonical joy[] state

**Files:**
- Modify: `aae/system/input/Joystick.cpp:1339-1361` (`joystick_check_combo`), plus delete now-dead `s_comboWasHeld` (line ~91) and `COMBO_CONFIRM_FRAMES` (line ~84, superseded by `PAD_COMBO_CONFIRM_FRAMES`)
- Modify: include block of Joystick.cpp — add `#include "pad_map.h"`

- [ ] **Step 1: Rewrite `joystick_check_combo`**

```cpp
// System chords are global shortcuts: scan every gamepad-class device and
// fire when any of them holds the full mask for PAD_COMBO_CONFIRM_FRAMES
// consecutive polls. The player parameter is legacy (all call sites pass 0)
// and no longer selects a device. Non-gamepad devices (raw DirectInput
// sticks, WinMM) are never scanned, so a cab stick's high buttons can't
// phantom-trigger menu/exit/pause. Reads the canonical joy[] state filled
// by poll_joystick() -- call sites must poll first, which the frame loop
// already does.
bool joystick_check_combo(int /*player*/, uint16_t buttonMask)
{
	int idx = get_combo_index(buttonMask);

	bool triggered = false;
	for (int d = 0; d < MAX_JOYSTICKS; ++d) {
		if (!joy[d].is_gamepad)
			continue;

		uint8_t canonical[PAD_BTN_COUNT];
		for (int b = 0; b < PAD_BTN_COUNT; ++b)
			canonical[b] = (uint8_t)joy[d].button[b].b;

		bool held = (pad_map_chord_mask(canonical) & buttonMask) == buttonMask;
		if (pad_map_combo_step(held, &s_comboHoldFrames[d][idx]))
			triggered = true;
	}
	return triggered;
}
```

Then delete `s_comboWasHeld` (line ~91) and its writes, and `COMBO_CONFIRM_FRAMES` (line ~84); `s_comboHoldFrames`, `s_comboMasks`, `s_numCombos`, `get_combo_index` stay. If `xinput::get_cached_buttons` has no remaining callers after this, delete it too.

- [ ] **Step 2: Build (MSBuild command from Task 2), expect green.**

- [ ] **Step 3: OWNER CHECKPOINT — Xbox pad regression test**

Ask the owner to run `x64\Release\aae.exe` with the Xbox pad and confirm: LS+Start opens the menu, LS+Back exits, Start+Back pauses, each firing once per press. `systemlog.txt` should still show `Using hybrid DirectInput8 + XInput joystick driver`. Do not proceed on assumption — wait for their confirmation.

- [ ] **Step 4: Commit**

```bash
git add aae/system/input/Joystick.cpp
git commit -m "feat(input): chords scan canonical joy[] across gamepad-class devices"
```

---

### Task 5: Sony pads fill the canonical layout in the DirectInput path

**Files:**
- Modify: `aae/system/input/Joystick.cpp` — `dinput` namespace: `Device` struct (~line 915), `enum_cb` (~line 1044), `setup_descriptor` (~line 993), `poll` (~line 1166). Also hoist `BUTTON_NAMES` and the XInput pad-descriptor logic for reuse.

- [ ] **Step 1: Hoist shared pad descriptor**

Move the `BUTTON_NAMES` array from the `xinput` namespace to file scope (above `namespace xinput`), cut-paste unchanged. Then add a file-scope helper directly below it, and make `xinput::setup_descriptor()` call it (replacing its body):

```cpp
// Shared canonical-pad descriptor: 2 named sticks, 16 canonical buttons,
// gamepad-class. Used by the XInput path and by DirectInput Sony pads.
static void setup_pad_descriptor(int index)
{
	JOYSTICK_INFO& j = joy[index];

	j.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
	j.num_sticks = 2;

	j.stick[0].flags = j.flags;
	j.stick[0].num_axis = 2;
	j.stick[0].name = "Left Stick";
	j.stick[0].axis[0].name = "X";
	j.stick[0].axis[1].name = "Y";

	j.stick[1].flags = j.flags;
	j.stick[1].num_axis = 2;
	j.stick[1].name = "Right Stick";
	j.stick[1].axis[0].name = "X";
	j.stick[1].axis[1].name = "Y";

	j.num_buttons = 16;
	for (int b = 0; b < j.num_buttons; ++b)
		j.button[b].name = BUTTON_NAMES[b];

	j.is_gamepad = 1;
}
```

(`xinput::setup_descriptor` becomes a one-line call to it; Task 3's `is_gamepad = 1` line moves here.)

- [ ] **Step 2: Detect Sony pads in `enum_cb`**

Add to `dinput::Device` (line ~915): `int sony;  // pad_map_is_sony matched: fill canonical layout`.

In `enum_cb`, `vid`/`pid` are already extracted (line ~1057). After the XInput-skip check, add:

```cpp
		const bool sony = pad_map_is_sony(vid, pid);
```

In the device setup block (after `D.has_pov = ...`), add `D.sony = sony ? 1 : 0;`.

Replace the axis-range block so Sony pads get a signed right-stick pair instead of unsigned extras:

```cpp
		if (sony)
		{
			// Canonical pad: left stick lX/lY, right stick lZ/lRz, all signed.
			// No extra sliders; L2/R2 analog intentionally not exposed
			// (parity with the XInput path, which exposes none).
			D.num_extra = 0;
			set_axis_range(dev, DIJOFS_X,  -128, 127);
			set_axis_range(dev, DIJOFS_Y,  -128, 127);
			set_axis_range(dev, DIJOFS_Z,  -128, 127);
			set_axis_range(dev, DIJOFS_RZ, -128, 127);
		}
		else
		{
			// (existing extras + range code, unchanged)
		}
```

And extend the registration log line to say which map applied, e.g. append `D.sony ? ", sony pad map" : ""`.

- [ ] **Step 3: Descriptor branch**

At the top of `dinput::setup_descriptor(int d)` add:

```cpp
		if (s_devices[d].sony) { setup_pad_descriptor(d); return; }
```

- [ ] **Step 4: Canonical fill in `dinput::poll`**

In the per-device loop, after `D.alive = 1;`, branch before the existing generic fill:

```cpp
			if (D.sony)
			{
				// Sticks: DI reports up as negative already; no inversion.
				j.stick[0].axis[0].pos = clamp_int((int)st.lX, -128, 127);
				j.stick[0].axis[1].pos = clamp_int((int)st.lY, -128, 127);
				j.stick[1].axis[0].pos = clamp_int((int)st.lZ, -128, 127);
				j.stick[1].axis[1].pos = clamp_int((int)st.lRz, -128, 127);
				for (int s = 0; s < 2; ++s)
					for (int a = 0; a < 2; ++a)
					{
						j.stick[s].axis[a].d1 = (j.stick[s].axis[a].pos < -64) ? 1 : 0;
						j.stick[s].axis[a].d2 = (j.stick[s].axis[a].pos >  64) ? 1 : 0;
					}

				// Buttons: Sony raw order -> canonical slots.
				uint8_t raw[PAD_SONY_RAW_BUTTONS];
				for (int b = 0; b < PAD_SONY_RAW_BUTTONS; ++b)
					raw[b] = st.rgbButtons[b];
				uint8_t canonical[PAD_BTN_COUNT];
				pad_map_sony_buttons(raw, canonical);

				// D-pad: POV hat -> canonical 10..13 + stick[0] override,
				// mirroring xinput's apply_dpad_to_left_stick behavior.
				int px = 0, py = 0;
				DWORD pov = st.rgdwPOV[0];
				if ((pov & 0xFFFF) != 0xFFFF)
				{
					int deg = pov / 100;
					if (deg > 337 || deg < 23)         py = -1;
					else if (deg < 68)  { py = -1; px = 1; }
					else if (deg < 113)                 px = 1;
					else if (deg < 158) { py = 1; px = 1; }
					else if (deg < 203)                 py = 1;
					else if (deg < 248) { py = 1; px = -1; }
					else if (deg < 293)                 px = -1;
					else                { py = -1; px = -1; }
				}
				canonical[PAD_BTN_DPAD_UP]    = (py < 0) ? 1 : 0;
				canonical[PAD_BTN_DPAD_DOWN]  = (py > 0) ? 1 : 0;
				canonical[PAD_BTN_DPAD_LEFT]  = (px < 0) ? 1 : 0;
				canonical[PAD_BTN_DPAD_RIGHT] = (px > 0) ? 1 : 0;
				if (px) { j.stick[0].axis[0].pos = px * 127;
				          j.stick[0].axis[0].d1 = (px < 0); j.stick[0].axis[0].d2 = (px > 0); }
				if (py) { j.stick[0].axis[1].pos = py * 127;
				          j.stick[0].axis[1].d1 = (py < 0); j.stick[0].axis[1].d2 = (py > 0); }

				for (int b = 0; b < PAD_BTN_COUNT; ++b)
					j.button[b].b = canonical[b];

				continue;   // generic fill below must not run for this device
			}
```

The hat-degree decode is intentionally the same as the existing generic-hat block — do not refactor them together in this task; that's a follow-up if it ever changes.

Note the failure path just above (`FAILED(hr)` → neutral state): it clears buttons/axes generically and already covers the Sony branch — no change needed there.

- [ ] **Step 5: Build (MSBuild command from Task 2), expect green.**

- [ ] **Step 6: OWNER CHECKPOINT — DualSense + Ultra-Stik matrix**

Ask the owner to run with all three devices attached and confirm, per the spec's verification matrix:
- systemlog shows the DualSense registered with the Sony map and the Ultra-Stik registered raw.
- DualSense: left stick + d-pad navigate the GUI, Cross launches, Create = 1P Start, Options = 2P Start, R3 = coin, all three chords fire.
- DualSense over Bluetooth as well as USB if convenient (enumeration can differ; if BT misbehaves, capture the log line and file it — do not chase it inside this task).
- Ultra-Stik: still playable via INPUT DEVICES assignment; mashing its buttons never opens menu/exit/pause.
- Xbox pad: unchanged.
Wait for their confirmation before committing.

- [ ] **Step 7: Commit**

```bash
git add aae/system/input/Joystick.cpp
git commit -m "feat(input): DualSense/DS4 fill the canonical pad layout via DirectInput"
```

---

### Task 6: Documentation

**Files:**
- Modify: `aae/system/input/joystick.h` — doc comments (layout block ~lines 57-67, combo block ~lines 117-148)
- Modify: `CHANGELOG.txt` — append to the current in-progress section (do NOT stage/commit this file; it carries unrelated uncommitted owner edits)

- [ ] **Step 1: Update joystick.h docs**

In the layout comment block, state that the XInput layout is the canonical contract for every gamepad-class device, including DirectInput-served Sony pads (list the four VID:PIDs). In the combo block, replace "BUTTON COMBOS (XINPUT ONLY)" and the WinMM caveat with: combos scan all gamepad-class devices' canonical state; raw DirectInput sticks and WinMM devices never trigger them; any connected pad can fire a chord.

- [ ] **Step 2: CHANGELOG entry** (append after the combo-fix line added 2026-08-08):

```
PS5/PS4 controllers now work properly: DualSense and DualShock 4 are remapped to the standard Xbox button layout (Cross=A, Create=Back, Options=Start, R3=coin, etc.), and the system button combos (menu/exit/pause) now work from any connected gamepad - while generic sticks like the Ultimarc can never trigger them by accident.
```

- [ ] **Step 3: Build once more (MSBuild command from Task 2), run the pad_map tests once more (Task 1 Step 5 command). Both green.**

- [ ] **Step 4: Commit**

```bash
git add aae/system/input/joystick.h
git commit -m "docs(input): canonical pad layout + chord contract in joystick.h"
```

---

## Out of scope (from the spec — do not add)

- `gamecontrollerdb.txt` parsing, Switch Pro, rumble/LED/touchpad/motion, PS glyphs on the help screen, Linux evdev `is_gamepad` tagging (follow-up owned by the Linux hardware pass — but do not break the evdev build).
