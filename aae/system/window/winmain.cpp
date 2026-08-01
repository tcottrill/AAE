// -----------------------------------------------------------------------------
// winmain.cpp
//
// Main entry point and window management for the game engine.
// Includes full Win32 Unicode support, DPI awareness, Raw Input handling,
// OpenGL context creation, joystick initialization, INI and command-line
// parsing, and high-speed main loop management.
//
// Features:
// - UTF-8 compatible logging and message dialogs
// - Customizable window configuration via INI and command-line
// - Borderless fullscreen and aspect ratio-locked modes
// - Mouse cursor clipping and hiding support
// - DPI awareness with Windows 10/11 feature handling
// - Safe accessibility feature suppression (StickyKeys, etc.)
// - Modular game entry and exit flow: game_init(), game_run(), game_end()
// - Robust joystick support with fallback logging
// - Clean separation of platform-specific responsibilities
// - Multi-monitor support: starting_monitor config key and -monitor N cmdline flag
//
// Notes:
// - Uses CreateConfiguredWindow() and WindowSetup for centralized window creation
// - Requires utf8conv.h, sys_log.h, gl_basics.h, rawinput_win32.h
// - Supports ALT+ENTER toggle to fullscreen
// - Supports resize and aspect enforcement via WM_SIZING/WM_SIZE
// - Saves valid client and window rects for FBO scaling and restoration
// - starting_monitor is 1-based: 1 = primary, 2 = second monitor, etc.
//   Values <= 0 or out of range fall back to primary with a log warning.
//
// -----------------------------------------------------------------------------

#include <windows.h>
#include <string>
#include <cstdio>
#include "iniFile.h"
#include "path_helper.h"
#include "sys_log.h"
#include "utf8conv.h"
#include "rawinput_win32.h"
#include "sys_gl.h"
#include "win32/win32_private.h"
#include "win32/win32_window.h"
#include "aae_emulator.h"
#include "resource.h"
#include "joystick.h"
#include "sys_timer.h"
#ifndef WIN7BUILD
#include "win10_win11_required_code.h"
#endif
#include "aae_mame_driver.h"  // for global 'done'
#include "windows_util.h"
#include "opengl_renderer.h"
#include "led_service_handler.h"

// Vulkan surface creation for the Win32 window backend (Phase 4a Plan 2).
// Uses the vendored headers; binds the two needed entry points from the
// runtime loader so nothing links vulkan-1.lib (spec sec. 6).
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

// Forward declaration: Win32Window::Create() (below) registers the window
// class with this WndProc, same as wWinMain does; the definition itself
// comes later in this file.
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
HWND g_hWnd = nullptr;

// True once InitOpenGLContext() has succeeded this session. Renderer=vulkan
// sessions never set this (see wWinMain's GL-bring-up gate), so every other
// GL/WGL call site in this file guards on it to stay a no-op under Vulkan.
static bool g_glContextCreated = false;

// Initialize the audio mixer
const int audioSampleRate = 44100;
const int targetFPS = 60;

WindowSetup g_windowSetup;

// Accessors

WindowSetup& GetWindowSetup() {
	return g_windowSetup;
}

// The Win32-only counterpart to g_windowSetup (style/exStyle/disableNC/
// disableRoundedCorners). Kept as a separate singleton so sys_window.h can
// stay platform-neutral; see win32/win32_private.h.
Win32WindowState g_win32WindowState;

Win32WindowState& GetWin32WindowState() {
	return g_win32WindowState;
}

// -----------------------------------------------------------------------------
// Win32Window - the ISystemWindow implementation.
//
// Method bodies here are re-homed free functions that used to live in this
// file (see win32_window.h for why they stay in this translation unit rather
// than a separate win32_window.cpp): ClientWidth/ClientHeight replace the old
// GetClientWidth/GetClientHeight, and RestoreViewport replaces the old
// RestoreWindowViewport(), unchanged logic throughout.
// -----------------------------------------------------------------------------
int Win32Window::ClientWidth() const {
	return g_windowSetup.clientWidth;
}

int Win32Window::ClientHeight() const {
	return g_windowSetup.clientHeight;
}

float Win32Window::DpiScale() const {
	return g_windowSetup.dpiScale;
}

void Win32Window::RestoreViewport()
{
	// No-op under Vulkan: there is no GL context/viewport to restore, and
	// WM_SIZE fires long before a renderer-agnostic resize hook exists here.
	if (g_glContextCreated)
		glViewport(0, 0, g_windowSetup.clientWidth, g_windowSetup.clientHeight);
}

// When starting in fullscreen, we still need a valid windowed-mode target
// (style/exstyle/rect) for restoring on ALT+ENTER.
static WindowSetup g_windowedFallbackSetup;
// Win32-only counterpart to g_windowedFallbackSetup, for the same reason
// g_win32WindowState pairs with g_windowSetup: the live window's style and
// this fallback's style are two independent values that must not alias the
// same singleton.
static Win32WindowState g_windowedFallbackWin32State;

// -----------------------------------------------------------------------------
// Monitor helpers
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Win32_GetNearestMonitorRect
// Returns the monitor rect for the monitor nearest to the given window.
// Used at runtime when toggling fullscreen so we stay on the same monitor.
// -----------------------------------------------------------------------------
static RECT Win32_GetNearestMonitorRect(HWND hwnd)
{
	if (!hwnd) return RECT{ 0,0,0,0 };
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (mon && GetMonitorInfo(mon, &mi)) return mi.rcMonitor;
	return RECT{ 0,0,0,0 };
}

// -----------------------------------------------------------------------------
// Win32_GetPrimaryMonitorRect
// Returns the RECT of the primary monitor (the one containing point 0,0).
// -----------------------------------------------------------------------------
static RECT Win32_GetPrimaryMonitorRect()
{
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	if (mon && GetMonitorInfo(mon, &mi)) return mi.rcMonitor;
	return RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
}

// -----------------------------------------------------------------------------
// MonitorEnumData
// Helper struct used by the EnumDisplayMonitors callback below.
// Carries the 1-based target index and accumulates results during enumeration.
// -----------------------------------------------------------------------------
struct MonitorEnumData {
	int targetIndex;   // 1-based index we are looking for
	int currentIndex;  // counts up as monitors are enumerated
	RECT foundRect;    // filled in when we find the target
	bool found;
};

// -----------------------------------------------------------------------------
// MonitorEnumProc
// EnumDisplayMonitors callback. Counts monitors and captures the rect of the
// one matching targetIndex (1-based).
// -----------------------------------------------------------------------------
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
{
	MonitorEnumData* data = reinterpret_cast<MonitorEnumData*>(dwData);
	data->currentIndex++;

	if (data->currentIndex == data->targetIndex)
	{
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(hMonitor, &mi))
		{
			data->foundRect = mi.rcMonitor;
			data->found = true;
		}
		// Return FALSE to stop enumeration early once found
		return FALSE;
	}
	// Keep enumerating
	return TRUE;
}

// -----------------------------------------------------------------------------
// Win32_GetMonitorRectByIndex
// Returns the monitor rect for a 1-based monitor index.
//   index 1 = primary monitor (enumeration order, primary is usually first)
//   index 2 = second monitor, etc.
// If the index is out of range or enumeration fails, logs a warning and
// returns the primary monitor rect as a fallback.
// Note: Windows enumerates monitors in an order that typically places the
// primary monitor first, but this is not strictly guaranteed. For most
// desktop setups this works as expected.
// -----------------------------------------------------------------------------
static RECT Win32_GetMonitorRectByIndex(int index)
{
	// Clamp values <= 0 up to 1 (treat as primary)
	if (index <= 0)
		index = 1;

	MonitorEnumData data = {};
	data.targetIndex  = index;
	data.currentIndex = 0;
	data.found        = false;
	data.foundRect    = Win32_GetPrimaryMonitorRect(); // default if not found

	EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));

	if (!data.found)
	{
		LOG_WARN("starting_monitor %d is out of range -- falling back to primary monitor", index);
		return Win32_GetPrimaryMonitorRect();
	}

	LOG_INFO("Monitor %d selected: (%d,%d)-(%d,%d)",
		index,
		data.foundRect.left, data.foundRect.top,
		data.foundRect.right, data.foundRect.bottom);

	return data.foundRect;
}

// -----------------------------------------------------------------------------
// Win32_GetWorkAreaForMonitorRect
// Given a monitor rect (from GetMonitorInfo.rcMonitor), returns the work area
// (monitor rect minus taskbar) for that monitor.
// Falls back to the monitor rect itself if the query fails.
// This is used by windowed-mode setup functions to avoid placing the window
// under the taskbar on the target monitor.
// -----------------------------------------------------------------------------
static RECT Win32_GetWorkAreaForMonitorRect(const RECT& monitorRect)
{
	// Find the monitor handle for the center point of the given rect
	POINT center = {
		monitorRect.left + (monitorRect.right  - monitorRect.left) / 2,
		monitorRect.top  + (monitorRect.bottom - monitorRect.top)  / 2
	};
	HMONITOR hMon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	if (hMon && GetMonitorInfo(hMon, &mi))
		return mi.rcWork;
	// Fallback: use the full monitor rect
	return monitorRect;
}

// -----------------------------------------------------------------------------
// Helper: Update Window Title with Mouse State
// -----------------------------------------------------------------------------
void UpdateWindowTitle()
{
	if (!g_hWnd) return;

	std::wstring title = L"AAE Emulator";

	// Only append the suffix if the feature is enabled
	if (g_windowSetup.cursorClipEnabled) {
		title += L" - Mouse Captured (F9 to release)";
	}

	SetWindowTextW(g_hWnd, title.c_str());
}

// -----------------------------------------------------------------------------
// Centralized Cursor State Logic
// Adapted from WindowCode.cpp + Framework integration
// -----------------------------------------------------------------------------
void UpdateCursorState()
{
	// 1. Update the window title to reflect the current mode
	UpdateWindowTitle();

	// 2. Manage input pausing based on focus and minimize state
	bool isActive = g_windowSetup.isFocused && !g_windowSetup.isMinimized;
	RawInput_SetPaused(!isActive);

	// 3. Check Global Override first
	// If the user passed -noclip or set ini[window] cursor_clip=0
	if (!g_windowSetup.cursorClipEnabled) {
		ClipCursor(nullptr);
		while (ShowCursor(TRUE) < 0);
		return;
	}

	// 4. Determine Desired State
	// We capture if: We have Focus AND we are not Minimized.
	bool shouldCapture = isActive;

	// 5. Brute Force Visibility
	// Windows uses a counter for ShowCursor. Loop until we hit the state we want.
	if (shouldCapture) {
		while (ShowCursor(FALSE) >= 0); // Hide
	}
	else {
		while (ShowCursor(TRUE) < 0);   // Show
	}

	// 6. Handle Clipping (The "Trap")
	if (shouldCapture && g_hWnd) {
		RECT rect;
		GetClientRect(g_hWnd, &rect);

		// Convert Client (0,0) to Screen Coordinates for ClipCursor
		POINT tl = { rect.left, rect.top };
		POINT br = { rect.right, rect.bottom };
		ClientToScreen(g_hWnd, &tl);
		ClientToScreen(g_hWnd, &br);

		RECT clipRect = { tl.x, tl.y, br.x, br.y };
		ClipCursor(&clipRect);
	}
	else {
		ClipCursor(nullptr);
	}
}

// -----------------------------------------------------------------------------
// Win32Window - cursor control.
//
// EnableCursorClip/ForceCursorClipUpdate/SetMousePos/GetMousePos are the same
// bodies as the old framework.h free functions of the same name (SetMousePos/
// GetMousePos adapted from an HWND parameter to the interface's g_hWnd/out-
// param signatures). ClipAndHideCursor/UnclipAndShowCursor were already
// explicitly deprecated aliases for EnableCursorClip(true/false) with zero
// callers anywhere in the tree - deleted rather than ported, since
// ISystemWindow has no equivalent slot for them.
// -----------------------------------------------------------------------------

void Win32Window::EnableCursorClip(bool enable)
{
	g_windowSetup.cursorClipEnabled = enable;
	UpdateCursorState();
}

void Win32Window::ForceCursorClipUpdate()
{
	UpdateCursorState();
}

// No prior standalone body existed for this one - cursor visibility used to
// be an inline side effect of UpdateCursorState's capture logic. Reuses the
// same ShowCursor-counter idiom used there.
void Win32Window::SetCursorVisible(bool visible)
{
	if (visible) { while (ShowCursor(TRUE)  < 0); }
	else         { while (ShowCursor(FALSE) >= 0); }
}

// -----------------------------------------------------------------------------
// SetMousePos
// Sets the mouse cursor position relative to the client area of the live window.
// Converts to screen coordinates before applying.
// -----------------------------------------------------------------------------
void Win32Window::SetMousePos(int x, int y)
{
	POINT pos = { x, y };
	ClientToScreen(g_hWnd, &pos);
	SetCursorPos(pos.x, pos.y);
}

// -----------------------------------------------------------------------------
// GetMousePos
// Returns the current mouse cursor position relative to the client area
// of the live window. If conversion fails, returns {0,0}.
// -----------------------------------------------------------------------------
void Win32Window::GetMousePos(int* x, int* y) const
{
	POINT p{};
	if (GetCursorPos(&p))
		ScreenToClient(g_hWnd, &p);
	if (x) *x = p.x;
	if (y) *y = p.y;
}

void GetWindowFrameSize(DWORD style, DWORD exStyle, int& frameW, int& frameH)
{
	RECT tmp = { 0, 0, 100, 100 }; // dummy client rect
	if (!AdjustWindowRectEx(&tmp, style, FALSE, exStyle)) {
		frameW = frameH = 0;
		return;
	}
	frameW = (tmp.right - tmp.left) - 100;
	frameH = (tmp.bottom - tmp.top) - 100;
}

// -----------------------------------------------------------------------------
// GetBorderlessFullscreenSetup
// Builds a WindowSetup for borderless fullscreen on the specified monitor.
// targetMonitor: the screen rect of the desired monitor, from
//   Win32_GetMonitorRectByIndex() or Win32_GetPrimaryMonitorRect().
// At startup, the target is chosen from config.starting_monitor. At runtime
// when the user presses ALT+ENTER, Win32_GetNearestMonitorRect(hwnd) is used
// instead so we stay on whichever monitor the window is currently on.
// -----------------------------------------------------------------------------
WindowSetup GetBorderlessFullscreenSetup(const RECT& targetMonitor, Win32WindowState& win32Out)
{
	WindowSetup ws;
	win32Out.style   = WS_POPUP;
	win32Out.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	// Use the caller-supplied monitor rect
	ws.rect = FromWin32Rect(targetMonitor);

	ws.borderlessFullscreen = true;
	return ws;
}

// -----------------------------------------------------------------------------
// GetClassicWindowSetup
// Builds a WindowSetup for a plain overlapped window, centered on the
// specified monitor's work area (the area excluding the taskbar).
// targetMonitor: the monitor rect to center on.
// -----------------------------------------------------------------------------
WindowSetup GetClassicWindowSetup(int width, int height, bool center, const RECT& targetMonitor, Win32WindowState& win32Out)
{
	WindowSetup ws;
	win32Out.style   = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	win32Out.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	RECT wr = { 0, 0, width, height };
	AdjustWindowRect(&wr, win32Out.style, FALSE);

	int windowW = wr.right  - wr.left;
	int windowH = wr.bottom - wr.top;

	int x = CW_USEDEFAULT;
	int y = CW_USEDEFAULT;

	if (center) {
		// Get the work area for the target monitor so we avoid the taskbar
		RECT workArea = Win32_GetWorkAreaForMonitorRect(targetMonitor);

		int screenW = workArea.right  - workArea.left;
		int screenH = workArea.bottom - workArea.top;

		x = workArea.left + (screenW - windowW) / 2;
		y = workArea.top  + (screenH - windowH) / 2;
	}

	ws.rect.left   = x;
	ws.rect.top    = y;
	ws.rect.right  = x + windowW;
	ws.rect.bottom = y + windowH;

	ws.windowWidth  = width;
	ws.windowHeight = height;

	// Force override the aspect ratio to the one we are using at window creation time
	ws.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	LOG_INFO("Classic Window Aspect at start, %f", ws.aspectRatio);
	ws.resizable = true;
	return ws;
}

// -----------------------------------------------------------------------------
// GetCenteredAspectWindowSetup
// Builds a WindowSetup that fills the target monitor's work area while
// maintaining the given aspect ratio.
// targetMonitor: the monitor rect to size and center the window against.
// -----------------------------------------------------------------------------
WindowSetup GetCenteredAspectWindowSetup(float aspectRatio, bool disableNC, const RECT& targetMonitor, Win32WindowState& win32Out)
{
	WindowSetup ws;
	win32Out.style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	win32Out.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	// Get the work area for the target monitor (excludes taskbar on that monitor)
	RECT workArea = Win32_GetWorkAreaForMonitorRect(targetMonitor);

	int screenW = workArea.right  - workArea.left;
	int screenH = workArea.bottom - workArea.top;

	// Optional correction for Win11 visual padding
	if (GetOsVersion() == Win11 && !disableNC) {
		workArea.left   -= 7;
		workArea.right  += 14;
		workArea.bottom += 7;
		screenW = workArea.right  - workArea.left;
		screenH = workArea.bottom - workArea.top;
	}

	// Compute window frame size for style
	int frameW = 0, frameH = 0;
	GetWindowFrameSize(win32Out.style, win32Out.exStyle, frameW, frameH);

	// Determine maximum client height we can use
	int maxClientH = screenH - frameH;
	int clientH    = maxClientH;
	int clientW    = static_cast<int>(roundf(clientH * aspectRatio));

	// If too wide, clamp width and recompute height
	if (clientW + frameW > screenW) {
		clientW = screenW - frameW;
		clientH = static_cast<int>(roundf(clientW / aspectRatio));
	}

	// Final full window dimensions
	int windowW = clientW + frameW;
	int windowH = clientH + frameH;

	// Center the full window on the target monitor's work area
	int x = workArea.left + (screenW - windowW) / 2;
	int y = workArea.top  + (screenH - windowH) / 2;

	ws.rect.left   = x;
	ws.rect.top    = y;
	ws.rect.right  = x + windowW;
	ws.rect.bottom = y + windowH;

	ws.windowWidth  = clientW;
	ws.windowHeight = clientH;
	ws.aspectRatio  = aspectRatio;
	win32Out.disableNC = disableNC;

	return ws;
}

// -----------------------------------------------------------------------------
// LoadWindowIniConfig
// Reads window and monitor settings from aae.ini into the given WindowSetup.
// This runs early (before emulator_init), so it reads starting_monitor
// directly from INI rather than relying on the main config system.
// -----------------------------------------------------------------------------
void LoadWindowIniConfig(WindowSetup& config, Win32WindowState& win32Config)
{
	// Always point at the real aae.ini in the exe/root folder.
	std::string iniPath = getpathM(0, "aae.ini");
	SetIniFile(iniPath.c_str());

	// -----------------------------
	// 1) Prefer [window] if present
	// -----------------------------
	const int win_fullscreen = get_config_int("window", "fullscreen", -1);
	const int win_width      = get_config_int("window", "width", -1);
	const int win_height     = get_config_int("window", "height", -1);

	// Legacy keys saved by the menu/config system
	const int main_windowed = get_config_int("main", "windowed", -1);
	const int main_screenw  = get_config_int("main", "screenw", -1);
	const int main_screenh  = get_config_int("main", "screenh", -1);

	// Fullscreen/windowed selection:
	// - If [window].fullscreen exists, use it.
	// - Else fall back to [main].windowed (existing saved value).
	if (win_fullscreen != -1) {
		config.useFullscreen = (win_fullscreen != 0);
	}
	else if (main_windowed != -1) {
		config.useFullscreen = (main_windowed == 0); // windowed=1 => fullscreen=false
	}
	else {
		config.useFullscreen = false; // safe default
	}

	config.centerWindow   = get_config_int("window", "center", 1) != 0;
	config.useAspectRatio = get_config_int("window", "use_aspect", 0) != 0;
	config.aspectOverrideActive = config.useAspectRatio;
	win32Config.disableNC = get_config_int("window", "disable_nc", 0) != 0;

	// Width/Height selection:
	// - Prefer [window].width/height if present
	// - Else fall back to [main].screenw/screenh (what the menu saves)
	if (win_width  != -1) config.windowWidth  = win_width;
	else if (main_screenw != -1) config.windowWidth  = main_screenw;
	else config.windowWidth  = 1024;

	if (win_height != -1) config.windowHeight = win_height;
	else if (main_screenh != -1) config.windowHeight = main_screenh;
	else config.windowHeight = 768;

	win32Config.disableRoundedCorners = get_config_bool("window", "disable_rounded_corners", false);
	config.dpiAware              = get_config_bool("window", "dpi_aware", true);
	config.cursorClipEnabled     = get_config_bool("window", "cursor_clip", true);

	// Aspect ratio string stays in [window] for now (fallback default is fine)
	std::string aspect = get_config_string("window", "aspect_ratio", "4:3");
	int ax = 0, ay = 0;
	if (sscanf_s(aspect.c_str(), "%d:%d", &ax, &ay) != 2 || ax == 0 || ay == 0) {
		LOG_INFO("Invalid aspect ratio: %s - defaulting to 4:3", aspect.c_str());
		ax = 4; ay = 3;
	}
	config.aspectRatio = (float)ax / (float)ay;

	// -----------------------------
	// Monitor selection
	// -----------------------------
	// Read from [main] starting_monitor (1-based, 1 = primary).
	// This is loaded here directly because window setup runs before emulator_init.
	// The value is also stored in config.starting_monitor by setup_config() later,
	// but we need it now for window placement.
	config.startingMonitor = get_config_int("main", "starting_monitor", 1);
	if (config.startingMonitor <= 0)
		config.startingMonitor = 1;

	LOG_INFO("Window config from %s: useFullscreen=%d window=%dx%d monitor=%d",
		iniPath.c_str(),
		config.useFullscreen ? 1 : 0,
		config.windowWidth, config.windowHeight,
		config.startingMonitor);
}

// -----------------------------------------------------------------------------
// ParseCommandLineArgs
// Applies command-line overrides to the given WindowSetup.
// -monitor N   sets the starting monitor (1-based, overrides INI)
// All other flags work as before.
// -----------------------------------------------------------------------------
void ParseCommandLineArgs(WindowSetup& config, Win32WindowState& win32Config)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) return;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring arg = argv[i];
		if      (arg == L"-fullscreen")    config.useFullscreen  = true;
		else if (arg == L"-windowed")      config.useFullscreen  = false;
		else if (arg == L"-nocenter")      config.centerWindow   = false;
		else if (arg == L"-aspectwindow")  config.useAspectRatio = true;
		else if (arg == L"-disableNC")     win32Config.disableNC = true;
		else if (arg == L"-noclip")        config.cursorClipEnabled = false;
		else if (arg == L"-clip")          config.cursorClipEnabled = true;
		else if (arg == L"-aspect" && i + 1 < argc) {
			std::wstring wide = argv[++i];
			std::string val = win32::Utf16ToUtf8(wide);
			int ax = 0, ay = 0;
			if (sscanf_s(val.c_str(), "%d:%d", &ax, &ay) == 2 && ax > 0 && ay > 0)
			{
				config.aspectRatio = (float)ax / (float)ay;
				config.aspectOverrideActive = true;
				config.useAspectRatio = true;
			}
		}
		else if (arg == L"-width"  && i + 1 < argc)
			config.windowWidth  = _wtoi(argv[++i]);
		else if (arg == L"-height" && i + 1 < argc)
			config.windowHeight = _wtoi(argv[++i]);
		else if (arg == L"-monitor" && i + 1 < argc) {
			// 1-based monitor index; 0 or negative treated as 1 (primary)
			int mon = _wtoi(argv[++i]);
			if (mon <= 0) mon = 1;
			config.startingMonitor = mon;
			LOG_INFO("Command-line override: starting_monitor = %d", config.startingMonitor);
		}
	}

	LocalFree(argv);
}

// -----------------------------------------------------------------------------
// EarlyRendererIsVulkan
// The GL context must be skipped BEFORE config.renderer is populated (that
// happens later, inside run_game's setup_config). Read the same two sources
// the dispatch reads - [main] renderer in aae.ini, then a -renderer cmdline
// override - so the window layer and the dispatch always agree.
// -----------------------------------------------------------------------------
static bool EarlyRendererIsVulkan(void)
{
	bool vulkan = false;
	const char* r = get_config_string("main", "renderer", "opengl");
	if (r && strcmp(r, "vulkan") == 0)
		vulkan = true;

	// Cmdline override, same precedence as the dispatch.
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		for (int i = 1; i < argc - 1; ++i)
		{
			if (_wcsicmp(argv[i], L"-renderer") == 0)
			{
				if (_wcsicmp(argv[i + 1], L"vulkan") == 0)      vulkan = true;
				else if (_wcsicmp(argv[i + 1], L"opengl") == 0) vulkan = false;
			}
		}
		LocalFree(argv);
	}
	return vulkan;
}

// -----------------------------------------------------------------------------
// GenerateFinalWindowSetup
// Resolves the final window position, style, and size by:
//   1. Reading INI config
//   2. Resolving the target monitor rect from config.startingMonitor
//   3. Computing windowed fallback on the same monitor (for ALT+ENTER restore)
//   4. Applying command-line overrides
//   5. Returning the appropriate WindowSetup for the chosen mode
//
// forceWindowed: when true, always returns a windowed setup regardless of
//   config.useFullscreen. Used at startup so we have a valid windowed rect
//   before toggling to fullscreen (see wWinMain).
// -----------------------------------------------------------------------------
WindowSetup GenerateFinalWindowSetup(bool forceWindowed = false)
{
	WindowSetup config;
	Win32WindowState win32Config;
	LoadWindowIniConfig(config, win32Config);

	// Resolve which monitor to target based on the index read from INI.
	// Command-line will be applied next and may override startingMonitor,
	// so we re-resolve after ParseCommandLineArgs if needed.
	// For the windowed fallback we must compute this before ParseCommandLineArgs
	// because the fallback is needed for ALT+ENTER restore.
	RECT targetMonitor = Win32_GetMonitorRectByIndex(config.startingMonitor);

	// Always compute a windowed-mode fallback so ALT+ENTER restore works even
	// when the app starts in fullscreen. Place the fallback on the same monitor.
	// g_windowedFallbackWin32State captures the fallback's style/exStyle/disableNC
	// independently of the live g_win32WindowState computed below.
	if (config.useAspectRatio)
		g_windowedFallbackSetup = GetCenteredAspectWindowSetup(config.aspectRatio, win32Config.disableNC, targetMonitor, g_windowedFallbackWin32State);
	else
		g_windowedFallbackSetup = GetClassicWindowSetup(config.windowWidth, config.windowHeight, config.centerWindow, targetMonitor, g_windowedFallbackWin32State);

	// Now apply command-line overrides (these may change startingMonitor)
	ParseCommandLineArgs(config, win32Config);

	// If -monitor was passed on the command line, re-resolve the target monitor
	// so the actual window is placed on the overridden monitor.
	// We check by comparing: if startingMonitor changed, recompute.
	// (LoadWindowIniConfig and ParseCommandLineArgs both write to config.startingMonitor)
	targetMonitor = Win32_GetMonitorRectByIndex(config.startingMonitor);

#ifndef WIN7BUILD
	if (config.dpiAware) {
		EnableDPIAwareness(); // from win10_win11_required_code
	}
#endif

	WindowSetup finalSetup;
	finalSetup.aspectRatio = config.aspectRatio;

	// Apply correct window setup based on (possibly forced) mode.
	// GetWin32WindowState() is written directly here since finalSetup becomes
	// the live g_windowSetup (see wWinMain), so its Win32 half is the live
	// g_win32WindowState singleton.
	if (!forceWindowed && config.useFullscreen) {
		finalSetup = GetBorderlessFullscreenSetup(targetMonitor, GetWin32WindowState());
	}
	else if (config.useAspectRatio) {
		finalSetup = GetCenteredAspectWindowSetup(config.aspectRatio, win32Config.disableNC, targetMonitor, GetWin32WindowState());
	}
	// ---- HACK: Fix for skipping ClassicWindowSetup ----- Added 4/5/2026 -- Get Real Fix.
	else {
	//	finalSetup = GetClassicWindowSetup(config.windowWidth, config.windowHeight, config.centerWindow, targetMonitor, GetWin32WindowState());
		finalSetup = GetCenteredAspectWindowSetup(config.aspectRatio, win32Config.disableNC, targetMonitor, GetWin32WindowState());
	}
	// ------------------- END HACK ------------------------
	// Copy override flags back into final setup
	finalSetup.useFullscreen   = config.useFullscreen;
	finalSetup.useAspectRatio  = config.useAspectRatio;
	finalSetup.aspectOverrideActive = config.aspectOverrideActive;
	finalSetup.centerWindow    = config.centerWindow;
	finalSetup.windowWidth     = config.windowWidth;
	finalSetup.windowHeight    = config.windowHeight;
	finalSetup.dpiAware        = config.dpiAware;
	finalSetup.cursorClipEnabled = config.cursorClipEnabled;
	finalSetup.startingMonitor = config.startingMonitor;
	GetWin32WindowState().disableNC = win32Config.disableNC;
	GetWin32WindowState().disableRoundedCorners = win32Config.disableRoundedCorners;

	return finalSetup;
}

// -----------------------------------------------------------------------------
// CreateConfiguredWindow
// Creates a Win32 window using position and style from the given WindowSetup.
// Also applies Win11-specific visual tweaks and reads the initial DPI scale.
// -----------------------------------------------------------------------------
HWND CreateConfiguredWindow(HINSTANCE hInstance, const wchar_t* className, const wchar_t* title, WindowSetup& config)
{
	// This is always called with the live g_windowSetup, so its Win32 half is
	// the live g_win32WindowState singleton.
	Win32WindowState& win32 = GetWin32WindowState();

	int w = config.rect.right  - config.rect.left;
	int h = config.rect.bottom - config.rect.top;

	HWND hwnd = CreateWindowExW(
		win32.exStyle,
		className,
		title,
		win32.style,
		config.rect.left,
		config.rect.top,
		w,
		h,
		nullptr, nullptr, hInstance, nullptr
	);

	if (!hwnd)
	{
		MessageBoxW(nullptr, L"Failed to create window", title, MB_OK | MB_ICONERROR);
		return nullptr;
	}

#ifndef WIN7BUILD
	if (GetOsVersion() == Win11)
	{
		if (win32.disableNC == 0)
		{
			if (win32.disableRoundedCorners) {
				DisableRoundedCorners(hwnd);
			}
		}
		else DisableNCRendering(hwnd);
	}
#endif

	// Check for DPI Scaling.
#ifndef WIN7BUILD
	g_windowSetup.dpiScale = GetDPIScaleForWindow(hwnd);
	LOG_INFO("Final DPI scale factor: %.2f", g_windowSetup.dpiScale);
#else
	g_windowSetup.dpiScale = 1.0f;
	LOG_INFO("DPI scale defaulted to 1.0 (WIN7BUILD)");
#endif

	RECT client{};
	if (GetClientRect(hwnd, &client)) {
		config.clientWidth  = client.right  - client.left;
		config.clientHeight = client.bottom - client.top;
	}

	return hwnd;
}

// -----------------------------------------------------------------------------
// Win32Window::Create / Destroy / PumpEvents / Presentation
//
// wWinMain (below) still registers its own window class and calls
// CreateConfiguredWindow directly - its startup sequencing (icon load,
// staying hidden until the first black frame is presented, DPI awareness)
// is more involved than a single entry point. These methods are the
// equivalent path for a future caller that goes through the interface
// instead: same class name/title, same CreateConfiguredWindow, registering
// the class lazily if needed. Not currently called by anything in this file.
// -----------------------------------------------------------------------------
bool Win32Window::Create(const WindowSetup& setup)
{
	static bool s_classRegistered = false;
	const wchar_t* kClassName = L"OpenGLWindowClass";
	HINSTANCE hInstance = GetModuleHandleW(nullptr);

	if (!s_classRegistered)
	{
		WNDCLASSW wc = {};
		wc.lpfnWndProc   = WndProc;
		wc.hInstance     = hInstance;
		wc.lpszClassName = kClassName;
		wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
		wc.style         = CS_HREDRAW | CS_VREDRAW;
		wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		RegisterClassW(&wc);
		s_classRegistered = true;
	}

	g_windowSetup = setup;
	g_hWnd = CreateConfiguredWindow(hInstance, kClassName, L"AAE Emulator", g_windowSetup);
	return g_hWnd != nullptr;
}

void Win32Window::Destroy()
{
	if (g_hWnd)
	{
		DestroyWindow(g_hWnd);
		g_hWnd = nullptr;
	}
}

bool Win32Window::PumpEvents()
{
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return false;

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return true;
}

IPresentSurface* Win32Window::Presentation()
{
	return &m_presentSurface;
}

// -----------------------------------------------------------------------------
// Win32PresentSurface
// CreateVkSurface() creates the real VkSurfaceKHR for this window (Phase 4a
// Plan 2): it loads vulkan-1.dll at runtime, resolves vkGetInstanceProcAddr,
// and calls vkCreateWin32SurfaceKHR against g_hWnd. No vulkan-1.lib link
// (spec sec. 6) -- sys_vk.cpp's own loader bootstrap does the same for every
// other Vulkan entry point.
// -----------------------------------------------------------------------------
void Win32PresentSurface::SwapBuffers()
{
	glchain_swap_buffers();
}

void Win32PresentSurface::GetDrawableSize(int* w, int* h) const
{
	if (w) *w = g_windowSetup.clientWidth;
	if (h) *h = g_windowSetup.clientHeight;
}

const char* const* Win32PresentSurface::RequiredVkInstanceExtensions(uint32_t* count) const
{
	static const char* kExtensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
	if (count) *count = 2;
	return kExtensions;
}

bool Win32PresentSurface::CreateVkSurface(void* instance, void* outSurface)
{
	if (!instance || !outSurface || !g_hWnd)
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: bad args or no window");
		return false;
	}

	HMODULE loader = LoadLibraryA("vulkan-1.dll");
	if (!loader)
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: vulkan-1.dll not found");
		return false;
	}

	PFN_vkGetInstanceProcAddr gipa =
		(PFN_vkGetInstanceProcAddr)GetProcAddress(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateWin32SurfaceKHR createWin32Surface = gipa
		? (PFN_vkCreateWin32SurfaceKHR)gipa((VkInstance)instance, "vkCreateWin32SurfaceKHR")
		: nullptr;

	bool ok = false;
	if (createWin32Surface)
	{
		VkWin32SurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		sci.hinstance = GetModuleHandleW(nullptr);
		sci.hwnd = g_hWnd;

		VkResult r = createWin32Surface((VkInstance)instance, &sci, nullptr, (VkSurfaceKHR*)outSurface);
		ok = (r == VK_SUCCESS);
		if (!ok)
			LOG_ERROR("Win32PresentSurface::CreateVkSurface: vkCreateWin32SurfaceKHR failed (VkResult=%d)", (int)r);
	}
	else
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: vkCreateWin32SurfaceKHR not available");
	}

	FreeLibrary(loader);  // refcounted; sys_vk holds its own reference for the session
	return ok;
}

// -----------------------------------------------------------------------------
// GetSystemWindow
// The active window - never null after startup (see sys_window.h).
// -----------------------------------------------------------------------------
ISystemWindow& GetSystemWindow()
{
	static Win32Window instance;
	return instance;
}

// -----------------------------------------------------------------------------
// Win32Window::ToggleBorderlessFullscreen
// Switches between borderless fullscreen and windowed mode.
// When going fullscreen at runtime (ALT+ENTER), uses the monitor nearest to the
// current window position rather than config.startingMonitor, so we stay on
// whichever monitor the window currently lives on.
// When restoring to windowed, uses the saved windowedRect (or fallback).
// -----------------------------------------------------------------------------
void Win32Window::ToggleBorderlessFullscreen()
{
	LOG_INFO("Calling ToggleBorderlessFullscreen");
	HWND hwnd = g_hWnd;
	WindowSetup& config = g_windowSetup;
	// Both call sites (WndProc's WM_SYSKEYDOWN and menu.cpp) act on
	// g_windowSetup, so its Win32 half is the live g_win32WindowState singleton.
	Win32WindowState& win32 = GetWin32WindowState();
	if (!config.borderlessFullscreen)
	{
		// Backup current window rect for restoration
		RECT wr{};
		GetWindowRect(hwnd, &wr);
		config.windowedRect = FromWin32Rect(wr);

		SetWindowLong(hwnd, GWL_STYLE,   WS_POPUP);
		SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
		win32.style   = WS_POPUP;
		win32.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

		// MULTI-MONITOR SAFE: at runtime we fullscreen on the monitor the window
		// is currently on, not necessarily the starting_monitor.
		RECT screen = Win32_GetNearestMonitorRect(hwnd);
		if ((screen.right <= screen.left) || (screen.bottom <= screen.top)) {
			// Fallback if monitor query fails.
			screen = Win32_GetPrimaryMonitorRect();
		}

		SetWindowPos(hwnd, HWND_TOPMOST,
			screen.left, screen.top,
			screen.right  - screen.left,
			screen.bottom - screen.top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		config.borderlessFullscreen = true;
	}
	else
	{
		// Restore to windowed mode. If we never captured a valid windowed rect
		// (e.g., the app started in fullscreen), fall back to the computed
		// windowed setup from config/ini.
		RECT wr = ToWin32Rect(config.windowedRect);
		int w = wr.right  - wr.left;
		int h = wr.bottom - wr.top;
		if (w <= 0 || h <= 0) {
			wr = ToWin32Rect(g_windowedFallbackSetup.rect);
			w  = wr.right  - wr.left;
			h  = wr.bottom - wr.top;
		}

		DWORD restoreStyle   = g_windowedFallbackWin32State.style;
		DWORD restoreExStyle = g_windowedFallbackWin32State.exStyle;
		SetWindowLong(hwnd, GWL_STYLE,   restoreStyle);
		SetWindowLong(hwnd, GWL_EXSTYLE, restoreExStyle);
		win32.style   = restoreStyle;
		win32.exStyle = restoreExStyle;

		LOG_INFO("Restoring to windowed size in ToggleScreenSize: %d x %d", w, h);
		SetWindowPos(hwnd, HWND_TOPMOST,
			wr.left, wr.top,
			w, h,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		config.borderlessFullscreen = false;
	}

	RECT client{};
	if (GetClientRect(hwnd, &client)) {
		config.clientWidth  = client.right  - client.left;
		config.clientHeight = client.bottom - client.top;
		ViewOrtho(config.clientWidth, config.clientHeight);
		UpdateCursorState();
	}
	LOG_INFO("Now in %s mode %d", config.borderlessFullscreen ? "borderless fullscreen" : "windowed", config.borderlessFullscreen);
	LOG_INFO("Setting Client size: %d x %d", config.clientWidth, config.clientHeight);
	LOG_INFO("End Calling ToggleBorderlessFullscreen");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		return 0;

	case WM_CLOSE:
		done = 1;
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);  // still let Windows post WM_QUIT
		return 0;

	case WM_SIZE:
	{
		int width  = LOWORD(lParam);
		int height = HIWORD(lParam);

		g_windowSetup.clientWidth  = width;
		g_windowSetup.clientHeight = height;

		g_windowSetup.isMinimized = (wParam == SIZE_MINIMIZED);

		if (!g_windowSetup.isMinimized) {
			GetSystemWindow().RestoreViewport();
			ViewOrtho(width, height);
			emulator_on_window_resize(width, height);
#ifndef WIN7BUILD
			g_windowSetup.dpiScale = GetDPIScaleForWindow(hWnd);
#else
			g_windowSetup.dpiScale = 1.0f;
#endif
		}

		// Update cursor trap when window size or minimized state changes
		UpdateCursorState();
		return 0;
	}

	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU) {
			// Suppress Alt key system menu to prevent game pause
			return 0;
		}
		break;

	case WM_SIZING:
	{
		WindowSetup* config = &g_windowSetup;
		if (!config || !config->resizable)
			return TRUE;

		RECT* rect = reinterpret_cast<RECT*>(lParam);
		int frameW = 0, frameH = 0;

		// Measure window frame size
		RECT tmp = { 0, 0, 100, 100 };
		AdjustWindowRectEx(&tmp, GetWin32WindowState().style, FALSE, GetWin32WindowState().exStyle);
		frameW = (tmp.right - tmp.left) - 100;
		frameH = (tmp.bottom - tmp.top) - 100;

		// Compute current client size
		int fullW   = rect->right  - rect->left;
		int fullH   = rect->bottom - rect->top;
		int clientW = fullW - frameW;
		int clientH = fullH - frameH;

		// Maintain client aspect
		clientW = std::max(clientW, config->minWindowWidth);
		clientH = static_cast<int>(roundf(clientW / config->aspectRatio));

		// Expand to total window size again
		fullW = clientW + frameW;
		fullH = clientH + frameH;

		rect->right  = rect->left + fullW;
		rect->bottom = rect->top  + fullH;
		return TRUE;
	}

	case WM_GETMINMAXINFO:
	{
		auto* minmax = reinterpret_cast<MINMAXINFO*>(lParam);
		minmax->ptMinTrackSize.x = 320;
		minmax->ptMinTrackSize.y = 240;
		return 0;
	}

	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)) {
			GetSystemWindow().ToggleBorderlessFullscreen();
			return 0;
		}
		break;

	case WM_ERASEBKGND:
		// Prevent flickering behind OpenGL surface
		return 1;

		// --- MOUSE & FOCUS LOGIC START ---

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		// If user clicks the window, ensure we have focus and trap the mouse immediately.
		if (!g_windowSetup.isFocused) {
			SetFocus(hWnd);
			g_windowSetup.isFocused = true;
		}

		// If they explicitly clicked inside the game view, auto-resume mouse capture
		// just in case they had previously disabled it with F9.
		if (!g_windowSetup.cursorClipEnabled) {
			g_windowSetup.cursorClipEnabled = true;
		}

		UpdateCursorState();
		return 0;

	// F9 is NOT handled here. The mouse-capture toggle lives in msg_loop()
	// (aae_emulator.cpp) so that Linux, whose keyboard comes from evdev and
	// never passes through this message pump, gets the same hotkey.

	case WM_KILLFOCUS:
	case WM_SETFOCUS:
		g_windowSetup.isFocused = (msg == WM_SETFOCUS);
		UpdateCursorState();
		return 0;

	case WM_ACTIVATEAPP:
		g_windowSetup.isFocused = (wParam != 0);
		UpdateCursorState();
		return 0;

		// --- MOUSE & FOCUS LOGIC END ---

	case SC_KEYMENU:
	case SC_SCREENSAVE:
	case SC_MONITORPOWER:
		return 0;

	case WM_INPUT:
		return RawInput_ProcessInput(hWnd, wParam, lParam);

	case WM_DEVICECHANGE:
		// Joystick hotplug: flag a rescan (performed on the next
		// poll_joystick). Fires for any device-tree change; the handler
		// is cheap and idempotent, so no filtering is needed here.
		joystick_device_change();
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

HWND win_get_window() {
	return g_hWnd;
}

// -----------------------------------------------------------------------------
// BuildUTF8Args
// Converts global command line arguments from wide (__wargv) to UTF-8.
// This lets the rest of the emulator code use standard char** argv.
// -----------------------------------------------------------------------------
void BuildUTF8Args(int& argc_out, char**& argv_out)
{
	extern int __argc;
	extern wchar_t** __wargv;

	static std::vector<std::string> utf8Args;
	static std::vector<char*> argPointers;
	utf8Args.clear();
	argPointers.clear();

	utf8Args.reserve(__argc);
	argPointers.reserve(__argc + 1);

	for (int i = 0; i < __argc; ++i) {
		utf8Args.push_back(win32::Utf16ToUtf8(__wargv[i]));
	}

	for (auto& s : utf8Args) {
		argPointers.push_back(&s[0]);
	}
	argPointers.push_back(nullptr);

	argc_out = __argc;
	argv_out = argPointers.data();
}

// -----------------------------------------------------------------------------
// Description
// Entry point for Win32 application with full Unicode, INI, and DPI handling
// -----------------------------------------------------------------------------
int WINAPI wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow
)
{
	std::wstring temppath = getpathU(0, 0);

	// Initialize Logging and Directory EARLY so initialization doesn't fail
	LogOpen("systemlog.txt");

	if (!SetCurrentDirectory(temppath.c_str())) {
		LOG_ERROR("SetCurrentDirectory failed (%lu)", GetLastError());
	}

	const wchar_t CLASS_NAME[] = L"OpenGLWindowClass";

	HICON hIcon = static_cast<HICON>(LoadImage(
		hInstance,
		MAKEINTRESOURCE(IDI_ICON1),
		IMAGE_ICON,
		0, 0,
		LR_DEFAULTSIZE | LR_SHARED
	));

	WNDCLASSW wc = {};
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon         = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	// Black background so any GDI erase (e.g. during a resize before a GL frame
	// lands) paints black, never white.
	wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	RegisterClassW(&wc);

	// -------------------------------------------------------------------------
	// Step 1: Load config + cmdline into a temporary structure.
	// LoadWindowIniConfig reads starting_monitor from [main] starting_monitor.
	// ParseCommandLineArgs may override it with -monitor N.
	// -------------------------------------------------------------------------
	WindowSetup temp;
	Win32WindowState tempWin32;
	LoadWindowIniConfig(temp, tempWin32);
	ParseCommandLineArgs(temp, tempWin32);
	bool requestedFullscreen = temp.useFullscreen;

	// -------------------------------------------------------------------------
	// Step 2: Force windowed mode setup to ensure valid rect for restore.
	// GenerateFinalWindowSetup(true) builds the windowed rect on the correct
	// monitor (from starting_monitor / -monitor) and saves g_windowedFallbackSetup.
	// -------------------------------------------------------------------------
	g_windowSetup = GenerateFinalWindowSetup(/* forceWindowed = */ true);

	// -------------------------------------------------------------------------
	// Step 3: Create window and show it
	// -------------------------------------------------------------------------
	g_hWnd = CreateConfiguredWindow(hInstance, CLASS_NAME, L"AAE Emulator", g_windowSetup);
	if (!g_hWnd) return -1;

	SendMessage(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	SendMessage(g_hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);

	// NOTE: the window is created hidden here on purpose. We do not ShowWindow()
	// until after the OpenGL context exists and we have presented one black
	// frame (see below), so the first pixels the user ever sees are black
	// instead of an uninitialized white client area.

	SaveAndDisableAccessibilityPopups();

	// Register Raw Input devices (keyboard + mouse)
	if (FAILED(RawInput_Initialize(g_hWnd))) {
		LOG_ERROR("Failed to initialize Raw Input");
		return false;
	}

	// Start keyboard LED service (pushes LED states to all KeyboardClass devices)
	osd_led_service_start();

	if (!install_joystick()) {
		LOG_INFO("Win32 joystick initialized: %d detected", num_joysticks);
	}
	else {
		LOG_ERROR("No joysticks detected or initialization failed");
	}

	const bool wantVulkan = EarlyRendererIsVulkan();
	g_glContextCreated = false;

	if (!wantVulkan)
	{
		// useCoreProfile = true: request a forward-compatible OpenGL core context.
		// The whole render path is now core-clean (no fixed-function matrix/state,
		// no client arrays, core FBO calls, all #version 330 core shaders).
		if (!InitOpenGLContext(false, false, true)) {
			LOG_ERROR("Failed to initialize OpenGL");
			return -1;
		}
		g_glContextCreated = true;

		// Present one black frame into the front buffer *before* the window is
		// ever visible, so the first thing the user sees is black, not white.
		// Our WM_ERASEBKGND handler returns 1, so GDI never repaints over this.
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glchain_swap_buffers();
	}
	else
	{
		LOG_INFO("Renderer=vulkan: skipping OpenGL context creation (window layer)");
		// Keep the first-visible-pixels-are-black invariant without GL: paint
		// the hidden window's client area once with GDI.
		RECT rc{};
		GetClientRect(g_hWnd, &rc);
		HDC dc = GetDC(g_hWnd);
		if (dc)
		{
			FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
			ReleaseDC(g_hWnd, dc);
		}
	}

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);

	if (wantVulkan)
	{
		// GDI paint on a hidden window does not survive the show (the user
		// saw a white flash), so repeat the black fill now that it is visible.
		RECT rc{};
		GetClientRect(g_hWnd, &rc);
		HDC dc = GetDC(g_hWnd);
		if (dc)
		{
			FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
			ReleaseDC(g_hWnd, dc);
		}
	}

	// Assume focus because we just showed the window
	g_windowSetup.isFocused = true;

	// -------------------------------------------------------------------------
	// Step 4: Save a valid windowedRect for fullscreen restore.
	// This is the actual Window RECT before any fullscreen toggle.
	// -------------------------------------------------------------------------
	RECT wr{};
	if (GetWindowRect(g_hWnd, &wr)) {
		g_windowSetup.windowedRect = FromWin32Rect(wr);
		LOG_INFO("Saved windowedRect before fullscreen: (%d,%d)-(%d,%d)",
			wr.left, wr.top, wr.right, wr.bottom);
	}
	else {
		LOG_ERROR("Failed to get windowedRect before fullscreen");
	}

	// Save a Fullscreen Window RECT so we can use it later.
	// Note: this captures the primary monitor; ToggleBorderlessFullscreen uses
	// Win32_GetNearestMonitorRect at runtime for correct multi-monitor behavior.
	g_windowSetup.screenRect.left   = 0;
	g_windowSetup.screenRect.top    = 0;
	g_windowSetup.screenRect.right  = GetSystemMetrics(SM_CXSCREEN);
	g_windowSetup.screenRect.bottom = GetSystemMetrics(SM_CYSCREEN);

	// -------------------------------------------------------------------------
	// Step 5: Capture client size and apply projection
	// -------------------------------------------------------------------------
	RECT client{};
	if (GetClientRect(g_hWnd, &client)) {
		g_windowSetup.clientWidth  = client.right  - client.left;
		g_windowSetup.clientHeight = client.bottom - client.top;

		LOG_INFO("Windowed Client size: %d x %d", g_windowSetup.clientWidth, g_windowSetup.clientHeight);
	}
	else {
		g_windowSetup.clientWidth = g_windowSetup.clientHeight = 0;
		LOG_ERROR("GetClientRect failed.");
	}
	ViewOrtho(g_windowSetup.clientWidth, g_windowSetup.clientHeight);

	// -------------------------------------------------------------------------
	// Step 6: Now toggle to fullscreen if originally requested
	// -------------------------------------------------------------------------
	g_windowSetup.borderlessFullscreen = false;
	if (requestedFullscreen)
		GetSystemWindow().ToggleBorderlessFullscreen();

	// Now that all subsystems (Window, RawInput, OpenGL) are ready,
	// enforce the cursor trap/hide logic.
	//
	// ALWAYS capture at startup - deliberately NOT the ini/cmdline value.
	// Honouring cursor_clip=0 here meant a stale ini entry started every session
	// with a visible, unconfined pointer, which reads as broken mouse support.
	// Capture-by-default is the product decision (2026-07-29); F9 releases and a
	// click recaptures, so an escape hatch at startup buys nothing.
	GetSystemWindow().EnableCursorClip(true);

	// This sets the High Performance timer.
	TimerInit();

	int argc = 0;
	char** argv = nullptr;

	BuildUTF8Args(argc, argv);

	// Init Emulator Here.
	emulator_init(argc, argv);

	// -------------------------------------------------------------------------
	// Step 7: Main high-speed game loop
	// -------------------------------------------------------------------------
	MSG msg = {};
	while (!done)
	{
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				done = 1;
				goto exit_main;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		poll_joystick();
		emulator_run();

		// If a game requested a switch (ESC-to-GUI or future GUI selection), do it here.
		if (emulator_apply_pending_switch()) {
			// A new game (or GUI) was started. Keep running.
			continue;
		}

		// If the game ended and no switch is pending, this means quit emulator.
		if (done)
		{
			break;
		}
	}

exit_main:
	// Shutdown Emulator Here
	emulator_end();
	// Stop keyboard LED service thread
	osd_led_service_stop();
	RestoreAccessibilityPopups();
	TimerShutdown();
	if (g_glContextCreated)
		DeleteGLContext();
	RawInput_Shutdown();
	LogClose();
	return static_cast<int>(msg.wParam);
}
