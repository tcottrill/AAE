//==============================================================================
// evdev_device.cpp -- one /dev/input/event* node: open, classify, read, rumble.
//==============================================================================
#include "evdev_device.h"
#include "sys_log.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

//------------------------------------------------------------------------------
// Bit-array helpers. EVIOCGBIT fills an array of unsigned long; the kernel
// headers supply no accessor, so every evdev client writes these two.
//------------------------------------------------------------------------------
namespace {

constexpr size_t kBitsPerLong = 8 * sizeof(unsigned long);
constexpr size_t BitsToLongs(size_t n) { return (n + kBitsPerLong - 1) / kBitsPerLong; }

inline bool TestBit(const unsigned long* arr, size_t bit)
{
	return (arr[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1ul;
}

// Count how many of KEY_A..KEY_Z the device reports. A real keyboard has all
// 26; a media-key node or a mouse's macro collection has none or a handful.
int CountLetterKeys(const unsigned long* keyBits)
{
	static const int kLetters[] = {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
		KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
		KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
	};
	int n = 0;
	for (int k : kLetters)
		if (TestBit(keyBits, k)) n++;
	return n;
}

} // namespace

const char* EvdevKindName(EvdevKind kind)
{
	switch (kind) {
	case EvdevKind::Keyboard: return "keyboard";
	case EvdevKind::Mouse:    return "mouse";
	case EvdevKind::Gamepad:  return "gamepad";
	default:                  return "unknown";
	}
}

//------------------------------------------------------------------------------
EvdevDevice& EvdevDevice::operator=(EvdevDevice&& other) noexcept
{
	if (this != &other) {
		Close();
		m_fd           = other.m_fd;
		m_kind         = other.m_kind;
		m_name         = std::move(other.m_name);
		m_devNode      = std::move(other.m_devNode);
		m_identity     = std::move(other.m_identity);
		m_weakIdentity = other.m_weakIdentity;
		m_seenInput    = other.m_seenInput;
		m_writable     = other.m_writable;
		m_hasRumble    = other.m_hasRumble;
		m_ffEffectId   = other.m_ffEffectId;
		other.m_fd     = -1;
		other.m_ffEffectId = -1;
	}
	return *this;
}

//------------------------------------------------------------------------------
bool EvdevDevice::Open(const std::string& devNode, const std::string& identity)
{
	Close();

	// O_RDWR first: force feedback needs write access. Read-only is a valid
	// fallback for everything except rumble, so a device that only opens
	// read-only is still usable - it just cannot vibrate, and SupportsRumble
	// reports that rather than failing silently at the first EVIOCSFF.
	m_fd = open(devNode.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
	m_writable = (m_fd >= 0);
	if (m_fd < 0)
		m_fd = open(devNode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (m_fd < 0) {
		// A backend that finds nothing because of permissions looks exactly
		// like a backend on a machine with no hardware. Say which it is, and
		// say the fix - this is the single most common first-run failure on a
		// desktop distro, and it is not discoverable from "no input works".
		if (errno == EACCES || errno == EPERM) {
			// Once per node, not once per rescan. ScanDevices retries every
			// ~2 seconds for hotplug, and an unguarded message here wrote 43
			// identical lines per device in a 45-second run - burying the
			// summary that actually tells you what to do.
			static std::vector<std::string> s_complained;
			if (std::find(s_complained.begin(), s_complained.end(), devNode)
			    == s_complained.end()) {
				s_complained.push_back(devNode);
				LOG_ERROR("evdev: permission denied opening %s - add your user to "
				          "the 'input' group (sudo usermod -aG input $USER), then "
				          "log out and back in", devNode.c_str());
			}
		} else if (errno != ENOENT) {
			LOG_WARN("evdev: open(%s) failed: %s", devNode.c_str(), strerror(errno));
		}
		return false;
	}

	m_devNode = devNode;

	char nameBuf[256] = {0};
	if (ioctl(m_fd, EVIOCGNAME(sizeof(nameBuf) - 1), nameBuf) < 0 || !nameBuf[0])
		snprintf(nameBuf, sizeof(nameBuf), "Unnamed device (%s)", devNode.c_str());
	m_name = nameBuf;

	if (!identity.empty()) {
		m_identity     = EvdevSanitizeIdentity(identity);
		m_weakIdentity = false;
	} else {
		// No by-id entry. The event number is assigned in probe order and can
		// change across reboots, so an assignment persisted against it may not
		// come back to the same device. Worth saying out loud - a player whose
		// controls move after a reboot needs to know why.
		m_identity     = EvdevSanitizeIdentity(devNode);
		m_weakIdentity = true;
	}

	Classify();
	return true;
}

//------------------------------------------------------------------------------
void EvdevDevice::Close()
{
	if (m_fd >= 0) {
		StopRumble();
		close(m_fd);
		m_fd = -1;
	}
	m_kind      = EvdevKind::Unknown;
	m_writable  = false;
	m_hasRumble = false;
	m_ffEffectId = -1;
}

//------------------------------------------------------------------------------
// Classify by CAPABILITY BITS, never by name.
//
// Names are not usable for this and the failure is not theoretical:
//   * A single USB keyboard commonly exposes two or three event nodes with
//     identical names - one with the actual keys, one with only volume and
//     brightness keys. Picking by name gets whichever enumerated first.
//   * Gaming mice expose a keyboard node for their macro keys, named after
//     the mouse. Name-matching "mouse" registers it as a mouse; it has no
//     REL_X and so never moves anything.
//   * BTN_LEFT (0x110) lives in the KEY_* number space, so "reports EV_KEY"
//     does not mean "keyboard" either.
//
// Order matters. Gamepad is tested first because pads report EV_KEY bits in
// the BTN_ range; mouse before keyboard because a mouse with macro keys should
// still be a mouse if it has REL_X/REL_Y itself.
//------------------------------------------------------------------------------
void EvdevDevice::Classify()
{
	unsigned long evBits [BitsToLongs(EV_MAX  + 1)] = {0};
	unsigned long keyBits[BitsToLongs(KEY_MAX + 1)] = {0};
	unsigned long relBits[BitsToLongs(REL_MAX + 1)] = {0};
	unsigned long absBits[BitsToLongs(ABS_MAX + 1)] = {0};
	unsigned long ffBits [BitsToLongs(FF_MAX  + 1)] = {0};

	ioctl(m_fd, EVIOCGBIT(0,      sizeof(evBits)),  evBits);
	const bool hasEvKey = TestBit(evBits, EV_KEY);
	const bool hasEvRel = TestBit(evBits, EV_REL);
	const bool hasEvAbs = TestBit(evBits, EV_ABS);
	const bool hasEvFF  = TestBit(evBits, EV_FF);

	if (hasEvKey) ioctl(m_fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);
	if (hasEvRel) ioctl(m_fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits);
	if (hasEvAbs) ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
	if (hasEvFF)  ioctl(m_fd, EVIOCGBIT(EV_FF,  sizeof(ffBits)),  ffBits);

	m_hasRumble = hasEvFF && TestBit(ffBits, FF_RUMBLE);

	const bool padButtons = hasEvKey &&
		(TestBit(keyBits, BTN_GAMEPAD) ||   // == BTN_SOUTH / BTN_A
		 TestBit(keyBits, BTN_JOYSTICK));   // == BTN_TRIGGER
	const bool padAxes = hasEvAbs &&
		TestBit(absBits, ABS_X) && TestBit(absBits, ABS_Y);

	const bool mouseAxes    = hasEvRel &&
		TestBit(relBits, REL_X) && TestBit(relBits, REL_Y);
	const bool mouseButtons = hasEvKey && TestBit(keyBits, BTN_LEFT);

	const int letters = hasEvKey ? CountLetterKeys(keyBits) : 0;

	if (padButtons || (padAxes && hasEvKey && !mouseAxes))
		m_kind = EvdevKind::Gamepad;
	else if (mouseAxes && mouseButtons)
		m_kind = EvdevKind::Mouse;
	else if (letters >= 20)              // a real keyboard has all 26
		m_kind = EvdevKind::Keyboard;
	else
		m_kind = EvdevKind::Unknown;

	LOG_INFO("evdev: %s '%s' -> %s (letters=%d rel=%d abs=%d ff_rumble=%d "
	         "writable=%d identity=%s%s)",
	         m_devNode.c_str(), m_name.c_str(), EvdevKindName(m_kind),
	         letters, mouseAxes ? 1 : 0, padAxes ? 1 : 0, m_hasRumble ? 1 : 0,
	         m_writable ? 1 : 0, m_identity.c_str(),
	         m_weakIdentity ? " WEAK" : "");

	if (m_weakIdentity && m_kind != EvdevKind::Unknown) {
		LOG_WARN("evdev: '%s' has no /dev/input/by-id/ entry, so its identity is "
		         "its event number - a saved player assignment may not survive a "
		         "reboot", m_name.c_str());
	}
	if (m_hasRumble && !m_writable) {
		LOG_WARN("evdev: '%s' supports rumble but opened read-only, so it cannot "
		         "be driven - check write permission on %s",
		         m_name.c_str(), m_devNode.c_str());
	}
}

//------------------------------------------------------------------------------
bool EvdevDevice::ReadEvents(std::vector<input_event>& out)
{
	out.clear();
	if (m_fd < 0) return false;

	input_event batch[64];
	for (;;) {
		const ssize_t n = read(m_fd, batch, sizeof(batch));
		if (n > 0) {
			const size_t count = (size_t)n / sizeof(input_event);
			out.insert(out.end(), batch, batch + count);
			if ((size_t)n < sizeof(batch)) break;   // drained
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;                                   // nothing pending
		if (n < 0 && errno == EINTR)
			continue;
		// ENODEV is what an unplug looks like. Anything else here is also
		// terminal for this fd, so the caller drops it either way.
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
bool EvdevDevice::GetAbsInfo(int axisCode, input_absinfo* out) const
{
	if (m_fd < 0 || !out) return false;
	return ioctl(m_fd, EVIOCGABS(axisCode), out) == 0;
}

bool EvdevDevice::HasAbsAxis(int axisCode) const
{
	if (m_fd < 0 || axisCode < 0 || axisCode > ABS_MAX) return false;
	unsigned long absBits[BitsToLongs(ABS_MAX + 1)] = {0};
	if (ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) return false;
	return TestBit(absBits, (size_t)axisCode);
}

bool EvdevDevice::HasKeyCode(int keyCode) const
{
	if (m_fd < 0 || keyCode < 0 || keyCode > KEY_MAX) return false;
	unsigned long keyBits[BitsToLongs(KEY_MAX + 1)] = {0};
	if (ioctl(m_fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) return false;
	return TestBit(keyBits, (size_t)keyCode);
}

//------------------------------------------------------------------------------
// Rumble.
//
// The evdev model is upload-then-play, not set-magnitude: an ff_effect is
// uploaded with EVIOCSFF and started by writing an EV_FF event naming its id.
// Re-uploading with the SAME id (rather than allocating a new one) replaces
// the effect in place; allocating a fresh id per call exhausts the device's
// effect slots - ff_effects_max is often 4 - after a few seconds of play.
//------------------------------------------------------------------------------
bool EvdevDevice::SetRumble(float strong, float weak)
{
	if (!SupportsRumble()) return false;

	strong = std::max(0.0f, std::min(1.0f, strong));
	weak   = std::max(0.0f, std::min(1.0f, weak));

	ff_effect effect {};
	effect.type = FF_RUMBLE;
	effect.id   = (int16_t)m_ffEffectId;    // -1 asks the driver to allocate
	effect.u.rumble.strong_magnitude = (uint16_t)(strong * 65535.0f);
	effect.u.rumble.weak_magnitude   = (uint16_t)(weak   * 65535.0f);
	effect.replay.length = 0;               // 0 = until explicitly stopped
	effect.replay.delay  = 0;

	if (ioctl(m_fd, EVIOCSFF, &effect) < 0) {
		LOG_WARN("evdev: EVIOCSFF on '%s' failed: %s", m_name.c_str(), strerror(errno));
		return false;
	}
	m_ffEffectId = effect.id;

	input_event play {};
	play.type  = EV_FF;
	play.code  = (uint16_t)m_ffEffectId;
	play.value = 1;
	if (write(m_fd, &play, sizeof(play)) != (ssize_t)sizeof(play)) {
		LOG_WARN("evdev: starting rumble on '%s' failed: %s",
		         m_name.c_str(), strerror(errno));
		return false;
	}
	return true;
}

void EvdevDevice::StopRumble()
{
	if (m_fd < 0 || m_ffEffectId < 0) return;

	input_event stop {};
	stop.type  = EV_FF;
	stop.code  = (uint16_t)m_ffEffectId;
	stop.value = 0;
	// The result is checked rather than cast to void: write() is declared
	// warn_unused_result, and a (void) cast does not silence that in GCC. A
	// failure here is worth a line in the log - it means a motor may still be
	// running - but it must not stop us releasing the effect slot below.
	if (write(m_fd, &stop, sizeof(stop)) != (ssize_t)sizeof(stop))
		LOG_WARN("evdev: stopping rumble on '%s' failed: %s",
		         m_name.c_str(), strerror(errno));

	// Release the slot as well - a process that exits without erasing leaves
	// the effect uploaded until the device is closed, and a device shared with
	// another running client then has one fewer slot.
	(void)ioctl(m_fd, EVIOCRMFF, m_ffEffectId);
	m_ffEffectId = -1;
}

//------------------------------------------------------------------------------
// Enumeration.
//------------------------------------------------------------------------------
std::string EvdevSanitizeIdentity(const std::string& path)
{
	std::string out = path;
	for (char& c : out) {
		const bool ok = (c >= '0' && c <= '9') ||
		                (c >= 'A' && c <= 'Z') ||
		                (c >= 'a' && c <= 'z');
		if (!ok) c = '_';
	}
	return out;
}

std::vector<EvdevNode> EvdevEnumerateNodes()
{
	std::vector<EvdevNode> nodes;

	// Pass 1: /dev/input/by-id/, which is where stable identity comes from.
	// Entries are symlinks; only the ones pointing at an event node are of
	// any use here - the same device also appears as ...-mouse (a mousedev
	// node, a different and much weaker interface).
	//
	// The directory is created by udev. It is absent on a system without udev
	// running - including WSL, and including devices created through
	// /dev/uinput - so its absence is normal, not an error.
	if (DIR* d = opendir("/dev/input/by-id")) {
		while (dirent* e = readdir(d)) {
			if (e->d_name[0] == '.') continue;

			const std::string linkPath = std::string("/dev/input/by-id/") + e->d_name;
			char target[512] = {0};
			const ssize_t n = readlink(linkPath.c_str(), target, sizeof(target) - 1);
			if (n <= 0) continue;
			target[n] = 0;

			const char* base = strrchr(target, '/');
			base = base ? base + 1 : target;
			if (strncmp(base, "event", 5) != 0) continue;

			nodes.push_back({std::string("/dev/input/") + base, linkPath});
		}
		closedir(d);
	}

	// Pass 2: every event node, adding the ones by-id did not cover. On a
	// udev-less system this is the only pass that finds anything.
	if (DIR* d = opendir("/dev/input")) {
		while (dirent* e = readdir(d)) {
			if (strncmp(e->d_name, "event", 5) != 0) continue;

			const std::string devNode = std::string("/dev/input/") + e->d_name;
			const bool known = std::any_of(nodes.begin(), nodes.end(),
				[&](const EvdevNode& n) { return n.devNode == devNode; });
			if (!known)
				nodes.push_back({devNode, std::string()});
		}
		closedir(d);
	} else {
		// Logged on TRANSITION only. Both evdev_input.cpp and
		// evdev_joystick.cpp rescan for hotplug every ~2 seconds, so an
		// unconditional warning here writes two lines every two seconds for
		// the entire session - on the WSL dev box that is the whole log.
		static int s_lastErrno = 0;
		if (errno != s_lastErrno) {
			s_lastErrno = errno;
			if (errno == ENOENT)
				LOG_WARN("evdev: /dev/input does not exist - no input devices on "
				         "this system (expected under WSL, which has no input "
				         "subsystem)");
			else
				LOG_ERROR("evdev: opendir(/dev/input) failed: %s", strerror(errno));
		}
	}

	// Stable order within a boot: event2 before event10, not lexicographic.
	std::sort(nodes.begin(), nodes.end(), [](const EvdevNode& a, const EvdevNode& b) {
		const int na = atoi(a.devNode.c_str() + sizeof("/dev/input/event") - 1);
		const int nb = atoi(b.devNode.c_str() + sizeof("/dev/input/event") - 1);
		return na < nb;
	});

	return nodes;
}
