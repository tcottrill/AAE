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
	bool  weakIdentity() const { return m_weakIdentity; }
	bool  seenInput()    const { return m_seenInput; }
	void  MarkSeen()           { m_seenInput = true; }

	// Reads every pending event. Returns false if the device went away
	// (ENODEV on unplug), which the caller uses to drop it. A read that would
	// block returns true with an empty batch.
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

	int         m_fd = -1;
	EvdevKind   m_kind = EvdevKind::Unknown;
	std::string m_name;
	std::string m_devNode;
	std::string m_identity;
	bool        m_weakIdentity = false;
	bool        m_seenInput    = false;
	bool        m_writable     = false;
	bool        m_hasRumble    = false;
	bool        m_hasLeds      = false;
	int         m_ffEffectId   = -1;   // -1 = nothing uploaded yet
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
