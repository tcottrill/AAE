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
// - Borderless fullscreen and aspect ratio–locked modes
// - Mouse cursor clipping and hiding support
// - DPI awareness with Windows 10/11 feature handling
// - Safe accessibility feature suppression (StickyKeys, etc.)
// - Modular game entry and exit flow: game_init(), game_run(), game_end()
// - Robust joystick support with fallback logging
// - Clean separation of platform-specific responsibilities
//
// Notes:
// - Uses CreateConfiguredWindow() and WindowSetup for centralized window creation
// - Requires utf8conv.h, sys_log.h, gl_basics.h, debug_draw.h, rawinput.h
// - Supports ALT+ENTER toggle to fullscreen
// - Supports resize and aspect enforcement via WM_SIZING/WM_SIZE
// - Saves valid client and window rects for FBO scaling and restoration
//
// -----------------------------------------------------------------------------

#include <windows.h>
#include <string>
#include <cstdio>
#include "iniFile.h"
#include "path_helper.h"
#include "sys_log.h"
#include "utf8conv.h"
#include "rawinput.h"
#include "sys_gl.h"
#include "debug_draw.h"
#include "framework.h"
#include "aae_emulator.h"
#include "resource.h"
#include "joystick.h"
#include "wintimer.h"
#ifndef Win7Build
#include "win10_win11_required_code.h"
#endif
#include "windows_util.h"
#include "glcode.h"
#include "aae_mame_driver.h"  // for global 'done'
// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
bool g_cursorClipped = false;
HWND g_hWnd = nullptr;

// Initialize the audio mixer
const int audioSampleRate = 44100;
const int targetFPS = 60;

WindowSetup g_windowSetup;

// Accessors

WindowSetup& GetWindowSetup() {
	return g_windowSetup;
}

int GetClientWidth() {
	return g_windowSetup.clientWidth;
}

int GetClientHeight() {
	return g_windowSetup.clientHeight;
}

void RestoreWindowViewport()
{
	glViewport(0, 0, g_windowSetup.clientWidth, g_windowSetup.clientHeight);
}

// -----------------------------------------------------------------------------
// Clips mouse to window and hides cursor
// -----------------------------------------------------------------------------
void ClipAndHideCursor(HWND hWnd) // Replace with g_hWnd
{
	RECT rect;
	GetClientRect(hWnd, &rect);
	MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&rect), 2);
	ClipCursor(&rect);
	while (ShowCursor(FALSE) >= 0);
	g_cursorClipped = true;
}

// -----------------------------------------------------------------------------
// Unclips mouse and shows cursor
// -----------------------------------------------------------------------------
void UnclipAndShowCursor()
{
	ClipCursor(nullptr);
	while (ShowCursor(TRUE) < 0);
	g_cursorClipped = false;
}

void UpdateCursorClipState(HWND hwnd)
{
	if (!g_windowSetup.cursorClipEnabled) {
		if (g_cursorClipped)
			UnclipAndShowCursor();
		return;
	}

	if (g_windowSetup.isFocused && !g_windowSetup.isMinimized)
	{
		if (!g_cursorClipped)
			ClipAndHideCursor(hwnd);
	}
	else
	{
		if (g_cursorClipped)
			UnclipAndShowCursor();
	}
}

void EnableCursorClip(bool enable)
{
	g_windowSetup.cursorClipEnabled = enable;
	UpdateCursorClipState(g_hWnd);
}

void ForceCursorClipUpdate()
{
	UpdateCursorClipState(g_hWnd);
}

// -----------------------------------------------------------------------------
// SetMousePos
// Sets the mouse cursor position relative to the client area of the given HWND.
// Converts to screen coordinates before applying.
// -----------------------------------------------------------------------------
void SetMousePos(HWND hwnd, int x, int y)
{
	POINT pos = { x, y };
	ClientToScreen(hwnd, &pos);
	SetCursorPos(pos.x, pos.y);
}

// -----------------------------------------------------------------------------
// GetMousePos
// Returns the current mouse cursor position relative to the client area
// of the given HWND. If conversion fails, returns {0,0}.
// -----------------------------------------------------------------------------
POINT GetMousePos(HWND hwnd)
{
	POINT p{};
	if (GetCursorPos(&p))
		ScreenToClient(hwnd, &p);
	return p;
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

WindowSetup GetBorderlessFullscreenSetup()
{
	WindowSetup ws;
	ws.style = WS_POPUP;
	ws.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	ws.rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	ws.rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	ws.rect.right = ws.rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
	ws.rect.bottom = ws.rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

	ws.borderlessFullscreen = true;
	return ws;
}

WindowSetup GetClassicWindowSetup(int width, int height, bool center)
{
	WindowSetup ws;
	ws.style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	ws.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	RECT wr = { 0, 0, width, height };
	AdjustWindowRect(&wr, ws.style, FALSE);

	int windowW = wr.right - wr.left;
	int windowH = wr.bottom - wr.top;

	int x = CW_USEDEFAULT;
	int y = CW_USEDEFAULT;

	if (center) {
		RECT workArea{};
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

		int screenW = workArea.right - workArea.left;
		int screenH = workArea.bottom - workArea.top;

		x = workArea.left + (screenW - windowW) / 2;
		y = workArea.top + (screenH - windowH) / 2;
	}

	ws.rect.left = x;
	ws.rect.top = y;
	ws.rect.right = x + windowW;
	ws.rect.bottom = y + windowH;

	ws.windowWidth = width;
	ws.windowHeight = height;
	// Force override the aspect ratio to the one that we are using at window creation time.
	ws.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	LOG_INFO("Classic Window Aspect at start, %f", ws.aspectRatio);
	ws.resizable = true;
	return ws;
}

WindowSetup GetCenteredAspectWindowSetup(float aspectRatio, bool disableNC)
{
	WindowSetup ws;
	ws.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	ws.exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

	// Get available desktop area (excludes taskbar)
	RECT workArea{};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

	int screenW = workArea.right - workArea.left;
	int screenH = workArea.bottom - workArea.top;

	// Optional correction for Win11 visual padding
	if (GetOsVersion() == Win11 && !disableNC) {
		workArea.left -= 7;
		workArea.right += 14;
		workArea.bottom += 7;
		screenW = workArea.right - workArea.left;
		screenH = workArea.bottom - workArea.top;
	}

	// Compute window frame size for style
	int frameW = 0, frameH = 0;
	GetWindowFrameSize(ws.style, ws.exStyle, frameW, frameH);

	// Determine maximum client height we can use
	int maxClientH = screenH - frameH;
	int clientH = maxClientH;
	int clientW = static_cast<int>(roundf(clientH * aspectRatio));

	// If too wide, clamp width and recompute height
	if (clientW + frameW > screenW) {
		clientW = screenW - frameW;
		clientH = static_cast<int>(roundf(clientW / aspectRatio));
	}

	// Final full window dimensions
	int windowW = clientW + frameW;
	int windowH = clientH + frameH;

	// Center the full window on screen
	int x = workArea.left + (screenW - windowW) / 2;
	int y = workArea.top + (screenH - windowH) / 2;

	ws.rect.left = x;
	ws.rect.top = y;
	ws.rect.right = x + windowW;
	ws.rect.bottom = y + windowH;

	ws.windowWidth = clientW;
	ws.windowHeight = clientH;
	ws.aspectRatio = aspectRatio;
	ws.disableNC = disableNC;

	return ws;
}

void LoadWindowIniConfig(WindowSetup& config)
{
	SetIniFile("aae.ini");

	config.useFullscreen = get_config_int("window", "fullscreen", 0) != 0;
	config.centerWindow = get_config_int("window", "center", 1) != 0;
	config.useAspectRatio = get_config_int("window", "use_aspect", 0) != 0;
	config.disableNC = get_config_int("window", "disable_nc", 0) != 0;
	config.windowWidth = get_config_int("window", "width", 1024);
	config.windowHeight = get_config_int("window", "height", 768);
	config.disableRoundedCorners = get_config_bool("window", "disable_rounded_corners", false);
	config.dpiAware = get_config_bool("window", "dpi_aware", true);
	config.cursorClipEnabled = get_config_bool("window", "cursor_clip", true);

	// Load aspect ratio as "4:3", "16:9", etc.
	std::string aspect = get_config_string("window", "aspect_ratio", "4:3");
	int ax = 0, ay = 0;
	if (sscanf_s(aspect.c_str(), "%d:%d", &ax, &ay) != 2 || ax == 0 || ay == 0) {
		LOG_INFO("Invalid aspect ratio: %s — defaulting to 4:3", aspect.c_str());
		ax = 4; ay = 3;
	}
	config.aspectRatio = (float)ax / (float)ay;
}

void ParseCommandLineArgs(WindowSetup& config)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) return;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring arg = argv[i];
		if (arg == L"-fullscreen") config.useFullscreen = true;
		else if (arg == L"-windowed") config.useFullscreen = false;
		else if (arg == L"-nocenter") config.centerWindow = false;
		else if (arg == L"-aspectwindow") config.useAspectRatio = true;
		else if (arg == L"-disableNC") config.disableNC = true;
		else if (arg == L"-aspect" && i + 1 < argc) {
			std::wstring wide = argv[++i];
			std::string val = win32::Utf16ToUtf8(wide);
			int ax = 0, ay = 0;
			if (sscanf_s(val.c_str(), "%d:%d", &ax, &ay) == 2 && ax > 0 && ay > 0)
				config.aspectRatio = (float)ax / (float)ay;
		}
		else if (arg == L"-width" && i + 1 < argc)
			config.windowWidth = _wtoi(argv[++i]);
		else if (arg == L"-height" && i + 1 < argc)
			config.windowHeight = _wtoi(argv[++i]);
		else if (arg == L"-noclip")
			config.cursorClipEnabled = false;
		else if (arg == L"-clip")
			config.cursorClipEnabled = true;
	}

	LocalFree(argv);
}

WindowSetup GenerateFinalWindowSetup(bool forceWindowed = false)
{
	WindowSetup config;
	LoadWindowIniConfig(config);
	ParseCommandLineArgs(config);
	WindowSetup finalSetup;

	finalSetup.aspectRatio = config.aspectRatio;
#ifndef Win7Build
	if (config.dpiAware) {
		EnableDPIAwareness(); // from win10_win11_required_code
	}
#endif

	// Apply correct window setup based on (possibly forced) logic
	if (!forceWindowed && config.useFullscreen) {
		finalSetup = GetBorderlessFullscreenSetup();
	}
	else if (config.useAspectRatio) {
		finalSetup = GetCenteredAspectWindowSetup(config.aspectRatio, config.disableNC);
	}
	else {
		finalSetup = GetClassicWindowSetup(config.windowWidth, config.windowHeight, config.centerWindow);
	}

	// Copy override flags back into final setup
	finalSetup.useFullscreen = config.useFullscreen;
	finalSetup.useAspectRatio = config.useAspectRatio;
	finalSetup.centerWindow = config.centerWindow;
	finalSetup.disableNC = config.disableNC;
	finalSetup.windowWidth = config.windowWidth;
	finalSetup.windowHeight = config.windowHeight;
	finalSetup.disableRoundedCorners = config.disableRoundedCorners;
	// Set aspect ratio early for fallback sizing

	//finalSetup.resizable = config.resizable;
	finalSetup.dpiAware = config.dpiAware;
	finalSetup.cursorClipEnabled = config.cursorClipEnabled;

	return finalSetup;
}

HWND CreateConfiguredWindow(HINSTANCE hInstance, const wchar_t* className, const wchar_t* title, WindowSetup& config)
{
	int w = config.rect.right - config.rect.left;
	int h = config.rect.bottom - config.rect.top;

	HWND hwnd = CreateWindowExW(
		config.exStyle,
		className,
		title,
		config.style,
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

#ifndef Win7Build
	if (GetOsVersion() == Win11)
	{
		if (config.disableNC == 0)
		{
			if (config.disableRoundedCorners) {
				DisableRoundedCorners(hwnd);
			}
		}
		else DisableNCRendering(hwnd);
	}
#endif

	//Check for DPI Scaling.
#ifndef Win7Build
	g_windowSetup.dpiScale = GetDPIScaleForWindow(hwnd);
	LOG_INFO("Final DPI scale factor: %.2f", g_windowSetup.dpiScale);
#else
	g_windowSetup.dpiScale = 1.0f;
	LOG_INFO("DPI scale defaulted to 1.0 (Win7Build)");
#endif

	RECT client{};
	if (GetClientRect(hwnd, &client)) {
		config.clientWidth = client.right - client.left;
		config.clientHeight = client.bottom - client.top;
	}

	return hwnd;
}

void ToggleBorderlessFullscreen(HWND hwnd, WindowSetup& config)
{
	LOG_INFO("Calling ToggleBorderlessFullscreen");
	if (!config.borderlessFullscreen)
	{
		// This backs up the current "Window Size for restoration when returning to windowed mode.
		GetWindowRect(hwnd, &config.windowedRect);

		SetWindowLong(hwnd, GWL_STYLE, WS_POPUP);
		SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);

		RECT screen{};
		screen.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
		screen.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
		screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
		screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

		SetWindowPos(hwnd, HWND_TOPMOST,
			screen.left, screen.top,
			screen.right - screen.left,
			screen.bottom - screen.top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		config.borderlessFullscreen = true;
	}
	else
	{
		SetWindowLong(hwnd, GWL_STYLE, config.style);
		SetWindowLong(hwnd, GWL_EXSTYLE, config.exStyle);

		// This sets the Windowed mode to the backed up previous Window Size and location.
		int w = config.windowedRect.right - config.windowedRect.left;
		int h = config.windowedRect.bottom - config.windowedRect.top;
		LOG_INFO("Restoring to windowed size in ToggleScreenSize: %d x %d", w, h);
		SetWindowPos(hwnd, HWND_TOPMOST,
			config.windowedRect.left, config.windowedRect.top,
			w, h,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		config.borderlessFullscreen = false;
	}

	RECT client{};
	if (GetClientRect(hwnd, &client)) {
		config.clientWidth = client.right - client.left;
		config.clientHeight = client.bottom - client.top;
		ViewOrtho(config.clientWidth, config.clientHeight);
		UpdateCursorClipState(hwnd);
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
		int width = LOWORD(lParam);
		int height = HIWORD(lParam);

		g_windowSetup.clientWidth = width;
		g_windowSetup.clientHeight = height;

		g_windowSetup.isMinimized = (wParam == SIZE_MINIMIZED);

		if (!g_windowSetup.isMinimized) {
			RestoreWindowViewport();
			ViewOrtho(width, height);
			emulator_on_window_resize(width, height);
#ifndef Win7Build
			g_windowSetup.dpiScale = GetDPIScaleForWindow(hWnd);
#else
			g_windowSetup.dpiScale = 1.0f;
#endif
		}

		UpdateCursorClipState(hWnd);
		return 0;
	}

	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU) {
			// Suppress Alt key system menu
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
		AdjustWindowRectEx(&tmp, config->style, FALSE, config->exStyle);
		frameW = (tmp.right - tmp.left) - 100;
		frameH = (tmp.bottom - tmp.top) - 100;

		// Compute current client size
		int fullW = rect->right - rect->left;
		int fullH = rect->bottom - rect->top;
		int clientW = fullW - frameW;
		int clientH = fullH - frameH;

		// Maintain client aspect
		clientW = std::max(clientW, config->minWindowWidth);
		clientH = static_cast<int>(roundf(clientW / config->aspectRatio));

		// Expand to total window size again
		fullW = clientW + frameW;
		fullH = clientH + frameH;

		rect->right = rect->left + fullW;
		rect->bottom = rect->top + fullH;

		LOG_INFO("WM_SIZING Adjusted rect: %dx%d (Client: %dx%d, Aspect %.6f)", fullW, fullH, clientW, clientH, (float)clientW / (float)clientH);
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
			ToggleBorderlessFullscreen(hWnd, g_windowSetup);
			return 0;
		}
		break;
/*
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			PostQuitMessage(0);
			return 0;
		}
		break;
*/
	case WM_ERASEBKGND:
		// Prevent flickering behind OpenGL surface
		return 1;

	case WM_KILLFOCUS:
	case WM_SETFOCUS:
		g_windowSetup.isFocused = (msg == WM_SETFOCUS);
		UpdateCursorClipState(hWnd);
		return 0;

	case WM_ACTIVATEAPP:
		g_windowSetup.isFocused = (wParam != 0);
		UpdateCursorClipState(hWnd);
		return 0;

	case SC_KEYMENU:
	case SC_SCREENSAVE:
	case SC_MONITORPOWER:
		return 0;

	case WM_INPUT:
		return RawInput_ProcessInput(hWnd, wParam, lParam);
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
	const wchar_t CLASS_NAME[] = L"OpenGLWindowClass";

	HICON hIcon = static_cast<HICON>(LoadImage(
		hInstance,
		MAKEINTRESOURCE(IDI_ICON1),
		IMAGE_ICON,
		0, 0,
		LR_DEFAULTSIZE | LR_SHARED
	));

	WNDCLASSW wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	//wc.hInstance = GetModuleHandleW(nullptr); ?? Should I be using this one?
	wc.lpszClassName = CLASS_NAME;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClassW(&wc);

	std::wstring temppath = getpathU(0, 0);
	// -------------------------------------------------------------------------
	// Step 1: Load config + cmdline into a temporary structure
	// -------------------------------------------------------------------------
	WindowSetup temp;
	LoadWindowIniConfig(temp);
	ParseCommandLineArgs(temp);
	bool requestedFullscreen = temp.useFullscreen;
	// -------------------------------------------------------------------------
	// Step 2: Force windowed mode setup to ensure valid rect for restore
	// -------------------------------------------------------------------------
	g_windowSetup = GenerateFinalWindowSetup(/* forceWindowed = */ true);
	// -------------------------------------------------------------------------
	// Step 3: Create window and show it
	// -------------------------------------------------------------------------
	g_hWnd = CreateConfiguredWindow(hInstance, CLASS_NAME, L"AAE Emulator", g_windowSetup);
	if (!g_hWnd) return -1;

	SendMessage(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	SendMessage(g_hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);

	LogOpen("systemlog.txt");

	// Working directory
	if (!SetCurrentDirectory(temppath.c_str())) {
		LOG_ERROR("SetCurrentDirectory failed (%lu)", GetLastError());
	}
	SaveAndDisableAccessibilityPopups();

	// Register Raw Input devices (keyboard + mouse)
	if (FAILED(RawInput_Initialize(g_hWnd))) {
		LOG_ERROR("Failed to initialize Raw Input");
		return false;
	}

	if (!install_joystick()) {
		LOG_INFO("Win32 joystick initialized: %d detected", num_joysticks);
	}
	else {
		LOG_ERROR("No joysticks detected or initialization failed");
	}

	if (!InitOpenGLContext(true, false, false)) {
		LOG_ERROR("Failed to initialize OpenGL");
		return -1;
	}
	// -------------------------------------------------------------------------
	// Step 4: Save a valid windowedRect for fullscreen restore, this does nothing, fix.
	// This is actually the "Window" RECT
	// -------------------------------------------------------------------------
	RECT wr{};
	if (GetWindowRect(g_hWnd, &wr)) {
		g_windowSetup.windowedRect = wr;
		LOG_INFO("Saved windowedRect before fullscreen: (%d,%d)-(%d,%d)",
			wr.left, wr.top, wr.right, wr.bottom);
	}
	else {
		LOG_ERROR("Failed to get windowedRect before fullscreen");
	}
	// Save a Fullscreen Window RECT so we can use it later:
	g_windowSetup.screenRect.left = 0;
	g_windowSetup.screenRect.top = 0;
	g_windowSetup.screenRect.right = GetSystemMetrics(SM_CXSCREEN);
	g_windowSetup.screenRect.bottom = GetSystemMetrics(SM_CYSCREEN);

	// -------------------------------------------------------------------------
	// Step 5: Capture client size and apply projection
	// -------------------------------------------------------------------------
	RECT client{};
	if (GetClientRect(g_hWnd, &client)) {
		g_windowSetup.clientWidth = client.right - client.left;
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
		ToggleBorderlessFullscreen(g_hWnd, g_windowSetup);

	// This sets the High Performance timer. 
	TimerInit(); 

	int argc = 0;
	char** argv = nullptr;

	BuildUTF8Args(argc, argv);
	// Init Emulator Here.
	emulator_init(argc, argv);


	//emulator_init(__argc, __argv);

	// -------------------------------------------------------------------------
	// Step 7: Main high-speed game loop
	// -------------------------------------------------------------------------
	MSG msg = {};
	while (!done)
	{
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT && done == 1)
				goto exit_main;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		poll_joystick();
		emulator_run();
		//GLSwapBuffers(); // provided in gl_basics
	}

exit_main:
	//Shutdown Emulator Here
	emulator_end();
	RestoreAccessibilityPopups();
	TimerShutdown();
	DeleteGLContext();
	RawInput_Shutdown();
	LogClose();
	return static_cast<int>(msg.wParam);
}