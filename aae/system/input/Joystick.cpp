//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//------------------------------------------------------------------------------
// joystick.cpp
// Unified joystick support with Allegro-compatible API
// Supports XInput (preferred) and Win32 WinMM fallback
//
// XInput hotplug is handled via polling - each poll() checks connection state
// and fires callbacks on changes. WinMM does not support hotplug reliably.
//------------------------------------------------------------------------------

#include "joystick.h"
#include "pad_map.h"
#include "sys_log.h"

#include <windows.h>
#include <mmsystem.h>
#include <Xinput.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstring>
#include <cstdio>   // snprintf
#include <atomic>   // device-change flag
#include <new>      // std::nothrow
#include <cstdlib>  // atoi
#include "win32/win32_private.h"  // win_get_window

// joystick.h declares the neutral AAE_JOYBTN_* bits so it can stay free of
// <Xinput.h> - it is reached from aae/aae/os_input.cpp, which is core code
// that must build on Linux and on a freestanding Teensy. The values ARE
// XInput's, so this backend passes them through untouched. These assertions
// are what makes that safe: if a future SDK ever renumbers the gamepad bits,
// the build fails here instead of silently breaking every controller combo.
static_assert(AAE_JOYBTN_DPAD_UP        == XINPUT_GAMEPAD_DPAD_UP,        "AAE_JOYBTN_DPAD_UP drifted from XInput");
static_assert(AAE_JOYBTN_DPAD_DOWN      == XINPUT_GAMEPAD_DPAD_DOWN,      "AAE_JOYBTN_DPAD_DOWN drifted from XInput");
static_assert(AAE_JOYBTN_DPAD_LEFT      == XINPUT_GAMEPAD_DPAD_LEFT,      "AAE_JOYBTN_DPAD_LEFT drifted from XInput");
static_assert(AAE_JOYBTN_DPAD_RIGHT     == XINPUT_GAMEPAD_DPAD_RIGHT,     "AAE_JOYBTN_DPAD_RIGHT drifted from XInput");
static_assert(AAE_JOYBTN_START          == XINPUT_GAMEPAD_START,          "AAE_JOYBTN_START drifted from XInput");
static_assert(AAE_JOYBTN_BACK           == XINPUT_GAMEPAD_BACK,           "AAE_JOYBTN_BACK drifted from XInput");
static_assert(AAE_JOYBTN_LEFT_THUMB     == XINPUT_GAMEPAD_LEFT_THUMB,     "AAE_JOYBTN_LEFT_THUMB drifted from XInput");
static_assert(AAE_JOYBTN_RIGHT_THUMB    == XINPUT_GAMEPAD_RIGHT_THUMB,    "AAE_JOYBTN_RIGHT_THUMB drifted from XInput");
static_assert(AAE_JOYBTN_LEFT_SHOULDER  == XINPUT_GAMEPAD_LEFT_SHOULDER,  "AAE_JOYBTN_LEFT_SHOULDER drifted from XInput");
static_assert(AAE_JOYBTN_RIGHT_SHOULDER == XINPUT_GAMEPAD_RIGHT_SHOULDER, "AAE_JOYBTN_RIGHT_SHOULDER drifted from XInput");
static_assert(AAE_JOYBTN_A              == XINPUT_GAMEPAD_A,              "AAE_JOYBTN_A drifted from XInput");
static_assert(AAE_JOYBTN_B              == XINPUT_GAMEPAD_B,              "AAE_JOYBTN_B drifted from XInput");
static_assert(AAE_JOYBTN_X              == XINPUT_GAMEPAD_X,              "AAE_JOYBTN_X drifted from XInput");
static_assert(AAE_JOYBTN_Y              == XINPUT_GAMEPAD_Y,              "AAE_JOYBTN_Y drifted from XInput");

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib, "winmm.lib")
#ifndef WIN7BUILD
// Modern Win8+ build: link the full XInput 1.4 import library.  This pulls
// in xinput1_4.dll at runtime which does not exist on Windows 7.
#pragma comment(lib, "xinput.lib")
#else
// Windows 7 build: link the legacy XInput 9.1.0 import library so the
// binary resolves against xinput9_1_0.dll, which ships with Win7.  All
// three XInput functions used here -- XInputGetState, XInputSetState,
// XInputGetCapabilities -- are present in xinput9_1_0.  Two compatibility
// notes already satisfied by the code below:
//   * XInputGetCapabilities must be called with dwFlags = 0 (no
//     XINPUT_FLAG_GAMEPAD support).  See xinput::poll().
//   * The Guide (Xbox) button is not reported in XInputGetState on this
//     version.  The code never reads it.
#pragma comment(lib, "xinput9_1_0.lib")
#endif

//------------------------------------------------------------------------------
// Global State (Allegro-compatible)
//------------------------------------------------------------------------------

int num_joysticks = 0;
int _joystick_installed = 0;
JOYSTICK_INFO joy[MAX_JOYSTICKS];

//------------------------------------------------------------------------------
// Internal State
//------------------------------------------------------------------------------

static int  s_comboHoldFrames[MAX_JOYSTICKS][JOY_MAX_COMBOS] = {};
static JoystickHotplugCallback s_hotplug_callback = nullptr;

// Generalized combo edge-detection state.
// Each distinct buttonMask gets its own slot so combos don't interfere.
static WORD s_comboMasks[JOY_MAX_COMBOS] = {};
static int  s_numCombos = 0;

static int get_combo_index(WORD mask)
{
	for (int i = 0; i < s_numCombos; i++)
		if (s_comboMasks[i] == mask) return i;
	if (s_numCombos < JOY_MAX_COMBOS)
	{
		s_comboMasks[s_numCombos] = mask;
		return s_numCombos++;
	}
	return 0; // table full: fall back to slot 0 rather than crash
}

enum class JoystickDriver {
	None,
	XInput,
	WinMM,
	Hybrid    // DirectInput8 generic sticks + XInput pads simultaneously
};

static JoystickDriver s_active_driver = JoystickDriver::None;

static const char* const NAME_UNUSED = "unused";

//------------------------------------------------------------------------------
// Utility Macros and Functions
//------------------------------------------------------------------------------

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

//------------------------------------------------------------------------------
// Common: Clear all joystick state
//------------------------------------------------------------------------------

static void clear_all_joystick_state()
{
	for (int i = 0; i < MAX_JOYSTICKS; ++i) {
		joy[i].flags = 0;
		joy[i].num_sticks = 0;
		joy[i].num_buttons = 0;
		joy[i].is_gamepad = 0;

		for (int s = 0; s < MAX_JOYSTICK_STICKS; ++s) {
			joy[i].stick[s].flags = 0;
			joy[i].stick[s].num_axis = 0;
			joy[i].stick[s].name = NAME_UNUSED;

			for (int a = 0; a < MAX_JOYSTICK_AXIS; ++a) {
				joy[i].stick[s].axis[a].pos = 0;
				joy[i].stick[s].axis[a].d1 = 0;
				joy[i].stick[s].axis[a].d2 = 0;
				joy[i].stick[s].axis[a].name = NAME_UNUSED;
			}
		}

		for (int b = 0; b < MAX_JOYSTICK_BUTTONS; ++b) {
			joy[i].button[b].b = 0;
			joy[i].button[b].name = NAME_UNUSED;
		}
	}

	num_joysticks = 0;
}

static void reset_single_joystick(int index)
{
	if (index < 0 || index >= MAX_JOYSTICKS)
		return;

	joy[index].flags = 0;
	joy[index].num_sticks = 0;
	joy[index].num_buttons = 0;
	joy[index].is_gamepad = 0;

	for (int s = 0; s < MAX_JOYSTICK_STICKS; ++s) {
		joy[index].stick[s].flags = 0;
		joy[index].stick[s].num_axis = 0;
		joy[index].stick[s].name = NAME_UNUSED;

		for (int a = 0; a < MAX_JOYSTICK_AXIS; ++a) {
			joy[index].stick[s].axis[a].pos = 0;
			joy[index].stick[s].axis[a].d1 = 0;
			joy[index].stick[s].axis[a].d2 = 0;
			joy[index].stick[s].axis[a].name = NAME_UNUSED;
		}
	}

	for (int b = 0; b < MAX_JOYSTICK_BUTTONS; ++b) {
		joy[index].button[b].b = 0;
		joy[index].button[b].name = NAME_UNUSED;
	}
}

//------------------------------------------------------------------------------
// Canonical gamepad descriptor (shared by XInput and DirectInput Sony pads)
//------------------------------------------------------------------------------

static const char* const BUTTON_NAMES[16] = {
	"A", "B", "X", "Y",
	"LB", "RB",
	"Back", "Start",
	"LStick", "RStick",
	"DPadUp", "DPadDown", "DPadLeft", "DPadRight",
	"LT", "RT"
};

// Axis position (|pos| out of 127) beyond which the digital d1/d2 view of a
// canonical-pad stick reports pressed. Shared by XInput and DI Sony pads.
static constexpr int DIGITAL_THRESHOLD = 32;

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

//==============================================================================
// XInput Implementation
//==============================================================================

namespace xinput {
	static constexpr int MAX_CONTROLLERS = 4;
	static constexpr int TRIGGER_THRESHOLD = 30;

	// Stale packet detection
	static constexpr int STALE_FRAME_THRESHOLD = 120;

	// Per-controller state tracking
	static bool s_connected[MAX_CONTROLLERS] = {};
	static DWORD s_last_packet[MAX_CONTROLLERS] = {};
	static int s_stale_frames[MAX_CONTROLLERS] = {};

	// Performance throttling for offline controllers and state caching
	static int s_offline_check_timer[MAX_CONTROLLERS] = {};
	static XINPUT_STATE s_cached_states[MAX_CONTROLLERS] = {};
	static int s_joy_to_xinput[MAX_JOYSTICKS] = {};

	// Keep track of rumble duration internally
	static int s_rumble_timer[MAX_CONTROLLERS] = {};

	static void reset_state()
	{
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			s_connected[i] = false;
			s_last_packet[i] = 0;
			s_stale_frames[i] = 0;
			s_offline_check_timer[i] = 0;
			s_rumble_timer[i] = 0;
		}
		for (int i = 0; i < MAX_JOYSTICKS; ++i) {
			s_joy_to_xinput[i] = -1;
		}
	}

	static bool is_available()
	{
		for (DWORD i = 0; i < MAX_CONTROLLERS; ++i) {
			XINPUT_STATE st = {};
			if (XInputGetState(i, &st) == ERROR_SUCCESS)
				return true;
		}
		return false;
	}

	static int scale_thumb(SHORT v, int deadzone)
	{
		int iv = static_cast<int>(v);
		// Fix: Prevent magnitude overflow when input is exactly -32768
		if (iv < -32767) iv = -32767;

		int av = (iv < 0) ? -iv : iv;

		if (av <= deadzone)
			return 0;

		int sign = (iv < 0) ? -1 : 1;
		int mag = av - deadzone;
		int denom = 32767 - deadzone;

		if (denom <= 0)
			return 0;

		int out = (mag * 127) / denom;
		out = clamp_int(out, 0, 127);
		return out * sign;
	}

	static void set_axis(JOYSTICK_AXIS_INFO& axis, int pos)
	{
		axis.pos = clamp_int(pos, -128, 127);
		axis.d1 = (axis.pos < -DIGITAL_THRESHOLD) ? 1 : 0;
		axis.d2 = (axis.pos > DIGITAL_THRESHOLD) ? 1 : 0;
	}

	static void setup_descriptor(int index)
	{
		setup_pad_descriptor(index);
	}

	static void apply_dpad_to_left_stick(JOYSTICK_INFO& j, WORD buttons)
	{
		// X axis
		if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) {
			j.stick[0].axis[0].pos = -128;
			j.stick[0].axis[0].d1 = 1;
			j.stick[0].axis[0].d2 = 0;
		}
		else if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) {
			j.stick[0].axis[0].pos = 127;
			j.stick[0].axis[0].d1 = 0;
			j.stick[0].axis[0].d2 = 1;
		}

		// Y axis (inverted: negative = up)
		if (buttons & XINPUT_GAMEPAD_DPAD_UP) {
			j.stick[0].axis[1].pos = -128;
			j.stick[0].axis[1].d1 = 1;
			j.stick[0].axis[1].d2 = 0;
		}
		else if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) {
			j.stick[0].axis[1].pos = 127;
			j.stick[0].axis[1].d1 = 0;
			j.stick[0].axis[1].d2 = 1;
		}
	}

	static void fill_state(JOYSTICK_INFO& j, const XINPUT_STATE& st)
	{
		const XINPUT_GAMEPAD& g = st.Gamepad;
		WORD b = g.wButtons;

		int lx = scale_thumb(g.sThumbLX, static_cast<int>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
		int ly = scale_thumb(g.sThumbLY, static_cast<int>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
		int rx = scale_thumb(g.sThumbRX, static_cast<int>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));
		int ry = scale_thumb(g.sThumbRY, static_cast<int>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));

		// Invert Y so "up" is negative
		set_axis(j.stick[0].axis[0], lx);
		set_axis(j.stick[0].axis[1], -ly);
		set_axis(j.stick[1].axis[0], rx);
		set_axis(j.stick[1].axis[1], -ry);

		apply_dpad_to_left_stick(j, b);

		j.button[0].b = (b & XINPUT_GAMEPAD_A) ? 1 : 0;
		j.button[1].b = (b & XINPUT_GAMEPAD_B) ? 1 : 0;
		j.button[2].b = (b & XINPUT_GAMEPAD_X) ? 1 : 0;
		j.button[3].b = (b & XINPUT_GAMEPAD_Y) ? 1 : 0;
		j.button[4].b = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1 : 0;
		j.button[5].b = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : 0;
		j.button[6].b = (b & XINPUT_GAMEPAD_BACK) ? 1 : 0;
		j.button[7].b = (b & XINPUT_GAMEPAD_START) ? 1 : 0;
		j.button[8].b = (b & XINPUT_GAMEPAD_LEFT_THUMB) ? 1 : 0;
		j.button[9].b = (b & XINPUT_GAMEPAD_RIGHT_THUMB) ? 1 : 0;
		j.button[10].b = (b & XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0;
		j.button[11].b = (b & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
		j.button[12].b = (b & XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0;
		j.button[13].b = (b & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0;
		j.button[14].b = (g.bLeftTrigger > TRIGGER_THRESHOLD) ? 1 : 0;
		j.button[15].b = (g.bRightTrigger > TRIGGER_THRESHOLD) ? 1 : 0;
	}

	// joy_base: first joy[] slot this backend fills. 0 in XInput-only mode;
	// the DirectInput device count in hybrid mode (sticks first, pads after).
	// Returns highest connected slot + 1 (0 if none). Does NOT set
	// num_joysticks -- the driver dispatch owns that.
	static int poll(int joy_base)
	{
		bool connected_now[MAX_CONTROLLERS];
		std::memset(connected_now, 0, sizeof(connected_now));

		// Query all controller slots
		for (DWORD i = 0; i < MAX_CONTROLLERS; ++i) {
			// Fix: Throttle polling disconnected slots to avoid thread starvation (stuttering)
			if (!s_connected[i] && s_offline_check_timer[i] > 0) {
				s_offline_check_timer[i]--;
				continue;
			}

			XINPUT_STATE st = {};
			DWORD res = XInputGetState(i, &st);

			if (res == ERROR_SUCCESS) {
				// Stale packet detection logic
				if (st.dwPacketNumber == s_last_packet[i]) {
					s_stale_frames[i]++;
					if (s_stale_frames[i] >= STALE_FRAME_THRESHOLD) {
						XINPUT_CAPABILITIES caps = {};
						if (XInputGetCapabilities(i, 0, &caps) != ERROR_SUCCESS) {
							connected_now[i] = false;
							s_stale_frames[i] = 0;
							s_offline_check_timer[i] = 60; // Wait 60 frames before checking again
							continue;
						}
						s_stale_frames[i] = 0;
					}
				}
				else {
					s_last_packet[i] = st.dwPacketNumber;
					s_stale_frames[i] = 0;
				}

				s_cached_states[i] = st;
				connected_now[i] = true;
				s_offline_check_timer[i] = 0; // Reset timer since connected
			}
			else {
				connected_now[i] = false;
				s_stale_frames[i] = 0;
				s_offline_check_timer[i] = 60; // Slot empty; wait 60 frames before checking again
			}
		}

		// Detect hotplug events
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			if (connected_now[i] && !s_connected[i]) {
				LOG_INFO("XInput controller %d connected", i);
				if (s_hotplug_callback)
					s_hotplug_callback(i, true, nullptr);

				// --- Trigger a crisp "connected" buzz on the right motor ---
				XINPUT_VIBRATION vib;
				vib.wLeftMotorSpeed = 0;
				vib.wRightMotorSpeed = static_cast<WORD>(0.6f * 65535.0f); // 60% high-frequency buzz
				XInputSetState(i, &vib);
				s_rumble_timer[i] = 15; // Hold for 15 frames (~0.25 seconds)
			}
			else if (!connected_now[i] && s_connected[i]) {
				LOG_INFO("XInput controller %d disconnected", i);
				if (s_hotplug_callback)
					s_hotplug_callback(i, false, "Controller disconnected");

				// Ensure rumble shuts off if disconnected while rumbling
				s_rumble_timer[i] = 0;
			}
			s_connected[i] = connected_now[i];
		}

		// --- NEW: Process internal rumble timers ---
		for (int i = 0; i < MAX_CONTROLLERS; ++i) {
			if (s_rumble_timer[i] > 0) {
				s_rumble_timer[i]--;

				if (s_rumble_timer[i] == 0) {
					// Timer hit 0, stop the motors
					XINPUT_VIBRATION vib = { 0, 0 };
					XInputSetState(i, &vib);
				}
			}
		}

		// STABLE mapping: joy[joy_base + i] mirrors XInput slot i directly
		// (no compaction). If pad 0 drops, pad 1 stays in its slot --
		// player assignments survive a mid-session unplug. Empty slots
		// read as neutral state.
		for (int j = joy_base; j < joy_base + MAX_CONTROLLERS && j < MAX_JOYSTICKS; ++j) {
			reset_single_joystick(j);
			s_joy_to_xinput[j] = -1;
		}

		int highest = -1;
		for (int i = 0; i < MAX_CONTROLLERS && joy_base + i < MAX_JOYSTICKS; ++i) {
			if (!connected_now[i])
				continue;

			setup_descriptor(joy_base + i);
			fill_state(joy[joy_base + i], s_cached_states[i]);
			s_joy_to_xinput[joy_base + i] = i; // physical slot (combos & rumble)
			highest = i;
		}

		return highest + 1;
	}

	// Force the next poll to re-probe every slot immediately (device-change
	// notification): clears the empty-slot throttle timers.
	static void request_reprobe()
	{
		for (int i = 0; i < MAX_CONTROLLERS; ++i)
			s_offline_check_timer[i] = 0;
	}

	static bool any_connected()
	{
		for (int i = 0; i < MAX_CONTROLLERS; ++i)
			if (s_connected[i]) return true;
		return false;
	}

	// NEW: Rumble implementation
	static bool set_rumble(int player, float left_motor, float right_motor)
	{
		if (player < 0 || player >= MAX_JOYSTICKS) return false;
		int x_idx = s_joy_to_xinput[player];
		if (x_idx < 0 || x_idx >= MAX_CONTROLLERS) return false;

		// Clamp 0.0f to 1.0f
		left_motor = left_motor < 0.0f ? 0.0f : (left_motor > 1.0f ? 1.0f : left_motor);
		right_motor = right_motor < 0.0f ? 0.0f : (right_motor > 1.0f ? 1.0f : right_motor);

		XINPUT_VIBRATION vibration;
		vibration.wLeftMotorSpeed = static_cast<WORD>(left_motor * 65535.0f);
		vibration.wRightMotorSpeed = static_cast<WORD>(right_motor * 65535.0f);

		return XInputSetState(x_idx, &vibration) == ERROR_SUCCESS;
	}

	static bool init()
	{
		reset_state();
		return true; // Always return true to allow hotplug upgrading
	}

	static void shutdown()
	{
		// Stop any active rumble before exiting
		for (int i = 0; i < MAX_JOYSTICKS; ++i) {
			if (s_joy_to_xinput[i] >= 0)
				set_rumble(i, 0.0f, 0.0f);
		}

		reset_state();
		for (int j = 0; j < MAX_JOYSTICKS; ++j)
			reset_single_joystick(j);
		num_joysticks = 0;
	}
} // namespace xinput

//==============================================================================
// WinMM Implementation
//==============================================================================

namespace winmm {
	static constexpr int MAX_AXES = 6;
	static constexpr int JOY_POVFORWARD_WRAP = 36000;

	struct DeviceInfo {
		int caps;
		int num_axes;
		int axis[MAX_AXES];
		char* axis_name[MAX_AXES];
		int hat;
		char* hat_name;
		int num_buttons;
		int button[MAX_JOYSTICK_BUTTONS];
		char* button_name[MAX_JOYSTICK_BUTTONS];
		int device;
		int axis_min[MAX_AXES];
		int axis_max[MAX_AXES];
		char pname[64];   // product name from JOYCAPS (for the menu)
		int alive;        // last joyGetPosEx succeeded (hotplug display)
	};

	static DeviceInfo s_devices[MAX_JOYSTICKS];
	static int s_num_devices = 0;
	static bool s_initialized = false;

	static char name_x[] = "X";
	static char name_y[] = "Y";
	static char name_stick[] = "stick";
	static char name_throttle[] = "throttle";
	static char name_rudder[] = "rudder";
	static char name_slider[] = "slider";
	static char name_hat[] = "hat";
	static const char* name_buttons[MAX_JOYSTICK_BUTTONS] = {
		"B1",  "B2",  "B3",  "B4",  "B5",  "B6",  "B7",  "B8",
		"B9",  "B10", "B11", "B12", "B13", "B14", "B15", "B16",
		"B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24",
		"B25", "B26", "B27", "B28", "B29", "B30", "B31", "B32"
	};

	static int update_joystick_status(int n, DeviceInfo* dev)
	{
		if (n >= num_joysticks)
			return -1;

		int n_stick = 0;
		int win_axis = 0;
		int max_stick;

		if (dev->caps & JOYCAPS_HASPOV)
			max_stick = joy[n].num_sticks - 1;
		else
			max_stick = joy[n].num_sticks;

		for (n_stick = 0; n_stick < max_stick; n_stick++) {
			for (int n_axis = 0; n_axis < joy[n].stick[n_stick].num_axis; n_axis++) {
				int p = dev->axis[win_axis];

				if (joy[n].stick[n_stick].flags & JOYFLAG_ANALOGUE) {
					if (joy[n].stick[n_stick].flags & JOYFLAG_SIGNED)
						joy[n].stick[n_stick].axis[n_axis].pos = p - 128;
					else
						joy[n].stick[n_stick].axis[n_axis].pos = p;
				}

				if (joy[n].stick[n_stick].flags & JOYFLAG_DIGITAL) {
					joy[n].stick[n_stick].axis[n_axis].d1 = (p < 64) ? 1 : 0;
					joy[n].stick[n_stick].axis[n_axis].d2 = (p > 192) ? 1 : 0;
				}

				win_axis++;
			}
		}

		if (dev->caps & JOYCAPS_HASPOV) {
			joy[n].stick[n_stick].axis[0].pos = 0;
			joy[n].stick[n_stick].axis[1].pos = 0;

			// Left
			if ((dev->hat > JOY_POVBACKWARD) && (dev->hat < JOY_POVFORWARD_WRAP)) {
				joy[n].stick[n_stick].axis[0].d1 = 1;
				joy[n].stick[n_stick].axis[0].pos = -128;
			}
			else {
				joy[n].stick[n_stick].axis[0].d1 = 0;
			}

			// Right
			if ((dev->hat > JOY_POVFORWARD) && (dev->hat < JOY_POVBACKWARD)) {
				joy[n].stick[n_stick].axis[0].d2 = 1;
				joy[n].stick[n_stick].axis[0].pos = 128;
			}
			else {
				joy[n].stick[n_stick].axis[0].d2 = 0;
			}

			// Forward (up)
			if (((dev->hat > JOY_POVLEFT) && (dev->hat <= JOY_POVFORWARD_WRAP)) ||
				((dev->hat >= JOY_POVFORWARD) && (dev->hat < JOY_POVRIGHT))) {
				joy[n].stick[n_stick].axis[1].d1 = 1;
				joy[n].stick[n_stick].axis[1].pos = -128;
			}
			else {
				joy[n].stick[n_stick].axis[1].d1 = 0;
			}

			// Backward (down)
			if ((dev->hat > JOY_POVRIGHT) && (dev->hat < JOY_POVLEFT)) {
				joy[n].stick[n_stick].axis[1].d2 = 1;
				joy[n].stick[n_stick].axis[1].pos = 128;
			}
			else {
				joy[n].stick[n_stick].axis[1].d2 = 0;
			}
		}

		for (int n_but = 0; n_but < dev->num_buttons; n_but++)
			joy[n].button[n_but].b = dev->button[n_but];

		return 0;
	}

	static int add_joystick(DeviceInfo* dev)
	{
		if (num_joysticks >= MAX_JOYSTICKS - 1)
			return -1;

		joy[num_joysticks].flags = JOYFLAG_ANALOGUE | JOYFLAG_DIGITAL;

		int n_stick = 0;
		int win_axis = 0;

		if (dev->num_axes > 0) {
			if (dev->num_axes > 1) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
				joy[num_joysticks].stick[n_stick].axis[0].name = dev->axis_name[0] ? dev->axis_name[0] : name_x;
				joy[num_joysticks].stick[n_stick].axis[1].name = dev->axis_name[1] ? dev->axis_name[1] : name_y;
				joy[num_joysticks].stick[n_stick].name = name_stick;

				if (dev->caps & JOYCAPS_HASZ) {
					joy[num_joysticks].stick[n_stick].num_axis = 3;
					joy[num_joysticks].stick[n_stick].axis[2].name = dev->axis_name[2] ? dev->axis_name[2] : name_throttle;
					win_axis += 3;
				}
				else {
					joy[num_joysticks].stick[n_stick].num_axis = 2;
					win_axis += 2;
				}
				n_stick++;
			}

			if (dev->caps & JOYCAPS_HASR) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 1;
				joy[num_joysticks].stick[n_stick].axis[0].name = "";
				joy[num_joysticks].stick[n_stick].name = dev->axis_name[win_axis] ? dev->axis_name[win_axis] : name_rudder;
				win_axis++;
				n_stick++;
			}

			int max_stick = (dev->caps & JOYCAPS_HASPOV) ? MAX_JOYSTICK_STICKS - 1 : MAX_JOYSTICK_STICKS;

			while ((win_axis < dev->num_axes) && (n_stick < max_stick)) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 1;
				joy[num_joysticks].stick[n_stick].axis[0].name = "";
				joy[num_joysticks].stick[n_stick].name = dev->axis_name[win_axis] ? dev->axis_name[win_axis] : name_slider;
				win_axis++;
				n_stick++;
			}

			if (dev->caps & JOYCAPS_HASPOV) {
				joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
				joy[num_joysticks].stick[n_stick].num_axis = 2;
				joy[num_joysticks].stick[n_stick].axis[0].name = "left/right";
				joy[num_joysticks].stick[n_stick].axis[1].name = "up/down";
				joy[num_joysticks].stick[n_stick].name = dev->hat_name ? dev->hat_name : name_hat;
				n_stick++;
			}
		}

		joy[num_joysticks].num_sticks = n_stick;
		joy[num_joysticks].num_buttons = dev->num_buttons;

		for (int n_but = 0; n_but < joy[num_joysticks].num_buttons; n_but++)
			joy[num_joysticks].button[n_but].name = dev->button_name[n_but] ? dev->button_name[n_but] : name_buttons[n_but];

		num_joysticks++;
		return 0;
	}

	static int poll()
	{
		for (int n_joy = 0; n_joy < s_num_devices; n_joy++) {
			JOYINFOEX js;
			js.dwSize = sizeof(js);
			js.dwFlags = JOY_RETURNALL;

			if (joyGetPosEx(s_devices[n_joy].device, &js) == JOYERR_NOERROR) {
				s_devices[n_joy].alive = 1;
				s_devices[n_joy].axis[0] = js.dwXpos;
				s_devices[n_joy].axis[1] = js.dwYpos;
				int n_axis = 2;

				if (s_devices[n_joy].caps & JOYCAPS_HASZ) s_devices[n_joy].axis[n_axis++] = js.dwZpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASR) s_devices[n_joy].axis[n_axis++] = js.dwRpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASU) s_devices[n_joy].axis[n_axis++] = js.dwUpos;
				if (s_devices[n_joy].caps & JOYCAPS_HASV) s_devices[n_joy].axis[n_axis++] = js.dwVpos;

				for (n_axis = 0; n_axis < s_devices[n_joy].num_axes; n_axis++) {
					int p = s_devices[n_joy].axis[n_axis] - s_devices[n_joy].axis_min[n_axis];
					int range = s_devices[n_joy].axis_max[n_axis] - s_devices[n_joy].axis_min[n_axis];
					if (range > 0)
						s_devices[n_joy].axis[n_axis] = p * 256 / range;
					else
						s_devices[n_joy].axis[n_axis] = 0;
				}

				if (s_devices[n_joy].caps & JOYCAPS_HASPOV)
					s_devices[n_joy].hat = js.dwPOV;

				for (int n_but = 0; n_but < s_devices[n_joy].num_buttons; n_but++)
					s_devices[n_joy].button[n_but] = ((js.dwButtons & (1 << n_but)) != 0);
			}
			else {
				s_devices[n_joy].alive = 0;
				for (int n_axis = 0; n_axis < s_devices[n_joy].num_axes; n_axis++)
					s_devices[n_joy].axis[n_axis] = 0;
				if (s_devices[n_joy].caps & JOYCAPS_HASPOV)
					s_devices[n_joy].hat = 0;
				for (int n_but = 0; n_but < s_devices[n_joy].num_buttons; n_but++)
					s_devices[n_joy].button[n_but] = 0;
			}
			update_joystick_status(n_joy, &s_devices[n_joy]);
		}
		return 0;
	}

	// Register one WinMM device id if it is present and not already known.
	// Returns true if a new device was added.
	static bool try_add_device(int n_dev)
	{
		if (s_num_devices >= MAX_JOYSTICKS) return false;

		// already registered? (devices are never removed, so positions --
		// and therefore player assignments -- stay stable across rescans)
		for (int i = 0; i < s_num_devices; i++)
			if (s_devices[i].device == n_dev) return false;

		JOYCAPS caps;
		if (joyGetDevCaps(n_dev, &caps, sizeof(caps)) != JOYERR_NOERROR) return false;

		JOYINFOEX js;
		js.dwSize = sizeof(js);
		js.dwFlags = JOY_RETURNALL;
		if (joyGetPosEx(n_dev, &js) == JOYERR_UNPLUGGED) return false;

		LOG_INFO("Detected WinMM joystick %d: %s", n_dev, caps.szPname);

		std::memset(&s_devices[s_num_devices], 0, sizeof(DeviceInfo));
		s_devices[s_num_devices].device = n_dev;
		s_devices[s_num_devices].caps = caps.wCaps;
		s_devices[s_num_devices].num_buttons = MIN((int)caps.wNumButtons, MAX_JOYSTICK_BUTTONS);
		s_devices[s_num_devices].num_axes = MIN((int)caps.wNumAxes, MAX_AXES);
		s_devices[s_num_devices].alive = 1;
		/* szPname is WCHAR under UNICODE builds; %ls narrows it */
		snprintf(s_devices[s_num_devices].pname, sizeof(s_devices[s_num_devices].pname),
			"%ls", caps.szPname);

		s_devices[s_num_devices].axis_min[0] = caps.wXmin;
		s_devices[s_num_devices].axis_max[0] = caps.wXmax;
		s_devices[s_num_devices].axis_min[1] = caps.wYmin;
		s_devices[s_num_devices].axis_max[1] = caps.wYmax;
		int n_axis = 2;

		if (caps.wCaps & JOYCAPS_HASZ) {
			s_devices[s_num_devices].axis_min[n_axis] = caps.wZmin;
			s_devices[s_num_devices].axis_max[n_axis] = caps.wZmax;
			n_axis++;
		}
		if (caps.wCaps & JOYCAPS_HASR) {
			s_devices[s_num_devices].axis_min[n_axis] = caps.wRmin;
			s_devices[s_num_devices].axis_max[n_axis] = caps.wRmax;
			n_axis++;
		}
		if (caps.wCaps & JOYCAPS_HASU) {
			s_devices[s_num_devices].axis_min[n_axis] = caps.wUmin;
			s_devices[s_num_devices].axis_max[n_axis] = caps.wUmax;
			n_axis++;
		}
		if (caps.wCaps & JOYCAPS_HASV) {
			s_devices[s_num_devices].axis_min[n_axis] = caps.wVmin;
			s_devices[s_num_devices].axis_max[n_axis] = caps.wVmax;
			n_axis++;
		}

		if (add_joystick(&s_devices[s_num_devices]) != 0) {
			LOG_ERROR("Failed to register joystick %d (%s)", n_dev, caps.szPname);
			return false;
		}

		LOG_INFO("Joystick %d registered: %d button(s), %d axis/axes",
			n_dev, s_devices[s_num_devices].num_buttons, s_devices[s_num_devices].num_axes);

		s_num_devices++;
		return true;
	}

	// Re-enumerate after a device-change notification. New devices are
	// APPENDED; existing entries are never removed or reordered (unplugged
	// ones just read neutral until they return).
	static bool rescan()
	{
		bool added = false;
		int max_devs = joyGetNumDevs();
		for (int n_dev = 0; n_dev < max_devs; n_dev++)
			if (try_add_device(n_dev)) added = true;
		if (added)
			s_initialized = true;
		return added;
	}

	static bool init()
	{
		if (s_initialized) return true;

		int max_devs = joyGetNumDevs();
		LOG_INFO("WinMM reports %d joystick device slot(s)", max_devs);

		s_num_devices = 0;

		for (int n_dev = 0; n_dev < max_devs; n_dev++)
			try_add_device(n_dev);

		if (s_num_devices == 0) {
			LOG_INFO("No WinMM joysticks detected");
			return false;
		}

		s_initialized = true;
		LOG_INFO("WinMM joystick initialization complete: %d device(s)", s_num_devices);
		poll();
		return true;
	}

	static void shutdown()
	{
		s_num_devices = 0;
		s_initialized = false;
	}
} // namespace winmm

//==============================================================================
// DirectInput 8 Implementation (generic HID sticks; XInput pads are filtered
// out here and served by the XInput backend simultaneously -- hybrid mode).
//
// Device identity is the DirectInput INSTANCE GUID, which is stable per
// machine: the INPUT DEVICES menu persists assignments as "DI:{guid}"
// strings, so two identical Ultimarc sticks stay pinned to their players
// across reboots regardless of enumeration order.
//==============================================================================

namespace dinput {
	// leave headroom for the 4 XInput slots behind the DI devices in joy[]
	static constexpr int MAX_DEVICES = MAX_JOYSTICKS - 4;

	struct Device {
		IDirectInputDevice8A* dev;
		GUID  guid;
		char  guid_str[48];
		char  name[64];
		int   alive;
		int   num_buttons;
		int   num_extra;              // extra 1-axis sticks after X/Y
		LONG  DIJOYSTATE2::* extra[6];  // source members for the extras
		int   has_pov;
		int   sony;                   // pad_map_is_sony matched: fill canonical layout
	};

	static IDirectInput8A* s_di = nullptr;
	static Device s_devices[MAX_DEVICES];
	static int s_num_devices = 0;

	static void guid_to_string(const GUID& g, char* out, size_t outlen)
	{
		snprintf(out, outlen, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
			g.Data1, g.Data2, g.Data3,
			g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
			g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
	}

	// Is this VID/PID an XInput device? Classic check: the matching HID
	// interface path contains "IG_" (Microsoft's XInput marker). Those
	// devices are skipped here and handled by the XInput backend.
	static bool is_xinput_vidpid(unsigned short vid, unsigned short pid)
	{
		UINT count = 0;
		if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0)
			return false;

		RAWINPUTDEVICELIST* list = new (std::nothrow) RAWINPUTDEVICELIST[count];
		if (!list) return false;

		bool found = false;
		count = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
		if (count != (UINT)-1)
		{
			for (UINT i = 0; i < count && !found; i++)
			{
				if (list[i].dwType != RIM_TYPEHID) continue;

				RID_DEVICE_INFO info;
				info.cbSize = sizeof(info);
				UINT sz = sizeof(info);
				if (GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &info, &sz) == (UINT)-1)
					continue;
				if (info.hid.dwVendorId != vid || info.hid.dwProductId != pid)
					continue;

				char path[256] = { 0 };
				sz = sizeof(path) - 1;
				if (GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, path, &sz) != (UINT)-1)
					if (strstr(path, "IG_")) found = true;
			}
		}
		delete[] list;
		return found;
	}

	static void set_axis_range(IDirectInputDevice8A* dev, DWORD ofs, LONG lo, LONG hi)
	{
		DIPROPRANGE pr;
		pr.diph.dwSize = sizeof(pr);
		pr.diph.dwHeaderSize = sizeof(pr.diph);
		pr.diph.dwHow = DIPH_BYOFFSET;
		pr.diph.dwObj = ofs;
		pr.lMin = lo;
		pr.lMax = hi;
		dev->SetProperty(DIPROP_RANGE, &pr.diph);
	}

	// Build the joy[] descriptor for DI device d at joy index d (DI devices
	// occupy joy[0..count-1]; layout mirrors the winmm model so downstream
	// consumers see the same shapes: stick0 = signed X/Y, extra unsigned
	// 1-axis sticks, then a digital hat).
	static void setup_descriptor(int d)
	{
		if (s_devices[d].sony) { setup_pad_descriptor(d); return; }

		JOYSTICK_INFO& j = joy[d];
		Device& D = s_devices[d];

		j.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;

		int n_stick = 0;
		j.stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
		j.stick[n_stick].num_axis = 2;
		j.stick[n_stick].name = "stick";
		j.stick[n_stick].axis[0].name = "X";
		j.stick[n_stick].axis[1].name = "Y";
		n_stick++;

		for (int e = 0; e < D.num_extra && n_stick < MAX_JOYSTICK_STICKS - 1; e++) {
			j.stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
			j.stick[n_stick].num_axis = 1;
			j.stick[n_stick].axis[0].name = "";
			j.stick[n_stick].name = "slider";
			n_stick++;
		}

		if (D.has_pov && n_stick < MAX_JOYSTICK_STICKS) {
			j.stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
			j.stick[n_stick].num_axis = 2;
			j.stick[n_stick].axis[0].name = "left/right";
			j.stick[n_stick].axis[1].name = "up/down";
			j.stick[n_stick].name = "hat";
			n_stick++;
		}

		j.num_sticks = n_stick;
		j.num_buttons = D.num_buttons;
		for (int b = 0; b < j.num_buttons; b++)
			j.button[b].name = "Button";
		j.is_gamepad = 0;     // raw stick: never chord-scanned
	}

	// EnumObjects callback: note which optional axes exist.
	struct AxisProbe { bool z, rx, ry, rz; int sliders; };
	static BOOL CALLBACK axis_cb(LPCDIDEVICEOBJECTINSTANCEA obj, LPVOID ref)
	{
		AxisProbe* p = (AxisProbe*)ref;
		if (obj->guidType == GUID_ZAxis)  p->z = true;
		else if (obj->guidType == GUID_RxAxis) p->rx = true;
		else if (obj->guidType == GUID_RyAxis) p->ry = true;
		else if (obj->guidType == GUID_RzAxis) p->rz = true;
		else if (obj->guidType == GUID_Slider) p->sliders++;
		return DIENUM_CONTINUE;
	}

	static BOOL CALLBACK enum_cb(LPCDIDEVICEINSTANCEA inst, LPVOID ref)
	{
		bool* added = (bool*)ref;

		if (s_num_devices >= MAX_DEVICES)
			return DIENUM_STOP;

		// already registered? (rescan: positions never move)
		for (int i = 0; i < s_num_devices; i++)
			if (IsEqualGUID(s_devices[i].guid, inst->guidInstance))
				return DIENUM_CONTINUE;

		// XInput devices are served by the XInput backend
		unsigned short vid = (unsigned short)(inst->guidProduct.Data1 & 0xffff);
		unsigned short pid = (unsigned short)((inst->guidProduct.Data1 >> 16) & 0xffff);
		if (is_xinput_vidpid(vid, pid))
		{
			LOG_INFO("DirectInput: skipping XInput device %s (handled by XInput)", inst->tszInstanceName);
			return DIENUM_CONTINUE;
		}

		const bool sony = pad_map_is_sony(vid, pid);

		IDirectInputDevice8A* dev = nullptr;
		if (FAILED(s_di->CreateDevice(inst->guidInstance, &dev, nullptr)) || !dev)
			return DIENUM_CONTINUE;

		if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2)) ||
			FAILED(dev->SetCooperativeLevel(win_get_window(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
		{
			dev->Release();
			return DIENUM_CONTINUE;
		}

		DIDEVCAPS caps;
		caps.dwSize = sizeof(caps);
		if (FAILED(dev->GetCapabilities(&caps)))
		{
			dev->Release();
			return DIENUM_CONTINUE;
		}

		AxisProbe probe = {};
		dev->EnumObjects(axis_cb, &probe, DIDFT_AXIS);

		Device& D = s_devices[s_num_devices];
		std::memset(&D, 0, sizeof(D));
		D.dev = dev;
		D.guid = inst->guidInstance;
		guid_to_string(D.guid, D.guid_str, sizeof(D.guid_str));
		snprintf(D.name, sizeof(D.name), "%s", inst->tszInstanceName);
		D.num_buttons = MIN((int)caps.dwButtons, MAX_JOYSTICK_BUTTONS);
		D.has_pov = (caps.dwPOVs > 0) ? 1 : 0;
		D.sony = sony ? 1 : 0;
		D.alive = 1;

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
			// map optional axes to extra 1-axis sticks (0..255)
			D.num_extra = 0;
			if (probe.z)           D.extra[D.num_extra++] = &DIJOYSTATE2::lZ;
			if (probe.rx)          D.extra[D.num_extra++] = &DIJOYSTATE2::lRx;
			if (probe.ry)          D.extra[D.num_extra++] = &DIJOYSTATE2::lRy;
			if (probe.rz)          D.extra[D.num_extra++] = &DIJOYSTATE2::lRz;
			// sliders handled through lRz-style members is device-specific;
			// rglSlider needs array access -- covered separately in poll

			// X/Y signed -128..127; optional axes 0..255
			set_axis_range(dev, DIJOFS_X, -128, 127);
			set_axis_range(dev, DIJOFS_Y, -128, 127);
			if (probe.z)  set_axis_range(dev, DIJOFS_Z, 0, 255);
			if (probe.rx) set_axis_range(dev, DIJOFS_RX, 0, 255);
			if (probe.ry) set_axis_range(dev, DIJOFS_RY, 0, 255);
			if (probe.rz) set_axis_range(dev, DIJOFS_RZ, 0, 255);
		}

		dev->Acquire();

		setup_descriptor(s_num_devices);
		s_num_devices++;
		*added = true;

		LOG_INFO("DirectInput: joystick %d registered: %s [%s] (%d buttons%s%s)",
			s_num_devices, D.name, D.guid_str, D.num_buttons,
			D.has_pov ? ", hat" : "",
			D.sony ? ", sony pad map" : "");

		return DIENUM_CONTINUE;
	}

	// Enumerate and APPEND unseen devices; existing entries never move.
	static bool rescan()
	{
		if (!s_di) return false;
		bool added = false;
		s_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_cb, &added, DIEDFL_ATTACHEDONLY);

		// a re-plugged known device just needs re-acquiring
		for (int i = 0; i < s_num_devices; i++)
			if (!s_devices[i].alive && s_devices[i].dev)
				if (SUCCEEDED(s_devices[i].dev->Acquire()))
					s_devices[i].alive = 1;

		return added;
	}

	static bool init()
	{
		if (s_di) return true;

		HWND hwnd = win_get_window();
		if (!hwnd)
			return false;

		if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
			IID_IDirectInput8A, (LPVOID*)&s_di, nullptr)) || !s_di)
		{
			LOG_INFO("DirectInput8Create failed; falling back to legacy joystick drivers");
			s_di = nullptr;
			return false;
		}

		s_num_devices = 0;
		rescan();
		LOG_INFO("DirectInput: %d generic joystick device(s)", s_num_devices);
		return true;
	}

	// Fill joy[0..count-1] from device state. Returns the device count.
	static int poll()
	{
		for (int d = 0; d < s_num_devices; d++)
		{
			Device& D = s_devices[d];
			JOYSTICK_INFO& j = joy[d];

			DIJOYSTATE2 st;
			HRESULT hr = D.dev->Poll();
			if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
			{
				D.dev->Acquire();
				hr = D.dev->Poll();
			}
			if (SUCCEEDED(hr) || hr == DI_NOEFFECT)
				hr = D.dev->GetDeviceState(sizeof(st), &st);

			if (FAILED(hr))
			{
				// unplugged: neutral state, keep the slot
				D.alive = 0;
				for (int s = 0; s < j.num_sticks; s++)
					for (int a = 0; a < j.stick[s].num_axis; a++)
					{
						j.stick[s].axis[a].pos = 0;
						j.stick[s].axis[a].d1 = j.stick[s].axis[a].d2 = 0;
					}
				for (int b = 0; b < j.num_buttons; b++)
					j.button[b].b = 0;
				continue;
			}
			D.alive = 1;

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
						j.stick[s].axis[a].d1 = (j.stick[s].axis[a].pos < -DIGITAL_THRESHOLD) ? 1 : 0;
						j.stick[s].axis[a].d2 = (j.stick[s].axis[a].pos > DIGITAL_THRESHOLD) ? 1 : 0;
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
				if (px)
				{
					j.stick[0].axis[0].pos = px * 127;
					j.stick[0].axis[0].d1 = (px < 0);
					j.stick[0].axis[0].d2 = (px > 0);
				}
				if (py)
				{
					j.stick[0].axis[1].pos = py * 127;
					j.stick[0].axis[1].d1 = (py < 0);
					j.stick[0].axis[1].d2 = (py > 0);
				}

				for (int b = 0; b < PAD_BTN_COUNT; ++b)
					j.button[b].b = canonical[b];

				continue;   // generic fill below must not run for this device
			}

			// stick 0: signed X/Y with digital thresholds
			j.stick[0].axis[0].pos = clamp_int((int)st.lX, -128, 127);
			j.stick[0].axis[1].pos = clamp_int((int)st.lY, -128, 127);
			for (int a = 0; a < 2; a++)
			{
				j.stick[0].axis[a].d1 = (j.stick[0].axis[a].pos < -64) ? 1 : 0;
				j.stick[0].axis[a].d2 = (j.stick[0].axis[a].pos > 64) ? 1 : 0;
			}

			// extra unsigned axes
			int n_stick = 1;
			for (int e = 0; e < D.num_extra && n_stick < j.num_sticks; e++, n_stick++)
			{
				int p = clamp_int((int)(st.*(D.extra[e])), 0, 255);
				j.stick[n_stick].axis[0].pos = p;
				j.stick[n_stick].axis[0].d1 = (p < 64) ? 1 : 0;
				j.stick[n_stick].axis[0].d2 = (p > 192) ? 1 : 0;
			}

			// hat (POV 0): centered = 0xFFFF/-1
			if (D.has_pov && n_stick < j.num_sticks)
			{
				DWORD pov = st.rgdwPOV[0];
				int px = 0, py = 0;
				if ((pov & 0xFFFF) != 0xFFFF)
				{
					int deg = pov / 100;   // 0 = up, 90 = right ...
					if (deg > 337 || deg < 23)        py = -1;
					else if (deg < 68) { py = -1; px = 1; }
					else if (deg < 113)                px = 1;
					else if (deg < 158) { py = 1; px = 1; }
					else if (deg < 203)                py = 1;
					else if (deg < 248) { py = 1; px = -1; }
					else if (deg < 293)                px = -1;
					else { py = -1; px = -1; }
				}
				j.stick[n_stick].axis[0].pos = px * 128;
				j.stick[n_stick].axis[0].d1 = (px < 0) ? 1 : 0;
				j.stick[n_stick].axis[0].d2 = (px > 0) ? 1 : 0;
				j.stick[n_stick].axis[1].pos = py * 128;
				j.stick[n_stick].axis[1].d1 = (py < 0) ? 1 : 0;
				j.stick[n_stick].axis[1].d2 = (py > 0) ? 1 : 0;
			}

			for (int b = 0; b < j.num_buttons; b++)
				j.button[b].b = (st.rgbButtons[b] & 0x80) ? 1 : 0;
		}

		return s_num_devices;
	}

	static void shutdown()
	{
		for (int i = 0; i < s_num_devices; i++)
		{
			if (s_devices[i].dev)
			{
				s_devices[i].dev->Unacquire();
				s_devices[i].dev->Release();
				s_devices[i].dev = nullptr;
			}
		}
		s_num_devices = 0;
		if (s_di) { s_di->Release(); s_di = nullptr; }
	}
} // namespace dinput

//==============================================================================
// Public API Implementation
//==============================================================================

int install_joystick()
{
	if (_joystick_installed)
		return 0;

	clear_all_joystick_state();
	s_active_driver = JoystickDriver::None;

	LOG_INFO("Initializing joystick system...");

	// Preferred: hybrid DirectInput8 + XInput. DI serves generic HID sticks
	// (Ultimarc etc., identified by stable instance GUIDs); XInput serves
	// Xbox-family pads, which DI enumeration filters out. Both run at once:
	// DI devices occupy joy[0..], XInput slots follow.
	if (dinput::init()) {
		xinput::init();
		s_active_driver = JoystickDriver::Hybrid;
		LOG_INFO("Using hybrid DirectInput8 + XInput joystick driver");
	}

	if (s_active_driver == JoystickDriver::None && xinput::is_available()) {
		if (xinput::init()) {
			s_active_driver = JoystickDriver::XInput;
			LOG_INFO("Using XInput joystick driver (supports hotplug)");
		}
	}

	if (s_active_driver == JoystickDriver::None) {
		if (winmm::init()) {
			s_active_driver = JoystickDriver::WinMM;
			LOG_INFO("Using WinMM joystick driver");
		}
		else {
			// Default to XInput passive mode if no WinMM found
			LOG_INFO("No joysticks found. Defaulting to XInput for hotplug detection.");
			xinput::init();
			s_active_driver = JoystickDriver::XInput;
		}
	}

	_joystick_installed = 1;
	return 0;
}

void remove_joystick()
{
	if (!_joystick_installed)
		return;

	switch (s_active_driver) {
	case JoystickDriver::XInput:
		xinput::shutdown();
		break;
	case JoystickDriver::WinMM:
		winmm::shutdown();
		break;
	case JoystickDriver::Hybrid:
		xinput::shutdown();
		dinput::shutdown();
		break;
	default:
		break;
	}

	clear_all_joystick_state();
	s_active_driver = JoystickDriver::None;
	_joystick_installed = 0;
}

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
		if (!joy[d].is_gamepad) {
			s_comboHoldFrames[d][idx] = 0;
			continue;
		}

		uint8_t canonical[PAD_BTN_COUNT];
		for (int b = 0; b < PAD_BTN_COUNT; ++b)
			canonical[b] = (joy[d].button[b].b != 0) ? 1 : 0;

		bool held = (pad_map_chord_mask(canonical) & buttonMask) == buttonMask;
		if (pad_map_combo_step(held, &s_comboHoldFrames[d][idx]))
			triggered = true;
	}
	return triggered;
}

// Set from WM_DEVICECHANGE (message-pump context); handled on the next
// poll_joystick() so all device-list mutation happens on the polling thread.
static std::atomic<bool> s_device_change_pending{ false };

void joystick_device_change()
{
	s_device_change_pending = true;
}

static void handle_device_change()
{
	LOG_INFO("Joystick: device change notification, rescanning");

	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
		// append any new DI sticks (positions never move) and let the
		// next poll re-probe every XInput slot immediately
		dinput::rescan();
		xinput::request_reprobe();
		break;

	case JoystickDriver::XInput:
		// let the next poll re-probe every slot immediately
		xinput::request_reprobe();

		// XInput was selected passively (no pads found at install): if a
		// non-XInput stick has appeared, hand over to WinMM.
		if (!xinput::any_connected()) {
			if (winmm::rescan()) {
				xinput::shutdown();
				s_active_driver = JoystickDriver::WinMM;
				LOG_INFO("Joystick: switching to WinMM driver (non-XInput device arrived)");
				winmm::poll();
			}
		}
		break;

	case JoystickDriver::WinMM:
		// append any newly-arrived devices; existing slots never move
		winmm::rescan();
		break;

	default:
		break;
	}
}

int poll_joystick()
{
	if (!_joystick_installed)
		return -1;

	if (s_device_change_pending.exchange(false))
		handle_device_change();

	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
	{
		// DI sticks fill joy[0..d-1]; XInput pads fill joy[d..d+3]
		int d = dinput::poll();
		int x = xinput::poll(d);
		num_joysticks = (x > 0) ? (d + x) : d;
		return 0;
	}

	case JoystickDriver::XInput:
		num_joysticks = xinput::poll(0);
		return 0;

	case JoystickDriver::WinMM:
		return winmm::poll();

	default:
		return -1;
	}
}

// -----------------------------------------------------------------------------
// Device display info for the INPUT DEVICES menu
// -----------------------------------------------------------------------------
const char* joystick_get_display_name(int index)
{
	static char namebuf[80];

	if (index < 0 || index >= MAX_JOYSTICKS) return "NONE";

	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
		if (index < dinput::s_num_devices)
			return dinput::s_devices[index].name;
		snprintf(namebuf, sizeof(namebuf), "XINPUT PAD %d",
			index - dinput::s_num_devices + 1);
		return namebuf;

	case JoystickDriver::XInput:
		snprintf(namebuf, sizeof(namebuf), "XINPUT PAD %d", index + 1);
		return namebuf;

	case JoystickDriver::WinMM:
		if (index < winmm::s_num_devices && winmm::s_devices[index].pname[0])
			return winmm::s_devices[index].pname;
		snprintf(namebuf, sizeof(namebuf), "JOY %d", index + 1);
		return namebuf;

	default:
		return "NONE";
	}
}

int joystick_is_connected(int index)
{
	if (index < 0 || index >= MAX_JOYSTICKS) return 0;

	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
		if (index < dinput::s_num_devices)
			return dinput::s_devices[index].alive;
		{
			int slot = index - dinput::s_num_devices;
			return (slot < xinput::MAX_CONTROLLERS && xinput::s_connected[slot]) ? 1 : 0;
		}

	case JoystickDriver::XInput:
		return (index < xinput::MAX_CONTROLLERS && xinput::s_connected[index]) ? 1 : 0;

	case JoystickDriver::WinMM:
		return (index < winmm::s_num_devices && winmm::s_devices[index].alive) ? 1 : 0;

	default:
		return 0;
	}
}

int joystick_device_count()
{
	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
		// DI devices + the 4 XInput slots (empty pads show disconnected)
		return dinput::s_num_devices + xinput::MAX_CONTROLLERS;

	case JoystickDriver::XInput:
		// fixed slot space; empty slots show as disconnected
		return (MAX_JOYSTICKS < xinput::MAX_CONTROLLERS) ? MAX_JOYSTICKS : xinput::MAX_CONTROLLERS;

	case JoystickDriver::WinMM:
		return winmm::s_num_devices;

	default:
		return 0;
	}
}

// -----------------------------------------------------------------------------
// Stable device identity for persisted assignments:
//   "DI:{instance-guid}"  DirectInput stick (stable per machine)
//   "XINPUT:n"            XInput pad slot n
//   "WINMM:n"             WinMM device position (weak identity; best-effort)
// joystick_find_by_id resolves an id string to the CURRENT joy[] index, or
// -1 when that device is not present.
// -----------------------------------------------------------------------------
const char* joystick_get_id(int index)
{
	static char idbuf[64];

	if (index < 0 || index >= MAX_JOYSTICKS) return "";

	switch (s_active_driver) {
	case JoystickDriver::Hybrid:
		if (index < dinput::s_num_devices) {
			snprintf(idbuf, sizeof(idbuf), "DI:%s", dinput::s_devices[index].guid_str);
			return idbuf;
		}
		snprintf(idbuf, sizeof(idbuf), "XINPUT:%d", index - dinput::s_num_devices);
		return idbuf;

	case JoystickDriver::XInput:
		snprintf(idbuf, sizeof(idbuf), "XINPUT:%d", index);
		return idbuf;

	case JoystickDriver::WinMM:
		snprintf(idbuf, sizeof(idbuf), "WINMM:%d", index);
		return idbuf;

	default:
		return "";
	}
}

int joystick_find_by_id(const char* id)
{
	if (!id || !id[0]) return -1;

	if (strncmp(id, "DI:", 3) == 0) {
		if (s_active_driver != JoystickDriver::Hybrid) return -1;
		for (int i = 0; i < dinput::s_num_devices; i++)
			if (strcmp(dinput::s_devices[i].guid_str, id + 3) == 0)
				return dinput::s_devices[i].alive ? i : -1;
		return -1;
	}

	if (strncmp(id, "XINPUT:", 7) == 0) {
		int slot = atoi(id + 7);
		if (slot < 0 || slot >= xinput::MAX_CONTROLLERS) return -1;
		if (s_active_driver == JoystickDriver::Hybrid)
			return xinput::s_connected[slot] ? (dinput::s_num_devices + slot) : -1;
		if (s_active_driver == JoystickDriver::XInput)
			return xinput::s_connected[slot] ? slot : -1;
		return -1;
	}

	if (strncmp(id, "WINMM:", 6) == 0) {
		if (s_active_driver != JoystickDriver::WinMM) return -1;
		int idx = atoi(id + 6);
		return (idx >= 0 && idx < winmm::s_num_devices) ? idx : -1;
	}

	return -1;
}

void set_joystick_hotplug_callback(JoystickHotplugCallback callback)
{
	s_hotplug_callback = callback;
}

bool joystick_using_xinput()
{
	// Hybrid mode is XInput serving the pads with DirectInput8 alongside for
	// generic HID sticks, so combo detection must treat both as XInput-active.
	return s_active_driver == JoystickDriver::XInput ||
	       s_active_driver == JoystickDriver::Hybrid;
}

const char* joystick_driver_name()
{
	switch (s_active_driver) {
	case JoystickDriver::XInput: return "XInput";
	case JoystickDriver::WinMM:  return "WinMM";
	case JoystickDriver::Hybrid: return "DirectInput8+XInput";
	default:                     return "None";
	}
}

bool joystick_any_connected()
{
	return num_joysticks > 0;
}

//------------------------------------------------------------------------------
// Rumble Implementation
//------------------------------------------------------------------------------

bool joystick_set_rumble(int player, float left_motor_speed, float right_motor_speed)
{
	if (!_joystick_installed || s_active_driver != JoystickDriver::XInput)
		return false;

	return xinput::set_rumble(player, left_motor_speed, right_motor_speed);
}

void joystick_stop_rumble(int player)
{
	joystick_set_rumble(player, 0.0f, 0.0f);
}