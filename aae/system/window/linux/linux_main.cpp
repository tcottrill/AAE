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
#include "aae_mame_driver.h"   // the global `done` - ESC sets it, see the loop

#include <X11/Xlib.h>   // screen-size probe below; the window owns its own Display
#include <X11/Xatom.h>  // XA_CARDINAL - the _NET_WORKAREA read below

#include <chrono>   // frame-pacing instrumentation in main()
#include <cmath>    // lroundf - aspect sizing below
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
	// Build stamp. Without one, "am I running the build you just gave me?"
	// could only be answered by grepping the binary for strings that happened
	// to be new - which wasted a round trip on a 300MB bundle.
	LOG_INFO("AAE starting (Linux/X11/GLX) - built %s %s", __DATE__, __TIME__);

	SetIniFile(getpathM(nullptr, "aae.ini").c_str());

	// Window geometry: fallback values only. The real default is computed from
	// the screen size below - full size at the configured aspect, centered -
	// matching what winmain.cpp's windowed startup produces on Windows.
	g_windowSetup.windowWidth  = 1024;
	g_windowSetup.windowHeight = 768;
	g_windowSetup.centerWindow = true;

	// Aspect for the default window, same [window] aspect_ratio key winmain.cpp
	// reads, same 4:3 fallback.
	{
		std::string aspect = get_config_string("window", "aspect_ratio", "4:3");
		int ax = 0, ay = 0;
		if (sscanf(aspect.c_str(), "%d:%d", &ax, &ay) != 2 || ax <= 0 || ay <= 0) {
			LOG_INFO("Invalid aspect ratio: %s - defaulting to 4:3", aspect.c_str());
			ax = 4; ay = 3;
		}
		g_windowSetup.aspectRatio = (float)ax / (float)ay;
	}

	// Mouse capture: ALWAYS on at startup, matching winmain.cpp. The cursor_clip
	// ini key and -clip/-noclip switches are deliberately not consulted - a stale
	// cursor_clip=0 made sessions start with a visible, unconfined pointer, and
	// capture-by-default with no exceptions is the product decision (2026-07-29).
	// F9 releases at runtime and a click on the window recaptures.
	//
	// Set BEFORE Create(), which seeds its own capture state from WindowSetup.
	g_windowSetup.cursorClipEnabled = true;

	// Explicit window size, same [main] screenw/screenh contract as Windows:
	// both positive = exact client size, 0 (the default) = AUTO, which keeps
	// the computed work-area fit below. The WM may still clamp what we ask for.
	const int cfgScreenW = get_config_int("main", "screenw", 0);
	const int cfgScreenH = get_config_int("main", "screenh", 0);
	const bool explicitSize = (cfgScreenW > 0 && cfgScreenH > 0);

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

		// Default window size: the largest window at the configured aspect that
		// fits the usable screen, centered - the Linux counterpart of
		// GetCenteredAspectWindowSetup(), which is what every windowed Windows
		// startup goes through. Prefer _NET_WORKAREA so panels/taskbars are
		// excluded; fall back to the full screen where the WM does not set it
		// (gamescope, bare WSLg).
		long workW = DisplayWidth(probe, scr);
		long workH = DisplayHeight(probe, scr);
		Atom workAtom = XInternAtom(probe, "_NET_WORKAREA", True);
		if (workAtom != None) {
			Atom type = None; int format = 0;
			unsigned long nitems = 0, after = 0;
			unsigned char* data = nullptr;
			if (XGetWindowProperty(probe, RootWindow(probe, scr), workAtom, 0, 4,
			                       False, XA_CARDINAL, &type, &format,
			                       &nitems, &after, &data) == Success && data) {
				if (type == XA_CARDINAL && format == 32 && nitems >= 4) {
					const long* wa = reinterpret_cast<const long*>(data);
					if (wa[2] > 0 && wa[3] > 0) { workW = wa[2]; workH = wa[3]; }
				}
				XFree(data);
			}
		}

		// Windows subtracts the real frame size here; X11 cannot know it before
		// the window is mapped (_NET_FRAME_EXTENTS arrives afterwards), so a
		// fixed allowance stands in for the title bar.
		//
		// Only for AUTO - an explicit screenw/screenh is honored literally,
		// matching GetClassicWindowSetup on Windows.
		if (explicitSize) {
			g_windowSetup.windowWidth  = cfgScreenW;
			g_windowSetup.windowHeight = cfgScreenH;
		} else {
			const int frameAllowance = 32;
			int clientH = (int)workH - frameAllowance;
			int clientW = (int)lroundf(clientH * g_windowSetup.aspectRatio);
			if (clientW > (int)workW) {           // too wide: clamp and recompute
				clientW = (int)workW;
				clientH = (int)lroundf(clientW / g_windowSetup.aspectRatio);
			}
			if (clientW > 0 && clientH > 0) {
				g_windowSetup.windowWidth  = clientW;
				g_windowSetup.windowHeight = clientH;
			}
		}

		XCloseDisplay(probe);
		LOG_INFO("Primary screen: %dx%d, work area %ldx%ld -> default window %dx%d",
		         g_windowSetup.screenRect.right, g_windowSetup.screenRect.bottom,
		         workW, workH,
		         g_windowSetup.windowWidth, g_windowSetup.windowHeight);
	}

	if (!g_window.Create(g_windowSetup)) {
		LOG_ERROR("Window creation failed - cannot continue");
		Log::close();
		return 1;
	}

	// Renderer selection, decided EARLY like winmain.cpp's EarlyRendererIsVulkan:
	// under Vulkan no GLX context is created at all (the dispatch layer routes
	// every gl*chain call to vkchain, and ViewOrtho is a core-profile no-op, so
	// nothing touches GL without a context - proven by the Windows VK path,
	// which has skipped context creation since Plan 2). This also means a GLX
	// failure can no longer kill a Vulkan session that never needed GL.
	// Default matches config.cpp: vulkan. The cmdline can override per launch.
	bool wantVulkan = true;
	{
		std::string r = get_config_string("main", "renderer", "vulkan");
		if (r == "opengl")
			wantVulkan = false;
		for (int i = 1; i < argc - 1; ++i) {
			if (strcmp(argv[i], "-renderer") == 0) {
				if (strcmp(argv[i + 1], "vulkan") == 0)      wantVulkan = true;
				else if (strcmp(argv[i + 1], "opengl") == 0) wantVulkan = false;
			}
		}
	}

	if (!wantVulkan) {
		// Core profile, matching winmain.cpp's InitOpenGLContext(false, false, true).
		if (!InitOpenGLContext(false, false, true)) {
			LOG_ERROR("OpenGL context creation failed - cannot continue");
			g_window.Destroy();
			Log::close();
			return 1;
		}
	} else {
		LOG_INFO("Renderer=vulkan: skipping GLX context creation (window layer)");
	}

	g_window.EnableCursorClip(g_windowSetup.cursorClipEnabled);
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

	// Frame-pacing instrumentation. Measured rather than guessed at: the
	// stutter has already survived one fix aimed at a suspected cause, so the
	// next move is to find out what the frame intervals actually are instead
	// of proposing a third theory.
	//
	// Counters only - no logging in the loop, because a per-frame log is a
	// plausible cause of the very thing being measured.
	using DiagClock = std::chrono::steady_clock;
	auto     lastFrame   = DiagClock::now();
	auto     runStart    = lastFrame;
	int64_t  frameCount  = 0;
	int64_t  hitchCount  = 0;      // frames taking >1.5x the target
	int64_t  worstUs     = 0;
	double   hitchSumSec = 0.0;    // for the mean gap BETWEEN hitches
	double   lastHitchAt = -1.0;
	int64_t  hitchGaps   = 0;

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

		{
			const auto now = DiagClock::now();
			const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
			                       now - lastFrame).count();
			lastFrame = now;
			++frameCount;

			// 1.5x of a 60Hz frame. Deliberately generous: this is looking for
			// the 1-2 frame stalls being reported, not ordinary jitter.
			if (frameCount > 60 && us > 25000) {
				++hitchCount;
				if (us > worstUs) worstUs = us;

				const double at = std::chrono::duration<double>(now - runStart).count();
				if (lastHitchAt >= 0.0) {
					hitchSumSec += (at - lastHitchAt);
					++hitchGaps;
				}
				lastHitchAt = at;
			}
		}

		if (emulator_apply_pending_switch())
			continue;

		// THE quit condition, and it was missing.
		//
		// ESC does not close the window - it sets the global `done`, after
		// which emulator_run() returns immediately on every call. winmain.cpp
		// gates its whole loop on this (`while (!done)`, plus the same check
		// after a pending switch); this loop only ever broke on
		// WM_DELETE_WINDOW.
		//
		// So pressing ESC left the emulator running here with nothing to do
		// until the window was closed by hand, which from outside is
		// indistinguishable from a hang on exit.
		if (done)
			break;
	}

	{
		const double elapsed = std::chrono::duration<double>(
		                           DiagClock::now() - runStart).count();
		LOG_INFO("Frame pacing: %lld frames in %.1fs (%.1f fps avg), %lld hitches "
		         ">25ms, worst %.1f ms, mean gap between hitches %.2f s",
		         (long long)frameCount, elapsed,
		         elapsed > 0.0 ? (double)frameCount / elapsed : 0.0,
		         (long long)hitchCount, (double)worstUs / 1000.0,
		         hitchGaps ? hitchSumSec / (double)hitchGaps : 0.0);
	}

	// Breadcrumbs through teardown. A hang on exit leaves no window, no error
	// and no stack - just a process that will not die - and the only thing
	// that distinguishes "the audio thread will not join" from "GL teardown
	// blocked" is which of these was the last line written. Seven lines at
	// shutdown cost nothing and turn a force-kill into a diagnosis.
	LOG_INFO("Shutdown: emulator_end...");
	emulator_end();
	LOG_INFO("Shutdown: remove_joystick...");
	remove_joystick();
	LOG_INFO("Shutdown: input...");
	RawInput_Shutdown();
	LOG_INFO("Shutdown: led service...");
	osd_led_service_stop();
	LOG_INFO("Shutdown: timer...");
	TimerShutdown();
	LOG_INFO("Shutdown: GL context...");
	DeleteGLContext();
	LOG_INFO("Shutdown: window...");
	g_window.Destroy();
	LOG_INFO("Shutdown: complete");
	Log::close();

	LOG_INFO("AAE exiting");
	return 0;
}
