//==============================================================================
// linux_main.cpp -- the Linux counterpart of winmain.cpp.
//
// Supplies the three things winmain.cpp provides that are not part of any
// backend contract: the process entry point, GetSystemWindow() and
// GetWindowSetup(). The main loop mirrors wWinMain's structure deliberately -
// pump events, poll pads, run a frame, honour a pending game switch - so the
// two stay recognisably the same program.
//
// winmain.cpp is ~1400 lines; this is a fraction of that because most of the
// difference is Win32 message-pump, DPI-awareness and accessibility plumbing
// with no X11 counterpart.
//==============================================================================
#include "sys_window.h"
#include "linux/linux_window.h"

#include "sys_gl.h"
#include "sys_timer.h"
#include "sys_log.h"
#include "iniFile.h"
#include "path_helper.h"
#include "joystick.h"
#include "linux/evdev_input.h"

#include <X11/Xlib.h>   // screen-size probe below; the window owns its own Display

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Emulator entry points (aae_emulator.cpp)
// opengl_renderer.h - declared here rather than including that header,
// which drags in GL and the whole renderer surface for one function.
void emulator_on_window_resize(int newW, int newH);

void emulator_init(int argc, char** argv);
void emulator_run();
void emulator_end();
bool emulator_apply_pending_switch();

// osdepend.h
void osd_led_service_stop();

//------------------------------------------------------------------------------
// The single window and its setup, matching winmain.cpp's ownership model.
//
// winmain.cpp keeps TWO WindowSetup instances - the live one and a windowed
// fallback snapshot for ALT+ENTER restore. That distinction is a Win32
// style/exstyle concern; EWMH fullscreen restores the previous geometry itself,
// so one instance is correct here.
//------------------------------------------------------------------------------
static WindowSetup g_windowSetup;
static LinuxWindow g_window;

WindowSetup& GetWindowSetup()   { return g_windowSetup; }
ISystemWindow& GetSystemWindow() { return g_window; }

//------------------------------------------------------------------------------
// The two windows_util.h entry points that portable code calls. Their Win32
// versions live in system/window/windows_util.cpp; these are the Linux halves.
//------------------------------------------------------------------------------

// aae_emulator.cpp calls this when a game's aspect ratio is known.
//
// The Win32 version also RESIZES the window; this one does not - under a
// compositor the window manager owns geometry, and a client that fights it
// produces a flickering resize loop. The renderer letterboxes to the requested
// aspect inside whatever client area it is given, so the visible result is the
// same. What must NOT be skipped is the viewport re-fit below.
void WindowUtil_UpdateAspect(float gameAspect)
{
	if (gameAspect <= 0.0f) return;

	// Storing the aspect and re-fitting screen_rect is the part that MATTERS -
	// not the window resize. screen_rect is built once inside init_gl's
	// init_one guard, so emulator_on_window_resize is the only thing that
	// re-fits it for a new game; without this call, a game launched from the
	// GUI renders with the GUI's fit.
	GetWindowSetup().aspectRatio = gameAspect;
	emulator_on_window_resize(GetWindowSetup().clientWidth,
	                          GetWindowSetup().clientHeight);

	LOG_INFO("WindowUtil_UpdateAspect(%.4f): aspect stored and viewport re-fitted; "
	         "the window itself is left to the WM on this platform", gameAspect);
}

// Message box equivalent. There is no portable dialog here and AAE has no
// toolkit dependency, so this logs at error level - which is visible on the
// console and in systemlog.txt - rather than silently swallowing the message.
void allegro_message(const char* title, const char* message)
{
	LOG_ERROR("%s: %s", title ? title : "MESSAGE", message ? message : "");
	fprintf(stderr, "%s: %s\n", title ? title : "MESSAGE", message ? message : "");
}

int main(int argc, char** argv)
{
	Log::open("systemlog.txt");
	LOG_INFO("AAE starting (Linux/X11/GLX)");

	SetIniFile(getpathM(nullptr, "aae.ini").c_str());

	// Window geometry from the ini, same keys winmain.cpp reads.
	g_windowSetup.windowWidth  = 1024;
	g_windowSetup.windowHeight = 768;
	g_windowSetup.centerWindow = true;

	// screenRect is the primary monitor's pixel size. run_game() reads it to
	// sanity-check the configured resolution, so it must be populated before
	// emulator_init - winmain.cpp fills it from GetSystemMetrics at the same
	// point. Queried through Xlib here without keeping a display open.
	if (Display* probe = XOpenDisplay(nullptr)) {
		const int scr = DefaultScreen(probe);
		g_windowSetup.screenRect.left   = 0;
		g_windowSetup.screenRect.top    = 0;
		g_windowSetup.screenRect.right  = DisplayWidth(probe, scr);
		g_windowSetup.screenRect.bottom = DisplayHeight(probe, scr);
		XCloseDisplay(probe);
		LOG_INFO("Primary screen: %dx%d",
		         g_windowSetup.screenRect.right, g_windowSetup.screenRect.bottom);
	}

	if (!g_window.Create(g_windowSetup)) {
		LOG_ERROR("Window creation failed - cannot continue");
		Log::close();
		return 1;
	}

	// Core profile, matching winmain.cpp's InitOpenGLContext(false, false, true).
	if (!InitOpenGLContext(false, false, true)) {
		LOG_ERROR("OpenGL context creation failed - cannot continue");
		g_window.Destroy();
		Log::close();
		return 1;
	}

	g_window.EnableCursorClip(true);
	TimerInit();

	// winmain.cpp calls RawInput_Initialize(hwnd) at the equivalent point.
	// This takes no window handle: evdev reads the devices directly rather
	// than routing input through the window, which is what gives per-device
	// multi-HID identity in the first place.
	if (!EvdevInput_Initialize()) {
		LOG_WARN("No input devices could be opened - the game will run but will "
		         "not respond to the keyboard. If devices are present, this is "
		         "almost certainly permissions: sudo usermod -aG input $USER");
	}
	install_joystick();

	emulator_init(argc, argv);

	bool running = true;
	while (running) {
		// X11 events first. PumpEvents returns false on WM_DELETE_WINDOW.
		if (!g_window.PumpEvents())
			break;

		// Input is drained here, on the game thread. The Win32 backend does
		// this on a worker thread because WM_INPUT arrives on the message
		// pump; sys_input.h notes that is a backend choice, not the contract.
		EvdevInput_Poll();
		poll_joystick();
		emulator_run();

		if (emulator_apply_pending_switch())
			continue;
	}

	emulator_end();
	remove_joystick();
	RawInput_Shutdown();
	osd_led_service_stop();
	TimerShutdown();
	DeleteGLContext();
	g_window.Destroy();
	Log::close();

	LOG_INFO("AAE exiting");
	return 0;
}
