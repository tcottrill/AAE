///////////////////////////////////////////////////////////////////////////////
#include "allegro.h"
#include "winalleg.h" // Required for Win_get_window

#include "win10_win11_required_code.h"

#include "dwmapi.h" // Windows 10 rendering code

#pragma comment (lib,"dwmapi.lib")


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

