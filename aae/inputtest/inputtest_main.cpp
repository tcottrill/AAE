//==============================================================================
// inputtest_main.cpp -- Phase 3c's observable proof for the evdev backend.
//
// Links the REAL evdev backend (evdev_input.cpp, evdev_device.cpp,
// evdev_keymap.cpp, evdev_joystick.cpp) against the same sys_input.h /
// joystick.h contract the emulator uses, and reports every state change it
// sees. Directly modelled on aae_audiotest, which links the real mixer for the
// same reason: a green build says nothing about whether input actually works.
//
// It exists because there is nothing else to look at. The emulator does not
// log key presses - it responds to them - so on a machine with no display and
// no hands (WSL, CI) a working backend and a dead one produce identical logs.
//
// Pair it with tools/linux/uinput_devices.cpp, which fabricates the devices:
//
//   terminal A:  sudo ./aae_uinput_test --script tools/linux/tests/keyboard.txt
//   terminal B:  ./aae_inputtest --seconds 20
//
// Order does not matter. Devices appearing while this is running exercise the
// hotplug rescan, which is worth testing anyway.
//==============================================================================
#include "linux/evdev_input.h"

#include "sys_input.h"
#include "sys_window.h"
#include "joystick.h"
#include "sys_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

//------------------------------------------------------------------------------
// evdev_input.cpp's get_mouse_win() asks the window where the pointer is, the
// way the Win32 backend asks Windows. There is no window here, so this
// satisfies the contract honestly rather than pretending.
//------------------------------------------------------------------------------
namespace {

class NoWindow : public ISystemWindow {
public:
	bool Create(const WindowSetup&) override { return true; }
	void Destroy() override {}
	bool PumpEvents() override { return true; }
	int   ClientWidth()  const override { return 0; }
	int   ClientHeight() const override { return 0; }
	float DpiScale()     const override { return 1.0f; }
	void ToggleBorderlessFullscreen() override {}
	void RestoreViewport() override {}
	void SetCursorVisible(bool) override {}
	void EnableCursorClip(bool) override {}
	void ForceCursorClipUpdate() override {}
	void SetMousePos(int, int) override {}
	void GetMousePos(int* x, int* y) const override { if (x) *x = 0; if (y) *y = 0; }
};

NoWindow    g_window;
WindowSetup g_setup;

//------------------------------------------------------------------------------
// Callback observers.
//
// The polled view below compares key[] between frames, which CANNOT see a key
// that is pressed and released inside one 16ms window - the net state change
// is zero. That is a real property of polling and it is shared with the Win32
// backend, so it is not a defect; but it means the polled view alone cannot
// prove no event was dropped. KEY_Z vanished from a run for exactly this
// reason and looked like a keymap hole.
//
// The callbacks fire per EVENT, so they catch those. They are also part of the
// sys_input.h contract and nothing else here exercises them.
//------------------------------------------------------------------------------
int g_cbKeyEvents = 0;
int g_cbMouseEvents = 0;

void DescribeKey(int aae, char* out, size_t n);

void OnKey(int k, int scancode, int action, int mods)
{
	char nameBuf[32];
	DescribeKey(k, nameBuf, sizeof(nameBuf));
	printf("[cb  ] key %-9s %-7s scancode=%-3d mods=0x%x\n",
	       nameBuf, action ? "PRESS" : "RELEASE", scancode, mods);
	fflush(stdout);
	g_cbKeyEvents++;
}

void OnMouseButton(int button, int action, int mods)
{
	static const char* names[3] = {"left", "right", "middle"};
	printf("[cb  ] mousebtn %-6s %-7s mods=0x%x\n",
	       (button >= 0 && button < 3) ? names[button] : "?",
	       action ? "PRESS" : "RELEASE", mods);
	fflush(stdout);
	g_cbMouseEvents++;
}

void msleep(int ms)
{
	timespec ts;
	ts.tv_sec  = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, nullptr);
}

// Print an AAE key code the way a person can check it against sys_input.h.
void DescribeKey(int aae, char* out, size_t n)
{
	if (aae >= AAEKEY_A && aae <= AAEKEY_Z)      snprintf(out, n, "%c", 'A' + (aae - AAEKEY_A));
	else if (aae >= AAEKEY_0 && aae <= AAEKEY_9) snprintf(out, n, "%c", '0' + (aae - AAEKEY_0));
	else if (aae >= AAEKEY_F1 && aae <= AAEKEY_F12) snprintf(out, n, "F%d", 1 + (aae - AAEKEY_F1));
	else if (aae >= AAEKEY_0_PAD && aae <= AAEKEY_9_PAD)
		snprintf(out, n, "%d_PAD", aae - AAEKEY_0_PAD);
	else switch (aae) {
		case AAEKEY_ESC:      snprintf(out, n, "ESC");       break;
		case AAEKEY_SPACE:    snprintf(out, n, "SPACE");     break;
		case AAEKEY_ENTER:    snprintf(out, n, "ENTER");     break;
		case AAEKEY_TAB:      snprintf(out, n, "TAB");       break;
		case AAEKEY_LEFT:     snprintf(out, n, "LEFT");      break;
		case AAEKEY_RIGHT:    snprintf(out, n, "RIGHT");     break;
		case AAEKEY_UP:       snprintf(out, n, "UP");        break;
		case AAEKEY_DOWN:     snprintf(out, n, "DOWN");      break;
		case AAEKEY_LSHIFT:   snprintf(out, n, "LSHIFT");    break;
		case AAEKEY_RSHIFT:   snprintf(out, n, "RSHIFT");    break;
		case AAEKEY_LCONTROL: snprintf(out, n, "LCONTROL");  break;
		case AAEKEY_RCONTROL: snprintf(out, n, "RCONTROL");  break;
		case AAEKEY_ALT:      snprintf(out, n, "ALT");       break;
		case AAEKEY_ALTGR:    snprintf(out, n, "ALTGR");     break;
		default:              snprintf(out, n, "0x%02x", aae); break;
	}
}

} // namespace

// The two window entry points evdev_input.cpp reaches for.
WindowSetup&   GetWindowSetup()  { return g_setup; }
ISystemWindow& GetSystemWindow() { return g_window; }

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
	// Shorter than it looks like it should be, on purpose. The harness holds
	// the devices open until this exits, so a duration much longer than the
	// script leaves the operator staring at "waiting for the child" - and a
	// Ctrl-C there kills this process group mid-write, truncating the very
	// output the run existed to produce.
	int seconds = 25;
	bool doRumble = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = atoi(argv[++i]);
		else if (strcmp(argv[i], "--rumble") == 0)             doRumble = true;
	}

	Log::open("inputtest.log");
	printf("=== AAE evdev input test ===\n");
	printf("Polling for %d seconds at 60Hz. Ctrl-C to stop early.\n\n", seconds);

	EvdevInput_Initialize();
	install_joystick();
	SetKeyCallback(OnKey);
	SetMouseButtonCallback(OnMouseButton);

	printf("--- devices at startup ---\n");
	printf("keyboards: %d\n", RawInput_GetKeyboardCount());
	for (int i = 0; i < RawInput_GetKeyboardCount(); i++)
		printf("  [%d] %-32s id=%s\n", i, RawInput_GetKeyboardName(i), RawInput_GetKeyboardPath(i));
	printf("mice: %d\n", RawInput_GetMouseCount());
	for (int i = 0; i < RawInput_GetMouseCount(); i++)
		printf("  [%d] %-32s id=%s\n", i, RawInput_GetMouseName(i), RawInput_GetMousePath(i));
	printf("gamepads: %d (driver: %s)\n", joystick_device_count(), joystick_driver_name());
	for (int i = 0; i < joystick_device_count(); i++)
		printf("  [%d] %-32s id=%s connected=%d\n", i, joystick_get_display_name(i),
		       joystick_get_id(i), joystick_is_connected(i));
	printf("\n--- events ---\n");
	fflush(stdout);

	unsigned char prevKey[256] = {0};
	int  prevMouseB = 0;
	int32_t prevWheel = 0;
	int  prevHeld[256] = {0};
	int  prevPads   = -1;
	int  prevKbds   = -1;
	int  prevMice   = -1;
	int  prevJoyBtn[MAX_JOYSTICKS][16] = {};
	int  prevAxis[MAX_JOYSTICKS][4] = {};
	bool rumbleFired = false;

	const int frames = seconds * 60;
	for (int frame = 0; frame < frames; frame++) {
		EvdevInput_Poll();
		poll_joystick();

		// Device count changes (hotplug)
		if (RawInput_GetKeyboardCount() != prevKbds ||
		    RawInput_GetMouseCount()    != prevMice ||
		    joystick_device_count()     != prevPads) {
			prevKbds = RawInput_GetKeyboardCount();
			prevMice = RawInput_GetMouseCount();
			prevPads = joystick_device_count();
			printf("[devices] keyboards=%d mice=%d gamepads=%d\n", prevKbds, prevMice, prevPads);
			for (int i = 0; i < prevKbds; i++)
				printf("          kbd[%d] %s (id=%s)\n", i,
				       RawInput_GetKeyboardName(i), RawInput_GetKeyboardPath(i));
			for (int i = 0; i < prevMice; i++)
				printf("          mouse[%d] %s (id=%s)\n", i,
				       RawInput_GetMouseName(i), RawInput_GetMousePath(i));
			for (int i = 0; i < prevPads; i++)
				printf("          pad[%d] %s (id=%s)\n", i,
				       joystick_get_display_name(i), joystick_get_id(i));
			fflush(stdout);
		}

		// Keyboard: merged state, plus which device produced it. Reporting
		// BOTH is the point - a backend can update key[] correctly and still
		// route every device to slot 0, which is the multi-HID claim failing
		// silently.
		for (int k = 0; k < 256; k++) {
			if (key[k] == prevKey[k]) continue;
			prevKey[k] = key[k];

			char nameBuf[32];
			DescribeKey(k, nameBuf, sizeof(nameBuf));

			char which[128] = {0};
			size_t used = 0;
			for (int d = 0; d < RawInput_GetKeyboardCount(); d++)
				if (RawInput_IsKeyDownEx(d, k))
					used += (size_t)snprintf(which + used, sizeof(which) - used,
					                         "%skbd%d", used ? "," : "", d);

			printf("[key ] %-9s %-7s held=%-3d mods=0x%x  from=[%s]\n",
			       nameBuf, key[k] ? "DOWN" : "UP", isKeyHeld(k), GetModifierFlags(),
			       used ? which : "-");
			fflush(stdout);
		}

		// Auto-repeat feeds the hold counter without re-reporting a press.
		// Only visible here: key[] does not change on a repeat, so the
		// state-change loop above cannot show it.
		for (int k = 0; k < 256; k++) {
			if (!key[k]) continue;
			const int held = isKeyHeld(k);
			if (held == prevHeld[k]) continue;
			prevHeld[k] = held;
			if (held > 1) {
				char nameBuf[32];
				DescribeKey(k, nameBuf, sizeof(nameBuf));
				printf("[rpt ] %-9s hold counter now %d (auto-repeat)\n", nameBuf, held);
				fflush(stdout);
			}
		}

		// Mouse
		int mx = 0, my = 0;
		get_mouse_mickeys(&mx, &my);
		if (mx || my) {
			printf("[mouse] mickeys dx=%-5d dy=%-5d  abs=(%d,%d) wheel=%d",
			       mx, my, GetMouseX(), GetMouseY(), GetMouseWheel());
			for (int d = 0; d < RawInput_GetMouseCount(); d++) {
				int dx = 0, dy = 0;
				get_mouse_mickeys_ex(d, &dx, &dy);
				if (dx || dy) printf("  mouse%d=(%d,%d)", d, dx, dy);
			}
			printf("\n");
			fflush(stdout);
		}
		if (GetMouseWheel() != prevWheel) {
			printf("[mouse] wheel abs=%-6d change=%d\n",
			       GetMouseWheel(), GetMouseWheelChange());
			prevWheel = GetMouseWheel();
			fflush(stdout);
		}
		if (mouse_b != prevMouseB) {
			printf("[mouse] buttons=0x%x (L=%d R=%d M=%d)\n", mouse_b,
			       IsMouseLButtonDown(), IsMouseRButtonDown(), IsMouseMButtonDown());
			prevMouseB = mouse_b;
			fflush(stdout);
		}

		// Gamepad
		for (int p = 0; p < joystick_device_count() && p < MAX_JOYSTICKS; p++) {
			if (!joystick_is_connected(p)) continue;

			const int ax[4] = {
				joy[p].stick[0].axis[0].pos, joy[p].stick[0].axis[1].pos,
				joy[p].stick[1].axis[0].pos, joy[p].stick[1].axis[1].pos
			};
			for (int a = 0; a < 4; a++) {
				if (ax[a] == prevAxis[p][a]) continue;
				memcpy(prevAxis[p], ax, sizeof(ax));
				printf("[pad%d] LX=%-5d LY=%-5d RX=%-5d RY=%-5d  d1/d2 LX=%d%d LY=%d%d  hat=%d\n",
				       p, ax[0], ax[1], ax[2], ax[3],
				       joy[p].stick[0].axis[0].d1, joy[p].stick[0].axis[0].d2,
				       joy[p].stick[0].axis[1].d1, joy[p].stick[0].axis[1].d2,
				       joy_hat);
				fflush(stdout);
				break;
			}

			for (int b = 0; b < 16; b++) {
				if (joy[p].button[b].b == prevJoyBtn[p][b]) continue;
				prevJoyBtn[p][b] = joy[p].button[b].b;
				printf("[pad%d] button %-10s %s\n", p,
				       joy[p].button[b].name ? joy[p].button[b].name : "?",
				       joy[p].button[b].b ? "DOWN" : "UP");
				fflush(stdout);
			}

			if (joystick_check_combo(p, JOY_COMBO_PAUSE)) { printf("[pad%d] COMBO: PAUSE (Start+Back)\n", p); fflush(stdout); }
			if (joystick_check_combo(p, JOY_COMBO_ESC))   { printf("[pad%d] COMBO: ESC (LStick+Back)\n", p);  fflush(stdout); }
			if (joystick_check_combo(p, JOY_COMBO_MENU))  { printf("[pad%d] COMBO: MENU (LStick+Start)\n", p); fflush(stdout); }
		}

		// Rumble, once, a second after a pad first appears - the uinput
		// harness prints the magnitudes it receives, which is what turns
		// "returned true" into evidence the values arrived.
		if (doRumble && !rumbleFired && joystick_any_connected() && frame > 60) {
			rumbleFired = true;
			const bool ok = joystick_set_rumble(0, 0.75f, 0.25f);
			printf("[pad0] joystick_set_rumble(0.75, 0.25) -> %s\n", ok ? "true" : "FALSE");
			fflush(stdout);
		}
		if (doRumble && rumbleFired && frame == 180) {
			joystick_stop_rumble(0);
			printf("[pad0] joystick_stop_rumble()\n");
			fflush(stdout);
		}

		msleep(16);
	}

	printf("\n--- summary ---\n");
	printf("callback events: %d key, %d mouse-button\n", g_cbKeyEvents, g_cbMouseEvents);
	printf("keyboards=%d mice=%d gamepads=%d\n",
	       RawInput_GetKeyboardCount(), RawInput_GetMouseCount(), joystick_device_count());
	for (int i = 0; i < RawInput_GetKeyboardCount(); i++)
		printf("  kbd[%d] seen_input=%d %s\n", i, RawInput_KeyboardSeenInput(i),
		       RawInput_GetKeyboardName(i));
	for (int i = 0; i < RawInput_GetMouseCount(); i++)
		printf("  mouse[%d] seen_input=%d buttons=0x%x %s\n", i,
		       RawInput_MouseSeenInput(i), RawInput_GetMouseButtons(i),
		       RawInput_GetMouseName(i));

	remove_joystick();
	RawInput_Shutdown();
	Log::close();
	return 0;
}
