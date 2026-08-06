//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// evdev_input.cpp -- the Linux implementation of the sys_input.h contract.
//
// Mirrors rawinput.cpp: a merged legacy view (key[], mouse_b) that every
// existing consumer keeps using unchanged, plus per-device state behind the
// multi-HID _Ex API so players can be routed to individual keyboards and mice.
//
// The gamepad half of the port lives in evdev_joystick.cpp, matching the
// Windows split between rawinput.cpp and Joystick.cpp.
//
// NO WORKER THREAD -- see the rationale on EvdevInput_Poll in evdev_input.h.
//==============================================================================
#include "evdev_input.h"
#include "evdev_device.h"
#include "evdev_keymap.h"

#include "sys_input.h"
#include "sys_window.h"
#include "sys_log.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// State the contract says the backend DEFINES. sys_input.h declares these
// extern; a backend that omits them fails at link time, not compile time.
//------------------------------------------------------------------------------
unsigned char key[256] = {0};
int mouse_b = 0;

namespace {

// Hold counter, the twin of rawinput.cpp's lastkey[]. Internal to the backend
// on both platforms - nothing outside reads it directly, only isKeyHeld().
unsigned int s_lastkey[256] = {0};

float s_mouseScale = 1.0f;
bool  s_paused     = false;
bool  s_initialized = false;

// GLFW-style callbacks. Unlike Win32 these fire on the GAME thread, because
// that is where polling happens - strictly easier for callers, and the
// threading caveat in sys_input.h's docs does not apply to this backend.
KeyCallback            s_keyCallback    = nullptr;
MouseButtonCallback    s_mouseBtnCallback = nullptr;
CursorPositionCallback s_cursorCallback = nullptr;

//------------------------------------------------------------------------------
// Merged mouse state, laid out to match rawinput.cpp's DXTI_MOUSE_STATE so the
// two backends' accessors can be read side by side.
//------------------------------------------------------------------------------
struct MouseState {
	long x = 0, y = 0, wheel = 0;        // accumulated raw totals
	long dx = 0, dy = 0, dwheel = 0;     // deltas since last read
	bool left = false, middle = false, right = false;
};
MouseState s_mouse;

//------------------------------------------------------------------------------
// Per-device tables. Fixed-size to match RI_MAX_KBDS / RI_MAX_MICE, so index
// semantics are identical on both platforms.
//------------------------------------------------------------------------------
struct KbdSlot {
	int           devIndex = -1;
	bool          seen     = false;
	unsigned char key[256] = {0};
	std::string   name;
	std::string   identity;
};

struct MouseSlot {
	int         devIndex = -1;
	bool        seen     = false;
	long        dx = 0, dy = 0, dwheel = 0;
	int         buttons  = 0;      // bit0 left, bit1 right, bit2 middle
	std::string name;
	std::string identity;
};

KbdSlot   s_kbds[RI_MAX_KBDS];
MouseSlot s_mice[RI_MAX_MICE];
int s_numKbds = 0;
int s_numMice = 0;

// Every open node. Slots hold indices rather than pointers, so entries are
// never removed from the middle - a device that goes away is closed in place
// and re-opened there if it comes back.
std::vector<EvdevDevice> s_devices;

// Nodes already probed and decided about, INCLUDING the ones rejected. See
// ScanDevices for why re-probing them every two seconds was expensive enough
// to see on screen.
std::vector<std::string> s_examined;

//------------------------------------------------------------------------------
int RegisterKeyboard(int devIndex, const EvdevDevice& dev)
{
	for (int i = 0; i < s_numKbds; i++)
		if (s_kbds[i].identity == dev.identity()) {
			s_kbds[i].devIndex = devIndex;      // re-attached after a replug
			return i;
		}

	if (s_numKbds >= RI_MAX_KBDS) {
		LOG_WARN("evdev: more than %d keyboards present; '%s' is not routable",
		         RI_MAX_KBDS, dev.name().c_str());
		return -1;
	}

	const int i = s_numKbds++;
	s_kbds[i].devIndex = devIndex;
	s_kbds[i].seen     = false;
	memset(s_kbds[i].key, 0, sizeof(s_kbds[i].key));
	s_kbds[i].name     = dev.name();
	s_kbds[i].identity = dev.identity();
	LOG_INFO("evdev: keyboard %d registered: %s", i + 1, s_kbds[i].name.c_str());
	return i;
}

int RegisterMouse(int devIndex, const EvdevDevice& dev)
{
	for (int i = 0; i < s_numMice; i++)
		if (s_mice[i].identity == dev.identity()) {
			s_mice[i].devIndex = devIndex;
			return i;
		}

	if (s_numMice >= RI_MAX_MICE) {
		LOG_WARN("evdev: more than %d mice present; '%s' is not routable",
		         RI_MAX_MICE, dev.name().c_str());
		return -1;
	}

	const int i = s_numMice++;
	s_mice[i].devIndex = devIndex;
	s_mice[i].seen     = false;
	s_mice[i].dx = s_mice[i].dy = s_mice[i].dwheel = 0;
	s_mice[i].buttons  = 0;
	s_mice[i].name     = dev.name();
	s_mice[i].identity = dev.identity();
	LOG_INFO("evdev: mouse %d registered: %s", i + 1, s_mice[i].name.c_str());
	return i;
}

int KbdSlotForDevice(int devIndex)
{
	for (int i = 0; i < s_numKbds; i++)
		if (s_kbds[i].devIndex == devIndex) return i;
	return -1;
}

int MouseSlotForDevice(int devIndex)
{
	for (int i = 0; i < s_numMice; i++)
		if (s_mice[i].devIndex == devIndex) return i;
	return -1;
}

//------------------------------------------------------------------------------
// Modifier flags.
//
// Win32 asks the OS (GetAsyncKeyState), which reports system-wide state. There
// is no evdev equivalent - evdev reports events, not state - so this is
// derived from the merged key[] the backend just built. The observable
// difference is that a modifier held before AAE started is not seen here until
// it is released and pressed again, which is the same limitation any
// evdev client has.
//------------------------------------------------------------------------------
int ComputeModifierFlags()
{
	int mods = 0;
	if (key[AAEKEY_LSHIFT]   || key[AAEKEY_RSHIFT])   mods |= RI_MOD_SHIFT;
	if (key[AAEKEY_LCONTROL] || key[AAEKEY_RCONTROL]) mods |= RI_MOD_CONTROL;
	if (key[AAEKEY_ALT]      || key[AAEKEY_ALTGR])    mods |= RI_MOD_ALT;
	if (key[AAEKEY_LWIN]     || key[AAEKEY_RWIN])     mods |= RI_MOD_SUPER;
	return mods;
}

//------------------------------------------------------------------------------
// One keyboard event.
//
// evdev value: 0 = release, 1 = press, 2 = auto-repeat. The repeat case must
// NOT be treated as a fresh press for key[] (it is already 1), but it is
// exactly what the hold counter wants - which is how the Win32 side behaves
// too, where held keys generate repeated WM_INPUT messages.
//------------------------------------------------------------------------------
void HandleKeyEvent(int devIndex, const input_event& ev)
{
	const uint8_t aae = EvdevKeyToAae(ev.code);
	if (!aae) return;                    // key with no AAE equivalent

	const bool released = (ev.value == 0);

	if (released) {
		key[aae]      = 0;
		s_lastkey[aae] = 0;
	} else {
		key[aae] = 1;
		// Match rawinput.cpp exactly, including its wrap guard: the counter
		// must never land back on 0, because 0 means "not held".
		s_lastkey[aae] = (s_lastkey[aae] + 1) % 0xFFFFFFFF;
		if (s_lastkey[aae] == 0) s_lastkey[aae] = 1;
	}

	const int slot = KbdSlotForDevice(devIndex);
	if (slot >= 0) {
		s_kbds[slot].seen = true;
		s_kbds[slot].key[aae] = released ? 0 : 1;
	}

	// Auto-repeat is not a state change; firing the callback for it would make
	// a held key look like a machine-gun of presses to anything counting them.
	if (s_keyCallback && ev.value != 2)
		s_keyCallback((int)aae, (int)ev.code, released ? 0 : 1, ComputeModifierFlags());
}

//------------------------------------------------------------------------------
// One mouse event. Buttons, relative motion and wheel.
//------------------------------------------------------------------------------
void HandleMouseEvent(int devIndex, const input_event& ev)
{
	const int slot = MouseSlotForDevice(devIndex);

	if (ev.type == EV_REL) {
		switch (ev.code) {
		case REL_X:
			s_mouse.dx += ev.value;
			s_mouse.x  += ev.value;
			if (slot >= 0) { s_mice[slot].dx += ev.value; s_mice[slot].seen = true; }
			break;
		case REL_Y:
			s_mouse.dy += ev.value;
			s_mouse.y  += ev.value;
			if (slot >= 0) { s_mice[slot].dy += ev.value; s_mice[slot].seen = true; }
			break;
		case REL_WHEEL:
			// Win32 reports wheel motion in WHEEL_DELTA (120) units and AAE
			// passes that through; evdev reports detents. Scaling here keeps
			// GetMouseWheel() meaning the same number on both platforms.
			s_mouse.dwheel += ev.value * 120;
			s_mouse.wheel  += ev.value * 120;
			if (slot >= 0) { s_mice[slot].dwheel += ev.value * 120; s_mice[slot].seen = true; }
			break;
		default:
			return;
		}

		if (s_cursorCallback)
			s_cursorCallback((double)s_mouse.x, (double)s_mouse.y);
		return;
	}

	if (ev.type != EV_KEY) return;

	// Allegro-compatible bit order, as sys_input.h documents:
	// bit0 = left, bit1 = right, bit2 = middle. Note that this is NOT the
	// order the evdev codes run in (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE are
	// 0x110, 0x111, 0x112), so middle and right must not be transcribed
	// positionally.
	int bit = 0, button = 0;
	switch (ev.code) {
	case BTN_LEFT:   bit = 0x01; button = 0; break;
	case BTN_RIGHT:  bit = 0x02; button = 1; break;
	case BTN_MIDDLE: bit = 0x04; button = 2; break;
	default: return;
	}

	const bool down = (ev.value != 0);
	if (ev.value == 2) return;            // buttons do not meaningfully repeat

	// Written out rather than using bset/bclr: those macros are declared in
	// rawinput_win32.h, which is Win32-PRIVATE and includes <windows.h>.
	// Redefining them here would put two definitions of the same names in the
	// tree for the sake of two lines.
	if (down) mouse_b |= bit; else mouse_b &= ~bit;
	switch (bit) {
	case 0x01: s_mouse.left   = down; break;
	case 0x02: s_mouse.right  = down; break;
	case 0x04: s_mouse.middle = down; break;
	}

	if (slot >= 0) {
		s_mice[slot].seen = true;
		if (down) s_mice[slot].buttons |= bit; else s_mice[slot].buttons &= ~bit;
	}

	if (s_mouseBtnCallback)
		s_mouseBtnCallback(button, down ? 1 : 0, ComputeModifierFlags());
}

//------------------------------------------------------------------------------
void ClearAllState()
{
	memset(key, 0, sizeof(key));
	memset(s_lastkey, 0, sizeof(s_lastkey));

	mouse_b = 0;
	s_mouse.left = s_mouse.right = s_mouse.middle = false;
	s_mouse.dx = s_mouse.dy = s_mouse.dwheel = 0;

	for (int i = 0; i < s_numKbds; i++)
		memset(s_kbds[i].key, 0, sizeof(s_kbds[i].key));
	for (int i = 0; i < s_numMice; i++) {
		s_mice[i].dx = s_mice[i].dy = s_mice[i].dwheel = 0;
		s_mice[i].buttons = 0;
	}
}

//------------------------------------------------------------------------------
// Open every node worth opening. Gamepads are skipped here - evdev_joystick.cpp
// opens those itself, mirroring the Windows split where Joystick.cpp owns its
// own devices independently of rawinput.cpp.
//------------------------------------------------------------------------------
void ScanDevices()
{
	const std::vector<EvdevNode> nodes = EvdevEnumerateNodes();

	// Drop remembered nodes that have gone away, so unplug-then-replug probes
	// the device again instead of it staying "rejected" for the session.
	s_examined.erase(
		std::remove_if(s_examined.begin(), s_examined.end(),
			[&nodes](const std::string& p) {
				return std::none_of(nodes.begin(), nodes.end(),
					[&p](const EvdevNode& n) { return n.devNode == p; });
			}),
		s_examined.end());

	int permissionDenied = 0;
	for (const EvdevNode& node : nodes) {
		int existing = -1;
		for (size_t i = 0; i < s_devices.size(); i++)
			if (s_devices[i].devNode() == node.devNode) { existing = (int)i; break; }

		if (existing >= 0) {
			if (s_devices[existing].IsOpen()) continue;

			// Was unplugged and is back. Re-open IN PLACE: the keyboard and
			// mouse slot tables hold this index, and reattaching by identity
			// is what keeps a player's device assignment across a replug.
			if (s_devices[existing].Open(node.devNode, node.identity)) {
				if (s_devices[existing].kind() == EvdevKind::Keyboard)
					RegisterKeyboard(existing, s_devices[existing]);
				else if (s_devices[existing].kind() == EvdevKind::Mouse)
					RegisterMouse(existing, s_devices[existing]);
			}
			continue;
		}

		// Probed before and not ours - a gamepad, a power button, a lid
		// switch, an HDMI audio jack. THIS CHECK IS THE POINT: without it
		// every rescan re-opened and re-classified every such node, five
		// EVIOCGBIT ioctls and a log line each, on the GAME THREAD, twice
		// every two seconds once evdev_joystick.cpp's identical scan is
		// counted. On a machine with twenty event nodes that is a visible
		// stutter roughly once a second.
		if (std::find(s_examined.begin(), s_examined.end(), node.devNode) != s_examined.end())
			continue;
		s_examined.push_back(node.devNode);

		EvdevDevice dev;
		if (!dev.Open(node.devNode, node.identity)) {
			permissionDenied++;
			continue;
		}
		if (dev.kind() != EvdevKind::Keyboard && dev.kind() != EvdevKind::Mouse)
			continue;                     // gamepads and unclassifiable nodes

		s_devices.push_back(std::move(dev));
		const int idx = (int)s_devices.size() - 1;
		if (s_devices[idx].kind() == EvdevKind::Keyboard)
			RegisterKeyboard(idx, s_devices[idx]);
		else
			RegisterMouse(idx, s_devices[idx]);
	}

	// Reported on TRANSITION only: ScanDevices runs every ~2 seconds for
	// hotplug, and an unconditional warning here fills the log with the same
	// line forever on a machine that simply has no input devices.
	static int s_lastComplaint = -1;
	const int complaint = s_devices.empty() ? (permissionDenied > 0 ? 1 : 2) : 0;
	if (complaint != s_lastComplaint) {
		s_lastComplaint = complaint;
		if (complaint == 1)
			LOG_ERROR("evdev: %d device node(s) present but none could be opened - "
			          "this is a PERMISSIONS problem, not missing hardware",
			          permissionDenied);
		else if (complaint == 2)
			LOG_WARN("evdev: no input device nodes found at all - keyboard and "
			         "mouse will not work");
	}
}

//------------------------------------------------------------------------------
// Drop a device that went away, leaving its slot in place.
//
// The slot is NOT removed: a player assigned to keyboard 2 must still be
// assigned to keyboard 2 after it is unplugged and plugged back in. Clearing
// devIndex marks it detached; RegisterKeyboard reattaches by identity.
//------------------------------------------------------------------------------
void DetachDevice(int devIndex)
{
	const int k = KbdSlotForDevice(devIndex);
	if (k >= 0) {
		LOG_INFO("evdev: keyboard %d disconnected: %s", k + 1, s_kbds[k].name.c_str());
		memset(s_kbds[k].key, 0, sizeof(s_kbds[k].key));
		s_kbds[k].devIndex = -1;
	}
	const int m = MouseSlotForDevice(devIndex);
	if (m >= 0) {
		LOG_INFO("evdev: mouse %d disconnected: %s", m + 1, s_mice[m].name.c_str());
		s_mice[m].buttons = 0;
		s_mice[m].devIndex = -1;
	}
	s_devices[devIndex].Close();
}

} // namespace

//==============================================================================
// Lifecycle
//==============================================================================
bool EvdevInput_Initialize()
{
	if (s_initialized) RawInput_Shutdown();

	ClearAllState();
	s_numKbds = s_numMice = 0;
	s_paused  = false;

	EvdevKeymapLogCoverage();
	ScanDevices();

	LOG_INFO("evdev: %d mouse device(s), %d keyboard device(s) present",
	         s_numMice, s_numKbds);

	s_initialized = true;
	return !s_devices.empty();
}

void RawInput_Shutdown()
{
	if (!s_initialized) return;
	s_devices.clear();          // EvdevDevice::~EvdevDevice closes each fd
	s_numKbds = s_numMice = 0;
	ClearAllState();
	s_initialized = false;
	LOG_INFO("evdev: shutdown complete");
}

void EvdevInput_Poll()
{
	if (!s_initialized) return;

	static int rescanCountdown = 0;
	std::vector<input_event> events;
	bool lostDevice = false;

	for (size_t i = 0; i < s_devices.size(); i++) {
		if (!s_devices[i].IsOpen()) continue;

		if (!s_devices[i].ReadEvents(events)) {
			DetachDevice((int)i);
			lostDevice = true;
			continue;
		}

		// While paused the fds are still DRAINED, just not acted on. Leaving
		// them unread instead would let the kernel buffer fill and then
		// deliver the whole backlog the moment focus returned - every key
		// pressed in another application arriving at once.
		if (s_paused) continue;

		const EvdevKind kind = s_devices[i].kind();
		for (const input_event& ev : events) {
			if (ev.type == EV_SYN) continue;
			if (kind == EvdevKind::Keyboard) {
				if (ev.type == EV_KEY) HandleKeyEvent((int)i, ev);
			} else if (kind == EvdevKind::Mouse) {
				HandleMouseEvent((int)i, ev);
			}
		}
	}

	// Hotplug: re-enumerate periodically rather than taking a libudev
	// dependency for it. At 60fps this is roughly every two seconds, which is
	// well inside the time it takes a person to plug something in and reach
	// for it, and costs one readdir of a directory with a handful of entries.
	if (--rescanCountdown <= 0 || lostDevice) {
		rescanCountdown = 120;
		ScanDevices();
	}
}

//==============================================================================
// Keyboard - merged view
//==============================================================================
int  isKeyHeld(int vkCode) { return (int)s_lastkey[vkCode & 0xff]; }
bool IsKeyDown(int vkCode) { return key[vkCode & 0xff] != 0; }
bool IsKeyUp(int vkCode)   { return key[vkCode & 0xff] == 0; }

int GetModifierFlags() { return ComputeModifierFlags(); }

void test_clr()
{
	memset(key, 0, sizeof(key));
	memset(s_lastkey, 0, sizeof(s_lastkey));
}

void RawInput_SetPaused(bool paused)
{
	const bool wasPaused = s_paused;
	s_paused = paused;

	// Only the transition into paused flushes, matching the Win32 backend -
	// clearing on every call would wipe state a caller had just set.
	if (paused && !wasPaused)
		ClearAllState();
}

//==============================================================================
// Keyboard - per-device view
//==============================================================================
int RawInput_GetKeyboardCount() { return s_numKbds; }

const char* RawInput_GetKeyboardName(int index)
{
	if (index < 0 || index >= s_numKbds) return "NONE";
	return s_kbds[index].name.c_str();
}

const char* RawInput_GetKeyboardPath(int index)
{
	if (index < 0 || index >= s_numKbds) return "";
	return s_kbds[index].identity.c_str();
}

int RawInput_FindKeyboardByPath(const char* path)
{
	if (!path || !path[0]) return -1;
	for (int i = 0; i < s_numKbds; i++)
		if (s_kbds[i].identity == path) return i;
	return -1;
}

int RawInput_KeyboardSeenInput(int index)
{
	if (index < 0 || index >= s_numKbds) return 0;
	return s_kbds[index].seen ? 1 : 0;
}

int RawInput_IsKeyDownEx(int index, int vk)
{
	if (index < 0) return key[vk & 0xff];      // -1 = merged, per the contract
	if (index >= s_numKbds) return 0;
	return s_kbds[index].key[vk & 0xff];
}

//==============================================================================
// Callbacks
//==============================================================================
void SetKeyCallback(KeyCallback cb)                   { s_keyCallback = cb; }
void SetMouseButtonCallback(MouseButtonCallback cb)    { s_mouseBtnCallback = cb; }
void SetCursorPositionCallback(CursorPositionCallback cb) { s_cursorCallback = cb; }

//==============================================================================
// Mouse - merged view
//==============================================================================
void set_mouse_mickey_scale(float scale) { s_mouseScale = scale; }

void get_mouse_mickeys(int* mickeyx, int* mickeyy)
{
	const long tx = s_mouse.dx;
	const long ty = s_mouse.dy;
	s_mouse.dx = 0;                 // read-AND-RESET: trackball and spinner
	s_mouse.dy = 0;                 // games integrate these, and a missed
	                                // reset doubles the reported motion
	if (mickeyx) *mickeyx = (int)(tx * s_mouseScale);
	if (mickeyy) *mickeyy = (int)(ty * s_mouseScale);
}

void get_mouse_mickeys_ex(int index, int* mickeyx, int* mickeyy)
{
	if (index < 0) { get_mouse_mickeys(mickeyx, mickeyy); return; }

	if (index >= s_numMice) {
		if (mickeyx) *mickeyx = 0;
		if (mickeyy) *mickeyy = 0;
		return;
	}
	const long tx = s_mice[index].dx;
	const long ty = s_mice[index].dy;
	s_mice[index].dx = 0;
	s_mice[index].dy = 0;
	if (mickeyx) *mickeyx = (int)(tx * s_mouseScale);
	if (mickeyy) *mickeyy = (int)(ty * s_mouseScale);
}

// Cursor position in client coordinates.
//
// Win32 reads this from the OS (GetCursorPos + ScreenToClient) using the HWND
// the backend cached at init. The equivalent here is XQueryPointer, which the
// window already wraps - so this goes through ISystemWindow rather than
// opening a second X connection from the input layer.
void get_mouse_win(int* mickeyx, int* mickeyy)
{
	int x = 0, y = 0;
	GetSystemWindow().GetMousePos(&x, &y);
	if (mickeyx) *mickeyx = x;
	if (mickeyy) *mickeyy = y;
}

int32_t GetMouseX()     { return (int32_t)s_mouse.x; }
int32_t GetMouseY()     { return (int32_t)s_mouse.y; }
int32_t GetMouseWheel() { return (int32_t)s_mouse.wheel; }
void SetMouseX(int32_t x)         { s_mouse.x = x; }
void SetMouseY(int32_t y)         { s_mouse.y = y; }
void SetMouseWheel(int32_t wheel) { s_mouse.wheel = wheel; }

// These do NOT reset and do NOT apply the mickey scale - unlike
// get_mouse_mickeys. The asymmetry is the documented contract, not an
// oversight; rawinput.cpp behaves identically.
int32_t GetMouseXChange()     { return (int32_t)s_mouse.dx; }
int32_t GetMouseYChange()     { return (int32_t)s_mouse.dy; }
int32_t GetMouseWheelChange() { return (int32_t)s_mouse.dwheel; }

bool IsMouseLButtonDown() { return s_mouse.left; }
bool IsMouseLButtonUp()   { return !s_mouse.left; }
bool IsMouseRButtonDown() { return s_mouse.right; }
bool IsMouseRButtonUp()   { return !s_mouse.right; }
bool IsMouseMButtonDown() { return s_mouse.middle; }
bool IsMouseMButtonUp()   { return !s_mouse.middle; }

//==============================================================================
// Mouse - per-device view
//==============================================================================
int RawInput_GetMouseCount() { return s_numMice; }

const char* RawInput_GetMouseName(int index)
{
	if (index < 0 || index >= s_numMice) return "NONE";
	return s_mice[index].name.c_str();
}

const char* RawInput_GetMousePath(int index)
{
	if (index < 0 || index >= s_numMice) return "";
	return s_mice[index].identity.c_str();
}

int RawInput_FindMouseByPath(const char* path)
{
	if (!path || !path[0]) return -1;
	for (int i = 0; i < s_numMice; i++)
		if (s_mice[i].identity == path) return i;
	return -1;
}

int RawInput_GetMouseButtons(int index)
{
	if (index < 0) return mouse_b;              // -1 = merged
	if (index >= s_numMice) return 0;
	return s_mice[index].buttons;
}

int RawInput_MouseSeenInput(int index)
{
	if (index < 0 || index >= s_numMice) return 0;
	return s_mice[index].seen ? 1 : 0;
}

//==============================================================================
// Keyboard LEDs -- the osdepend.h LED contract.
//
// Declared locally rather than by including osdepend.h, which would drag the
// whole emulator surface into the input backend; linux_main.cpp forward-
// declares osd_led_service_stop the same way.
//
// Stubs for now: a later task fills these in. They live here rather than in
// led_service_handler.cpp because this file owns the open device fds the real
// implementation needs.
//==============================================================================
void osd_led_service_start();
void osd_led_service_stop();
void osd_set_leds(int state);
int  osd_get_leds();

void osd_led_service_start() {}
void osd_led_service_stop()  {}
void osd_set_leds(int)       {}
int  osd_get_leds()          { return 0; }
