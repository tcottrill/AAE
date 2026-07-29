#pragma once
// ===========================================================================
// win32_private.h - Win32-only window state and handles.
//
// PRIVATE to the Win32 backend. Only files under system/window/ may include
// this. Everything else uses sys_window.h.
// ===========================================================================
#include <windows.h>
#include "sys_window.h"

// Win32-only members formerly in WindowSetup. Survey (2026-07-28) confirmed
// none is read or written outside system/window/.
struct Win32WindowState {
	DWORD style = 0;
	DWORD exStyle = 0;
	bool  disableNC = false;              // non-client-area rendering
	bool  disableRoundedCorners = false;  // Win11 DWM corner preference
};

// The Win32 state paired with the live g_windowSetup / GetWindowSetup().
Win32WindowState& GetWin32WindowState();

// -----------------------------------------------------------------------------
// RECT <-> SysRect conversions, used at the Win32 API boundary
// (GetWindowRect, SetWindowPos, AdjustWindowRectEx, GetSystemMetrics, ...).
// -----------------------------------------------------------------------------
inline RECT    ToWin32Rect(const SysRect& r) { RECT o{ r.left, r.top, r.right, r.bottom }; return o; }
inline SysRect FromWin32Rect(const RECT& r) { SysRect o; o.left = r.left; o.top = r.top; o.right = r.right; o.bottom = r.bottom; return o; }
