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
//
// HOW A PAD'S EVDEV CODES BECOME THAT LAYOUT is decided per device by pad_db.h:
// the device is looked up in SDL's gamepad mapping database (SDL_GAMECONTROLLERCONFIG,
// an optional gamecontrollerdb.txt, then the built-in SDL list), else SDL's Linux
// automapper rules, else - for flight sticks and arcade encoders - raw button
// order. The result is a PadLayout ("canonical control <- evdev source") that
// the event handler and PublishPad below simply follow.
//==============================================================================
#include "evdev_device.h"

#include "joystick.h"
#include "pad_db.h"
#include "pad_map.h"
#include "sdl_gamepad_db_linux.h"
#include "sys_log.h"

#include <algorithm>
#include <climits>      // PATH_MAX
#include <cstdio>
#include <cstdlib>
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
	// True when the device resolved to a gamepad layout (an SDL database hit or
	// the automapper, see pad_db.h) - which includes a DirectInput-style pad the
	// database knows although it has no BTN_GAMEPAD. A raw joystick (flight
	// stick, arcade encoder: Classify() also admits those, and bare ABS_X/ABS_Y
	// devices) is NOT a gamepad and never fires system chords.
	bool        isGamepad = false;
	// Steam virtual pad while Steam is not hiding the physical ones (see
	// pad_demote_steam_virtual). The ONE kind of slot that may move: a physical
	// gamepad hotplugged later is inserted ahead of it (pad_hotplug_slot).
	bool        demoted   = false;
	PadLayout   layout;             // canonical control <- evdev source
	std::string name;
	std::string identity;

	uint16_t buttons = 0;            // AAE_JOYBTN_* bitmask, derived in PublishPad
	int      trigL   = 0;            // 0..255
	int      trigR   = 0;

	// Raw axis values as reported, plus the range needed to scale them, BY ABS
	// CODE (one code can feed several controls, e.g. lefttrigger:+a2 and
	// righttrigger:-a2). The range is read per device with EVIOCGABS and NEVER
	// assumed: the test pad advertises 0..1023 for the left stick and
	// -2048..2047 for the right, and code with an XInput-shaped -32768..32767
	// assumption reads both as pinned hard over. Only codes the layout uses are
	// read at attach; the rest stay !present.
	struct Axis {
		bool present = false;
		int  raw = 0, min = 0, max = 0, flat = 0;
	};
	Axis abs[PADDB_NUM_ABS];

	// Key state by code, kept raw like the axes; controls are derived from it
	// (and from the layout) in PublishPad.
	bool keyDown[PADDB_NUM_KEYS] = {};

	int comboHoldFrames[JOY_MAX_COMBOS] = {0};
};

PadSlot s_pads[MAX_JOYSTICKS];
int s_numPads = 0;

std::vector<EvdevDevice> s_devices;

// Nodes already probed and decided about, including rejects - see ScanPads.
std::vector<std::string> s_examined;

// Known pad nodes (in s_devices) whose replugged device was refused - not a
// gamepad, or filtered by joystick_ignore / Steam's hiding vars. Kept closed
// until the node disappears, so the refusal is logged once.
std::vector<std::string> s_replugRejected;

// aae.ini [input] joystick_ignore, set by the entry point before install_joystick
// (the backend cannot read the ini itself - see joystick.h).
std::string s_ignoreList;

// Steam's SDL_GAMECONTROLLER_IGNORE_DEVICES[_EXCEPT] (see joystick.h): the
// physical pads Steam replaced with virtual ones. Empty = unset.
std::string s_steamIgnore;
std::string s_steamExcept;

// Extra SDL mapping lines (joystick_set_mapping_sources), searched before the
// built-in list. The pointer vectors are what pad_db_find walks.
std::vector<std::string> s_envLines, s_userLines;
std::vector<const char*> s_envPtrs,  s_userPtrs;

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
	if (!a.present) return 0;
	// The arithmetic (centre offset, dead zone, half-range reduced by the dead
	// zone so a fully deflected stick still reaches the extreme) lives in
	// pad_db_axis_scale so the Windows DirectInput backend scales identically.
	return pad_db_axis_scale(a.raw, a.min, a.max, a.flat);
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
	j.is_gamepad = 0;
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
	// Same meaning as on Windows (joystick.h): 1 = fills the canonical XInput
	// layout and is eligible for system chords. Gated on BTN_GAMEPAD rather
	// than hardcoded because Classify() also attaches BTN_JOYSTICK sticks.
	j.is_gamepad = s_pads[index].isGamepad ? 1 : 0;
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
// Layout -> values. Everything below reads the PadLayout, never an evdev code
// by name.
//------------------------------------------------------------------------------

// Only called for codes the layout uses, which came from the device's own
// capability bits, so EVIOCGABS success is the presence test (no extra ioctl).
void ReadAxisRange(const EvdevDevice& dev, int code, PadSlot::Axis& out)
{
	input_absinfo info {};
	if (code < 0 || code >= PADDB_NUM_ABS || !dev.GetAbsInfo(code, &info)) {
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

// Which abs codes a layout reads (a hat reads its X axis and the one after it).
void UsedAbsCodes(const PadLayout& l, bool used[PADDB_NUM_ABS])
{
	for (int i = 0; i < PADDB_NUM_ABS; i++) used[i] = false;
	auto mark = [&](const PadSrc& s) {
		if (s.kind == PADSRC_AXIS && s.code < PADDB_NUM_ABS) used[s.code] = true;
		if (s.kind == PADSRC_HAT && s.code + 1 < PADDB_NUM_ABS) used[s.code] = used[s.code + 1] = true;
	};
	for (const PadSrc& s : l.btn)   mark(s);
	for (const PadSrc& s : l.stick) mark(s);
}

// Where an axis stands as 0..255 for a trigger or an axis-as-button source.
// A full-range source measures min..max; a half-range one measures from the
// centre of the range to the + (or -) end, so "+a2" is zero until the axis
// leaves its centre. `invert` flips the result.
int AxisNorm255(const PadSlot::Axis& a, int half, bool invert)
{
	if (!a.present) return 0;
	return pad_db_axis_norm255(a.raw, a.min, a.max, half, invert);
}

// A left/right stick axis: -128..127. A half-range source (unusual for a
// stick) is reported as its 0..127 magnitude.
int StickValue(const PadSlot& p, const PadSrc& s)
{
	if (s.kind != PADSRC_AXIS || s.code >= PADDB_NUM_ABS) return 0;
	const PadSlot::Axis& a = p.abs[s.code];
	if (s.axisHalf != 0)
		return (AxisNorm255(a, s.axisHalf, s.invert) * 127) / 255;
	const int v = ScaleAxis(a);
	return s.invert ? ClampInt(-v, -128, 127) : v;
}

// A hat's current direction against the SDL mask bits. A digital hat reports
// -1/0/+1, but a pad the automapper guessed digital may use another range
// (e.g. 0..2), so "negative"/"positive" is judged against the range midpoint.
bool HatPressed(const PadSlot& p, const PadSrc& s)
{
	if (s.code + 1 >= PADDB_NUM_ABS || !s.hatMask) return false;
	auto dir = [](const PadSlot::Axis& a) {
		if (!a.present) return 0;
		const int mid = (a.min + a.max) / 2;
		return a.raw < mid ? -1 : (a.raw > mid ? 1 : 0);
	};
	const int x = dir(p.abs[s.code]), y = dir(p.abs[s.code + 1]);
	const int bits = (y < 0 ? PADDB_HAT_UP : 0) | (x > 0 ? PADDB_HAT_RIGHT : 0) |
	                 (y > 0 ? PADDB_HAT_DOWN : 0) | (x < 0 ? PADDB_HAT_LEFT : 0);
	return (bits & s.hatMask) == s.hatMask;
}

// Is a digital control (button, d-pad direction, trigger-as-button) down?
// An axis counts as pressed past half of its (half) range.
bool SrcPressed(const PadSlot& p, const PadSrc& s)
{
	switch (s.kind) {
	case PADSRC_KEY:  return s.code < PADDB_NUM_KEYS && p.keyDown[s.code];
	case PADSRC_HAT:  return HatPressed(p, s);
	case PADSRC_AXIS: return s.code < PADDB_NUM_ABS &&
	                         AxisNorm255(p.abs[s.code], s.axisHalf, s.invert) > 127;
	default:          return false;
	}
}

// A trigger, 0..255: analogue from an axis, 255 when a key/hat source is down.
int TriggerValue(const PadSlot& p, const PadSrc& s)
{
	if (s.kind == PADSRC_AXIS && s.code < PADDB_NUM_ABS)
		return AxisNorm255(p.abs[s.code], s.axisHalf, s.invert);
	return SrcPressed(p, s) ? 255 : 0;
}

// "LX=abs0 LY=abs1 RX=abs2 RY=abs5 LT=abs10 RT=abs9 dpad=hat0 A=key0x130 ..."
// (the formatter is shared with the Windows backend: pad_db_describe_layout)
std::string DescribeLayout(const PadLayout& l)
{
	char buf[512];
	pad_db_describe_layout(l, buf, sizeof(buf), PADDB_DESCRIBE_LINUX);
	return buf;
}

// Resolve one device: caps, then SDL_GAMECONTROLLERCONFIG, the user's
// gamecontrollerdb.txt, the built-in SDL list, the automapper, raw order - in
// that priority (see pad_db_resolve). Once per device, never per frame.
void ResolveLayout(const EvdevDevice& dev, PadDbCaps* caps, PadLayout* layout, bool* gamepad)
{
	dev.FillPadDbCaps(caps);

	PadDbList lists[3];
	int n = 0;
	if (!s_envPtrs.empty()) {
		lists[n].lines = s_envPtrs.data();
		lists[n].count = s_envPtrs.size();
		lists[n].label = "SDL_GAMECONTROLLERCONFIG";
		n++;
	}
	if (!s_userPtrs.empty()) {
		lists[n].lines = s_userPtrs.data();
		lists[n].count = s_userPtrs.size();
		lists[n].label = "user gamecontrollerdb.txt";
		n++;
	}
	lists[n].lines = kSdlLinuxGamepadDb;
	lists[n].count = sizeof(kSdlLinuxGamepadDb) / sizeof(kSdlLinuxGamepadDb[0]);
	lists[n].label = "SDL db";
	n++;

	pad_db_resolve(*caps, lists, n, layout, gamepad);
}

int RankOfLayout(const PadLayout& l) { return pad_rank(pad_db_layout_caps(l)); }

// "/dev/input/event12" -> 12. Numeric, because the nodes arrive sorted as
// strings (event10 before event2). Unparseable -> 0.
int EventNumber(const std::string& devNode)
{
	size_t i = devNode.size();
	while (i > 0 && devNode[i - 1] >= '0' && devNode[i - 1] <= '9') --i;
	return (i < devNode.size()) ? atoi(devNode.c_str() + i) : 0;
}

// True when the kernel device behind an event node was created in software
// (uinput) rather than by a hardware driver: its sysfs path resolves under
// /sys/devices/virtual/. Steam Input's pads are made this way, and the legacy
// one reports 045e:028e - the same ids as a real wired Xbox 360 pad. Steam's
// SDL_GAMECONTROLLER_IGNORE_DEVICES names PHYSICAL pads only, so it must never
// be applied to a virtual node, or hiding a real 360 pad would also hide the
// virtual pad that replaces it and leave no controller at all. Uses sysfs, not
// /dev/input/by-id: Bluetooth pads are physical but have no by-id entry.
// If sysfs cannot be read (unusual sandbox), the node is treated as physical,
// which is the pre-existing behaviour.
bool IsVirtualInputNode(const std::string& devNode)
{
	const size_t slash = devNode.find_last_of('/');
	const std::string base = (slash == std::string::npos) ? devNode : devNode.substr(slash + 1);
	const std::string sysPath = "/sys/class/input/" + base;
	char resolved[PATH_MAX] = {0};
	if (!realpath(sysPath.c_str(), resolved)) return false;
	return strstr(resolved, "/devices/virtual/") != nullptr;
}

//------------------------------------------------------------------------------
// Whether `dev`, opened from `devNode`, is a Steam virtual pad standing in for
// a physical one Steam is not hiding (pad_demote_steam_virtual). Decided per
// attach, not per slot: see AttachPad.
bool IsDemotedPad(const std::string& devNode, const EvdevDevice& dev)
{
	return pad_demote_steam_virtual(IsVirtualInputNode(devNode), dev.vendorId(),
	                                dev.productId(), s_steamIgnore.c_str(),
	                                s_steamExcept.c_str());
}

// The filters every gamepad passes before it gets a slot, on first attach AND
// on replug (see ScanPads). True = do not attach; the reason is logged.
bool PadFilteredOut(const std::string& devNode, const EvdevDevice& dev)
{
	if (pad_ignore_match(s_ignoreList.c_str(), dev.vendorId(), dev.productId(),
	                     dev.name().c_str())) {
		LOG_INFO("evdev: ignoring '%s' [%04x:%04x] on %s (aae.ini [input] joystick_ignore)",
		         dev.name().c_str(), dev.vendorId(), dev.productId(), devNode.c_str());
		return true;
	}

	// Steam virtualizes some physical pads (Deck controls 28de:1205, Xbox
	// pads via xpad, ...) and tells games to hide the originals through
	// SDL's env vars. We read evdev, not SDL, so honour them here or the
	// same person would show up as two players. Physical nodes only - see
	// IsVirtualInputNode.
	if (!IsVirtualInputNode(devNode) &&
	    pad_env_ignore_match(s_steamIgnore.c_str(), s_steamExcept.c_str(),
	                         dev.vendorId(), dev.productId())) {
		LOG_INFO("evdev: skipping '%s' [%04x:%04x] on %s (hidden by Steam via "
		         "SDL_GAMECONTROLLER_IGNORE_DEVICES[_EXCEPT])",
		         dev.name().c_str(), dev.vendorId(), dev.productId(), devNode.c_str());
		return true;
	}
	return false;
}

void AttachPad(int devIndex, const EvdevDevice& dev, const PadDbCaps& caps,
               const PadLayout& layout, bool demoted)
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

		// Append - unless this is a physical gamepad arriving after a demoted
		// Steam virtual pad already took a slot. Then it goes in ahead of the
		// demoted pads, which move down one: otherwise a second pad plugged in
		// on SteamOS Desktop became Player 3, behind the first pad's virtual
		// twin. Physical pads never move.
		bool slotDemoted[MAX_JOYSTICKS];
		for (int i = 0; i < s_numPads; i++) slotDemoted[i] = s_pads[i].demoted;
		slot = pad_hotplug_slot(slotDemoted, s_numPads, RankOfLayout(layout), demoted);
		for (int i = s_numPads; i > slot; i--) {
			s_pads[i] = std::move(s_pads[i - 1]);
			LOG_INFO("evdev: gamepad %d -> %d: %s (Steam virtual pad, moved behind "
			         "the newly connected '%s')",
			         i - 1, i, s_pads[i].name.c_str(), dev.name().c_str());
		}
		// joy[] follows on the next poll (PublishPad rewrites every connected
		// slot); clear the moved range now so no stale state shows meanwhile.
		for (int i = slot; i <= s_numPads; i++) ResetJoyEntry(i);
		s_numPads++;

		s_pads[slot] = PadSlot();
		s_pads[slot].identity = dev.identity();
		s_pads[slot].name     = dev.name();
		s_pads[slot].demoted  = demoted;
	}

	PadSlot& p = s_pads[slot];
	p.devIndex  = devIndex;
	p.connected = true;
	p.name      = dev.name();   // refresh on slot reuse: a weak-identity slot
	                            // can be re-attached by a different device
	// Same reason. A pad without a /dev/input/by-id entry is identified by its
	// event number, which the kernel reuses, so a real pad can reattach to a
	// slot a Steam virtual pad held (or the reverse). A flag left from the old
	// occupant made pad_hotplug_slot push a real pad down behind a later one.
	p.demoted   = demoted;
	// The layout already carries the per-driver quirks (Sony's X/Y swap, a right
	// stick on Z/RZ, triggers on BRAKE/GAS): see pad_db_automap and the SDL
	// database. isGamepad follows it - a raw joystick is not one.
	p.layout    = layout;
	p.isGamepad = layout.gamepad;
	p.buttons   = 0;
	p.trigL = p.trigR = 0;
	for (bool& k : p.keyDown) k = false;
	for (PadSlot::Axis& a : p.abs) a = PadSlot::Axis();

	bool used[PADDB_NUM_ABS];
	UsedAbsCodes(layout, used);
	for (int a = 0; a < PADDB_NUM_ABS; a++)
		if (used[a]) ReadAxisRange(dev, a, p.abs[a]);

	uint8_t guid[16];
	char guidStr[33];
	pad_db_make_guid(caps.bus, caps.vendor, caps.product, caps.version, caps.name, guid);
	pad_db_guid_to_string(guid, guidStr);

	LOG_INFO("evdev: gamepad %d connected: %s [%04x:%04x] guid=%s (rank %d, rumble %s) "
	         "map=[%s] %s",
	         slot, p.name.c_str(), dev.vendorId(), dev.productId(), guidStr,
	         RankOfLayout(layout), dev.SupportsRumble() ? "yes" : "no",
	         layout.source, DescribeLayout(layout).c_str());

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

	// Newly found gamepad-class devices this scan, attached together at the end.
	struct Candidate {
		EvdevDevice dev;
		int rank  = 3;
		int evnum = 0;
		bool demoted = false;   // Steam virtual pad while Steam is not hiding
		                        // the physical one - see pad_demote_steam_virtual
		PadDbCaps caps;         // resolved once here; AttachPad reuses them
		PadLayout layout;
	};
	std::vector<Candidate> found;

	// Forget nodes that have gone, so a replugged pad is probed again.
	auto forgetGone = [&nodes](std::vector<std::string>& list) {
		list.erase(
			std::remove_if(list.begin(), list.end(),
				[&nodes](const std::string& p) {
					return std::none_of(nodes.begin(), nodes.end(),
						[&p](const EvdevNode& n) { return n.devNode == p; });
				}),
			list.end());
	};
	forgetGone(s_examined);
	forgetGone(s_replugRejected);

	for (const EvdevNode& node : nodes) {
		int existing = -1;
		for (size_t i = 0; i < s_devices.size(); i++)
			if (s_devices[i].devNode() == node.devNode) { existing = (int)i; break; }

		if (existing >= 0) {
			if (s_devices[existing].IsOpen()) continue;
			if (std::find(s_replugRejected.begin(), s_replugRejected.end(), node.devNode)
			    != s_replugRejected.end())
				continue;
			// Replugged: re-open in place so the pad keeps its joy[] slot.
			//
			// The node is known, but the DEVICE on it may not be: the kernel
			// hands the lowest free event number to whatever is plugged in next,
			// so a keyboard's pad-class node or an ignored stick can arrive on
			// the node a pad just left. It gets the same checks as a first
			// attach. A refusal closes the node and is remembered until the node
			// disappears, so it is logged once, not every rescan.
			EvdevDevice& d = s_devices[existing];
			if (!d.Open(node.devNode, node.identity)) continue;   // retried next scan
			if (d.kind() != EvdevKind::Gamepad || PadFilteredOut(node.devNode, d)) {
				d.Close();
				s_replugRejected.push_back(node.devNode);
				continue;
			}
			PadDbCaps caps;
			PadLayout layout;
			bool gamepad = false;
			ResolveLayout(d, &caps, &layout, &gamepad);
			AttachPad(existing, d, caps, layout, IsDemotedPad(node.devNode, d));
			continue;
		}

		// The same trap evdev_input.cpp's ScanDevices documents: every
		// keyboard, mouse and system button on the machine is NOT a gamepad,
		// and re-probing all of them every two seconds - opening, five
		// EVIOCGBIT ioctls, a log line, closing - happens on the thread that
		// is trying to render.
		if (std::find(s_examined.begin(), s_examined.end(), node.devNode) != s_examined.end())
			continue;

		EvdevDevice dev;
		if (!dev.Open(node.devNode, node.identity)) {
			// Not readable YET is not "examined": a pad plugged in a moment ago
			// may still be waiting for udev to grant access (EvdevNodeDenied),
			// and remembering it here lost the pad until it was replugged. A
			// failed open is one cheap syscall, so retrying each scan is fine.
			if (!EvdevNodeDenied(node.devNode)) s_examined.push_back(node.devNode);
			continue;
		}
		s_examined.push_back(node.devNode);
		if (dev.kind() != EvdevKind::Gamepad) continue;

		// Once per device: s_examined above keeps this node from being probed
		// again until it vanishes and comes back.
		if (PadFilteredOut(node.devNode, dev)) continue;
		const bool isVirtual = IsVirtualInputNode(node.devNode);

		// Do not attach yet: slots are handed out in rank order below, not in
		// the order this loop happens to meet the nodes.
		Candidate c;
		bool gamepadLayout = false;
		ResolveLayout(dev, &c.caps, &c.layout, &gamepadLayout);
		c.rank  = RankOfLayout(c.layout);
		c.evnum = EventNumber(node.devNode);
		c.demoted = pad_demote_steam_virtual(isVirtual, dev.vendorId(), dev.productId(),
		                                     s_steamIgnore.c_str(), s_steamExcept.c_str());
		if (c.demoted)
			LOG_INFO("evdev: '%s' [%04x:%04x] is a Steam virtual pad and Steam is not "
			         "hiding the physical pads: ordered after them",
			         dev.name().c_str(), dev.vendorId(), dev.productId());
		c.dev   = std::move(dev);
		found.push_back(std::move(c));
	}

	// Event numbers are assigned in probe order, and a gaming keyboard's or
	// mouse's extra pad-class nodes probe before a pad plugged in later - so
	// event order alone put the real pad at JOY 4. Full pads first, then the
	// event number as the tie-break (which is also the old behaviour for a
	// machine with only identical pads). std::stable_sort keeps equal keys in
	// enumeration order. Only NEW devices are ranked: a pad hotplugged later
	// appends and physical pads' slots never move - except that a physical
	// gamepad goes in ahead of demoted Steam virtual pads (see AttachPad).
	std::stable_sort(found.begin(), found.end(),
		[](const Candidate& a, const Candidate& b) {
			return pad_slot_before(a.rank, a.demoted, a.evnum,
			                       b.rank, b.demoted, b.evnum);
		});

	for (Candidate& c : found) {
		s_devices.push_back(std::move(c.dev));
		const int idx = (int)s_devices.size() - 1;
		AttachPad(idx, s_devices[idx], c.caps, c.layout, c.demoted);
	}
}

//------------------------------------------------------------------------------
void HandlePadEvent(PadSlot& p, const input_event& ev)
{
	// Raw state only: which control a key or axis feeds is the layout's business,
	// and one abs code can feed several controls (lefttrigger:+a2, righttrigger:-a2),
	// so controls are derived from these values in PublishPad rather than decided
	// per event code here.
	if (ev.type == EV_KEY) {
		if (ev.value == 2) return;                 // auto-repeat, not a change
		if (ev.code < PADDB_NUM_KEYS) p.keyDown[ev.code] = (ev.value != 0);
		return;
	}

	if (ev.type == EV_ABS && ev.code < PADDB_NUM_ABS)
		p.abs[ev.code].raw = ev.value;
}

//------------------------------------------------------------------------------
// Publish one pad into joy[].
//------------------------------------------------------------------------------
void PublishPad(int slot)
{
	PadSlot& p = s_pads[slot];
	JOYSTICK_INFO& j = joy[slot];

	SetupDescriptor(slot);

	// Digital controls from the layout. The d-pad slots are ordinary buttons
	// here whichever source feeds them (keys, a hat, half axes). LT/RT are not
	// AAE_JOYBTN bits; they go through the trigger threshold below.
	static const uint16_t kBtnBits[14] = {
		AAE_JOYBTN_A, AAE_JOYBTN_B, AAE_JOYBTN_X, AAE_JOYBTN_Y,
		AAE_JOYBTN_LEFT_SHOULDER, AAE_JOYBTN_RIGHT_SHOULDER,
		AAE_JOYBTN_BACK, AAE_JOYBTN_START,
		AAE_JOYBTN_LEFT_THUMB, AAE_JOYBTN_RIGHT_THUMB,
		AAE_JOYBTN_DPAD_UP, AAE_JOYBTN_DPAD_DOWN,
		AAE_JOYBTN_DPAD_LEFT, AAE_JOYBTN_DPAD_RIGHT
	};
	uint16_t held = 0;
	for (int i = 0; i < 14; i++)
		if (SrcPressed(p, p.layout.btn[i])) held |= kBtnBits[i];
	p.buttons = held;
	p.trigL = TriggerValue(p, p.layout.btn[PAD_BTN_LT]);
	p.trigR = TriggerValue(p, p.layout.btn[PAD_BTN_RT]);

	SetAxis(j.stick[0].axis[0], StickValue(p, p.layout.stick[PADDB_STICK_LX]));
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
	SetAxis(j.stick[0].axis[1], StickValue(p, p.layout.stick[PADDB_STICK_LY]));
	SetAxis(j.stick[1].axis[0], StickValue(p, p.layout.stick[PADDB_STICK_RX]));
	SetAxis(j.stick[1].axis[1], StickValue(p, p.layout.stick[PADDB_STICK_RY]));

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

// num_joysticks is an upper BOUND on slot numbers, not a head count: os_input.cpp
// rejects any slot >= num_joysticks. Slots are stable and can have gaps, so a
// count of connected pads cut off the highest slot whenever a lower one was
// unplugged. Same value Joystick.cpp's XInput path returns (highest + 1).
int HighestConnectedSlot()
{
	int highest = -1;
	for (int i = 0; i < s_numPads; i++)
		if (s_pads[i].connected) highest = i;
	return highest;
}

} // namespace

//==============================================================================
// Public API
//==============================================================================
void joystick_set_ignore_list(const char* list)
{
	s_ignoreList = list ? list : "";
}

void joystick_set_steam_hide_env(const char* ignoreDevices, const char* ignoreDevicesExcept)
{
	s_steamIgnore = ignoreDevices ? ignoreDevices : "";
	s_steamExcept = ignoreDevicesExcept ? ignoreDevicesExcept : "";
	if (!s_steamIgnore.empty() || !s_steamExcept.empty())
		LOG_INFO("evdev: Steam device hiding active: SDL_GAMECONTROLLER_IGNORE_DEVICES='%s' "
		         "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='%s'",
		         s_steamIgnore.c_str(), s_steamExcept.c_str());
}

void joystick_set_mapping_sources(const char* envConfig, const char* userDbText)
{
	s_envLines.clear();
	s_userLines.clear();
	pad_db_load_text(envConfig, &s_envLines);
	pad_db_load_text(userDbText, &s_userLines);

	// The pointer vectors are rebuilt only after both line vectors have settled:
	// a std::string's buffer moves when its vector grows.
	s_envPtrs.clear();
	s_userPtrs.clear();
	for (const std::string& l : s_envLines)  s_envPtrs.push_back(l.c_str());
	for (const std::string& l : s_userLines) s_userPtrs.push_back(l.c_str());

	if (!s_envLines.empty() || !s_userLines.empty())
		LOG_INFO("evdev: gamepad mappings loaded: %d from SDL_GAMECONTROLLERCONFIG, %d from "
		         "gamecontrollerdb.txt", (int)s_envLines.size(), (int)s_userLines.size());
}

int install_joystick()
{
	if (_joystick_installed) return 0;

	for (int i = 0; i < MAX_JOYSTICKS; i++) ResetJoyEntry(i);
	s_numPads = 0;
	num_joysticks = 0;

	ScanPads();

	int connected = 0;
	for (int i = 0; i < s_numPads; i++) if (s_pads[i].connected) connected++;
	num_joysticks = HighestConnectedSlot() + 1;

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

	num_joysticks = HighestConnectedSlot() + 1;

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
// Combos -- same contract as Joystick.cpp: system chords are global, scanned
// across every connected pad; the player parameter is legacy and ignored.
// Non-gamepad devices (isGamepad == false) are never scanned, matching the
// Windows rule that raw sticks cannot fire menu/exit/pause; their counters
// are zeroed each scan, which also covers a pad that disconnects mid-hold.
//------------------------------------------------------------------------------
bool joystick_check_combo(int /*player*/, uint16_t buttonMask)
{
	const int idx = GetComboIndex(buttonMask);

	bool triggered = false;
	for (int p = 0; p < s_numPads; ++p) {
		const bool held = s_pads[p].connected && s_pads[p].isGamepad &&
			(s_pads[p].buttons & buttonMask) == buttonMask;
		if (pad_map_combo_step(held, &s_pads[p].comboHoldFrames[idx]))
			triggered = true;
	}
	return triggered;
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
