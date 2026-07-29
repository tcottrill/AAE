#pragma once

#include <Windows.h>
#include "sys_log.h"
#include "sys_window.h"

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
// Runtime borderless-fullscreen toggle (same path as ALT+ENTER). Defined in
// winmain.cpp; flips ws.borderlessFullscreen and restyles the window live.
void ToggleBorderlessFullscreen(HWND hwnd, WindowSetup& ws);

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
