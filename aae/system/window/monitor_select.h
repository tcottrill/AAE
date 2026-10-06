// Vendored copy. Master: C:\Source2026\shared\monitor_select - keep in sync.
// -----------------------------------------------------------------------------
// monitor_select.h
//
// Stable multi-monitor selection for Win32 programs.
//
// EnumDisplayMonitors order is NOT stable (toggling G-Sync, driver updates and
// hot-plug all re-enumerate), so "monitor N by enumeration position" is a bug.
// This module numbers monitors by a rule that depends only on the physical
// layout, and can additionally pin a monitor by its per-physical-monitor
// device id (DisplayConfig monitorDevicePath).
//
// Numbering: 1 = OS primary; 2.. = the others sorted by rcMonitor.left, then
// rcMonitor.top.
//
// Plain C++17, Win32 only. Links user32.lib. Works with _WIN32_WINNT >= 0x0601.
// -----------------------------------------------------------------------------
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace monsel {

struct MonitorDesc {
	int          number = 0;      // 1 = OS primary, 2.. = the others sorted by rcMonitor.left, then .top
	HMONITOR     handle = nullptr;
	RECT         monitorRect{};   // rcMonitor
	RECT         workRect{};      // rcWork
	bool         primary = false;
	std::string  gdiName;         // "\\.\DISPLAY2", UTF-8; INFORMATIONAL ONLY, it renumbers too
	std::string  friendlyName;    // e.g. "DELL U2720Q", UTF-8, may be empty
	std::string  deviceId;        // stable per-physical-monitor id (monitorDevicePath), UTF-8, may be empty
};

// All monitors sorted by number. Never empty (synthesizes the primary on failure).
std::vector<MonitorDesc> Enumerate();

MonitorDesc Primary();

// MONITOR_DEFAULTTONEAREST, fully populated.
MonitorDesc FromWindow(HWND hwnd);

// Resolution order: deviceId (case-insensitive) if non-empty and connected ->
// number if in range -> primary. Never fails. 'reason' gets a one-line explanation.
MonitorDesc Select(int number, const std::string& deviceId, std::string* reason = nullptr);

// True if the rect touches any monitor.
bool IsRectVisible(const RECT& r);

// Outer window rect of width x height centered in m.workRect, clamped so the
// top-left stays on the monitor.
RECT CenterInWorkArea(int width, int height, const MonitorDesc& m);

// Multi-line text, one line per monitor.
std::string DescribeAll();

} // namespace monsel
