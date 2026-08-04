//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// evdev_joystick.cpp -- the Linux implementation of the joystick.h contract.
//
// Sibling of system/input/Joystick.cpp, which is the Win32 (XInput/WinMM)
// implementation of the same header. The split mirrors Windows deliberately:
// there, rawinput.cpp owns keyboards and mice and Joystick.cpp owns pads, each
// enumerating its own devices. Same here.
//
// joy[] is filled to match the XInput layout that joystick.h documents and
// Joystick.cpp produces - two sticks, sixteen buttons in a fixed order, axes
// in -128..127, digital flags at |pos| > 32, D-pad overriding the left stick.
// Anything reading joy_x / joy_b1 / joy_hat must not be able to tell which
// platform it is on.
//==============================================================================
#include "evdev_device.h"

#include "joystick.h"
#include "sys_log.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Globals the contract declares extern.
//------------------------------------------------------------------------------
int num_joysticks = 0;
int _joystick_installed = 0;
JOYSTICK_INFO joy[MAX_JOYSTICKS] = {};

namespace {

constexpr int DIGITAL_THRESHOLD    = 32;   // out of 127, matching Joystick.cpp
constexpr int TRIGGER_THRESHOLD    = 30;   // out of 255
constexpr int COMBO_CONFIRM_FRAMES = 2;
constexpr int RESCAN_FRAMES        = 120;  // ~2s at 60fps

const char* const kButtonNames[16] = {
	"A", "B", "X", "Y",
	"LB", "RB",
	"Back", "Start",
	"LStick", "RStick",
	"DPadUp", "DPadDown", "DPadLeft", "DPadRight",
	"LT", "RT"
};

//------------------------------------------------------------------------------
// One connected pad.
//
// Slots are STABLE: a pad keeps its joy[] index for the life of the process,
// identified by its by-id path, so a mid-session unplug does not renumber the
// other players. This is the same guarantee Joystick.cpp gives by mapping
// XInput slot directly to joy index.
//------------------------------------------------------------------------------
struct PadSlot {
	int         devIndex  = -1;      // into s_devices, -1 when detached
	bool        connected = false;
	std::string name;
	std::string identity;

	uint16_t buttons = 0;            // AAE_JOYBTN_* bitmask
	int      trigL   = 0;            // 0..255
	int      trigR   = 0;

	// Raw axis values as reported, plus the range needed to scale them.
	// The range is read per device with EVIOCGABS and NEVER assumed: the
	// test pad advertises 0..1023 for the left stick and -2048..2047 for the
	// right, and code with an XInput-shaped -32768..32767 assumption reads
	// both as pinned hard over.
	struct Axis {
		bool present = false;
		int  raw = 0, min = 0, max = 0, flat = 0;
	};
	Axis lx, ly, rx, ry, hatX, hatY;

	int comboHoldFrames[JOY_MAX_COMBOS] = {0};
};

PadSlot s_pads[MAX_JOYSTICKS];
int s_numPads = 0;

std::vector<EvdevDevice> s_devices;

// Nodes already probed and decided about, including rejects - see ScanPads.
std::vector<std::string> s_examined;

JoystickHotplugCallback s_hotplugCallback = nullptr;
bool s_deviceChangePending = false;
int  s_rescanCountdown = 0;

// Combo mask -> index, allocated on first use. Same scheme as Joystick.cpp.
uint16_t s_comboMasks[JOY_MAX_COMBOS] = {0};
int      s_numCombos = 0;

int GetComboIndex(uint16_t mask)
{
	for (int i = 0; i < s_numCombos; i++)
		if (s_comboMasks[i] == mask) return i;
	if (s_numCombos >= JOY_MAX_COMBOS) return 0;
	s_comboMasks[s_numCombos] = mask;
	return s_numCombos++;
}

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

//------------------------------------------------------------------------------
// Scale one axis from its OWN advertised range to -128..127.
//
// The range comes from EVIOCGABS, which is the whole point. Note that a
// device's resting value is the CENTRE of its range, not zero: the test pad's
// left stick rests at 511 within 0..1023, so a naive sign-preserving scale
// would read it as fully deflected.
//
// `flat` is the driver's own declared dead zone, in raw units. Honouring it
// rather than inventing a percentage means a pad that declares no dead zone
// gets none, and one that declares a large one is not double-corrected.
//------------------------------------------------------------------------------
int ScaleAxis(const PadSlot::Axis& a)
{
	if (!a.present || a.max <= a.min) return 0;

	const int centre = (a.min + a.max) / 2;
	int       offset = a.raw - centre;

	if (a.flat > 0) {
		if (offset > a.flat)       offset -= a.flat;
		else if (offset < -a.flat) offset += a.flat;
		else                       return 0;
	}

	// Half-range measured from the centre, reduced by the dead zone so a
	// fully deflected stick still reaches the extreme after the subtraction
	// above - otherwise every pad quietly loses its top few percent.
	const int halfRange = ((a.max - a.min) / 2) - a.flat;
	if (halfRange <= 0) return 0;

	return ClampInt((offset * 127) / halfRange, -128, 127);
}

void SetAxis(JOYSTICK_AXIS_INFO& axis, int pos)
{
	axis.pos = ClampInt(pos, -128, 127);
	axis.d1  = (axis.pos < -DIGITAL_THRESHOLD) ? 1 : 0;
	axis.d2  = (axis.pos >  DIGITAL_THRESHOLD) ? 1 : 0;
}

void ResetJoyEntry(int index)
{
	JOYSTICK_INFO& j = joy[index];
	j.flags = 0;
	j.num_sticks = 0;
	j.num_buttons = 0;
	for (int s = 0; s < MAX_JOYSTICK_STICKS; s++) {
		j.stick[s].flags = 0;
		j.stick[s].num_axis = 0;
		j.stick[s].name = "n/a";
		for (int a = 0; a < MAX_JOYSTICK_AXIS; a++) {
			j.stick[s].axis[a].pos = 0;
			j.stick[s].axis[a].d1 = 0;
			j.stick[s].axis[a].d2 = 0;
			j.stick[s].axis[a].name = "n/a";
		}
	}
	for (int b = 0; b < MAX_JOYSTICK_BUTTONS; b++) {
		j.button[b].b = 0;
		j.button[b].name = "n/a";
	}
}

void SetupDescriptor(int index)
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
	for (int b = 0; b < 16; b++)
		j.button[b].name = kButtonNames[b];
}

//------------------------------------------------------------------------------
// evdev BTN_* -> AAE_JOYBTN_*.
//
// A wart worth stating plainly: the kernel's cardinal names do not line up
// with the letters printed on an Xbox pad. BTN_NORTH is aliased to BTN_X and
// BTN_WEST to BTN_Y in <linux/input-event-codes.h>, whereas physically the
// north button is Y and the west button is X. The xpad driver emits the
// ALIASES for the printed letters, so matching on BTN_X / BTN_Y (rather than
// reasoning about compass points) is what puts the right letter on the right
// button. Using BTN_NORTH here would swap X and Y on every controller.
//------------------------------------------------------------------------------
uint16_t EvdevButtonToAae(int code)
{
	switch (code) {
	case BTN_A:       return AAE_JOYBTN_A;
	case BTN_B:       return AAE_JOYBTN_B;
	case BTN_X:       return AAE_JOYBTN_X;
	case BTN_Y:       return AAE_JOYBTN_Y;
	case BTN_TL:      return AAE_JOYBTN_LEFT_SHOULDER;
	case BTN_TR:      return AAE_JOYBTN_RIGHT_SHOULDER;
	case BTN_SELECT:  return AAE_JOYBTN_BACK;
	case BTN_START:   return AAE_JOYBTN_START;
	case BTN_THUMBL:  return AAE_JOYBTN_LEFT_THUMB;
	case BTN_THUMBR:  return AAE_JOYBTN_RIGHT_THUMB;
	case BTN_DPAD_UP:    return AAE_JOYBTN_DPAD_UP;
	case BTN_DPAD_DOWN:  return AAE_JOYBTN_DPAD_DOWN;
	case BTN_DPAD_LEFT:  return AAE_JOYBTN_DPAD_LEFT;
	case BTN_DPAD_RIGHT: return AAE_JOYBTN_DPAD_RIGHT;
	// BTN_MODE (the guide button) has no AAE bit and is intentionally dropped.
	default: return 0;
	}
}

void ReadAxisRange(const EvdevDevice& dev, int code, PadSlot::Axis& out)
{
	input_absinfo info {};
	if (!dev.HasAbsAxis(code) || !dev.GetAbsInfo(code, &info)) {
		out.present = false;
		return;
	}
	out.present = true;
	out.min  = info.minimum;
	out.max  = info.maximum;
	out.flat = info.flat;
	out.raw  = info.value;      // current position, so a stick held at startup
	                            // is not reported as centred
}

//------------------------------------------------------------------------------
void AttachPad(int devIndex, const EvdevDevice& dev)
{
	int slot = -1;
	for (int i = 0; i < s_numPads; i++)
		if (s_pads[i].identity == dev.identity()) { slot = i; break; }

	if (slot < 0) {
		if (s_numPads >= MAX_JOYSTICKS) {
			LOG_WARN("evdev: more than %d gamepads; '%s' ignored",
			         MAX_JOYSTICKS, dev.name().c_str());
			return;
		}
		slot = s_numPads++;
		s_pads[slot].identity = dev.identity();
		s_pads[slot].name     = dev.name();
	}

	PadSlot& p = s_pads[slot];
	p.devIndex  = devIndex;
	p.connected = true;
	p.buttons   = 0;
	p.trigL = p.trigR = 0;

	ReadAxisRange(dev, ABS_X,     p.lx);
	ReadAxisRange(dev, ABS_Y,     p.ly);
	ReadAxisRange(dev, ABS_RX,    p.rx);
	ReadAxisRange(dev, ABS_RY,    p.ry);
	ReadAxisRange(dev, ABS_HAT0X, p.hatX);
	ReadAxisRange(dev, ABS_HAT0Y, p.hatY);

	LOG_INFO("evdev: gamepad %d connected: %s (X %d..%d flat %d, RX %d..%d flat %d, "
	         "rumble %s)",
	         slot, p.name.c_str(),
	         p.lx.min, p.lx.max, p.lx.flat,
	         p.rx.min, p.rx.max, p.rx.flat,
	         dev.SupportsRumble() ? "yes" : "no");

	if (s_hotplugCallback) s_hotplugCallback(slot, true, nullptr);
}

void DetachPad(int slot)
{
	if (slot < 0 || slot >= s_numPads) return;
	LOG_INFO("evdev: gamepad %d disconnected: %s", slot, s_pads[slot].name.c_str());
	s_pads[slot].devIndex  = -1;
	s_pads[slot].connected = false;
	s_pads[slot].buttons   = 0;
	s_pads[slot].trigL = s_pads[slot].trigR = 0;
	if (s_hotplugCallback) s_hotplugCallback(slot, false, "Controller disconnected");
}

//------------------------------------------------------------------------------
void ScanPads()
{
	const std::vector<EvdevNode> nodes = EvdevEnumerateNodes();

	// Forget nodes that have gone, so a replugged pad is probed again.
	s_examined.erase(
		std::remove_if(s_examined.begin(), s_examined.end(),
			[&nodes](const std::string& p) {
				return std::none_of(nodes.begin(), nodes.end(),
					[&p](const EvdevNode& n) { return n.devNode == p; });
			}),
		s_examined.end());

	for (const EvdevNode& node : nodes) {
		int existing = -1;
		for (size_t i = 0; i < s_devices.size(); i++)
			if (s_devices[i].devNode() == node.devNode) { existing = (int)i; break; }

		if (existing >= 0) {
			if (s_devices[existing].IsOpen()) continue;
			// Replugged: re-open in place so the pad keeps its joy[] slot.
			if (s_devices[existing].Open(node.devNode, node.identity) &&
			    s_devices[existing].kind() == EvdevKind::Gamepad)
				AttachPad(existing, s_devices[existing]);
			continue;
		}

		// The same trap evdev_input.cpp's ScanDevices documents: every
		// keyboard, mouse and system button on the machine is NOT a gamepad,
		// and re-probing all of them every two seconds - opening, five
		// EVIOCGBIT ioctls, a log line, closing - happens on the thread that
		// is trying to render.
		if (std::find(s_examined.begin(), s_examined.end(), node.devNode) != s_examined.end())
			continue;
		s_examined.push_back(node.devNode);

		EvdevDevice dev;
		if (!dev.Open(node.devNode, node.identity)) continue;
		if (dev.kind() != EvdevKind::Gamepad) continue;

		s_devices.push_back(std::move(dev));
		const int idx = (int)s_devices.size() - 1;
		AttachPad(idx, s_devices[idx]);
	}
}

//------------------------------------------------------------------------------
void HandlePadEvent(PadSlot& p, const input_event& ev)
{
	if (ev.type == EV_KEY) {
		if (ev.value == 2) return;                 // auto-repeat, not a change
		const uint16_t bit = EvdevButtonToAae(ev.code);
		if (bit) {
			if (ev.value) p.buttons |= bit;
			else          p.buttons &= (uint16_t)~bit;
		}
		return;
	}

	if (ev.type != EV_ABS) return;

	switch (ev.code) {
	case ABS_X:     p.lx.raw = ev.value; break;
	case ABS_Y:     p.ly.raw = ev.value; break;
	case ABS_RX:    p.rx.raw = ev.value; break;
	case ABS_RY:    p.ry.raw = ev.value; break;
	// Triggers are analogue axes on evdev, not buttons. Their own ranges are
	// normalised to 0..255 so TRIGGER_THRESHOLD means the same thing it does
	// on the XInput side.
	case ABS_Z:     p.trigL = ev.value; break;
	case ABS_RZ:    p.trigR = ev.value; break;
	// A hat reports -1/0/+1 and is the D-pad on most pads. Pads that expose
	// the D-pad as BTN_DPAD_* instead are handled in EvdevButtonToAae; a pad
	// doing both simply sets the same bits twice.
	case ABS_HAT0X:
		p.hatX.raw = ev.value;
		p.buttons &= (uint16_t)~(AAE_JOYBTN_DPAD_LEFT | AAE_JOYBTN_DPAD_RIGHT);
		if (ev.value < 0) p.buttons |= AAE_JOYBTN_DPAD_LEFT;
		if (ev.value > 0) p.buttons |= AAE_JOYBTN_DPAD_RIGHT;
		break;
	case ABS_HAT0Y:
		p.hatY.raw = ev.value;
		p.buttons &= (uint16_t)~(AAE_JOYBTN_DPAD_UP | AAE_JOYBTN_DPAD_DOWN);
		if (ev.value < 0) p.buttons |= AAE_JOYBTN_DPAD_UP;
		if (ev.value > 0) p.buttons |= AAE_JOYBTN_DPAD_DOWN;
		break;
	default: break;
	}
}

//------------------------------------------------------------------------------
// Publish one pad into joy[].
//------------------------------------------------------------------------------
void PublishPad(int slot)
{
	PadSlot& p = s_pads[slot];
	JOYSTICK_INFO& j = joy[slot];

	SetupDescriptor(slot);

	SetAxis(j.stick[0].axis[0], ScaleAxis(p.lx));
	// Y is NOT negated here, and that is the opposite of what Joystick.cpp
	// does - deliberately, because the two sources disagree at the source:
	//
	//   XInput  reports the stick pushed UP as POSITIVE, so Joystick.cpp
	//           negates (set_axis(..., -ly)) to reach AAE's screen-space
	//           convention of "negative = up" (joystick.h).
	//   evdev   already reports UP as NEGATIVE - the kernel gamepad spec puts
	//           left and up at the minimum end - which IS AAE's convention.
	//
	// Negating here as well double-inverts. It was written that way first,
	// and the symptom was that pushing the stick up and pressing D-pad up
	// produced opposite signs (the D-pad path sets -128 for up explicitly
	// below), which is what caught it.
	SetAxis(j.stick[0].axis[1], ScaleAxis(p.ly));
	SetAxis(j.stick[1].axis[0], ScaleAxis(p.rx));
	SetAxis(j.stick[1].axis[1], ScaleAxis(p.ry));

	// D-pad overrides the left stick, matching apply_dpad_to_left_stick.
	if (p.buttons & AAE_JOYBTN_DPAD_LEFT) {
		j.stick[0].axis[0].pos = -128; j.stick[0].axis[0].d1 = 1; j.stick[0].axis[0].d2 = 0;
	} else if (p.buttons & AAE_JOYBTN_DPAD_RIGHT) {
		j.stick[0].axis[0].pos =  127; j.stick[0].axis[0].d1 = 0; j.stick[0].axis[0].d2 = 1;
	}
	if (p.buttons & AAE_JOYBTN_DPAD_UP) {
		j.stick[0].axis[1].pos = -128; j.stick[0].axis[1].d1 = 1; j.stick[0].axis[1].d2 = 0;
	} else if (p.buttons & AAE_JOYBTN_DPAD_DOWN) {
		j.stick[0].axis[1].pos =  127; j.stick[0].axis[1].d1 = 0; j.stick[0].axis[1].d2 = 1;
	}

	const uint16_t b = p.buttons;
	j.button[0].b  = (b & AAE_JOYBTN_A) ? 1 : 0;
	j.button[1].b  = (b & AAE_JOYBTN_B) ? 1 : 0;
	j.button[2].b  = (b & AAE_JOYBTN_X) ? 1 : 0;
	j.button[3].b  = (b & AAE_JOYBTN_Y) ? 1 : 0;
	j.button[4].b  = (b & AAE_JOYBTN_LEFT_SHOULDER)  ? 1 : 0;
	j.button[5].b  = (b & AAE_JOYBTN_RIGHT_SHOULDER) ? 1 : 0;
	j.button[6].b  = (b & AAE_JOYBTN_BACK)  ? 1 : 0;
	j.button[7].b  = (b & AAE_JOYBTN_START) ? 1 : 0;
	j.button[8].b  = (b & AAE_JOYBTN_LEFT_THUMB)  ? 1 : 0;
	j.button[9].b  = (b & AAE_JOYBTN_RIGHT_THUMB) ? 1 : 0;
	j.button[10].b = (b & AAE_JOYBTN_DPAD_UP)    ? 1 : 0;
	j.button[11].b = (b & AAE_JOYBTN_DPAD_DOWN)  ? 1 : 0;
	j.button[12].b = (b & AAE_JOYBTN_DPAD_LEFT)  ? 1 : 0;
	j.button[13].b = (b & AAE_JOYBTN_DPAD_RIGHT) ? 1 : 0;
	j.button[14].b = (p.trigL > TRIGGER_THRESHOLD) ? 1 : 0;
	j.button[15].b = (p.trigR > TRIGGER_THRESHOLD) ? 1 : 0;
}

} // namespace

//==============================================================================
// Public API
//==============================================================================
int install_joystick()
{
	if (_joystick_installed) return 0;

	for (int i = 0; i < MAX_JOYSTICKS; i++) ResetJoyEntry(i);
	s_numPads = 0;
	num_joysticks = 0;

	ScanPads();

	int connected = 0;
	for (int i = 0; i < s_numPads; i++) if (s_pads[i].connected) connected++;
	num_joysticks = connected;

	_joystick_installed = 1;
	LOG_INFO("evdev joystick: installed, %d pad(s) connected", connected);

	// Succeeds with zero pads on purpose, matching the Windows behaviour the
	// header documents - a pad plugged in later is picked up by the rescan.
	return 0;
}

void remove_joystick()
{
	if (!_joystick_installed) return;

	for (int i = 0; i < s_numPads; i++)
		if (s_pads[i].devIndex >= 0)
			s_devices[s_pads[i].devIndex].StopRumble();

	s_devices.clear();
	s_numPads = 0;
	num_joysticks = 0;
	for (int i = 0; i < MAX_JOYSTICKS; i++) ResetJoyEntry(i);
	_joystick_installed = 0;
	LOG_INFO("evdev joystick: removed");
}

int poll_joystick()
{
	if (!_joystick_installed) return -1;

	std::vector<input_event> events;

	for (int slot = 0; slot < s_numPads; slot++) {
		PadSlot& p = s_pads[slot];
		if (p.devIndex < 0) continue;

		if (!s_devices[p.devIndex].ReadEvents(events)) {
			s_devices[p.devIndex].Close();
			DetachPad(slot);
			ResetJoyEntry(slot);
			continue;
		}
		for (const input_event& ev : events) {
			if (ev.type == EV_SYN) continue;
			HandlePadEvent(p, ev);
		}
		PublishPad(slot);
	}

	if (--s_rescanCountdown <= 0 || s_deviceChangePending) {
		s_rescanCountdown = RESCAN_FRAMES;
		s_deviceChangePending = false;
		ScanPads();
	}

	int connected = 0;
	for (int i = 0; i < s_numPads; i++) if (s_pads[i].connected) connected++;
	num_joysticks = connected;

	return 0;
}

//------------------------------------------------------------------------------
// Rumble
//------------------------------------------------------------------------------
bool joystick_set_rumble(int player, float left_motor_speed, float right_motor_speed)
{
	if (player < 0 || player >= s_numPads) return false;
	if (s_pads[player].devIndex < 0) return false;
	return s_devices[s_pads[player].devIndex].SetRumble(left_motor_speed, right_motor_speed);
}

void joystick_stop_rumble(int player)
{
	if (player < 0 || player >= s_numPads) return;
	if (s_pads[player].devIndex < 0) return;
	s_devices[s_pads[player].devIndex].StopRumble();
}

void set_joystick_hotplug_callback(JoystickHotplugCallback callback)
{
	s_hotplugCallback = callback;
}

//------------------------------------------------------------------------------
// Combos
//
// Note this does NOT gate on joystick_using_xinput() the way Joystick.cpp
// does. That check exists there because the WinMM fallback cannot read the
// XInput button mask at all; it is a limitation of that path, not part of the
// contract, and combos work normally on evdev.
//------------------------------------------------------------------------------
bool joystick_check_combo(int player, uint16_t buttonMask)
{
	if (player < 0 || player >= s_numPads) return false;
	if (!s_pads[player].connected) return false;

	const bool held = (s_pads[player].buttons & buttonMask) == buttonMask;
	const int  idx  = GetComboIndex(buttonMask);

	if (held) s_pads[player].comboHoldFrames[idx]++;
	else      s_pads[player].comboHoldFrames[idx] = 0;

	// Fires exactly once, on the frame the count REACHES the threshold - not
	// every frame after it. Equality, not >=, is what makes it edge-triggered.
	return s_pads[player].comboHoldFrames[idx] == COMBO_CONFIRM_FRAMES;
}

//------------------------------------------------------------------------------
// Queries
//------------------------------------------------------------------------------
bool joystick_using_xinput() { return false; }

const char* joystick_driver_name() { return "evdev"; }

void joystick_device_change() { s_deviceChangePending = true; }

int joystick_device_count() { return s_numPads; }

const char* joystick_get_display_name(int index)
{
	if (index < 0 || index >= s_numPads) return "NONE";
	return s_pads[index].name.c_str();
}

int joystick_is_connected(int index)
{
	if (index < 0 || index >= s_numPads) return 0;
	return s_pads[index].connected ? 1 : 0;
}

// Identity strings are prefixed by driver, matching the "XINPUT:n" / "DI:{...}"
// convention joystick.h documents, so a config written on one backend is not
// silently matched against a device on another.
const char* joystick_get_id(int index)
{
	static std::string s_id;
	if (index < 0 || index >= s_numPads) return "";
	s_id = "EVDEV:" + s_pads[index].identity;
	return s_id.c_str();
}

int joystick_find_by_id(const char* id)
{
	if (!id || !id[0]) return -1;
	if (strncmp(id, "EVDEV:", 6) != 0) return -1;
	const char* want = id + 6;
	for (int i = 0; i < s_numPads; i++)
		if (s_pads[i].identity == want) return i;
	return -1;
}

bool joystick_any_connected() { return num_joysticks > 0; }
