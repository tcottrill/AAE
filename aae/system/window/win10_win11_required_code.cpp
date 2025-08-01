#include "framework.h"
#include "win10_win11_required_code.h"
#include <dwmapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

// -----------------------------------------------------------------------------
// GetDPIScaleForWindow
// Returns the DPI scale factor for the given window (e.g. 1.25 for 125%).
// Falls back to 1.0 if DPI cannot be determined. Windows 10+ preferred.
// -----------------------------------------------------------------------------
float GetDPIScaleForWindow(HWND hWnd)
{
	if (!hWnd)
		return 1.0f;

	UINT dpi = 96; // Default DPI baseline

	// Prefer GetDpiForWindow (Windows 10+)
	HMODULE user32 = LoadLibraryW(L"user32.dll");
	if (user32) {
		using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
		auto GetDpiForWindow = reinterpret_cast<GetDpiForWindow_t>(
			GetProcAddress(user32, "GetDpiForWindow"));
		if (GetDpiForWindow) {
			dpi = GetDpiForWindow(hWnd);
			FreeLibrary(user32);
			return static_cast<float>(dpi) / 96.0f;
		}
		FreeLibrary(user32);
	}

	// Fallback for Windows 7 and older APIs
	HDC screenDC = GetDC(hWnd);
	if (screenDC) {
		dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
		ReleaseDC(hWnd, screenDC);
	}

	return static_cast<float>(dpi) / 96.0f;
}

// -----------------------------------------------------------------------------
// EnableDPIAwareness
// Enables the best DPI mode available (Per Monitor V2 if supported).
// Call BEFORE window creation.
// -----------------------------------------------------------------------------
void EnableDPIAwareness()
{
	// Try Windows 10+ DPI Awareness Context first
	HMODULE user32 = LoadLibraryW(L"user32.dll");
	if (user32) {
		auto SetDpiAwarenessContext = reinterpret_cast<decltype(&SetProcessDpiAwarenessContext)>(
			GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

		if (SetDpiAwarenessContext) {
			if (SetDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
				LOG_INFO("DPI awareness set to PerMonitorV2 (via SetProcessDpiAwarenessContext)");
				FreeLibrary(user32);
				return;
			}
			else {
				LOG_ERROR("SetProcessDpiAwarenessContext failed: %lu", GetLastError());
			}
		}
		FreeLibrary(user32);
	}
	// Fallback: Use legacy Shcore.dll API (Windows 8.1+)
	HMODULE shcore = LoadLibraryW(L"Shcore.dll");
	if (shcore) {
		auto SetDpiAwareness = reinterpret_cast<decltype(&SetProcessDpiAwareness)>(
			GetProcAddress(shcore, "SetProcessDpiAwareness"));
		if (SetDpiAwareness) {
			HRESULT hr = SetDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
			if (SUCCEEDED(hr)) {
				LOG_INFO("SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE) succeeded");
			}
			else if (hr == E_ACCESSDENIED) {
				LOG_INFO("DPI awareness already set (possibly by manifest)");
			}
			else {
				LOG_ERROR("SetProcessDpiAwareness failed with HRESULT 0x%08X", hr);
			}
			FreeLibrary(shcore);
			return;
		}
		FreeLibrary(shcore);
	}

	// Final fallback: Windows Vista/7
	user32 = LoadLibraryW(L"user32.dll");
	if (user32) {
		auto SetDPIAware = reinterpret_cast<decltype(&SetProcessDPIAware)>(
			GetProcAddress(user32, "SetProcessDPIAware"));
		if (SetDPIAware) {
			if (SetDPIAware())
				LOG_INFO("SetProcessDPIAware (legacy Win7 fallback) succeeded");
			else
				LOG_ERROR("SetProcessDPIAware failed");
		}
		FreeLibrary(user32);
	}
}

// -----------------------------------------------------------------------------
// DisableRoundedCorners
// Disables the default rounded corners applied to windows on Windows 11.
// Has no effect on earlier Windows versions.
// -----------------------------------------------------------------------------
void DisableRoundedCorners(HWND hwnd)
{
	if (!hwnd) return;

	// Disable rounded corners on Windows 11
	DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_DONOTROUND;
	HRESULT hr = DwmSetWindowAttribute(
		hwnd,
		DWMWA_WINDOW_CORNER_PREFERENCE,
		&preference,
		sizeof(preference)
	);

	if (FAILED(hr)) {
		LOG_ERROR("Failed to disable rounded corners (DwmSetWindowAttribute)");
	}
}

// -----------------------------------------------------------------------------
// DisableNCRendering
// Disables non-client rendering (Windows 10/11 only)
// -----------------------------------------------------------------------------
HRESULT DisableNCRendering(HWND hWnd)
{
	HRESULT hr = S_OK;

	DWMNCRENDERINGPOLICY ncrp = DWMNCRP_DISABLED;

	// Disable non-client area rendering on the window.
	hr = ::DwmSetWindowAttribute(
		hWnd,
		DWMWA_NCRENDERING_POLICY,
		&ncrp,
		sizeof(ncrp));

	if (SUCCEEDED(hr))
	{
		// Optionally: disable drop shadow if desired
		BOOL disable = TRUE;
		DwmSetWindowAttribute(
			hWnd,
			DWMWA_NCRENDERING_ENABLED,
			&disable,
			sizeof(disable));
	}

	return hr;
}
