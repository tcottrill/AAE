#pragma once

#include <Windows.h>
#include "glew.h"
#include "wglew.h"
#include "sys_log.h"

// -----------------------------------------------------------------------------
// WindowSetup
// Central configuration for window creation and runtime state.
// -----------------------------------------------------------------------------
struct WindowSetup {
	RECT rect{};
	DWORD style = 0;
	DWORD exStyle = 0;
	bool disableNC = false;
	bool disableRoundedCorners = false;
	bool borderlessFullscreen = false;

	// This is the current WindowRect coordinated backed up in Windowed Mode.
	// This is updated whenever the primary window changes size/shape
	RECT windowedRect{};
	// This is a copy of the fullscreen resolution of the primary monitor.
	// This is set at the start of the program.
	// Right now this code only supports the primary monitor.
	RECT screenRect{};
	bool useFullscreen = false;
	bool centerWindow = true;
	bool useAspectRatio = false;
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
};
// -----------------------------------------------------------------------------
// GetWindowSetup
// Returns a reference to the global WindowSetup struct.
// -----------------------------------------------------------------------------
WindowSetup& GetWindowSetup();

// -----------------------------------------------------------------------------
// GetClientWidth / GetClientHeight
// Returns the current internal client width and height of the window.
// -----------------------------------------------------------------------------
int GetClientWidth();
int GetClientHeight();

// -----------------------------------------------------------------------------
// Global access to window handle
// -----------------------------------------------------------------------------
extern HWND win_get_window();

void RestoreWindowViewport();

// -----------------------------------------------------------------------------
// Message and dialog helpers (UTF-8 safe)
// -----------------------------------------------------------------------------
void allegro_message(const char* title, const char* message);
void osMessage(int ID, const char* fmt, ...);

// -----------------------------------------------------------------------------
// Error string helper
// -----------------------------------------------------------------------------
std::string GetLastErrorStdStr();

// -----------------------------------------------------------------------------
// Cursor control and mouse helpers
// -----------------------------------------------------------------------------
void ClipAndHideCursor(HWND hWnd);
void UnclipAndShowCursor();
void EnableCursorClip(bool enable);
void ForceCursorClipUpdate();
void SetMousePos(HWND hwnd, int x, int y);
POINT GetMousePos(HWND hwnd);
