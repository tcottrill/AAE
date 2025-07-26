///////////////////////////////////////////////////////////////////////////////
#include "framework.h"

#include "win10_win11_required_code.h"

#include "dwmapi.h" // Windows 10 rendering code

#pragma comment (lib,"dwmapi.lib")


// -----------------------------------------------------------------------------
// DisableRoundedCorners
// Disables rounded window corners on Windows 11.
// -----------------------------------------------------------------------------
void DisableRoundedCorners(HWND hwnd)
{
	if (!hwnd) return;

	const auto DWMWA_WINDOW_CORNER_PREFERENCE = 33; // not always defined
	const int DWMWCP_DONOTROUND = 1;

	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
		&DWMWCP_DONOTROUND, sizeof(DWMWCP_DONOTROUND));
}


HRESULT DisableNCRendering(HWND hWnd)
{
	HRESULT hr = S_OK;

	DWMNCRENDERINGPOLICY ncrp = DWMNCRP_DISABLED;

	// Disable non-client area rendering on the window.
	hr = ::DwmSetWindowAttribute(hWnd,
		DWMWA_NCRENDERING_POLICY,
		&ncrp,
		sizeof(ncrp));

	if (SUCCEEDED(hr))
	{
		// ...
	}

	return hr;
}

void disable_windows10_window_scaling()
{
 DisableNCRendering(win_get_window());
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
 SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
}

// -----------------------------------------------------------------------------
// EnableDPIAwareness
// Enables per-monitor DPI awareness for crisp scaling on HiDPI screens.
// -----------------------------------------------------------------------------
void EnableDPIAwareness()
{
	typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
	HMODULE hUser32 = LoadLibraryA("user32.dll");
	if (hUser32)
	{
		auto setAwareness = reinterpret_cast<SetProcessDpiAwarenessContextFunc>(
			GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
		if (setAwareness)
		{
			setAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		}
		FreeLibrary(hUser32);
	}
}