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
#ifdef AAE_HAVE_WAYLAND
#include "linux/wayland_window.h"
#endif

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

#include <algorithm> // std::max - window margins below
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
void osd_led_service_start();
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
#ifdef AAE_HAVE_WAYLAND
static WaylandWindow g_waylandWindow;
#endif

// The backend actually in use. Both objects exist for the life of the process
// (neither does anything until Create/Connect), and main() points this at the
// one it selected - X11 until told otherwise, so a GetSystemWindow() call made
// before selection still gets a valid object.
static ISystemWindow* g_activeWindow = &g_window;

WindowSetup& GetWindowSetup()   { return g_windowSetup; }
ISystemWindow& GetSystemWindow() { return *g_activeWindow; }

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

//------------------------------------------------------------------------------
// Display backend selection: native Wayland, or X11 (through XWayland on a
// Wayland desktop, natively elsewhere).
//
//   [main] display = auto | wayland | x11        (default: auto)
//   -wayland / -x11 on the command line override it for one launch, the same
//   way -fullscreen / -windowed override [window] fullscreen.
//
// auto picks Wayland when a Wayland compositor is reachable (WAYLAND_DISPLAY set)
// and we are NOT under gamescope. Steam Game Mode is gamescope: it hosts its own
// XWayland and everything in the session runs inside it, taking frames with no
// copy, so X11 is the proven path there and stays the default. An explicit
// `wayland` is honoured even under gamescope, for trying it.
//
// Whichever is chosen, a Wayland attempt that fails (no socket, a compositor
// missing a required protocol, window creation refused) falls back to X11 with a
// warning that says why, so an unusable Wayland session still gets a window.
//------------------------------------------------------------------------------
enum class DisplayBackend { X11, Wayland };

struct BackendChoice {
	DisplayBackend backend = DisplayBackend::X11;
	std::string    detail;    // why, in the words of the log line
};

static std::string LowerCopy(const char* s)
{
	std::string r = s ? s : "";
	for (char& c : r)
		c = (char)tolower((unsigned char)c);
	return r;
}

#ifdef AAE_HAVE_WAYLAND
// gamescope sets GAMESCOPE_WAYLAND_DISPLAY for its clients, and names itself in
// XDG_CURRENT_DESKTOP in the session it launches. Either is enough.
static bool UnderGamescope()
{
	const char* gs = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	if (gs && *gs) return true;
	return LowerCopy(getenv("XDG_CURRENT_DESKTOP")).find("gamescope") != std::string::npos;
}
#endif

static BackendChoice ChooseDisplayBackend(int argc, char** argv)
{
	BackendChoice c;

	// get_config_string hands back a char*, not a std::string.
	std::string request = LowerCopy(get_config_string("main", "display", "auto"));
	std::string source  = "[main] display=" + request;

	// Last switch wins, and a switch wins over the ini.
	for (int i = 1; i < argc; ++i) {
		if (!argv[i]) continue;
		if      (strcmp(argv[i], "-wayland") == 0) { request = "wayland"; source = "-wayland switch"; }
		else if (strcmp(argv[i], "-x11")     == 0) { request = "x11";     source = "-x11 switch"; }
	}

	if (request != "auto" && request != "wayland" && request != "x11") {
		LOG_WARN("[main] display=%s is not auto, wayland or x11 - using auto", request.c_str());
		request = "auto";
		source  = "auto";
	}

	if (request == "x11") {
		c.detail = source;
		return c;
	}

#ifdef AAE_HAVE_WAYLAND
	if (request == "wayland") {
		c.backend = DisplayBackend::Wayland;
		c.detail  = source;
		return c;
	}

	// auto. Gamescope first: it reports a WAYLAND_DISPLAY of its own, and "no
	// Wayland display" would be the wrong thing to blame.
	if (UnderGamescope()) {
		c.detail = "auto: gamescope detected - the proven Game Mode path; "
		           "[main] display=wayland to try native";
		return c;
	}
	const char* wl = getenv("WAYLAND_DISPLAY");
	if (!wl || !*wl) {
		c.detail = "auto: no WAYLAND_DISPLAY";
		return c;
	}
	c.backend = DisplayBackend::Wayland;
	c.detail  = std::string("auto, WAYLAND_DISPLAY=") + wl;
	return c;
#else
	if (request == "wayland")
		LOG_WARN("Wayland was requested (%s) but this build has no Wayland backend "
		         "(configured with AAE_WAYLAND=OFF, or the dependencies were missing) "
		         "- using X11", source.c_str());
	c.detail = "this build has no Wayland backend";
	return c;
#endif
}

static void LogBackend(const char* name, const std::string& detail)
{
	LOG_INFO("Display backend: %s (%s)", name, detail.c_str());
}

//------------------------------------------------------------------------------
// Default window size and screenRect, per backend.
//------------------------------------------------------------------------------

// X11. screenRect is the primary monitor's pixel size. run_game() reads it to
// sanity-check the configured resolution, so it must be populated before
// emulator_init - winmain.cpp fills it from GetSystemMetrics at the same
// point. Queried through Xlib here without keeping a display open.
static void SizeFromX11(bool explicitSize, int cfgScreenW, int cfgScreenH)
{
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
			// Must match kX11FrameAllowance in linux_window.cpp, which centers the
			// window in the work area with the same title-bar allowance. 32 was
			// short of Mutter's ~37 px title bar.
			const int frameAllowance = 48;
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
}

#ifdef AAE_HAVE_WAYLAND
// Wayland. Must not touch X: this is the path for a session that has none.
//
// Two differences from the X11 sizing above, both because Wayland works in
// LOGICAL units. screenRect stays in PIXELS (it is the monitor's pixel size, which
// run_game() compares against pixel resolutions), but the default window is
// computed and stored in LOGICAL units - pixels / the output's integer scale -
// because that is what a Wayland window is sized in; the framebuffer comes out at
// logical * scale. And there is no work area to read: a Wayland client cannot ask
// where the panel is, so a fixed allowance stands in for panel plus decorations.
// (The integer scale is also all the output reports, so a fractionally scaled
// desktop yields a conservative default - it is only a default, and the window is
// resizable.)
static void SizeFromWayland(bool explicitSize, int cfgScreenW, int cfgScreenH)
{
	int px = 0, py = 0, scale = 1;
	std::string outName;
	const bool haveOutput = g_waylandWindow.QueryFirstOutput(&px, &py, &scale, &outName);
	if (!haveOutput) {
		// A compositor may legitimately report no output (a nested one that has not
		// mapped anything yet). screenRect must still be non-zero, and the 1024x768
		// already in g_windowSetup stands as the logical default.
		px = 1024; py = 768; scale = 1;
	}

	g_windowSetup.screenRect.left   = 0;
	g_windowSetup.screenRect.top    = 0;
	g_windowSetup.screenRect.right  = px;
	g_windowSetup.screenRect.bottom = py;

	const int logicalW = (int)lroundf((float)px / (float)scale);
	const int logicalH = (int)lroundf((float)py / (float)scale);

	if (explicitSize) {
		// screenw/screenh are PIXELS everywhere else, so convert, to land on the
		// framebuffer size that was asked for. Honored literally, as on X11.
		g_windowSetup.windowWidth  = (int)lroundf((float)cfgScreenW / (float)scale);
		g_windowSetup.windowHeight = (int)lroundf((float)cfgScreenH / (float)scale);
		if (g_windowSetup.windowWidth  < 1) g_windowSetup.windowWidth  = 1;
		if (g_windowSetup.windowHeight < 1) g_windowSetup.windowHeight = 1;
	} else if (haveOutput) {
		// Fallback only. WaylandWindow::Create refits this to the compositor's
		// xdg_toplevel.configure_bounds when it sends them (KWin, Mutter, most
		// current compositors); this margin is what is left when it does not. It
		// has to cover a panel PLUS the title bar: 64 did not (GNOME's 32 px top
		// bar + a ~34 px client title bar, Plasma's 44-52 px panel + ~30 px server
		// title bar), which put the bottom of the window under the taskbar. Scaled
		// with the output so a tall display with a scaled-up panel is covered too,
		// and a side margin for a vertical dock (Ubuntu).
		const int heightAllowance = std::max(112, logicalH / 10);
		const int widthAllowance  = std::max(64, logicalW / 20);
		const int usableW = logicalW - widthAllowance;
		int clientH = logicalH - heightAllowance;
		int clientW = (int)lroundf(clientH * g_windowSetup.aspectRatio);
		if (clientW > usableW) {              // too wide: clamp and recompute
			clientW = usableW;
			clientH = (int)lroundf(clientW / g_windowSetup.aspectRatio);
		}
		if (clientW > 0 && clientH > 0) {
			g_windowSetup.windowWidth  = clientW;
			g_windowSetup.windowHeight = clientH;
		}
	}

	// wl_output only carries a name from version 4; plenty of compositors (Weston,
	// so WSLg) send none, and an empty '' in the log reads like a bug.
	const std::string outLabel = !haveOutput ? std::string("(none reported)")
	                           : outName.empty() ? std::string("(unnamed)")
	                           : "'" + outName + "'";
	LOG_INFO("Primary output %s: %dx%d px, scale %d (logical %dx%d) -> "
	         "default window %dx%d logical",
	         outLabel.c_str(), px, py, scale, logicalW, logicalH,
	         g_windowSetup.windowWidth, g_windowSetup.windowHeight);
}
#endif // AAE_HAVE_WAYLAND

int main(int argc, char** argv)
{
	Log::open("systemlog.txt");
	// Build stamp. Without one, "am I running the build you just gave me?"
	// could only be answered by grepping the binary for strings that happened
	// to be new - which wasted a round trip on a 300MB bundle. The window
	// backend is not named here: it is not decided yet, and a banner that says
	// "X11" over a Wayland session is exactly the kind of wrong that costs a
	// debugging session. "Display backend:" below says it once it is.
	LOG_INFO("AAE starting (Linux) - built %s %s", __DATE__, __TIME__);

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
	// This platform's WindowUtil_UpdateAspect never resizes the window, but
	// keep the shared contract's flag truthful all the same.
	g_windowSetup.explicitWindowSize = explicitSize;

	// Which window backend. Decided BEFORE sizing, because the two size the
	// default window differently (see SizeFromX11 / SizeFromWayland), and the
	// Wayland one needs its connection up to ask the compositor about outputs.
	const BackendChoice choice = ChooseDisplayBackend(argc, argv);
	bool useWayland    = (choice.backend == DisplayBackend::Wayland);
	bool backendLogged = false;
#ifdef AAE_HAVE_WAYLAND
	if (useWayland) {
		std::string reason;
		if (!g_waylandWindow.Connect(&reason)) {
			LOG_WARN("Wayland backend unavailable: %s - falling back to X11", reason.c_str());
			useWayland    = false;
			backendLogged = true;
			LogBackend("X11", "fallback: Wayland connect failed; wanted by " + choice.detail);
		}
	}
#endif
	if (!backendLogged)
		LogBackend(useWayland ? "Wayland" : "X11", choice.detail);

	// screenRect and the default window size. screenRect is read by run_game() to
	// sanity-check the configured resolution, so it must be populated before
	// emulator_init.
#ifdef AAE_HAVE_WAYLAND
	if (useWayland)
		SizeFromWayland(explicitSize, cfgScreenW, cfgScreenH);
	else
#endif
		SizeFromX11(explicitSize, cfgScreenW, cfgScreenH);

	// Startup window mode, from the same [window] fullscreen key the Video menu
	// writes on save. Applied by Create() below, which requests EWMH fullscreen
	// only once the window exists - the ordering winmain.cpp had to learn on
	// Windows too (b7073ae).
	//
	// The absence of this read is why fullscreen appeared not to SAVE on Linux.
	// It saved correctly: ToggleBorderlessFullscreen publishes the live state to
	// WindowSetup, and menu.cpp persists it to [window] fullscreen. Nothing ever
	// read it back, so every session started windowed and the setting looked
	// discarded. Windows was unaffected - winmain.cpp has had this read all along.
	//
	// Precedence matches winmain.cpp exactly: [window] fullscreen wins; the
	// legacy [main] windowed is the fallback (windowed=1 meaning fullscreen=0);
	// windowed is the safe default when neither key is present.
	{
		const int winFullscreen = get_config_int("window", "fullscreen", -1);
		const int mainWindowed  = get_config_int("main",   "windowed",   -1);

		if (winFullscreen != -1)
			g_windowSetup.useFullscreen = (winFullscreen != 0);
		else if (mainWindowed != -1)
			g_windowSetup.useFullscreen = (mainWindowed == 0);
		else
			g_windowSetup.useFullscreen = false;

		// The command line wins over the ini, for front-ends that launch AAE
		// with an explicit mode. Applied AFTER the ini read and before Create(),
		// so a switch is a one-launch override that leaves [window] fullscreen
		// untouched - the same contract ParseCommandLineArgs gives on Windows.
		//
		// Only these two of the Win32 window switches are meaningful here.
		// -clip/-noclip are deliberately inert on this platform (capture is
		// unconditional, see the note above) and -disableNC is a Win32 style
		// concern with no X11 counterpart. aae_emulator.cpp's own parser sees
		// the same argv and ignores tokens it does not recognise, so the two
		// need no coordination.
		for (int i = 1; i < argc; ++i) {
			if (!argv[i]) continue;
			if      (strcmp(argv[i], "-fullscreen") == 0) g_windowSetup.useFullscreen = true;
			else if (strcmp(argv[i], "-windowed")   == 0) g_windowSetup.useFullscreen = false;
		}

		LOG_INFO("Startup window mode: %s",
		         g_windowSetup.useFullscreen ? "fullscreen" : "windowed");
	}

	bool windowCreated = false;
#ifdef AAE_HAVE_WAYLAND
	if (useWayland) {
		if (g_waylandWindow.Create(g_windowSetup)) {
			g_activeWindow = &g_waylandWindow;
			windowCreated  = true;
		} else {
			// The connection worked but the window did not (a compositor missing a
			// required protocol, a refused configure). Same remedy as a failed
			// connect: X11, with the reason in the log. The size computed above is
			// in Wayland LOGICAL units, so redo it the X11 way first.
			LOG_WARN("Wayland window creation failed - falling back to X11");
			g_waylandWindow.Destroy();
			useWayland = false;
			LogBackend("X11", "fallback: Wayland window creation failed; wanted by " + choice.detail);
			SizeFromX11(explicitSize, cfgScreenW, cfgScreenH);
		}
	}
#endif
	if (!windowCreated) {
		if (!g_window.Create(g_windowSetup)) {
			LOG_ERROR("Window creation failed - cannot continue");
			Log::close();
			return 1;
		}
		g_activeWindow = &g_window;
	}

	// Renderer selection, decided EARLY like winmain.cpp's EarlyRendererIsVulkan:
	// under Vulkan no GLX/EGL context is created at all (the dispatch layer routes
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
			g_activeWindow->Destroy();
			Log::close();
			return 1;
		}
	} else {
		LOG_INFO("Renderer=vulkan: skipping GL context creation (window layer)");
	}

	g_activeWindow->EnableCursorClip(g_windowSetup.cursorClipEnabled);
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
	// Keyboard LEDs stand in for a cabinet's start-button lamps (asteroid,
	// bwidow, bzone, mhavoc, milliped, omegrace, pacman all drive them). The
	// service must start AFTER EvdevInput_Initialize(): it snapshots each
	// device's current LED state so quitting can put it back, and there are no
	// devices to snapshot before that call.
	//
	// NOT gated by [main] led_service the way winmain.cpp is, and deliberately
	// so: aae.ini is not loaded yet at this point. setup_config() runs from
	// run_a_game(), well after this line, so get_config_int() here would only
	// ever return its own default and the switch would be a lie.
	//
	// Nothing is lost by omitting it. The Windows gate exists because
	// IOCTL_KEYBOARD_SET_INDICATORS aimed at an Ultimarc-style encoder can
	// wedge that machine's whole keyboard input path; an EV_LED write to one
	// evdev node cannot do that, so there is no hazard here to switch off.
	//
	// If a real off switch is wanted on Linux it has to be read somewhere the
	// ini exists. It cannot simply move into the evdev backend: aae_inputtest
	// links that backend but not iniFile.cpp, so the reference would not link.
	osd_led_service_start();

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
		// Window events first. PumpEvents returns false on WM_DELETE_WINDOW (X11),
		// a close request or a lost connection (Wayland).
		if (!g_activeWindow->PumpEvents())
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
	g_activeWindow->Destroy();
	LOG_INFO("Shutdown: complete");
	Log::close();

	LOG_INFO("AAE exiting");
	return 0;
}
