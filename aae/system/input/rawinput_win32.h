#pragma once
// ===========================================================================
// rawinput_win32.h - Win32 Raw Input plumbing.
//
// PRIVATE to the Win32 backend. Only winmain.cpp and rawinput.cpp include
// this. Everything else uses sys_input.h.
// ===========================================================================
#include <windows.h>
#include "sys_input.h"

// Registers keyboard (HID usage 0x06) and mouse (0x02) for Raw Input with
// RIDEV_INPUTSINK, zeroes state, starts the worker thread.
// Returns S_OK on success, E_FAIL if registration fails.
HRESULT RawInput_Initialize(HWND hWnd);

// Call from WndProc on WM_INPUT.
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

// Stops the worker thread and joins it. Safe if never initialized.
void RawInput_Shutdown();
