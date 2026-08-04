//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// evdev_input.h -- lifecycle for the Linux input backend.
//
// sys_input.h is the neutral CONTRACT and deliberately declares no init or
// shutdown: those are backend-shaped (Win32's take an HWND and live in
// rawinput_win32.h). These three are the Linux equivalents, called from
// linux_main.cpp exactly where winmain.cpp calls RawInput_Initialize /
// RawInput_Shutdown.
//
// The `RawInput_` prefix is deliberately NOT extended here - sys_input.h asks
// new code not to spread it, since it names a Windows API the evdev backend
// has nothing to do with.
//==============================================================================
#pragma once

// Enumerates and opens every usable input node. Returns false only when
// nothing could be opened at all; a machine with no gamepad (or no devices at
// all) is not an error, and the reason is logged either way.
bool EvdevInput_Initialize();

// Drains every device fd and updates key[], mouse_b and the per-device state.
//
// Call once per frame from the main loop. There is NO background thread: the
// Win32 backend uses one because WM_INPUT arrives on the message pump and had
// to be moved off the render path, which sys_input.h notes is a backend design
// choice and not part of the contract. Polling non-blocking fds on the game
// thread is the natural shape for evdev, and it removes the cross-thread
// publication of key[]/mouse_b altogether - the ordering problem sys_input.h
// warns does not carry from x86 to ARM simply does not arise. It also matches
// the Teensy target, which has no threads.
//
// Nothing is lost between calls: the kernel buffers events per fd, so a frame
// that takes 30ms still sees every event in order.
void EvdevInput_Poll();

// Closes every device. Named to match the contract's existing shutdown entry
// point so linux_main.cpp and winmain.cpp read the same.
void RawInput_Shutdown();
