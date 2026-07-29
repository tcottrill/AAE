#pragma once
// ===========================================================================
// sys_dialog.h - user-facing messages, platform-neutral.
//
// Win32 shows a MessageBox; a headless backend logs. Not a window operation,
// hence its own header rather than a method on ISystemWindow.
//
// Not yet included by anything: framework.h still carries the Win32
// declarations of these same signatures, and this header stays unreferenced
// until Task 4 repoints callers and framework.h onto it. Declaring the same
// signature in two headers is legal C++, but keeping this header isolated
// for now avoids any ambiguity about which declaration is "the" contract.
// ===========================================================================

// UTF-8 safe. Win32 backend shows a MessageBoxW; a headless backend logs.
void allegro_message(const char* title, const char* message);

// UTF-8 safe, printf-style. ID selects the icon (e.g. IDOK/IDCANCEL) on the
// Win32 backend. Zero callers in the current tree as of this writing -
// carried over anyway since it is part of the same message/dialog contract
// as allegro_message; not deleted from framework.h in this task.
void osMessage(int ID, const char* fmt, ...);
