//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
// ===========================================================================
// rawinput_win32.h - Win32 Raw Input plumbing.
//
// PRIVATE to the Win32 backend. Only winmain.cpp and rawinput.cpp include
// this. Everything else uses sys_input.h.
// ===========================================================================
#include <windows.h>
#include "sys_input.h"

// Win32-implementation-detail macros. These lived in the neutral header until
// 2026-07-28; they are used only by rawinput.cpp and mean nothing to a
// non-Win32 backend (RI_MOUSE_HWHEEL mirrors a RAWMOUSE.usButtonFlags bit).
//
// toUpper() does not parenthesise its parameter - it is unsafe for arguments
// with side effects or lower-precedence operators, e.g. toUpper(x++). It has
// no call sites today; prefer std::toupper in new code.
#define bset(p,m) ((p) |= (m))
#define bclr(p,m) ((p) &= ~(m))
#define toUpper(ch) ((ch >= 'a' && ch <='z') ? ch & 0x5f : ch)
#define RI_MOUSE_HWHEEL 0x0800

// Registers keyboard (HID usage 0x06) and mouse (0x02) for Raw Input with
// RIDEV_INPUTSINK, zeroes state, starts the worker thread.
// Returns S_OK on success, E_FAIL if registration fails.
HRESULT RawInput_Initialize(HWND hWnd);

// Call from WndProc on WM_INPUT.
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

// Stops the worker thread and joins it. Safe if never initialized.
void RawInput_Shutdown();
