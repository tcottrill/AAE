// Includes
#include <windows.h>
// For command line processing
#include <vector>
#include <algorithm>
//
#include "framework.h"
#include "rawinput.h"
#include "sys_gl.h"
#include "gl_prim_debug.h"
#include "aae_emulator.h"
#include "iniFile.h"
#include "wintimer.h"
#include "aae_mame_driver.h"
#include "path_helper.h"

#ifndef WIN7BUILD
#include "win10_win11_required_code.h"
#endif // WIN7BUILD

//Globals
HWND hWnd;
int SCREEN_W = 1024;
int SCREEN_H = 768;
int currentWinWidth = 1024;
int currentWinHeight = 768;



// Function Declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);




//========================================================================
// Hide mouse cursor (lock it)
//========================================================================
void scare_mouse()
{
	// Get the window client area.
	RECT rc;
	GetClientRect(hWnd, &rc);

	// Convert the client area to screen coordinates.
	POINT pt = { rc.left, rc.top };
	POINT pt2 = { rc.right, rc.bottom };
	ClientToScreen(hWnd, &pt);
	ClientToScreen(hWnd, &pt2);
	SetRect(&rc, pt.x, pt.y, pt2.x, pt2.y);

	// Confine the cursor.
	ClipCursor(&rc);

	//Turn off the cursor pointer
	//ShowCursor(FALSE);
	while (ShowCursor(false) >= 0);

	// Capture cursor to user window
	SetCapture(hWnd);
}

//========================================================================
// Show mouse cursor (unlock it)
//========================================================================
void show_mouse()
{
	// Un-capture cursor
	ReleaseCapture();

	// Release the cursor from the window
	ClipCursor(NULL);

	ShowCursor(true);
}

void CaptureMouseToWindow(HWND hwnd)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	POINT pt = { rc.left, rc.top };
	POINT pt2 = { rc.right, rc.bottom };
	ClientToScreen(hwnd, &pt);
	ClientToScreen(hwnd, &pt2);
	SetRect(&rc, pt.x, pt.y, pt2.x, pt2.y);
	ClipCursor(&rc);
	SetCapture(hwnd);
}

void ReleaseMouseFromWindow()
{
	ClipCursor(nullptr);
	ReleaseCapture();
}

int KeyCheck(int keynum)
{
	int i;
	static int hasrun = 0;
	static int keys[256];
	//Init
	if (hasrun == 0) { for (i = 0; i < 256; i++) { keys[i] = 0; }	hasrun = 1; }

	if (!keys[keynum] && key[keynum]) //Return True if not in que
	{
		keys[keynum] = 1;	return 1;
	}
	else if (keys[keynum] && !key[keynum]) //Return False if in que
		keys[keynum] = 0;
	return 0;
}

void osMessage(const char* caption, const char* fmt, ...) {
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, fmt, args);
	va_end(args);
	MessageBoxA(hWnd, buffer, caption, MB_ICONERROR | MB_OK);
}

enum WindowsOS {
	NotFind,
	Win2000,
	WinXP,
	WinVista,
	Win7,
	Win8,
	Win81,
	Win10,
	Win11
};

WindowsOS GetOsVersion()
{
	using namespace std;
	double ret = 0.0;
	NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW);
	OSVERSIONINFOEXW osInfo;

	ZeroMemory(&osInfo, sizeof(OSVERSIONINFOEXW));
	osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

	*(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

	if (NULL != RtlGetVersion)
	{
		osInfo.dwOSVersionInfoSize = sizeof(osInfo);
		RtlGetVersion(&osInfo);
		ret = (double)osInfo.dwMajorVersion;
	}
	if (osInfo.dwMajorVersion == 11)
	{
		LOG_INFO("this is windows 11\n");
		return Win11;
	}

	if (osInfo.dwMajorVersion == 10 && osInfo.dwMinorVersion == 0)
	{
		LOG_INFO("this is windows 10\n");
		return Win10;
	}
	else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3)
	{
		LOG_INFO("this is windows 8.1\n");
		return Win8;
	}
	else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2)
	{
		LOG_INFO("this is windows 8\n");
		return Win8;
	}
	else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1)
	{
		LOG_INFO("this is windows 7 or Windows Server 2008 R2\n");
		return Win7;
	}

	return NotFind;
}

//========================================================================
// Popup a Windows Error Message, Allegro Style
//========================================================================
void allegro_message(const char* title, const char* message)
{
	MessageBoxA(NULL, message, title, MB_ICONEXCLAMATION | MB_OK | MB_TOPMOST);
}

//========================================================================
// Return a std string with last error message
//========================================================================
std::string GetLastErrorStdStr()
{
	DWORD error = GetLastError();
	if (error)
	{
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);
		if (bufLen)
		{
			LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
			std::string result(lpMsgStr, lpMsgStr + bufLen);

			LocalFree(lpMsgBuf);

			return result;
		}
	}
	return std::string();
}

//========================================================================
// Return the Window Handle
//========================================================================
HWND win_get_window()
{
	return hWnd;
}

void ResizeWindowToFullscreenWithBorders(HWND hWnd, float aspectRatio)
{
	// Restore standard window borders
	DWORD style = WS_OVERLAPPEDWINDOW;
	DWORD exStyle = WS_EX_APPWINDOW;
	SetWindowLong(hWnd, GWL_STYLE, style);
	SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);

	// Get the monitor dimensions excluding the taskbar
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(hMonitor, &mi);
	RECT workArea = mi.rcWork;  // This excludes the taskbar

	int screenW = workArea.right - workArea.left;
	int screenH = workArea.bottom - workArea.top;

	// Compute target window size with same aspect ratio
	int targetW = screenW;
	int targetH = static_cast<int>(targetW / aspectRatio);

	if (targetH > screenH) {
		targetH = screenH;
		targetW = static_cast<int>(targetH * aspectRatio);
	}

	// Compute centered position
	int posX = workArea.left + (screenW - targetW) / 2;
	int posY = workArea.top + (screenH - targetH) / 2;

	// Adjust for window style (so client area is correct)
	RECT windowRect = { 0, 0, targetW, targetH };
	AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
	int winW = windowRect.right - windowRect.left;
	int winH = windowRect.bottom - windowRect.top;

	SetWindowPos(hWnd, nullptr, posX, posY, winW, winH, SWP_NOZORDER | SWP_FRAMECHANGED);
	SCREEN_W = winW;
	SCREEN_H = winH;
//	ViewOrtho(SCREEN_W, SCREEN_H);
}

/*
void CenterWindow(void)
{
	//   long lWS;
   //    long lWSEX;

	HWND wnd = win_get_window();
	HWND desktop = GetDesktopWindow();
	RECT wndRect, desktopRect;
	int  w, h, dw, dh;

	GetWindowRect(wnd, &wndRect);
	GetWindowRect(desktop, &desktopRect);
	w = wndRect.right - wndRect.left;
	h = wndRect.bottom - wndRect.top;
	dw = desktopRect.right - desktopRect.left;
	dh = desktopRect.bottom - desktopRect.top;

	MoveWindow(wnd, (dw - w) / 2, (dh - h) / 2, w, h, TRUE);
}

void SetWindowForeground(void)
{
	HWND hCurrWnd;
	int iMyTID;
	int iCurrTID;

	hCurrWnd = win_get_window();
	iMyTID = GetCurrentThreadId();
	iCurrTID = GetWindowThreadProcessId(hCurrWnd, 0);

	AttachThreadInput(iMyTID, iCurrTID, TRUE);
	SetForegroundWindow(hCurrWnd);
	AttachThreadInput(iMyTID, iCurrTID, FALSE);
}

void AdjustWindowRectForBorders(const int borders, const int x, const int y,
	const int width, const int height, RECT& r)
{
	DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	if (borders)
	{
		style |= WS_OVERLAPPEDWINDOW;
	}
	else
	{
		style |= WS_POPUP;
	}
	r.left = x;
	r.top = y;
	r.right = r.left + width;
	r.bottom = r.top + height;
	bool result = AdjustWindowRect(&r, style, FALSE);
	if (!result)
	{
		LOG_INFO("AdjustWindowRect failed, error: ");
	}
}
*/
std::string toLowerCase(const std::string& str) {
	std::string result = str;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
		return std::tolower(c);
		});
	return result;
}

// WinMain
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	WNDCLASS wc;

	//Added in
	DWORD dwStyle, dwExStyle;
	int x, y, width, height;
	int adj;
	RECT WorkArea{};
	RECT rect;
	MSG msg;
	bool quit = FALSE;
	
	std::string temppath;

	//Buffer for command line parsing
	std::vector<std::string> my_argv_buf;
	std::vector<char*> my_argv;

	// register window class
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "Emulator";
	
	if (!RegisterClass(&wc)) {
		MessageBox(NULL, "Window Registration Failed!", "Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	// TODO: What is all this window sizing garbage? Please fix this mess
	//
	//
	//
	//

	// These are setup for Window mode, need tro add different ones for a full screen Window.
	dwStyle = WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN; //| WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	dwExStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;
	//Adjust the window to account for the frame
	RECT wr = { 0, 0, SCREEN_W, SCREEN_H };
	// Set the size to account for the title bar and borders (really important)
	AdjustWindowRect(&wr, dwStyle, FALSE);

	// Get window size and position as maximized
	SystemParametersInfo(SPI_GETWORKAREA, 0, &WorkArea, 0);
	x = WorkArea.left;
	y = WorkArea.top;
	//WorkArea.bottom += 8;
	width = (WorkArea.right - WorkArea.left);
	height = (WorkArea.bottom - WorkArea.top);

	//Get the type of Window we want to make.
	SetIniFile("./emulator.ini");
	int sz = get_config_int("main", "winsize", 0);
	if (sz > 1) sz = 1;
	double winsize[] = { 1.25,.75 };

	//adjust for 4:3 window sizing based on the screen resolution - This is assuming a square pixel ratio , needs fixed
	// 1.25 is a normal 4:3 screen wide, .75 is a rotated screen, need to check config, or have this passed as a parameter.
	if ((width - x) > (height * winsize[sz])) //1.25 /.75
	{
		adj = (int)((double)height * (double)winsize[sz]); //1.25 .75
		x += (int)((width - adj) / 2.0);
		width -= (width - adj);
	}
	SetRect(&rect, 0, 0, width, height);
	AdjustWindowRectEx(&rect, dwStyle, FALSE, dwExStyle);
	//
	//
	///////////////// FIX THIS CRAP
	//
	// ResizeWindowToFullscreenWithBorders(win_get_window(), 4.0f / 3.0f);
	//
	//
	// Create The Window
	if (!(hWnd = CreateWindowEx(dwExStyle,							// Extended Style For The Window
		"EMULATOR",						// Class Name
		"AAE",					    // Window Title
		dwStyle,					        // Required Window Style
		x, y,   				// Window Position
		width,	// Calculate Window Width
		height,	// Calculate Window Height
		NULL,								// No Parent Window
		NULL,								// No Menu
		GetModuleHandle(NULL),							// Instance
		NULL)))								// Dont Pass Anything To WM_CREATE
	{
		// Reset The Display
		MessageBox(NULL, "Window Creation Error.", "ERROR", MB_OK | MB_ICONEXCLAMATION);
		return FALSE;
	}

	// Make sure the window shows up.
	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
	SetForegroundWindow(hWnd);
	SetFocus(hWnd);

	::SetWindowPos(win_get_window(),       // handle to window
		HWND_TOPMOST,  // placement-order handle
		0,     // horizontal position
		0,      // vertical position
		0,  // width
		0, // height
		SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE// window-positioning options
	);

	// Set the current working directory to the location of the executable.
	// This solves any issues with running from a front end in a different folder.
	temppath = getpathM(0, 0);

	if (!SetCurrentDirectory(temppath.c_str()))
	{
		fprintf(stderr, "SetCurrentDirectory failed (%lu)\n", GetLastError());
	}

	LogOpen("./aaelog.txt");

#ifndef WIN7BUILD
	Log::setConsoleOutputEnabled(false);
#endif

	LOG_INFO("Starting Log");

#ifndef WIN7BUILD
	disable_windows10_window_scaling();
#endif // WIN7BUILD

	///////////////// Initialize everything here //////////////////////////////
	// Get the actual screen area for OpenGL.
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	int gl_width = clientRect.right - clientRect.left;
	int gl_height = clientRect.bottom - clientRect.top;
	LOG_INFO("Actual Screen Width %d, Actual Screen Height %d", gl_width, gl_height);

	SCREEN_W = gl_width;
	SCREEN_H = gl_height;

	AllowAccessibilityShortcutKeys(false);

	//Init OS Timer
	TimerInit(); //Start timer
	

	//Setup cmd line parsing (Not currently being used, but here so I don't lose it.
	int w_argc = 0;
	LPWSTR* w_argv = CommandLineToArgvW(GetCommandLineW(), &w_argc);
	if (w_argv)
	{
		my_argv_buf.reserve(w_argc);

		for (int i = 0; i < w_argc; ++i)
		{
			int w_len = lstrlenW(w_argv[i]);
			int len = WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, NULL, 0, NULL, NULL);
			std::string s;
			s.resize(len);
			WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, &s[0], len, NULL, NULL);
			my_argv_buf.push_back(toLowerCase(s));
			LOG_INFO("string here is %s", my_argv_buf[i].c_str());
		}

		my_argv.reserve(my_argv_buf.size());

		for (size_t i = 0; i < my_argv_buf.size(); ++i)
		{
			my_argv.push_back(const_cast<char*>(my_argv_buf[i].c_str()));
		}
		LocalFree(w_argv);
	}
	else
	{
		LOG_INFO("NOPENOPENOPENOPEN");
	}
	// enable OpenGL for the window
	InitOpenGLContext(true);
	// Set the Swap Interval to 60Hz, or whatever the monitor is set to.
	// Disabled for now.
	if (WGLEW_EXT_swap_control)
	{
		//osWaitVsync(true);
	}
	// Initalize Rawinput
	if ((RawInput_Initialize(hWnd) != 0))
	{
		// Something is really broken, abort.
		MessageBox(NULL, "Fail: Unable to init RawInput.", "ERROR", MB_OK | MB_ICONEXCLAMATION);
		//return FALSE;
	}
	//Setup OpenGL
	ViewOrtho(SCREEN_W, SCREEN_H);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	scare_mouse();
	CaptureMouseToWindow(hWnd);
	SetFocus(hWnd);

	//ResizeWindowToFullscreenWithBorders(win_get_window(), 4.0f / 3.0f);
	
	// Init Emulator Here.
	emulator_init(__argc, __argv);

	//
	/////////////////// END INITIALIZATION ////////////////////////////////////

	// program main loop
	while (!done)
	{
		// check for messages
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// handle or dispatch messages
			if (msg.message == WM_QUIT)
			{
				quit = TRUE;
			}
			else
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else
		{
			// Run Emulator Here.
			if (config.debug_profile_code) { LOG_INFO("starting emulator run"); }
			//CaptureMouseToWindow(hWnd);
			emulator_run();

			int err = glGetError();
			if (err != 0)
			{
				LOG_INFO("openglerror in before swap buffer: %d", err);
			}
			if (config.debug_profile_code) { LOG_INFO("Swapping Buffers"); }
			GLSwapBuffers();
			err = glGetError();
			if (err != 0)
			{
				LOG_INFO("openglerror in after swap buffer: %d", err);
			}
		}
	}

	//Shutdown Emulator Here
	emulator_end();
	// shutdown OpenGL
	OpenGLShutDown();

	AllowAccessibilityShortcutKeys(true);
	//Shutdown logging
	LogClose();
	// destroy the window explicitly
	DestroyWindow(hWnd);

	return 0;
}

// Window Procedure

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static RECT windowRect;

	switch (message)
	{
	case WM_CREATE:
		GetClientRect(hWnd, &windowRect);
		ShowCursor(FALSE);               // Hide cursor
		ClipCursor(&windowRect);         // Clip cursor to window
		return 0;

	case WM_DPICHANGED:
	{
		RECT* newRect = reinterpret_cast<RECT*>(lParam);
		SetWindowPos(hWnd, NULL,
			newRect->left, newRect->top,
			newRect->right - newRect->left,
			newRect->bottom - newRect->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		// TODO: Handle other DPI-dependent updates
		return 0;
	}

	case WM_INPUT:
		return RawInput_ProcessInput(hWnd, wParam, lParam);

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT) {
			SetCursor(NULL);  // Hide the hardware cursor in client area
			return TRUE;
		}
		break;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			PostQuitMessage(0);
			return 0;
		}
		break;

	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		if (wParam == VK_MENU) {
			return 0;  // Suppress Alt key (system menu)
		}
		break;

	case WM_CLOSE:
	case WM_DESTROY:
		ShowCursor(TRUE);         // Restore cursor
		ClipCursor(NULL);         // Release cursor clip
		PostQuitMessage(0);
		return 0;

	case WM_SYSCOMMAND:
		switch (wParam & 0xFFF0)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
		case SC_KEYMENU:
			return 0;  // Block screen saver, monitor power, ALT menu
		default:
			break;
		}
		break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}
