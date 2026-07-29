#pragma once
// ===========================================================================
// sys_window.h - the platform-neutral window contract.
//
// Backends: Win32 (today), GUI/Linux (Wayland or X11 - serving both the
// Steam Machine and the Pi, which run a desktop with AAE fullscreen), and
// Headless (Teensy - no window at all; video is redirected into the
// vector/DAC path for a real vector arcade monitor).
//
// Nothing here may reference windows.h or any platform type.
// ===========================================================================
#include <cstdint>

// Neutral rectangle with EDGE semantics, matching Win32 RECT field-for-field
// so existing arithmetic like `rect.right - rect.left` keeps its meaning.
// Deliberately NOT aae::math::Rect2D, which is extents-based (x/y/w/h) - an
// edges/extents mismatch compiles cleanly and inverts geometry silently.
struct SysRect {
	int left = 0, top = 0, right = 0, bottom = 0;
	int width()  const { return right - left; }
	int height() const { return bottom - top; }
};

// -----------------------------------------------------------------------------
// WindowSetup
// Central configuration for window creation and runtime state.
// -----------------------------------------------------------------------------
struct WindowSetup {
	SysRect rect{};
	bool borderlessFullscreen = false;

	// This is the current WindowRect coordinated backed up in Windowed Mode.
	// This is updated whenever the primary window changes size/shape
	SysRect windowedRect{};
	// This is a copy of the fullscreen resolution of the primary monitor.
	// This is set at the start of the program.
	// Right now this code only supports the primary monitor.
	SysRect screenRect{};
	bool useFullscreen = false;
	bool centerWindow = true;
	bool useAspectRatio = false;
	// True when the user has explicitly requested an aspect ratio override,
	// either via use_aspect=1 in the INI or -aspect N:M on the command line.
	// When true, aspectRatio overrides the game-computed aspect in run_game().
	// When false (default), every game uses its natural computed aspect ratio.
	bool aspectOverrideActive = false;
	float aspectRatio = 4.0f / 3.0f;
	int windowWidth = 1024;
	int windowHeight = 768;
	int clientWidth = 0;   // This is for the current window size.
	int clientHeight = 0;  // This is for the current window size.
	int minWindowWidth = 320;
	int minWindowHeight = 240;
	bool resizable = false;
	bool dpiAware = true;
	float dpiScale = 1.0f;  // Logical-to-physical pixel ratio (e.g., 1.25 for 125% DPI)

	bool isMinimized = false;
	bool isFocused = true;
	bool cursorClipEnabled = true;
	// Which monitor to launch on (1-based: 1 = primary, 2 = second, etc.)
	// Loaded from [main] starting_monitor in aae.ini.
	// Can be overridden at the command line with -monitor N.
	// Values <= 0 or out of range fall back to the primary monitor.
	int startingMonitor = 1;

};
// -----------------------------------------------------------------------------
// GetWindowSetup
// Returns a reference to the global WindowSetup struct.
// -----------------------------------------------------------------------------
WindowSetup& GetWindowSetup();
