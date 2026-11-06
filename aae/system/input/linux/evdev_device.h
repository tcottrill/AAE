//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// evdev_device.h -- one /dev/input/event* node.
//
// Separate from evdev_input.cpp on purpose: that file implements the neutral
// sys_input.h/joystick.h surface for four device classes at once, and letting
// device enumeration, capability probing and force-feedback tangle into it
// would produce a single file nobody can follow.
//
// PRIVATE to the Linux input backend. It includes <linux/input.h>, which
// defines KEY_A as 30 - harmless here only because AaeKey is an enum rather
// than macros (see the note above AaeKey in sys_input.h), and only as long as
// this header does not spread beyond aae/system/input/linux/.
//==============================================================================
#pragma once

#include <linux/input.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// What a node is FOR. Derived from capability bits, never from the device
// name - see ClassifyOpenDevice for why the name is not usable.
enum class EvdevKind { Unknown, Keyboard, Mouse, Gamepad };

const char* EvdevKindName(EvdevKind kind);

//------------------------------------------------------------------------------
// One open input node.
//------------------------------------------------------------------------------
class EvdevDevice {
public:
	EvdevDevice() = default;
	~EvdevDevice() { Close(); }

	EvdevDevice(const EvdevDevice&)            = delete;
	EvdevDevice& operator=(const EvdevDevice&) = delete;
	EvdevDevice(EvdevDevice&& other) noexcept { *this = std::move(other); }
	EvdevDevice& operator=(EvdevDevice&& other) noexcept;

	// devNode is "/dev/input/eventN". identity is the /dev/input/by-id/ path
	// when one exists, or empty - in which case devNode becomes the identity
	// and the device is flagged as weakly identified.
	bool Open(const std::string& devNode, const std::string& identity);
	void Close();

	bool IsOpen()  const { return m_fd >= 0; }
	int  fd()      const { return m_fd; }
	EvdevKind kind() const { return m_kind; }

	const std::string& name()     const { return m_name; }      // EVIOCGNAME
	const std::string& devNode()  const { return m_devNode; }
	const std::string& identity() const { return m_identity; }  // ini-safe

	// EVIOCGID. Zero when the ioctl failed. Used to recognise device families
	// whose kernel driver reports buttons differently - see pad_map_is_sony
	// and the face-button note above EvdevButtonToAae.
	uint16_t vendorId()  const { return m_vendorId; }
	uint16_t productId() const { return m_productId; }

	bool  weakIdentity() const { return m_weakIdentity; }
	bool  seenInput()    const { return m_seenInput; }
	void  MarkSeen()           { m_seenInput = true; }

	// Reads every pending event. Returns false if the device went away
	// (ENODEV on unplug), which the caller uses to drop it. A read that would
	// block returns true with an empty batch.
	//
	// The batch is already SYN_DROPPED-safe: when the kernel's per-client
	// buffer overflows, the incomplete packet is discarded here and replaced by
	// synthetic EV_KEY / EV_ABS events that bring the caller up to the device's
	// real state, so a key released during the overflow cannot stay held. The
	// callers apply the batch exactly as they would a normal one; they never
	// see EV_SYN/SYN_DROPPED. Relative motion lost in the overflow stays lost.
	bool ReadEvents(std::vector<input_event>& out);

	// EVIOCGABS. False when the axis is not present.
	bool GetAbsInfo(int axisCode, input_absinfo* out) const;
	bool HasAbsAxis(int axisCode) const;
	bool HasKeyCode(int keyCode)  const;

	// Force feedback. Uploads (or re-uploads) a single FF_RUMBLE effect and
	// plays it; magnitudes are 0..1. SupportsRumble() is false when the device
	// has no FF_RUMBLE bit OR when the node could only be opened read-only,
	// because EVIOCSFF needs write access - two different causes with the same
	// symptom, so they are logged apart.
	bool SupportsRumble() const { return m_hasRumble && m_writable; }
	bool SetRumble(float strong, float weak);
	void StopRumble();

	// Keyboard indicator LEDs. AAE drives these as stand-ins for a cabinet's
	// start-button lamps. Same two-causes-one-symptom split as rumble above:
	// false either because the device has no LED bits or because the node
	// could only be opened read-only, and Classify() logs those apart.
	//
	// The mask is AAE's own, NOT the kernel's LED_* numbering:
	// bit0 = NumLock, bit1 = CapsLock, bit2 = ScrollLock.
	bool SupportsLeds() const { return m_hasLeds && m_writable; }
	bool SetLeds(int mask);
	int  GetLeds() const;

private:
	void Classify();

	// ReadEvents' SYN_DROPPED handling. AcceptEvent is the per-event filter
	// (it, not read(), decides what reaches the caller, because a drop can
	// begin and end anywhere in a stream that arrives in 64-event chunks);
	// ResyncAfterDrop appends the synthetic state events once the damaged
	// packet has been swallowed. `tail` is that packet's SYN_REPORT, copied so
	// the synthetic events carry a real timestamp.
	void AcceptEvent(const input_event& ev, std::vector<input_event>& out);
	void ResyncAfterDrop(const input_event& tail, std::vector<input_event>& out);

	// EVIOCGKEY's bitmap is KEY_MAX+1 bits of unsigned long. Mirrors the
	// BitsToLongs helper in the .cpp, which is not visible from here.
	static constexpr size_t kKeyWords =
		((size_t)KEY_MAX + 1 + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long));

	int         m_fd = -1;
	EvdevKind   m_kind = EvdevKind::Unknown;
	std::string m_name;
	std::string m_devNode;
	std::string m_identity;
	uint16_t    m_vendorId  = 0;
	uint16_t    m_productId = 0;
	bool        m_weakIdentity = false;
	bool        m_seenInput    = false;
	bool        m_writable     = false;
	bool        m_hasRumble    = false;
	bool        m_hasLeds      = false;
	int         m_ffEffectId   = -1;   // -1 = nothing uploaded yet

	// Which keys/buttons the caller has been told are down: every EV_KEY that
	// ReadEvents passed through (value 1 or 2 sets the bit, 0 clears it). After
	// a SYN_DROPPED this is what the real EVIOCGKEY state is diffed against.
	std::array<unsigned long, kKeyWords> m_keyDown {};

	// True from SYN_DROPPED until the SYN_REPORT that ends the damaged packet.
	// A member, not a local: the damaged packet can straddle read() calls and
	// even ReadEvents() calls.
	bool        m_dropping     = false;

	// Drops seen on this device. Survives Close()/Open() on purpose - it only
	// bounds the log, and a flapping device must not get a fresh quota each
	// time it reconnects.
	int         m_synDropCount = 0;
};

//------------------------------------------------------------------------------
// Enumeration.
//------------------------------------------------------------------------------
struct EvdevNode {
	std::string devNode;    // /dev/input/eventN
	std::string identity;   // /dev/input/by-id/... , or empty if none exists
};

// Every event node on the system, each paired with its by-id identity when one
// exists. Sorted by devNode so enumeration order is stable within a boot.
std::vector<EvdevNode> EvdevEnumerateNodes();

// Maps every character that is not [0-9A-Za-z] to '_'.
//
// Not cosmetic: iniFile treats ';' and '#' as inline comment starters, and a
// path written raw comes back truncated and never matches again - the exact
// bug the Win32 backend hit with device paths full of '#' ("assigned device
// not attached" after every restart, rawinput.cpp ri_sanitize_path). by-id
// paths are full of '-', ':' and '/', so they need the same treatment, and
// both sides of every comparison are sanitized.
std::string EvdevSanitizeIdentity(const std::string& path);
