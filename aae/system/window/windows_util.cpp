// -----------------------------------------------------------------------------
// windows_util.cpp
//
// Implementation of Win32 window creation, fullscreen handling, and DPI setup
// using a unified WindowConfig structure.
// -----------------------------------------------------------------------------

#include "windows_util.h"
#include "sys_log.h"
#include "framework.h"
#include "utf8conv.h"

WindowsOS GetOsVersion()
{
	typedef LONG NTSTATUS;
	typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(LPOSVERSIONINFOEXW);

	OSVERSIONINFOEXW osInfo{};
	osInfo.dwOSVersionInfoSize = sizeof(osInfo);

	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (!ntdll) return NotFind;

	auto rtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
	if (!rtlGetVersion) return NotFind;

	if (rtlGetVersion(&osInfo) != 0) return NotFind;

	if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000) {
		LOG_INFO("This is Windows 11");
		return Win11;
	}
	if (osInfo.dwMajorVersion == 10) {
		LOG_INFO("This is Windows 10");
		return Win10;
	}
	if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) return Win8;
	if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) return Win7;
	return NotFind;
}

static STICKYKEYS g_stickyKeys = { sizeof(STICKYKEYS), 0 };
static TOGGLEKEYS g_toggleKeys = { sizeof(TOGGLEKEYS), 0 };
static FILTERKEYS g_filterKeys = { sizeof(FILTERKEYS), 0 };

// -----------------------------------------------------------------------------
// SaveAndDisableAccessibilityPopups
// Disables StickyKeys, ToggleKeys, and FilterKeys accessibility popups
// by saving the current state and updating system parameters.
// -----------------------------------------------------------------------------
void SaveAndDisableAccessibilityPopups()
{
	// StickyKeys
	SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(g_stickyKeys), &g_stickyKeys, 0);
	if (g_stickyKeys.dwFlags & SKF_HOTKEYACTIVE) {
		STICKYKEYS sk = g_stickyKeys;
		sk.dwFlags &= ~SKF_HOTKEYACTIVE;
		sk.dwFlags &= ~SKF_CONFIRMHOTKEY;
		SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(sk), &sk, 0);
	}

	// ToggleKeys
	SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(g_toggleKeys), &g_toggleKeys, 0);
	if (g_toggleKeys.dwFlags & TKF_HOTKEYACTIVE) {
		TOGGLEKEYS tk = g_toggleKeys;
		tk.dwFlags &= ~TKF_HOTKEYACTIVE;
		tk.dwFlags &= ~TKF_CONFIRMHOTKEY;
		SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(tk), &tk, 0);
	}

	// FilterKeys
	SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(g_filterKeys), &g_filterKeys, 0);
	if (g_filterKeys.dwFlags & FKF_HOTKEYACTIVE) {
		FILTERKEYS fk = g_filterKeys;
		fk.dwFlags &= ~FKF_HOTKEYACTIVE;
		fk.dwFlags &= ~FKF_CONFIRMHOTKEY;
		SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(fk), &fk, 0);
	}
}

// -----------------------------------------------------------------------------
// RestoreAccessibilityPopups
// Restores StickyKeys, ToggleKeys, and FilterKeys settings
// to their original state using previously saved values.
// -----------------------------------------------------------------------------
void RestoreAccessibilityPopups()
{
	SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(g_stickyKeys), &g_stickyKeys, 0);
	SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(g_toggleKeys), &g_toggleKeys, 0);
	SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(g_filterKeys), &g_filterKeys, 0);
}

// -----------------------------------------------------------------------------
// osMessage
// Displays a formatted message box using a UTF-8 format string and ID.
// ID values (e.g., IDOK or IDCANCEL) determine the icon type.
// UTF-8 errors are caught and fallback to ANSI message box.
// -----------------------------------------------------------------------------
void osMessage(int ID, const char* fmt, ...)
{
	int mType = MB_ICONERROR;
	char text[256] = {};
	va_list ap;
	if (!fmt) return;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	switch (ID)
	{
	case IDCANCEL: mType = MB_ICONERROR; break;
	case IDOK:     mType = MB_ICONASTERISK; break;
	}

	try {
		std::wstring wideText = win32::Utf8ToUtf16(text);
		std::wstring title = L"Message";

		MessageBoxW(win_get_window(), wideText.c_str(), title.c_str(), MB_OK | mType);
	}
	catch (const win32::Utf8ConversionException& e) {
		std::string fallback = std::string("Encoding Error: ") + e.what();
		MessageBoxA(win_get_window(), fallback.c_str(), "Message", MB_OK | MB_ICONERROR);
	}
}

//========================================================================
// Popup a Windows Error Message, Allegro Style
//========================================================================
void allegro_message(const char* title, const char* message)
{
	try {
		std::wstring wideTitle = win32::Utf8ToUtf16(title ? title : "Message");
		std::wstring wideMessage = win32::Utf8ToUtf16(message ? message : "");

		// Use MB_TOPMOST to force the box to the top of the Z-order
		MessageBoxW(GetForegroundWindow(), wideMessage.c_str(), wideTitle.c_str(),
			MB_ICONEXCLAMATION | MB_OK | MB_TOPMOST);
	}
	catch (const win32::Utf8ConversionException& e) {
		// Fallback: Show ANSI error message
		std::string fallback = std::string("UTF-8 conversion error: ") + e.what();
		MessageBoxA(GetForegroundWindow(), fallback.c_str(), "Encoding Error",
			MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
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

RECT GetOpenGLScreenRect(HWND hwnd)
{
	RECT client{};
	if (!GetClientRect(hwnd, &client)) {
		LOG_ERROR("GetClientRect failed in GetOpenGLScreenRect");
		return RECT{ 0, 0, 0, 0 };
	}

	POINT topLeft = { client.left, client.top };
	POINT bottomRight = { client.right, client.bottom };

	ClientToScreen(hwnd, &topLeft);
	ClientToScreen(hwnd, &bottomRight);

	RECT glRect;
	glRect.left = topLeft.x;
	glRect.top = topLeft.y;
	glRect.right = bottomRight.x;
	glRect.bottom = bottomRight.y;

	LOG_INFO("OpenGL drawable rect: (%d,%d)-(%d,%d)", glRect.left, glRect.top, glRect.right, glRect.bottom);
	return glRect;
}