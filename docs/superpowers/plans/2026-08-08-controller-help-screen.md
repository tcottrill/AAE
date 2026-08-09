# Controller Help Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A CONTROLLER GUIDE screen — vector-drawn Xbox controller with button callouts and the chord table — shown once when a gamepad is first detected, and on demand via Y in the game list or a CONTROLLER HELP menu entry.

**Architecture:** A new public `VF.DrawLine` primitive pushes segments into the vector font's existing `drawVerts` queue (flushed by `End()` through `beam_add_line` on Vulkan and the font's GL program on GL — both renderers for free). A new `controller_help` module owns the traced controller geometry (static tables, mock-space coordinates, one transform), the label/chord text, and the first-run-notice-style arm/dismiss/swallow state machine. Wiring copies the `first_run` pattern exactly: config flag, draw hook in `acommon.cpp`, input gates in `aae_emulator.cpp` and `driver_gui.cpp`, menu entry in `menu.cpp`.

**Tech Stack:** C++17, MSVC (aae.sln Release|x64), vector font layer (`aae/aae/aae_video/vector_fonts.*`), existing config/menu/GUI systems.

**Spec:** `docs/superpowers/specs/2026-08-08-controller-help-screen-design.md` (approved; labels = current input scheme, Xbox names).

## File Structure

- Modify `aae/aae/aae_video/vector_fonts.h/.cpp` — add `DrawLine` (Task 1).
- Create `aae/aae/controller_help.h/.cpp` — geometry data, drawing, state machine, flag write (Task 2).
- Modify `aae/aae/config.h/.cpp` (flag), `aae/aae/acommon.cpp` (draw hook), `aae/aae/aae_emulator.cpp` (hotkey gate), `aae/aae/gui/driver_gui.cpp` (Y + footer + input gate), `aae/aae/menu.cpp` (menu entry), `aae/aae.vcxproj`(+filters), `CMakeLists.txt` (Task 3).
- `CHANGELOG.txt` — append entry, NEVER stage/commit it (carries unrelated owner edits).

Branch: create `feat/controller-help` from main before Task 1.

---

### Task 1: `VF.DrawLine`

**Files:**
- Modify: `aae/aae/aae_video/vector_fonts.h` (~line 66, next to `DrawQuad`)
- Modify: `aae/aae/aae_video/vector_fonts.cpp` (next to `DrawQuad`'s implementation, ~line 464)

- [ ] **Step 1: Declare** in vector_fonts.h beside `DrawQuad`:

```cpp
    // Queue one line segment in VF screen space (1024x768). Rides the same
    // drawVerts batch as glyph strokes, so it renders through Begin()/End()
    // on both the GL and Vulkan chains at the font stroke width.
    void DrawLine(float x0, float y0, float x1, float y1, rgb_t color);
```

- [ ] **Step 2: Implement** in vector_fonts.cpp:

```cpp
// ----
// DrawLine
// ----
void VectorFont::DrawLine(float x0, float y0, float x1, float y1, rgb_t color)
{
	const aae::math::vec2 origin(0.0f, 0.0f);
	drawVerts.push_back({ aae::math::vec2(x0, y0), origin, 0.0f, color });
	drawVerts.push_back({ aae::math::vec2(x1, y1), origin, 0.0f, color });
}
```

(`VFVertex` is `{pos, origin, angle, color}` — see vector_fonts.h:38. Zero angle, zero origin = unrotated, exactly how `DrawTextInternal` pushes glyph strokes at vector_fonts.cpp:658-662.)

- [ ] **Step 3: Build Release x64** (PowerShell):
```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild C:\Source2026\AAE_publish\aae.sln /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
```
Green. (No rendering test rig exists; DrawLine is visually verified at the Task 4 checkpoint.)

- [ ] **Step 4: Commit** `git add aae/aae/aae_video/vector_fonts.h aae/aae/aae_video/vector_fonts.cpp && git commit -m "feat(video): VF.DrawLine primitive on the shared glyph-stroke batch"` (append the standard Co-Authored-By trailer used on this branch's siblings).

---

### Task 2: `controller_help` module

**Files:**
- Create: `aae/aae/controller_help.h`
- Create: `aae/aae/controller_help.cpp`

- [ ] **Step 1: Header** (`controller_help.h`) — mirror menu.h's first-run block style, with the project's 4-line GPL banner (copy from `aae/aae/menu.h` / `Joystick.cpp`):

```cpp
#pragma once
// (GPL banner here)
//
// CONTROLLER GUIDE -- a one-page vector-drawn help screen for couch/gamepad
// users: traced Xbox controller with button callouts, the chord table, and
// the game-list controls. Shown once when a gamepad-class device is first
// detected (config.controller_help_shown, [main] controller_help_shown in
// aae.ini), and on demand from the game-list GUI (Y) or the CONTROLLER HELP
// menu entry.
//
// controller_help_active() is the input gate, same contract as
// first_run_notice_active(): while non-zero, input handlers swallow their
// input so the dismissing press does not also fire whatever it is bound to.

int  controller_help_active();
void controller_help_open();
void do_the_controller_help();   // per-frame: trigger check, draw, dismissal
```

- [ ] **Step 2: Implementation** (`controller_help.cpp`). Complete file (GPL banner + this):

```cpp
#include "controller_help.h"

#include <string>

#include "aae_video/vector_fonts.h"
#include "config.h"
#include "menu.h"          // first_run_notice_active()
#include "os_input.h"      // key[], osd_joy_pressed, AAEKEY_MAX, OSD_JOY*_FIRE
#include "joystick.h"      // joy[], MAX_JOYSTICKS, is_gamepad

extern VectorFont VF;      // match however menu.cpp names/reaches VF -- check
                           // menu.cpp's include/extern and copy it exactly.

namespace {

// ---------------------------------------------------------------------------
// Geometry. Controller parts live in "trace space" (the design mock's SVG
// path coordinates); labels and text live in "mock space" (the approved
// 640x480 mock). Two tiny transforms map both into VF's 1024x768 (y-up).
// ---------------------------------------------------------------------------
constexpr float CTRL_TX = 220.0f, CTRL_TY = 60.0f, CTRL_S = 0.5f; // trace->mock
constexpr float M2VF = 1.6f;                                       // mock->VF

inline float MX(float mx) { return mx * M2VF; }            // mock x -> VF x
inline float MY(float my) { return 768.0f - my * M2VF; }   // mock y -> VF y (flip)
inline float CX(float tx) { return MX(CTRL_TX + tx * CTRL_S); }
inline float CY(float ty) { return MY(CTRL_TY + ty * CTRL_S); }

// Body outline: quadratic beziers of the traced controller silhouette,
// flattened at t=0/.25/.5/.75 per curve. Closed loop, trace space (y-down).
static const float kBodyOutline[][2] = {
	{  60.0f,   60.0f}, {  76.2f,   51.6f}, {  95.0f,   46.2f}, { 116.2f,   44.1f},
	{ 140.0f,   45.0f}, { 260.0f,   45.0f}, { 283.8f,   44.1f}, { 305.0f,   46.2f},
	{ 323.8f,   51.6f}, { 340.0f,   60.0f}, { 353.4f,   72.5f}, { 363.8f,   90.0f},
	{ 370.9f,  112.5f}, { 375.0f,  140.0f}, { 375.6f,  167.8f}, { 372.5f,  191.2f},
	{ 365.6f,  210.3f}, { 355.0f,  225.0f}, { 342.5f,  231.9f}, { 330.0f,  232.5f},
	{ 317.5f,  226.9f}, { 305.0f,  215.0f}, { 275.0f,  175.0f}, { 260.9f,  168.4f},
	{ 243.8f,  163.8f}, { 223.4f,  160.9f}, { 200.0f,  160.0f}, { 176.6f,  160.9f},
	{ 156.2f,  163.8f}, { 139.1f,  168.4f}, { 125.0f,  175.0f}, {  95.0f,  215.0f},
	{  82.5f,  226.9f}, {  70.0f,  232.5f}, {  57.5f,  231.9f}, {  45.0f,  225.0f},
	{  34.4f,  210.3f}, {  27.5f,  191.2f}, {  24.4f,  167.8f}, {  25.0f,  140.0f},
	{  29.1f,  112.5f}, {  36.2f,   90.0f}, {  46.6f,   72.5f}, {  60.0f,   60.0f},
};

// D-pad cross, trace space, closed.
static const float kDpad[][2] = {
	{125,123},{149,123},{149,111},{161,111},{161,123},{185,123},
	{185,147},{161,147},{161,159},{149,159},{149,147},{125,147},{125,123},
};

// Circles (trace space): sticks (outer+inner), ABXY, View/Menu.
struct Circle { float cx, cy, r; };
static const Circle kCircles[] = {
	{110, 95, 24}, {110, 95, 14},     // left stick
	{245,135, 24}, {245,135, 14},     // right stick
	{290,115, 10}, {312, 93, 10},     // A, B
	{268, 93, 10}, {290, 71, 10},     // X, Y
	{176, 93,  8}, {224, 93,  8},     // View(Back), Menu(Start)
};

// Callout leader lines: from a point on the controller (trace space) to a
// point near the label (mock space).
struct Leader { float tx, ty; float mx, my; };
static const Leader kLeaders[] = {
	{176,  93,  185,  88},   // View  -> "BACK = 1P START" (left)
	{224,  93,  455,  88},   // Menu  -> "START = 2P START" (right)
	{110, 108,  185, 142},   // LS    -> "LS CLICK = COIN 2" (left)
	{245, 148,  455, 150},   // RS    -> "RS CLICK = COIN" (right)
	{110,  80,  185, 115},   // LS top-> "MOVE = LS/DPAD" (left)
	{300,  85,  455, 118},   // ABXY  -> "A/B/X/Y = FIRE" (right)
};

// Labels: text, mock-space x (left edge), mock-space y, color, scale.
struct Label { const char* text; float mx, my; };
static const Label kCallouts[] = {
	{"BACK = 1P START",   56,  86},
	{"START = 2P START", 459,  86},
	{"LS CLICK = COIN 2", 52, 148},
	{"RS CLICK = COIN",  459, 153},
	{"MOVE = LS/DPAD",    66, 118},
	{"A/B/X/Y = FIRE",   459, 122},
};

constexpr float CALLOUT_SCALE = 1.3f;
constexpr float BODY_ROWS_SCALE = 1.7f;

// Colors: match the menu palette. Controller body green like the beam games,
// leaders red, callouts white, chords yellow, headers cyan, footer gray.
#define HELP_GREEN  MAKE_RGBA( 64, 255, 128, 255)
#define HELP_RED    MAKE_RGBA(255,  80,  80, 255)
#define HELP_CYAN   MAKE_RGBA( 80, 255, 255, 255)
#define HELP_GRAY   MAKE_RGBA(150, 150, 150, 255)

// Centered text rows below the diagram: text, mock y, color.
struct Row { const char* text; float my; rgb_t color; };
static const Row kRows[] = {
	{"-- HOLD TOGETHER --",              235, HELP_CYAN},
	{"LS CLICK + START ... MENU",        260, RGB_YELLOW},
	{"LS CLICK + BACK .... EXIT GAME",   282, RGB_YELLOW},
	{"START + BACK ....... PAUSE",       304, RGB_YELLOW},
	{"-- IN THE GAME LIST --",           336, HELP_CYAN},
	{"A = PLAY    B = MENU    Y = THIS SCREEN", 360, RGB_YELLOW},
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool s_visible  = false;
static bool s_firstRun = false;  // this showing is the one-shot: write the flag on dismiss
static bool s_armed    = false;

bool AnyGamepadConnected()
{
	for (int i = 0; i < MAX_JOYSTICKS; ++i)
		if (joy[i].is_gamepad) return true;
	return false;
}

// Same aggregate the first-run notice polls (menu.cpp NoticeAnyInputDown):
// any key, or any joystick fire button; analog drift can never dismiss.
bool AnyInputDown()
{
	for (int k = 1; k <= AAEKEY_MAX; ++k)
		if (key[k]) return true;
	return osd_joy_pressed(OSD_JOY_FIRE)  || osd_joy_pressed(OSD_JOY2_FIRE) ||
	       osd_joy_pressed(OSD_JOY3_FIRE) || osd_joy_pressed(OSD_JOY4_FIRE);
}

void DrawCirclePts(float cx, float cy, float r)
{
	constexpr int SEG = 20;
	float px = 0, py = 0;
	for (int i = 0; i <= SEG; ++i)
	{
		const float a = (float)i / SEG * 6.2831853f;
		const float x = CX(cx + r * cosf(a));
		const float y = CY(cy + r * sinf(a));
		if (i) VF.DrawLine(px, py, x, y, HELP_GREEN);
		px = x; py = y;
	}
}

void DrawScreen()
{
	// Title + footer
	VF.PrintCentered((int)MY(36), HELP_CYAN, 2.2f, "CONTROLLER GUIDE");
	VF.PrintCentered((int)MY(425), HELP_GRAY, 1.4f, "PRESS ANY BUTTON TO CLOSE");

	// Controller: body outline, d-pad, circles
	const int nBody = (int)(sizeof(kBodyOutline) / sizeof(kBodyOutline[0]));
	for (int i = 1; i < nBody; ++i)
		VF.DrawLine(CX(kBodyOutline[i-1][0]), CY(kBodyOutline[i-1][1]),
		            CX(kBodyOutline[i][0]),   CY(kBodyOutline[i][1]), HELP_GREEN);
	const int nDpad = (int)(sizeof(kDpad) / sizeof(kDpad[0]));
	for (int i = 1; i < nDpad; ++i)
		VF.DrawLine(CX(kDpad[i-1][0]), CY(kDpad[i-1][1]),
		            CX(kDpad[i][0]),   CY(kDpad[i][1]), HELP_GREEN);
	for (const Circle& c : kCircles)
		DrawCirclePts(c.cx, c.cy, c.r);

	// Leader lines + callout labels
	for (const Leader& l : kLeaders)
		VF.DrawLine(CX(l.tx), CY(l.ty), MX(l.mx), MY(l.my), HELP_RED);
	for (const Label& l : kCallouts)
		VF.Print(MX(l.mx), (int)MY(l.my), RGB_WHITE, CALLOUT_SCALE, "%s", l.text);

	// Chord table + game-list rows
	for (const Row& r : kRows)
		VF.PrintCentered((int)MY(r.my), r.color, BODY_ROWS_SCALE, r.text);
}

}  // namespace

int controller_help_active() { return s_visible ? 1 : 0; }

void controller_help_open()
{
	s_visible = true;
	s_armed = false;          // opening press must not instantly dismiss
}

void do_the_controller_help()
{
	// First-run trigger: one-shot, only with a gamepad-class device present,
	// and never while the credit notice still owns the screen.
	if (!s_visible && !config.controller_help_shown &&
	    !first_run_notice_active() && AnyGamepadConnected())
	{
		controller_help_open();
		s_firstRun = true;
	}

	if (!s_visible) return;

	// Dismissal: arm once nothing is held, then any key/button closes.
	const bool anyDown = AnyInputDown();
	if (!s_armed)
	{
		if (!anyDown) s_armed = true;
	}
	else if (anyDown)
	{
		s_visible = false;
		if (s_firstRun)
		{
			s_firstRun = false;
			config.controller_help_shown = 1;
			my_set_config_int("main", "controller_help_shown", 1, 0);
			LOG_INFO("Controller guide dismissed (first-run flag written)");
		}
		return;
	}

	DrawScreen();
}
```

Notes for the implementer:
- Verify every borrowed identifier against the codebase before assuming: `MAKE_RGBA`, `RGB_WHITE`/`RGB_YELLOW`, `my_set_config_int`, `LOG_INFO`, `AAEKEY_MAX`, `OSD_JOY*_FIRE`, the `VF` extern, and which header declares each (menu.cpp uses all of them — mirror its includes). `cosf/sinf` need `<cmath>`.
- `config.controller_help_shown` doesn't exist until Task 3 — build this task together with Task 3's config change or accept that this file first compiles in Task 3. Preferred: implement Task 2 and Task 3 as one commit-pair in sequence, building at Task 3 Step 4.
- Coordinate sanity: `MY(36)` ≈ 710 (title near top), `MY(425)` ≈ 88 (footer near bottom) — VF y-up. If Print's y parameter turns out to be top-anchored rather than baseline-up (check how menu.cpp positions text vs the notice code), keep the tables and flip only in MY().

- [ ] **Step 3:** No standalone build yet (needs Task 3's config field) — proceed to Task 3.

---

### Task 3: Wiring

**Files:**
- Modify: `aae/aae/config.h` (struct, near `first_run`), `aae/aae/config.cpp` (~line 194)
- Modify: `aae/aae/acommon.cpp` (~line 96-99)
- Modify: `aae/aae/aae_emulator.cpp` (~line 1201-1205)
- Modify: `aae/aae/gui/driver_gui.cpp` (~line 733-737 + Y handling + footer)
- Modify: `aae/aae/menu.cpp` (root menu builder, ~line 703-713)
- Modify: `aae/aae.vcxproj` + `aae/aae.vcxproj.filters`, `CMakeLists.txt`

- [ ] **Step 1 — config:** in config.h add `int controller_help_shown;` next to `first_run` with a one-line comment; in config.cpp after line 194's `first_run` read add:
```c
	// Controller guide one-shot: 0 = not yet shown; set to 1 when dismissed.
	config.controller_help_shown = get_config_int("main", "controller_help_shown", 0);
```

- [ ] **Step 2 — draw hook:** acommon.cpp, immediately BEFORE the `do_the_first_run_notice();` call at ~line 99 (notice draws after = on top, and the help's own trigger defers to the notice anyway):
```c
	// Controller guide: first-gamepad one-shot + on-demand help screen.
	// Drawn before the first-run notice so the notice keeps top priority.
	do_the_controller_help();
```
Add `#include "controller_help.h"` per the file's include style.

- [ ] **Step 3 — hotkey gate:** aae_emulator.cpp ~line 1204, extend the existing gate:
```c
	if (first_run_notice_active())
		return;
	// The controller guide owns input the same way while it is up.
	if (controller_help_active())
		return;
```
(+ include.)

- [ ] **Step 4 — GUI:** driver_gui.cpp:
  - Input gate ~line 734: add `|| controller_help_active() != 0` to the condition that zeroes input and sets `input_cooldown`.
  - Y opens the guide: the GUI reads its buttons through its input ports (IN0 bits; Button1=A launch, Button2=B menu — see the PORT_BIT block ~line 254-260 and where those bits are consumed). Find the consumption site of the Menu button (bit2) and mirror it for a Y press: IPT_BUTTON4 exists in IN0 (bit 0x10, Button4 = Joy Fire4 = Y). At the consumption site add: on Button4 pulse, `controller_help_open();`. If IN0's Button4 bit is consumed by something else already, report it instead of double-binding.
  - Footer hint: grep driver_gui.cpp for the footer/help line it draws (search `VF.Print` calls near the bottom of the screen, or an existing hint string) and append `  Y=HELP` in the same style. If there is no footer text at all, add a small gray `VF.PrintCentered` hint at the bottom consistent with the GUI's look.
  - Include controller_help.h.

- [ ] **Step 5 — menu entry:** menu.cpp root-menu builder (the block pushing `MenuItem::Link("JOY CONFIG (GLOBAL)"...` ~line 712): add
```cpp
    m_items.push_back(MenuItem::Link("CONTROLLER HELP", []() {
        set_menu_status(0);        // close the menu so the guide has the screen
        controller_help_open();
    }));
```
placed after the input-config links; include controller_help.h.

- [ ] **Step 6 — build files:** vcxproj: `<ClCompile Include="aae\controller_help.cpp" />` + `<ClInclude Include="aae\controller_help.h" />` matching the path style of menu.cpp's entries (VERIFY the prefix by grepping menu.cpp's entry); mirror into .filters groups. CMakeLists.txt: add to the same list that carries menu.cpp; bump the Windows source-count assertion 55→56 (verify the number by counting ClCompile entries after your edit).

- [ ] **Step 7 — build Release x64** (same MSBuild command as Task 1) — green. Also `cmake --build build-linux --target aae` under WSL if the Linux target lists the new file (controller_help is OSD-side; check which CMake list menu.cpp sits in and match — the Linux build must keep linking).

- [ ] **Step 8 — commit** (all Task 2+3 files EXCEPT CHANGELOG.txt):
```bash
git add aae/aae/controller_help.h aae/aae/controller_help.cpp aae/aae/config.h aae/aae/config.cpp aae/aae/acommon.cpp aae/aae/aae_emulator.cpp aae/aae/gui/driver_gui.cpp aae/aae/menu.cpp aae/aae.vcxproj aae/aae.vcxproj.filters CMakeLists.txt
git commit -m "feat(ui): CONTROLLER GUIDE help screen (first-pad one-shot + Y/menu access)"
```

---

### Task 4: Owner visual checkpoint + polish + CHANGELOG

- [ ] **Step 1 — self-check what can be checked headlessly:** run `x64\Release\aae.exe -listallgames > nul` (or a quick launch/quit) to confirm no startup crash; confirm `aae.ini` gains no flag until the guide is actually dismissed.

- [ ] **Step 2 — OWNER CHECKPOINT (visual, both renderers):** ask the owner to run the GUI with a pad connected and verify: guide auto-shows once (after the credit notice if that is also pending), draws correctly (controller shape legible, labels readable, nothing clipped) on Vulkan AND with `-renderer opengl`, any button closes it exactly once (the dismissing press doesn't launch a game / insert coins), `[main] controller_help_shown=1` written, never auto-shows again, Y re-opens it in the GUI, CONTROLLER HELP menu entry works in the GUI and mid-game, and with NO pad connected a fresh `controller_help_shown=0` ini never auto-shows it. Layout tuning (positions/scales in the tables) is expected iteration at this step — apply the owner's feedback directly in the constants and rebuild.

- [ ] **Step 3 — CHANGELOG** (append near the gamepad entries; DO NOT STAGE):
```
Added a CONTROLLER GUIDE screen: a drawn controller with the button layout, coin/start mapping and the button combos. Shows once the first time a gamepad is detected, and any time after from Y in the game list or the CONTROLLER HELP menu entry.
```

- [ ] **Step 4 — final commit of any checkpoint tuning, then merge** per the finishing-a-development-branch flow.
