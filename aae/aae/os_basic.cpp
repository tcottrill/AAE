#include "framework.h"
#include "sys_log.h"
#include <stdint.h>
#include <string.h>
// This header provides IOCTL_KEYBOARD_SET_INDICATORS and KEYBOARD_INDICATOR_PARAMETERS.
#include <ntddkbd.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

// Keyboard device interface GUID (GUID_DEVINTERFACE_KEYBOARD)
// {884b96c3-56ef-11d1-bc8c-00a0c91405dd}
static const GUID kGuidKeyboardInterface =
{
	0x884b96c3, 0x56ef, 0x11d1,
	{ 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd }
};

static int g_led0_numlock = 0;
static int g_led1_capslock = 0;
static int g_led2_scrolllock = 0;
static HANDLE g_kbdLedHandles[64];
static int g_kbdLedHandleCount = 0;
static HANDLE g_ledEvent = NULL;         // signals "LED state changed"
static volatile LONG g_ledLastRequestedMask = -1;

// Bitmask enum for keyboard LEDs
enum LedBitMask {
	LED_NUMLOCK = 1 << 0,
	LED_CAPSLOCK = 1 << 1,
	LED_SCROLLLOCK = 1 << 2,
};

static volatile LONG g_ledDesiredMask = 0;     // desired LED mask (LedBitMask)
static volatile LONG g_ledServiceRunning = 0;  // 0/1
static HANDLE g_ledThread = NULL;

static void Win32_CloseKeyboardLedTargets()
{
	for (int i = 0; i < g_kbdLedHandleCount; ++i)
	{
		if (g_kbdLedHandles[i] && g_kbdLedHandles[i] != INVALID_HANDLE_VALUE)
			CloseHandle(g_kbdLedHandles[i]);
		g_kbdLedHandles[i] = NULL;
	}
	g_kbdLedHandleCount = 0;
}

static void Win32_EnumKeyboardLedTargets()
{
	Win32_CloseKeyboardLedTargets();

	// Enumerate keyboard device interfaces and open handles once.
	HDEVINFO devs = SetupDiGetClassDevs(&kGuidKeyboardInterface,
		NULL,
		NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (devs != INVALID_HANDLE_VALUE)
	{
		SP_DEVICE_INTERFACE_DATA ifData;
		memset(&ifData, 0, sizeof(ifData));
		ifData.cbSize = sizeof(ifData);

		for (DWORD idx = 0; ; ++idx)
		{
			if (!SetupDiEnumDeviceInterfaces(devs, NULL, &kGuidKeyboardInterface, idx, &ifData))
				break;

			DWORD reqSize = 0;
			SetupDiGetDeviceInterfaceDetail(devs, &ifData, NULL, 0, &reqSize, NULL);
			if (reqSize == 0)
				continue;

			PSP_DEVICE_INTERFACE_DETAIL_DATA detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
			if (!detail)
				continue;

			memset(detail, 0, reqSize);
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			if (SetupDiGetDeviceInterfaceDetail(devs, &ifData, detail, reqSize, NULL, NULL))
			{
				HANDLE h = CreateFile(detail->DevicePath,
					GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);

				if (h == INVALID_HANDLE_VALUE)
				{
					h = CreateFile(detail->DevicePath,
						GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL,
						OPEN_EXISTING,
						0,
						NULL);
				}

				if (h != INVALID_HANDLE_VALUE)
				{
					if (g_kbdLedHandleCount < (int)(sizeof(g_kbdLedHandles) / sizeof(g_kbdLedHandles[0])))
					{
						g_kbdLedHandles[g_kbdLedHandleCount++] = h;
					}
					else
					{
						CloseHandle(h);
					}
				}
			}

			free(detail);
		}

		SetupDiDestroyDeviceInfoList(devs);
	}

	// Fallback to \\.\KeyboardClassX if interface enumeration found nothing.
	if (g_kbdLedHandleCount == 0)
	{
		for (int i = 0; i < 32; ++i)
		{
			char path[64];
			sprintf_s(path, "\\\\.\\KeyboardClass%d", i);

			HANDLE h = CreateFileA(path,
				GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);

			if (h != INVALID_HANDLE_VALUE)
			{
				if (g_kbdLedHandleCount < (int)(sizeof(g_kbdLedHandles) / sizeof(g_kbdLedHandles[0])))
					g_kbdLedHandles[g_kbdLedHandleCount++] = h;
				else
					CloseHandle(h);
			}
		}
	}
}

// Apply LED mask via KeyboardClass IOCTL.
// Returns true if at least one device accepted the IOCTL.
// Apply LED mask via keyboard device interface enumeration first,
// then fall back to \\.\KeyboardClassX.
//
// Returns true if at least one device accepted the IOCTL.
static bool Win32_ApplyKeyboardLeds_IOCTL(int mask)
{
	// If we have no targets, enumerate now (rare path).
	if (g_kbdLedHandleCount == 0)
		Win32_EnumKeyboardLedTargets();

	USHORT flags = 0;
	if (mask & LED_NUMLOCK)    flags |= KEYBOARD_NUM_LOCK_ON;
	if (mask & LED_CAPSLOCK)   flags |= KEYBOARD_CAPS_LOCK_ON;
	if (mask & LED_SCROLLLOCK) flags |= KEYBOARD_SCROLL_LOCK_ON;

	KEYBOARD_INDICATOR_PARAMETERS kip;
	memset(&kip, 0, sizeof(kip));
	kip.UnitId = 0;
	kip.LedFlags = flags;

	bool any_ok = false;

	for (int i = 0; i < g_kbdLedHandleCount; ++i)
	{
		HANDLE h = g_kbdLedHandles[i];
		if (!h || h == INVALID_HANDLE_VALUE)
			continue;

		DWORD bytesReturned = 0;
		BOOL ok = DeviceIoControl(h,
			IOCTL_KEYBOARD_SET_INDICATORS,
			&kip, sizeof(kip),
			NULL, 0,
			&bytesReturned,
			NULL);

		if (ok)
		{
			any_ok = true;
		}
		else
		{
			// If handles go stale (device hotplug), we can re-enumerate later.
			static int spam = 0;
			if (spam++ < 6)
			{
				DWORD e = GetLastError();
				LOG_INFO("LED IOCTL failed on cached handle (err=%lu)", (unsigned long)e);
			}
		}
	}

	return any_ok;
}

// Worker thread: periodically re-apply LEDs (some devices reset LEDs on activity).
static DWORD WINAPI LedServiceThreadProc(LPVOID)
{
	// Keep this thread from competing with the emulation/render loop.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

	// Enumerate once on start.
	Win32_EnumKeyboardLedTargets();

	int lastApplied = -1;

	while (InterlockedCompareExchange(&g_ledServiceRunning, 1, 1) == 1)
	{
		// Wait until there is work. Use a long timeout so we can exit cleanly.
		DWORD w = WaitForSingleObject(g_ledEvent, 250);
		if (w == WAIT_TIMEOUT)
			continue;

		if (InterlockedCompareExchange(&g_ledServiceRunning, 1, 1) != 1)
			break;

		const int desired = (int)InterlockedCompareExchange(&g_ledDesiredMask, 0, 0);
		if (desired != lastApplied)
		{
			Win32_ApplyKeyboardLeds_IOCTL(desired);
			lastApplied = desired;
		}
	}

	Win32_CloseKeyboardLedTargets();
	return 0;
}

void osd_led_service_start()
{
	if (InterlockedCompareExchange(&g_ledServiceRunning, 1, 0) != 0)
		return;

	// Create an auto-reset event (one wake per signal).
	if (!g_ledEvent)
		g_ledEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (!g_ledEvent)
	{
		InterlockedExchange(&g_ledServiceRunning, 0);
		return;
	}

	// Clear any cached targets so the thread will enumerate fresh.
	Win32_CloseKeyboardLedTargets();

	g_ledThread = CreateThread(NULL, 0, LedServiceThreadProc, NULL, 0, NULL);
	if (!g_ledThread)
	{
		InterlockedExchange(&g_ledServiceRunning, 0);
		CloseHandle(g_ledEvent);
		g_ledEvent = NULL;
	}
}

void osd_led_service_stop()
{
	if (InterlockedCompareExchange(&g_ledServiceRunning, 0, 1) != 1)
		return;

	// Wake the worker so it can exit promptly.
	if (g_ledEvent)
		SetEvent(g_ledEvent);

	if (g_ledThread)
	{
		WaitForSingleObject(g_ledThread, 2000);
		CloseHandle(g_ledThread);
		g_ledThread = NULL;
	}

	Win32_CloseKeyboardLedTargets();

	if (g_ledEvent)
	{
		CloseHandle(g_ledEvent);
		g_ledEvent = NULL;
	}
}

void osd_set_leds(int state)
{
	// Normalize to the 3-bit mask we support (Num/Caps/Scroll).
	state &= (LED_NUMLOCK | LED_CAPSLOCK | LED_SCROLLLOCK);

	// If the request hasn't changed, do nothing (no event, no IO).
	const LONG prev = InterlockedExchange(&g_ledLastRequestedMask, (LONG)state);
	if ((int)prev == state)
		return;

	// Store desired mask for the service thread.
	InterlockedExchange(&g_ledDesiredMask, (LONG)state);

	// Wake LED service thread to apply it (thread does the IOCTL).
	if (g_ledEvent)
		SetEvent(g_ledEvent);
}

int osd_get_leds()
{
	return (int)InterlockedCompareExchange(&g_ledDesiredMask, 0, 0);
}

// -----------------------------------------------------------------------------
// set_led_status
// Compatibility wrapper for legacy driver code.
//
// LED mapping:
//   0 -> NumLock
//   1 -> CapsLock
//   2 -> ScrollLock
//
// Parameters:
//   which - LED index (0..2)
//   on    - non-zero = on, 0 = off
// -----------------------------------------------------------------------------
void set_led_status(int which, int on)
{
	const int v = (on != 0) ? 1 : 0;

	if (which == 0) g_led0_numlock = v;
	else if (which == 1) g_led1_capslock = v;
	else if (which == 2) g_led2_scrolllock = v;
	else return;

	int mask = 0;
	if (g_led0_numlock)   mask |= (1 << 0); // NumLock
	if (g_led1_capslock)  mask |= (1 << 1); // CapsLock
	if (g_led2_scrolllock) mask |= (1 << 2); // ScrollLock

	osd_set_leds(mask);
}

// -----------------------------------------------------------------------------
// get_led_status
// Returns the currently latched LED state from set_led_status.
//
// Parameters:
//   which - LED index (0..2)
//
// Returns:
//   1 if on, 0 if off, or 0 for invalid index.
// -----------------------------------------------------------------------------
int get_led_status(int which)
{
	if (which == 0) return g_led0_numlock;
	if (which == 1) return g_led1_capslock;
	if (which == 2) return g_led2_scrolllock;
	return 0;
}

// -----------------------------------------------------------------------------
// set_led_status_all
// Sets all three LEDs at once using the same mapping as set_led_status.
//
// Parameters:
//   led0 - NumLock (0/1)
//   led1 - CapsLock (0/1)
//   led2 - ScrollLock (0/1)
// -----------------------------------------------------------------------------
void set_led_status_all(int led0, int led1, int led2)
{
	g_led0_numlock = (led0 != 0) ? 1 : 0;
	g_led1_capslock = (led1 != 0) ? 1 : 0;
	g_led2_scrolllock = (led2 != 0) ? 1 : 0;

	int mask = 0;
	if (g_led0_numlock)   mask |= (1 << 0);
	if (g_led1_capslock)  mask |= (1 << 1);
	if (g_led2_scrolllock) mask |= (1 << 2);

	osd_set_leds(mask);
}

bool Calculate(int cx, int cy, RECT& rect)
{
	HWND hWnd = win_get_window();

	if (hWnd == NULL)
		return FALSE;

	HDC hDC = ::GetDC(NULL);
	const int w = GetDeviceCaps(hDC, HORZRES);
	const int h = GetDeviceCaps(hDC, VERTRES);
	::ReleaseDC(NULL, hDC);

	RECT rcWindow;
	GetWindowRect(hWnd, &rcWindow);
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	cx += (rcWindow.right - rcWindow.left) - rcClient.right;
	cy += (rcWindow.bottom - rcWindow.top) - rcClient.bottom;

	rect.left = (w >> 1) - (cx >> 1);
	rect.top = (h >> 1) - (cy >> 1);
	rect.right = rect.left + cx;
	rect.bottom = rect.top + cy;

	return TRUE;
}

void ClientResize(int nWidth, int nHeight)
{
	HWND hWnd = win_get_window();
	RECT rcClient, rcWind;
	POINT ptDiff;
	GetClientRect(hWnd, &rcClient);
	GetWindowRect(hWnd, &rcWind);
	ptDiff.x = (rcWind.right - rcWind.left) - rcClient.right;
	ptDiff.y = (rcWind.bottom - rcWind.top) - rcClient.bottom;
	MoveWindow(hWnd, rcWind.left, rcWind.top, nWidth + ptDiff.x, nHeight + ptDiff.y, TRUE);
}

void Set_ForeGround()
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
	//remove_mouse(); //Reset mouse to ensure cursor is removed and to regain exclusive control
	//install_mouse();
}

void center_window()
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

	//lWS = GetWindowLong(wnd,GWL_STYLE); // for current WS style.
	//lWSEX = GetWindowLong(WS_EX); //for current WS_EX style.

	// Bitwise /*OR*/AND the flags you don't want (WS_OVERLAPPEDWINDOW etc.)
	//lWS /*=|*/ &= ~WS_OVERLAPPEDWINDOW;
	//lWSEX /*=|*/ &= ~WS_EX_WHATEVERELSE

	// Add the flags you want.
	//lWS /*=&*/ |= WS_POPUP;
	//lWSEX /*=&*/ |= WS_EX_WHATEVERELSE

	// Set the new styles:
	//SetWindowLong(wnd,GWL_STYLE,lWS);
	//SetWindowLong(wnd,GWL_STYLE,lWSEX);
	//THIS IS JUST FOR TESTING PURPOSES!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//Update window
	//SetWindowPos(wnd,HWND_TOP,0,SCREEN_H,SCREEN_W,SCREEN_H,SWP_SHOWWINDOW );
}

void SetTopMost(const bool TopMost)
{
	//ASSERT(::IsWindow(hWnd));
	HWND hWndInsertAfter = (TopMost ? HWND_TOPMOST : HWND_NOTOPMOST);
	SetWindowPos(win_get_window(), hWndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//SetWindowPos(hWnd, HWND_TOP, 0, 0, 1920, 1080, SWP_NOMOVE | SWP_NOSIZE);//SWP_NOZORDER | SWP_NOACTIVATE);
}

void setwindow()
{
	::SetWindowLong(win_get_window(), GWL_STYLE, ::GetWindowLong(win_get_window(), GWL_STYLE) & ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME));
	::SetWindowLong(win_get_window(), GWL_EXSTYLE, ::GetWindowLong(win_get_window(), GWL_EXSTYLE) & ~WS_EX_DLGMODALFRAME);
}

// Get the horizontal and vertical screen sizes in pixel
void GetDesktopResolution(int& horizontal, int& vertical)
{
	RECT desktop;
	// Get a handle to the desktop window
	const HWND hDesktop = GetDesktopWindow();
	// Get the size of screen to the variable desktop
	GetWindowRect(hDesktop, &desktop);
	// The top left corner will have coordinates (0,0)
	// and the bottom right corner will have coordinates
	// (horizontal, vertical)
	horizontal = desktop.right;
	vertical = desktop.bottom;
}

void GetRefresh()
{
	DEVMODE dm;
	// initialize the DEVMODE structure
	ZeroMemory(&dm, sizeof(dm));
	dm.dmSize = sizeof(dm);

	if (0 != EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm))
	{
		LOG_INFO("Primary Monitor refresh rate: %d", dm.dmDisplayFrequency);
		// inspect the DEVMODE structure to obtain details
		// about the display settings such as
		//  - Orientation
		//  - Width and Height
		//  - Frequency
		dm.dmDisplayFrequency = 60;
		ChangeDisplaySettings(&dm, 0);

		if (0 != EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm))
		{
			LOG_INFO("Primary Monitor refresh rate: %d", dm.dmDisplayFrequency);
		}
	}
}

//--------------------------------------------------------------------------------------
// Limit the current thread to one processor (the current one). This ensures that timing code
// runs on only one processor, and will not suffer any ill effects from power management.
// See "Game Timing and Multicore Processors" for more details
//--------------------------------------------------------------------------------------
void LimitThreadAffinityToCurrentProc()
{
	HANDLE hCurrentProcess = GetCurrentProcess();

	DWORD_PTR dwProcessAffinityMask = 0;
	DWORD_PTR dwSystemAffinityMask = 0;

	if (GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask) != 0 && dwProcessAffinityMask)
	{
		DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

		HANDLE hCurrentThread = GetCurrentThread();
		if (hCurrentThread != INVALID_HANDLE_VALUE)
		{
			SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
		}
	}
}

void SetProcessorAffinity()
{
	DWORD_PTR dwProcessAffinityMask = 0;
	DWORD_PTR dwSystemAffinityMask = 0;
	HANDLE hCurrentProcess = GetCurrentProcess();

	if (!GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask))
		return;

	if (dwProcessAffinityMask)
	{
		DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

		HANDLE hCurrentThread = GetCurrentThread();
		if (hCurrentThread != INVALID_HANDLE_VALUE)
		{
			SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
		}
	}
}