#pragma once
#ifndef WIN10_WIN11_REQUIRED_CODE_H
#define WIN10_WIN11_REQUIRED_CODE_H

#include <Windows.h>

// -----------------------------------------------------------------------------
// EnableDPIAwareness
// Enables the best DPI awareness available (Per Monitor V2 if supported).
// Call this before creating any window.
// -----------------------------------------------------------------------------
void EnableDPIAwareness();

// -----------------------------------------------------------------------------
// GetDPIScaleForWindow
// Returns the DPI scaling factor for a given HWND (e.g. 1.0 = 100%, 1.25 = 125%).
// -----------------------------------------------------------------------------
float GetDPIScaleForWindow(HWND hwnd);

// -----------------------------------------------------------------------------
// DisableRoundedCorners
// Disables Windows 11 rounded corners if supported.
// -----------------------------------------------------------------------------
void DisableRoundedCorners(HWND hwnd);

// -----------------------------------------------------------------------------
// DisableNCRendering
// Disables non-client area rendering (e.g., DWM drop shadow).
// -----------------------------------------------------------------------------
HRESULT DisableNCRendering(HWND hwnd);

#endif // WIN10_WIN11_REQUIRED_CODE_H
