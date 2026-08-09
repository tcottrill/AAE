//==============================================================================
// uinput_devices.cpp -- synthetic input devices for testing the evdev backend.
//
// WHY THIS EXISTS
// ---------------
// The development box is WSL2, which has no /dev/input directory at all - not
// an empty one, absent. There is no keyboard, mouse or gamepad for the evdev
// backend to enumerate, so none of Phase 3c Milestone B could be exercised
// before running it on real hardware for the first time.
//
// /dev/uinput IS present (built into the WSL kernel, no module to load), so
// this tool fabricates the devices instead. It covers everything mechanical:
// enumeration, capability-based classification, the KEY_* -> AAEKEY_* table,
// relative-motion accumulation, button bit order, absolute-axis scaling and
// force feedback. It cannot judge feel, latency, or whether a motor physically
// spins - those stay real-hardware items.
//
// It deliberately does NOT imitate well-behaved hardware:
//
//   * The pad's axes use ranges no controller uses (0..1023 unsigned for the
//     left stick, -2048..2047 for the right). Code that assumes a range
//     instead of reading EVIOCGABS produces garbage here and looks fine on a
//     real Xbox pad, which is exactly the wrong way round for a bug to hide.
//   * The mouse advertises BTN_LEFT, which lives in the KEY_* number space.
//     A classifier looking at "does it have EV_KEY bits" calls it a keyboard.
//   * Two keyboards are created, because one keyboard cannot prove the
//     per-device _Ex API routes anything.
//
// ROOT, AND WHY aae ITSELF DOES NOT NEED IT
// -----------------------------------------
// Writing to /dev/uinput requires root, and with no udev running the event
// nodes the kernel creates are 0600 root:root. This tool chmods the nodes it
// created to 0666 before handing back, so aae runs as an ordinary user. The
// nodes disappear with the process, taking the loosened permissions with them.
//
// USAGE
//   sudo ./aae_uinput_test --hold           create devices, hold until Ctrl-C
//   sudo ./aae_uinput_test --script FILE    create, replay FILE, exit
//   sudo ./aae_uinput_test --selftest       create, read back, verify, exit
//
//   --exec "<command>"                      run <command> as the invoking user
//                                           while the devices exist
//   --sony-pad                              also create 'pspad', a SECOND pad
//                                           identifying as a DualSense
//
// --exec turns the whole test into ONE command and one password prompt:
//
//   sudo ./aae_uinput_test --script tests/milestone_b.txt
//        --exec "./build-linux/aae_inputtest --seconds 45 --rumble"
//
// The child is dropped back to $SUDO_UID/$SUDO_GID before exec, so the thing
// under test runs unprivileged exactly as it would in real use - which also
// proves the 0666 chmod on the event nodes is doing its job.
//
// SCRIPT COMMANDS (one per line, '#' comments, blank lines ignored)
//   key   <dev> <KEY_NAME|code> <0|1|2>   press / release / autorepeat
//   rel   <dev> <REL_NAME|code> <delta>
//   abs   <dev> <ABS_NAME|code> <value>
//   tap   <dev> <KEY_NAME|code>           press, syn, release, syn
//   syn   <dev>
//   sleep <ms>
//   echo  <text>
// <dev> is one of: kbd1 kbd2 mouse pad  (and pspad with --sony-pad)
//==============================================================================

#include <linux/uinput.h>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Name -> code tables. Only the codes the Phase 3c tests actually drive; a
// numeric code is accepted anywhere a name is, so the table never blocks a
// test it does not happen to cover.
//------------------------------------------------------------------------------
struct CodeName { const char* name; int code; };

static const CodeName kKeyNames[] = {
	{"KEY_A",KEY_A},{"KEY_B",KEY_B},{"KEY_C",KEY_C},{"KEY_D",KEY_D},
	{"KEY_E",KEY_E},{"KEY_F",KEY_F},{"KEY_G",KEY_G},{"KEY_H",KEY_H},
	{"KEY_I",KEY_I},{"KEY_J",KEY_J},{"KEY_K",KEY_K},{"KEY_L",KEY_L},
	{"KEY_M",KEY_M},{"KEY_N",KEY_N},{"KEY_O",KEY_O},{"KEY_P",KEY_P},
	{"KEY_Q",KEY_Q},{"KEY_R",KEY_R},{"KEY_S",KEY_S},{"KEY_T",KEY_T},
	{"KEY_U",KEY_U},{"KEY_V",KEY_V},{"KEY_W",KEY_W},{"KEY_X",KEY_X},
	{"KEY_Y",KEY_Y},{"KEY_Z",KEY_Z},
	{"KEY_0",KEY_0},{"KEY_1",KEY_1},{"KEY_2",KEY_2},{"KEY_3",KEY_3},
	{"KEY_4",KEY_4},{"KEY_5",KEY_5},{"KEY_6",KEY_6},{"KEY_7",KEY_7},
	{"KEY_8",KEY_8},{"KEY_9",KEY_9},
	{"KEY_F1",KEY_F1},{"KEY_F2",KEY_F2},{"KEY_F3",KEY_F3},{"KEY_F4",KEY_F4},
	{"KEY_F5",KEY_F5},{"KEY_F6",KEY_F6},{"KEY_F7",KEY_F7},{"KEY_F8",KEY_F8},
	{"KEY_F9",KEY_F9},{"KEY_F10",KEY_F10},{"KEY_F11",KEY_F11},{"KEY_F12",KEY_F12},
	{"KEY_ESC",KEY_ESC},{"KEY_ENTER",KEY_ENTER},{"KEY_SPACE",KEY_SPACE},
	{"KEY_TAB",KEY_TAB},{"KEY_BACKSPACE",KEY_BACKSPACE},
	{"KEY_LEFT",KEY_LEFT},{"KEY_RIGHT",KEY_RIGHT},{"KEY_UP",KEY_UP},{"KEY_DOWN",KEY_DOWN},
	{"KEY_INSERT",KEY_INSERT},{"KEY_DELETE",KEY_DELETE},{"KEY_HOME",KEY_HOME},
	{"KEY_END",KEY_END},{"KEY_PAGEUP",KEY_PAGEUP},{"KEY_PAGEDOWN",KEY_PAGEDOWN},
	{"KEY_LEFTSHIFT",KEY_LEFTSHIFT},{"KEY_RIGHTSHIFT",KEY_RIGHTSHIFT},
	{"KEY_LEFTCTRL",KEY_LEFTCTRL},{"KEY_RIGHTCTRL",KEY_RIGHTCTRL},
	{"KEY_LEFTALT",KEY_LEFTALT},{"KEY_RIGHTALT",KEY_RIGHTALT},
	{"KEY_LEFTMETA",KEY_LEFTMETA},{"KEY_RIGHTMETA",KEY_RIGHTMETA},
	{"KEY_CAPSLOCK",KEY_CAPSLOCK},{"KEY_NUMLOCK",KEY_NUMLOCK},{"KEY_SCROLLLOCK",KEY_SCROLLLOCK},
	{"KEY_GRAVE",KEY_GRAVE},{"KEY_MINUS",KEY_MINUS},{"KEY_EQUAL",KEY_EQUAL},
	{"KEY_COMMA",KEY_COMMA},{"KEY_DOT",KEY_DOT},{"KEY_SLASH",KEY_SLASH},
	{"KEY_LEFTBRACE",KEY_LEFTBRACE},{"KEY_RIGHTBRACE",KEY_RIGHTBRACE},
	{"KEY_BACKSLASH",KEY_BACKSLASH},{"KEY_SEMICOLON",KEY_SEMICOLON},
	{"KEY_APOSTROPHE",KEY_APOSTROPHE},{"KEY_SYSRQ",KEY_SYSRQ},{"KEY_PAUSE",KEY_PAUSE},
	{"KEY_KP0",KEY_KP0},{"KEY_KP1",KEY_KP1},{"KEY_KP2",KEY_KP2},{"KEY_KP3",KEY_KP3},
	{"KEY_KP4",KEY_KP4},{"KEY_KP5",KEY_KP5},{"KEY_KP6",KEY_KP6},{"KEY_KP7",KEY_KP7},
	{"KEY_KP8",KEY_KP8},{"KEY_KP9",KEY_KP9},
	{"KEY_KPSLASH",KEY_KPSLASH},{"KEY_KPASTERISK",KEY_KPASTERISK},
	{"KEY_KPMINUS",KEY_KPMINUS},{"KEY_KPPLUS",KEY_KPPLUS},
	{"KEY_KPDOT",KEY_KPDOT},{"KEY_KPENTER",KEY_KPENTER},
	// mouse + gamepad buttons live in the same number space as the keys above -
	// which is precisely why classification cannot key off "has EV_KEY bits"
	{"BTN_LEFT",BTN_LEFT},{"BTN_RIGHT",BTN_RIGHT},{"BTN_MIDDLE",BTN_MIDDLE},
	{"BTN_SOUTH",BTN_SOUTH},{"BTN_EAST",BTN_EAST},{"BTN_NORTH",BTN_NORTH},
	{"BTN_WEST",BTN_WEST},{"BTN_TL",BTN_TL},{"BTN_TR",BTN_TR},
	{"BTN_SELECT",BTN_SELECT},{"BTN_START",BTN_START},{"BTN_MODE",BTN_MODE},
	{"BTN_THUMBL",BTN_THUMBL},{"BTN_THUMBR",BTN_THUMBR},
};

static const CodeName kRelNames[] = {
	{"REL_X",REL_X},{"REL_Y",REL_Y},{"REL_WHEEL",REL_WHEEL},{"REL_HWHEEL",REL_HWHEEL},
};

static const CodeName kAbsNames[] = {
	{"ABS_X",ABS_X},{"ABS_Y",ABS_Y},{"ABS_Z",ABS_Z},
	{"ABS_RX",ABS_RX},{"ABS_RY",ABS_RY},{"ABS_RZ",ABS_RZ},
	{"ABS_HAT0X",ABS_HAT0X},{"ABS_HAT0Y",ABS_HAT0Y},
};

static int lookup_code(const CodeName* table, size_t n, const char* s)
{
	if (s[0] >= '0' && s[0] <= '9') return atoi(s);
	for (size_t i = 0; i < n; i++)
		if (strcmp(table[i].name, s) == 0) return table[i].code;
	return -1;
}
#define LOOKUP(tbl, s) lookup_code(tbl, sizeof(tbl) / sizeof(tbl[0]), s)

//------------------------------------------------------------------------------
// One synthetic device.
//------------------------------------------------------------------------------
struct VirtualDevice {
	std::string shortName;      // "kbd1" - what scripts refer to it by
	std::string deviceName;     // EVIOCGNAME the backend will read
	int         fd       = -1;  // the /dev/uinput fd
	std::string eventPath;      // "/dev/input/event7", once created

	bool Emit(int type, int code, int value) const
	{
		struct input_event ev {};
		ev.type  = (unsigned short)type;
		ev.code  = (unsigned short)code;
		ev.value = value;
		// timestamp is filled in by the kernel for uinput writes
		if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
			fprintf(stderr, "  write(%s) failed: %s\n", shortName.c_str(), strerror(errno));
			return false;
		}
		return true;
	}
	bool Syn() const { return Emit(EV_SYN, SYN_REPORT, 0); }
};

static std::vector<VirtualDevice> g_devices;
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int) { g_stop = 1; }

//------------------------------------------------------------------------------
// After UI_DEV_CREATE the kernel knows the device as "inputN". sysfs is
// populated by the kernel itself (not udev, which is not running here), so the
// event node name is readable from /sys/class/input/inputN/eventM.
//
// Matching on EVIOCGNAME by scanning /dev/input would be the obvious
// alternative and is wrong: two keyboards are deliberately created with
// similar names, and a name collision would silently pair the wrong node.
//------------------------------------------------------------------------------
static void msleep(int ms);

static std::string resolve_event_node(int uifd)
{
	char sysname[64] = {0};
	if (ioctl(uifd, UI_GET_SYSNAME(sizeof(sysname) - 1), sysname) < 0) {
		fprintf(stderr, "  UI_GET_SYSNAME failed: %s\n", strerror(errno));
		return "";
	}

	// The eventN subdirectory does not exist the instant UI_DEV_CREATE
	// returns - the input core registers the device, then the evdev handler
	// attaches to it, and only then does the node appear. Resolving
	// immediately finds an inputN directory with no eventN inside it, which
	// looks exactly like "this device has no event interface".
	const std::string dir = std::string("/sys/class/input/") + sysname;
	std::string result;
	std::string sawInstead;

	for (int attempt = 0; attempt < 40 && result.empty(); attempt++) {   // ~2s
		if (attempt) msleep(50);

		DIR* d = opendir(dir.c_str());
		if (!d) {
			if (attempt == 0)
				sawInstead = std::string("opendir failed: ") + strerror(errno);
			continue;
		}
		sawInstead.clear();
		while (struct dirent* e = readdir(d)) {
			if (strncmp(e->d_name, "event", 5) == 0) {
				result = std::string("/dev/input/") + e->d_name;
				break;
			}
			if (e->d_name[0] != '.') {
				if (!sawInstead.empty()) sawInstead += " ";
				sawInstead += e->d_name;
			}
		}
		closedir(d);
	}

	if (result.empty()) {
		fprintf(stderr, "  no eventN under %s after 2s\n", dir.c_str());
		fprintf(stderr, "    directory contained: %s\n",
		        sawInstead.empty() ? "(nothing)" : sawInstead.c_str());
	}
	return result;
}

//------------------------------------------------------------------------------
// Create one device. keyCodes/relCodes are the capability bits; absAxes carries
// per-axis ranges so each device can advertise its own, which is the whole
// point of the pad's odd ones.
//------------------------------------------------------------------------------
struct AbsAxis { int code; int min; int max; int flat; };

static bool create_device(VirtualDevice& dev,
                          const std::vector<int>& keyCodes,
                          const std::vector<int>& relCodes,
                          const std::vector<AbsAxis>& absAxes,
                          bool wantRumble,
                          unsigned short vendor, unsigned short product)
{
	// O_RDWR, not O_WRONLY: force-feedback upload requests arrive by READING
	// this same fd. A write-only fd creates a pad that advertises FF_RUMBLE and
	// then never answers an upload, which the client sees as a hang.
	dev.fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
	if (dev.fd < 0) {
		fprintf(stderr, "open(/dev/uinput): %s\n", strerror(errno));
		if (errno == EACCES || errno == EPERM)
			fprintf(stderr, "  -> this tool must run as root (sudo)\n");
		return false;
	}

	if (!keyCodes.empty()) {
		ioctl(dev.fd, UI_SET_EVBIT, EV_KEY);
		for (int c : keyCodes) ioctl(dev.fd, UI_SET_KEYBIT, c);
	}
	if (!relCodes.empty()) {
		ioctl(dev.fd, UI_SET_EVBIT, EV_REL);
		for (int c : relCodes) ioctl(dev.fd, UI_SET_RELBIT, c);
	}
	if (!absAxes.empty()) {
		ioctl(dev.fd, UI_SET_EVBIT, EV_ABS);
		for (const AbsAxis& a : absAxes) {
			ioctl(dev.fd, UI_SET_ABSBIT, a.code);
			struct uinput_abs_setup as {};
			as.code            = (unsigned short)a.code;
			as.absinfo.minimum = a.min;
			as.absinfo.maximum = a.max;
			as.absinfo.flat    = a.flat;
			as.absinfo.value   = (a.min + a.max) / 2;   // centred at rest
			if (ioctl(dev.fd, UI_ABS_SETUP, &as) < 0)
				fprintf(stderr, "  UI_ABS_SETUP(%d): %s\n", a.code, strerror(errno));
		}
	}
	if (wantRumble) {
		ioctl(dev.fd, UI_SET_EVBIT, EV_FF);
		ioctl(dev.fd, UI_SET_FFBIT, FF_RUMBLE);
		ioctl(dev.fd, UI_SET_FFBIT, FF_PERIODIC);
	}

	struct uinput_setup us {};
	us.id.bustype = BUS_USB;
	us.id.vendor  = vendor;
	us.id.product = product;
	us.id.version = 1;
	us.ff_effects_max = wantRumble ? 16 : 0;
	snprintf(us.name, sizeof(us.name), "%s", dev.deviceName.c_str());

	if (ioctl(dev.fd, UI_DEV_SETUP, &us) < 0) {
		fprintf(stderr, "UI_DEV_SETUP(%s): %s\n", dev.deviceName.c_str(), strerror(errno));
		close(dev.fd); dev.fd = -1;
		return false;
	}
	if (ioctl(dev.fd, UI_DEV_CREATE) < 0) {
		fprintf(stderr, "UI_DEV_CREATE(%s): %s\n", dev.deviceName.c_str(), strerror(errno));
		close(dev.fd); dev.fd = -1;
		return false;
	}

	dev.eventPath = resolve_event_node(dev.fd);
	if (!dev.eventPath.empty()) {
		// Without udev the node is 0600 root:root, so an unprivileged client
		// cannot open it. Loosening it here keeps the program under test out
		// of sudo; the node is destroyed with this process either way.
		//
		// chown as well as chmod: on some filesystems the mode alone was not
		// enough, and handing the node to the invoking user is the thing that
		// actually matters. Both are attempted and the RESULT IS VERIFIED
		// below rather than assumed - an unverified chmod here produces a
		// permission failure inside the program under test, which reads as a
		// bug in that program rather than in this harness.
		const char* uidStr = getenv("SUDO_UID");
		const char* gidStr = getenv("SUDO_GID");
		if (uidStr) {
			if (chown(dev.eventPath.c_str(), (uid_t)atoi(uidStr),
			          gidStr ? (gid_t)atoi(gidStr) : (gid_t)-1) < 0)
				fprintf(stderr, "  chown(%s): %s\n", dev.eventPath.c_str(), strerror(errno));
		}
		if (chmod(dev.eventPath.c_str(), 0666) < 0)
			fprintf(stderr, "  chmod(%s): %s\n", dev.eventPath.c_str(), strerror(errno));
	}

	// Report what the node ACTUALLY looks like now.
	char modeInfo[64] = "";
	if (!dev.eventPath.empty()) {
		struct stat st {};
		if (stat(dev.eventPath.c_str(), &st) == 0)
			snprintf(modeInfo, sizeof(modeInfo), "  mode=%04o uid=%d gid=%d",
			         st.st_mode & 07777, (int)st.st_uid, (int)st.st_gid);
		else
			snprintf(modeInfo, sizeof(modeInfo), "  (stat failed: %s)", strerror(errno));
	}

	printf("  %-6s %-28s -> %s%s\n", dev.shortName.c_str(), dev.deviceName.c_str(),
	       dev.eventPath.empty() ? "(node not resolved)" : dev.eventPath.c_str(), modeInfo);
	return true;
}

static VirtualDevice* find_device(const char* shortName)
{
	for (VirtualDevice& d : g_devices)
		if (d.shortName == shortName) return &d;
	return nullptr;
}

//------------------------------------------------------------------------------
// The device set.
//------------------------------------------------------------------------------
static bool create_all(bool wantSonyPad)
{
	std::vector<int> fullKeyboard;
	for (size_t i = 0; i < sizeof(kKeyNames) / sizeof(kKeyNames[0]); i++) {
		const int c = kKeyNames[i].code;
		if (c < BTN_MISC) fullKeyboard.push_back(c);   // keys only, no BTN_*
	}

	g_devices.push_back({"kbd1",  "AAE Test Keyboard Alpha"});
	g_devices.push_back({"kbd2",  "AAE Test Keyboard Beta"});
	g_devices.push_back({"mouse", "AAE Test Mouse"});
	g_devices.push_back({"pad",   "AAE Test Gamepad"});
	if (wantSonyPad)
		g_devices.push_back({"pspad", "AAE Test DualSense"});

	printf("Creating virtual devices:\n");

	// Two keyboards, differing only in name and product id. One keyboard
	// cannot demonstrate that the per-device _Ex API routes anything.
	if (!create_device(g_devices[0], fullKeyboard, {}, {}, false, 0xAAE0, 0x0001)) return false;
	if (!create_device(g_devices[1], fullKeyboard, {}, {}, false, 0xAAE0, 0x0002)) return false;

	// BTN_LEFT/RIGHT/MIDDLE are EV_KEY codes. A classifier that asks "does it
	// report EV_KEY?" calls this a keyboard; only checking for keys in the
	// KEY_A..KEY_Z range, or for REL_X/REL_Y, gets it right.
	if (!create_device(g_devices[2], {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE},
	                   {REL_X, REL_Y, REL_WHEEL}, {}, false, 0xAAE0, 0x0003)) return false;

	// Axis ranges no real controller uses. An XInput-shaped assumption of
	// -32768..32767 reads the left stick as pinned hard left forever; a
	// backend that honours EVIOCGABS reads it centred.
	const std::vector<AbsAxis> padAxes = {
		{ABS_X,        0, 1023, 16},    // unsigned, 10-bit
		{ABS_Y,        0, 1023, 16},
		{ABS_RX,   -2048, 2047, 32},    // signed, 12-bit
		{ABS_RY,   -2048, 2047, 32},
		{ABS_HAT0X,   -1,    1,  0},    // d-pad as a hat
		{ABS_HAT0Y,   -1,    1,  0},
	};
	const std::vector<int> padButtons = {
		BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
		BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR
	};
	if (!create_device(g_devices[3], padButtons, {}, padAxes, true, 0xAAE0, 0x0004))
		return false;

	// A pad whose ONLY difference from the one above is the USB id it reports.
	// 054C:0CE6 is a DualSense, which the kernel serves with hid-playstation -
	// the driver that emits the face buttons by compass point rather than by
	// printed letter. Same button set on purpose: driving BTN_NORTH/BTN_WEST on
	// both pads in one run is what makes the per-device swap visible, because
	// nothing else about the two devices differs.
	if (wantSonyPad &&
	    !create_device(g_devices[4], padButtons, {}, padAxes, true, 0x054C, 0x0CE6))
		return false;

	printf("\nAxis ranges advertised by 'pad' (deliberately non-standard):\n"
	       "  ABS_X/ABS_Y     0..1023   unsigned\n"
	       "  ABS_RX/ABS_RY   -2048..2047\n"
	       "  ABS_HAT0X/Y     -1..1\n"
	       "A backend assuming a range instead of reading EVIOCGABS fails here.\n\n");
	return true;
}

static void destroy_all()
{
	for (VirtualDevice& d : g_devices) {
		if (d.fd >= 0) {
			ioctl(d.fd, UI_DEV_DESTROY);
			close(d.fd);
			d.fd = -1;
		}
	}
}

//------------------------------------------------------------------------------
// Force-feedback service.
//
// A client uploading an effect (EVIOCSFF) blocks in the kernel until this
// process answers with UI_BEGIN_FF_UPLOAD / UI_END_FF_UPLOAD. Printing the
// magnitudes is what turns "joystick_set_rumble returned true" into evidence
// that the right values reached the device.
//------------------------------------------------------------------------------
static void service_ff(VirtualDevice& dev)
{
	struct input_event ev;
	while (read(dev.fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_UINPUT) {
			if (ev.code == UI_FF_UPLOAD) {
				struct uinput_ff_upload up {};
				up.request_id = ev.value;
				if (ioctl(dev.fd, UI_BEGIN_FF_UPLOAD, &up) < 0) continue;
				if (up.effect.type == FF_RUMBLE) {
					printf("[FF] upload id=%d  strong=%u (%.0f%%)  weak=%u (%.0f%%)  "
					       "length=%ums\n",
					       up.effect.id,
					       up.effect.u.rumble.strong_magnitude,
					       up.effect.u.rumble.strong_magnitude * 100.0 / 65535.0,
					       up.effect.u.rumble.weak_magnitude,
					       up.effect.u.rumble.weak_magnitude * 100.0 / 65535.0,
					       up.effect.replay.length);
				} else {
					printf("[FF] upload id=%d  type=%u (not FF_RUMBLE)\n",
					       up.effect.id, up.effect.type);
				}
				up.retval = 0;
				ioctl(dev.fd, UI_END_FF_UPLOAD, &up);
				fflush(stdout);
			}
			else if (ev.code == UI_FF_ERASE) {
				struct uinput_ff_erase er {};
				er.request_id = ev.value;
				if (ioctl(dev.fd, UI_BEGIN_FF_ERASE, &er) < 0) continue;
				printf("[FF] erase id=%u\n", er.effect_id);
				er.retval = 0;
				ioctl(dev.fd, UI_END_FF_ERASE, &er);
				fflush(stdout);
			}
		}
		else if (ev.type == EV_FF) {
			// value 0 = stop, >0 = play that many times
			printf("[FF] %s effect id=%u (value=%d)\n",
			       ev.value ? "PLAY" : "STOP", ev.code, ev.value);
			fflush(stdout);
		}
	}
}

//------------------------------------------------------------------------------
// Script replay.
//------------------------------------------------------------------------------
static void msleep(int ms)
{
	struct timespec ts;
	ts.tv_sec  = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, nullptr);
}

// Sleep while continuing to answer force-feedback requests.
//
// A client's EVIOCSFF blocks in the kernel until this process completes the
// UI_BEGIN_FF_UPLOAD / UI_END_FF_UPLOAD handshake. A script that merely slept
// would leave joystick_set_rumble() hanging - so rumble would look broken for
// a reason that has nothing to do with the backend under test.
static void sleep_servicing_ff(int ms)
{
	// Every device, not just "pad": with --sony-pad there are two pads that
	// advertise FF_RUMBLE, and an unanswered upload blocks the client in the
	// kernel. Devices with no FF simply never produce a request.
	while (ms > 0 && !g_stop) {
		for (VirtualDevice& d : g_devices)
			if (d.fd >= 0) service_ff(d);
		const int slice = ms < 10 ? ms : 10;
		msleep(slice);
		ms -= slice;
	}
}

static bool run_script(FILE* f)
{
	char line[512];
	int lineNo = 0;
	bool ok = true;

	while (fgets(line, sizeof(line), f) && !g_stop) {
		lineNo++;
		char* p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

		char cmd[32] = {0}, a1[64] = {0}, a2[64] = {0}, a3[64] = {0};
		const int n = sscanf(p, "%31s %63s %63s %63s", cmd, a1, a2, a3);
		if (n < 1) continue;

		if (strcmp(cmd, "sleep") == 0 && n >= 2) { sleep_servicing_ff(atoi(a1)); continue; }
		if (strcmp(cmd, "echo") == 0) {
			// echo the remainder of the line verbatim
			char* rest = strstr(p, "echo");
			printf(">> %s", rest ? rest + 5 : "\n");
			fflush(stdout);
			continue;
		}

		VirtualDevice* dev = (n >= 2) ? find_device(a1) : nullptr;
		if (!dev) {
			fprintf(stderr, "line %d: unknown device '%s'\n", lineNo, a1);
			ok = false;
			continue;
		}

		if (strcmp(cmd, "syn") == 0) { dev->Syn(); continue; }

		if (strcmp(cmd, "tap") == 0 && n >= 3) {
			const int code = LOOKUP(kKeyNames, a2);
			if (code < 0) { fprintf(stderr, "line %d: unknown key '%s'\n", lineNo, a2); ok = false; continue; }
			dev->Emit(EV_KEY, code, 1); dev->Syn();
			// Held longer than one 60Hz frame on purpose. At 20ms a tap could
			// begin and end inside a single poll of a client that samples
			// key[] once per frame, so the net state change was zero and the
			// key silently never appeared - which looks like a missing keymap
			// entry rather than a test artefact.
			msleep(50);
			dev->Emit(EV_KEY, code, 0); dev->Syn();
			continue;
		}

		if (n < 4) { fprintf(stderr, "line %d: '%s' needs 3 arguments\n", lineNo, cmd); ok = false; continue; }

		if (strcmp(cmd, "key") == 0) {
			const int code = LOOKUP(kKeyNames, a2);
			if (code < 0) { fprintf(stderr, "line %d: unknown key '%s'\n", lineNo, a2); ok = false; continue; }
			dev->Emit(EV_KEY, code, atoi(a3)); dev->Syn();
		}
		else if (strcmp(cmd, "rel") == 0) {
			const int code = LOOKUP(kRelNames, a2);
			if (code < 0) { fprintf(stderr, "line %d: unknown rel axis '%s'\n", lineNo, a2); ok = false; continue; }
			dev->Emit(EV_REL, code, atoi(a3)); dev->Syn();
		}
		else if (strcmp(cmd, "abs") == 0) {
			const int code = LOOKUP(kAbsNames, a2);
			if (code < 0) { fprintf(stderr, "line %d: unknown abs axis '%s'\n", lineNo, a2); ok = false; continue; }
			dev->Emit(EV_ABS, code, atoi(a3)); dev->Syn();
		}
		else {
			fprintf(stderr, "line %d: unknown command '%s'\n", lineNo, cmd);
			ok = false;
		}
	}
	return ok;
}

//------------------------------------------------------------------------------
// Self-test: read our own devices back through the same interfaces the evdev
// backend uses. Proves the harness before anything is debugged against it -
// a broken harness and a broken backend look identical from the outside.
//------------------------------------------------------------------------------
static bool selftest()
{
	bool ok = true;
	printf("Self-test: reading the devices back through evdev\n");

	for (VirtualDevice& d : g_devices) {
		if (d.eventPath.empty()) {
			printf("  FAIL %-6s no event node resolved\n", d.shortName.c_str());
			ok = false;
			continue;
		}

		const int fd = open(d.eventPath.c_str(), O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			printf("  FAIL %-6s open(%s): %s\n", d.shortName.c_str(),
			       d.eventPath.c_str(), strerror(errno));
			ok = false;
			continue;
		}

		char name[256] = {0};
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0 ||
		    d.deviceName != name) {
			printf("  FAIL %-6s EVIOCGNAME = '%s', expected '%s'\n",
			       d.shortName.c_str(), name, d.deviceName.c_str());
			ok = false;
		}

		unsigned long evbits[(EV_MAX + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};
		ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
		#define HAS_BIT(arr, bit) (((arr)[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long)))) & 1)

		const bool hasKey = HAS_BIT(evbits, EV_KEY);
		const bool hasRel = HAS_BIT(evbits, EV_REL);
		const bool hasAbs = HAS_BIT(evbits, EV_ABS);
		const bool hasFF  = HAS_BIT(evbits, EV_FF);
		printf("  %-6s %s  caps:%s%s%s%s\n", d.shortName.c_str(), d.eventPath.c_str(),
		       hasKey ? " EV_KEY" : "", hasRel ? " EV_REL" : "",
		       hasAbs ? " EV_ABS" : "", hasFF ? " EV_FF" : "");

		// The pad's ranges are the point of the whole exercise - read them back
		if (hasAbs) {
			const int axes[] = {ABS_X, ABS_RX, ABS_HAT0X};
			const char* axisNames[] = {"ABS_X", "ABS_RX", "ABS_HAT0X"};
			for (int i = 0; i < 3; i++) {
				struct input_absinfo ai {};
				if (ioctl(fd, EVIOCGABS(axes[i]), &ai) == 0)
					printf("         %-10s min=%d max=%d flat=%d value=%d\n",
					       axisNames[i], ai.minimum, ai.maximum, ai.flat, ai.value);
			}
		}
		#undef HAS_BIT
		close(fd);
	}

	printf("Self-test: %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

//------------------------------------------------------------------------------
// Start a child process as the user who invoked sudo, so the program under
// test runs with ordinary privileges.
static pid_t spawn_child(const char* command)
{
	const pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed: %s\n", strerror(errno));
		return -1;
	}
	if (pid > 0) return pid;

	// setgid BEFORE setuid: dropping the uid first removes the privilege
	// needed to change the gid, leaving the child in root's group.
	const char* gid = getenv("SUDO_GID");
	const char* uid = getenv("SUDO_UID");
	if (gid && setgid((gid_t)atoi(gid)) != 0)
		fprintf(stderr, "setgid failed: %s\n", strerror(errno));
	if (uid && setuid((uid_t)atoi(uid)) != 0)
		fprintf(stderr, "setuid failed: %s\n", strerror(errno));

	execl("/bin/sh", "sh", "-c", command, (char*)nullptr);
	fprintf(stderr, "exec failed: %s\n", strerror(errno));
	_exit(127);
}

int main(int argc, char** argv)
{
	const char* mode = (argc > 1) ? argv[1] : "--hold";
	const char* scriptPath = nullptr;
	const char* execCmd = nullptr;
	bool wantSonyPad = false;

	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--exec") == 0 && i + 1 < argc) execCmd = argv[++i];
		else if (strcmp(argv[i], "--sony-pad") == 0)        wantSonyPad = true;
		else if (argv[i][0] != '-' && !scriptPath)          scriptPath = argv[i];
	}

	if (geteuid() != 0) {
		fprintf(stderr, "This tool writes to /dev/uinput and must run as root:\n"
		                "  sudo %s %s\n", argv[0], mode);
		return 1;
	}

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);

	if (!create_all(wantSonyPad)) {
		destroy_all();
		return 1;
	}

	// The kernel needs a moment between UI_DEV_CREATE and the node being
	// usable; without it a fast client sees ENODEV on a device that exists.
	msleep(200);

	pid_t child = -1;
	if (execCmd) {
		printf("Launching under test: %s\n\n", execCmd);
		fflush(stdout);
		child = spawn_child(execCmd);
	}

	int rc = 0;

	if (strcmp(mode, "--selftest") == 0) {
		rc = selftest() ? 0 : 1;
	}
	else if (strcmp(mode, "--script") == 0) {
		if (!scriptPath) { fprintf(stderr, "--script needs a file argument\n"); rc = 1; }
		else {
			FILE* f = fopen(scriptPath, "r");
			if (!f) { fprintf(stderr, "fopen(%s): %s\n", scriptPath, strerror(errno)); rc = 1; }
			else {
				printf("Replaying %s\n", scriptPath);
				rc = run_script(f) ? 0 : 1;
				fclose(f);
				msleep(200);   // let the reader drain before the nodes vanish
			}
		}
	}
	else {   // --hold
		printf("Devices held open. Force-feedback uploads will be printed here.\n"
		       "Press Ctrl-C to remove them.\n\n");
		while (!g_stop) {
			for (VirtualDevice& d : g_devices)
				if (d.fd >= 0) service_ff(d);
			msleep(20);
		}
		printf("\nRemoving devices.\n");
	}

	// Keep the devices alive until the child finishes. Destroying them first
	// would make the program under test see every device vanish mid-run and
	// report a failure that is entirely this harness's doing.
	if (child > 0) {
		printf("\nScript finished; waiting for the child to exit "
		       "(devices still present)...\n");
		fflush(stdout);
		int status = 0;
		while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
			if (g_stop) { kill(child, SIGTERM); }
		}
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			fprintf(stderr, "child exited with status %d\n", WEXITSTATUS(status));
			rc = rc ? rc : 1;
		}
	}

	destroy_all();
	return rc;
}
