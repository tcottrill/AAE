//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//------------------------------------------------------------------------------
// controller_help.cpp
// CONTROLLER GUIDE screen: vector-drawn Xbox controller with button callouts,
// the global chord table, and the game-list controls. See controller_help.h
// for the trigger/dismiss contract.
//------------------------------------------------------------------------------
#include "controller_help.h"

#include <math.h>          // cosf/sinf

#include "vector_fonts.h"
#include "colordefs.h"
#include "config.h"
#include "menu.h"          // first_run_notice_active(), get_menu_status(), ui_any_input_down()
#include "aae_emulator.h"  // get_exit_confirm_status(), emulator_is_gui_active()
#include "joystick.h"      // joy[], MAX_JOYSTICKS, is_gamepad
#include "sys_log.h"

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
const float kBodyOutline[][2] = {
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
const float kDpad[][2] = {
	{125,123},{149,123},{149,111},{161,111},{161,123},{185,123},
	{185,147},{161,147},{161,159},{149,159},{149,147},{125,147},{125,123},
};

// Circles (trace space): sticks (outer+inner), ABXY, View/Menu.
struct Circle { float cx, cy, r; };
const Circle kCircles[] = {
	{110, 95, 24}, {110, 95, 14},     // left stick
	{245,135, 24}, {245,135, 14},     // right stick
	{290,115, 10}, {312, 93, 10},     // A, B
	{268, 93, 10}, {290, 71, 10},     // X, Y
	{176, 93,  8}, {224, 93,  8},     // View(Back), Menu(Start)
};

// Callout leader lines: from a point on the controller (trace space) to a
// point near the label (mock space).
struct Leader { float tx, ty; float mx, my; };
const Leader kLeaders[] = {
	{176,  93,  185,  88},   // View  -> "BACK = 1P START" (left)
	{224,  93,  455,  88},   // Menu  -> "START = 2P START" (right)
	{110, 108,  185, 142},   // LS    -> "LS CLICK = COIN 2" (left)
	{245, 148,  455, 150},   // RS    -> "RS CLICK = COIN" (right)
	{110,  80,  185, 115},   // LS top-> "MOVE = LS/DPAD" (left)
	{300,  85,  455, 118},   // ABXY  -> "A/B/X/Y = FIRE" (right)
};

// Labels: text, mock-space x (left edge), mock-space y.
struct Label { const char* text; float mx, my; };
const Label kCallouts[] = {
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
constexpr rgb_t HELP_GREEN  = MAKE_RGBA( 64, 255, 128, 255);
constexpr rgb_t HELP_RED    = MAKE_RGBA(255,  80,  80, 255);
constexpr rgb_t HELP_CYAN   = MAKE_RGBA( 80, 255, 255, 255);
constexpr rgb_t HELP_GRAY   = MAKE_RGBA(150, 150, 150, 255);
constexpr rgb_t HELP_YELLOW = RGB_YELLOW;

// Centered text rows below the diagram: text, mock y, color.
struct Row { const char* text; float my; rgb_t color; };
const Row kRows[] = {
	{"-- HOLD TOGETHER --",              235, HELP_CYAN},
	{"LS CLICK + START ... MENU",        260, HELP_YELLOW},
	{"LS CLICK + BACK .... EXIT GAME",   282, HELP_YELLOW},
	{"START + BACK ....... PAUSE",       304, HELP_YELLOW},
	{"-- IN THE GAME LIST --",           336, HELP_CYAN},
	{"A = PLAY    B = MENU    Y = THIS SCREEN", 360, HELP_YELLOW},
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
	// Full-screen opaque backdrop so the guide is readable no matter what is
	// underneath. DrawQuad is an immediate draw on both chains (GL: direct
	// program draw; VK: ScreenQuadVK::RecordRect into the open swapchain
	// pass), while all VF strokes below are deferred to VF.End() -- so the
	// quad lands over the game/GUI layers and under every stroke drawn here.
	VF.DrawQuad(512.0f, 384.0f, 1024.0f, 768.0f, MAKE_RGBA(0, 0, 0, 255));

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

int controller_help_wants_pause()
{
	// The GUI frontend is itself a driver; freezing it would stop its
	// starfield and input polling for no benefit. Only a real game pauses.
	return (s_visible && !emulator_is_gui_active()) ? 1 : 0;
}

void controller_help_open()
{
	s_visible = true;
	s_armed = false;          // opening press must not instantly dismiss
	// Any showing while the ini flag is still 0 satisfies the one-shot --
	// including a manual open. Otherwise a pad hotplugged while the guide is
	// up manually would auto-trigger an immediate re-show after dismiss.
	s_firstRun = !config.controller_help_shown;
}

void do_the_controller_help()
{
	// First-run trigger: one-shot, only with a gamepad-class device present,
	// and never while the credit notice, the menu (incl. key-assign polling)
	// or the exit-confirm dialog owns the screen.
	if (!s_visible && !config.controller_help_shown &&
	    !first_run_notice_active() && !get_menu_status() &&
	    !get_exit_confirm_status() && AnyGamepadConnected())
	{
		controller_help_open();
	}

	if (!s_visible) return;

	// Dismissal: arm once nothing is held, then any key/button closes.
	const bool anyDown = ui_any_input_down() != 0;
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
