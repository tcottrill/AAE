
//#include <Windows.h>
#include "framework.h"
#include "log.h"


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
void GetDesktopResolution(int &horizontal, int &vertical)
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

	// Get the processor affinity mask for this process
	DWORD_PTR dwProcessAffinityMask = 0;
	DWORD_PTR dwSystemAffinityMask = 0;

	if (GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask) != 0 && dwProcessAffinityMask)
	{
		// Find the lowest processor that our process is allows to run against
		DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

		// Set this as the processor that our thread must always run against
		// This must be a subset of the process affinity mask
		HANDLE hCurrentThread = GetCurrentThread();
		if (INVALID_HANDLE_VALUE != hCurrentThread)
		{
			SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
			CloseHandle(hCurrentThread);
		}
	}

	CloseHandle(hCurrentProcess);
}


static void toggleKey(int key) 
{
	// Simulate a key press
	keybd_event(key, 0, 0, 0);
	// Simulate a key release
	keybd_event(key, 0, KEYEVENTF_KEYUP, 0);
}

/*
void force_all_kbdleds_off()
{
	// Check Caps Lock state
	SHORT capsLockState = GetKeyState(VK_CAPITAL);
	if (capsLockState & 0x0001)	toggleKey(VK_CAPITAL);

	// Check Num Lock state
	SHORT numLockState = GetKeyState(VK_NUMLOCK);
	if (numLockState & 0x0001)	toggleKey(VK_NUMLOCK);

	// Check Scroll Lock state
	SHORT scrollLockState = GetKeyState(VK_SCROLL);
	if (scrollLockState & 0x0001)	toggleKey(VK_SCROLL);

}
*/
//============================================================
//	osd_get_leds
//============================================================
/*
int osd_get_leds()
{
	BYTE key_states[256];
	int result = 0;

	// get the current state
	GetKeyboardState(&key_states[0]);

	// set the numl0ck bit
	result |= (key_states[VK_NUMLOCK] & 1);
	result |= (key_states[VK_CAPITAL] & 1) << 1;
	result |= (key_states[VK_SCROLL] & 1) << 2;
	return result;
}
*/

//============================================================
//	osd_set_leds
//============================================================
/*
void osd_set_leds(int state)
{
	BYTE key_states[256];
	int oldstate, newstate;

	// thanks to Lee Taylor for the original version of this code

	// get the current state
	GetKeyboardState(&key_states[0]);

	// see if the numlock key matches the state
	oldstate = key_states[VK_NUMLOCK] & 1;
	newstate = state & 1;

	// if not, simulate a key up/down
	if (oldstate != newstate)
	{
		keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
		keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
	}
	key_states[VK_NUMLOCK] = (key_states[VK_NUMLOCK] & ~1) | newstate;

	// see if the caps lock key matches the state
	oldstate = key_states[VK_CAPITAL] & 1;
	newstate = (state >> 1) & 1;

	// if not, simulate a key up/down
	if (oldstate != newstate)
	{
		keybd_event(VK_CAPITAL, 0x3a, 0, 0);
		keybd_event(VK_CAPITAL, 0x3a, KEYEVENTF_KEYUP, 0);
	}
	key_states[VK_CAPITAL] = (key_states[VK_CAPITAL] & ~1) | newstate;

	// see if the scroll lock key matches the state
	oldstate = key_states[VK_SCROLL] & 1;
	newstate = (state >> 2) & 1;

	// if not, simulate a key up/down
	if (oldstate != newstate)
	{
		keybd_event(VK_SCROLL, 0x46, 0, 0);
		keybd_event(VK_SCROLL, 0x46, KEYEVENTF_KEYUP, 0);
	}
	key_states[VK_SCROLL] = (key_states[VK_SCROLL] & ~1) | newstate;

}

*/

// Bitmask enum for keyboard LEDs
enum LedBitMask {
	LED_NUMLOCK = 1 << 0,
	LED_CAPSLOCK = 1 << 1,
	LED_SCROLLLOCK = 1 << 2,
};

// Helper: check current lock key state
bool IsLockKeyOn(WORD vk)
{
	BYTE keyState[256];
	if (!GetKeyboardState(keyState)) return false;
	return (keyState[vk] & 1) != 0;
}

// Helper: simulate toggle if state differs from desired
void ToggleLockKey(WORD vk, bool desiredState)
{
	if (IsLockKeyOn(vk) == desiredState)
		return;

	INPUT inputs[2] = {};

	// Key down
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = vk;

	// Key up
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = vk;
	inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

	SendInput(2, inputs, sizeof(INPUT));
}

// Set LEDs based on bitmask
void osd_set_leds(int state)
{
	ToggleLockKey(VK_NUMLOCK, (state & LED_NUMLOCK) != 0);
	ToggleLockKey(VK_CAPITAL, (state & LED_CAPSLOCK) != 0);
	ToggleLockKey(VK_SCROLL, (state & LED_SCROLLLOCK) != 0);
}

// Get LEDs as bitmask
int osd_get_leds()
{
	int state = 0;
	if (IsLockKeyOn(VK_NUMLOCK))    state |= LED_NUMLOCK;
	if (IsLockKeyOn(VK_CAPITAL))    state |= LED_CAPSLOCK;
	if (IsLockKeyOn(VK_SCROLL))     state |= LED_SCROLLLOCK;
	return state;
}

void SetProcessorAffinity()
{
	// Assign the current thread to one processor. This ensures that timing
	// code runs on only one processor, and will not suffer any ill effects
	// from power management.
	//
	// Based on DXUTSetProcessorAffinity() function from the DXUT framework.

	DWORD_PTR dwProcessAffinityMask = 0;
	DWORD_PTR dwSystemAffinityMask = 0;
	HANDLE hCurrentProcess = GetCurrentProcess();

	if (!GetProcessAffinityMask(hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask))
		return;

	if (dwProcessAffinityMask)
	{
		// Find the lowest processor that our process is allowed to run against.

		DWORD_PTR dwAffinityMask = (dwProcessAffinityMask & ((~dwProcessAffinityMask) + 1));

		// Set this as the processor that our thread must always run against.
		// This must be a subset of the process affinity mask.

		HANDLE hCurrentThread = GetCurrentThread();

		if (hCurrentThread != INVALID_HANDLE_VALUE)
		{
			SetThreadAffinityMask(hCurrentThread, dwAffinityMask);
			CloseHandle(hCurrentThread);
		}
	}

	CloseHandle(hCurrentProcess);
}