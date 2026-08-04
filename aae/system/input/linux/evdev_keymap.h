//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// evdev_keymap.h -- Linux KEY_* -> AAE AaeKey translation.
//
// This is the one place in the codebase where the two key-code spaces meet.
// They CAN meet, in this file and only in this file, because AaeKey is an enum
// rather than macros: <linux/input-event-codes.h> defines KEY_A as 30 and
// sys_input.h defines AAEKEY_A as 0x41, and a macro KEY_A would have made the
// two impossible to include together. (See the note above AaeKey.)
//
// PRIVATE to the Linux input backend.
//==============================================================================
#pragma once

#include <cstdint>

// Returns the AaeKey for a Linux key code, or 0 when the key has no AAE
// equivalent. 0 is safe as "none": no reachable AaeKey has the value 0 - the
// single enumerator that does, AAEKEY_EQUALS_PAD, is discussed in the .cpp.
uint8_t EvdevKeyToAae(int evdevCode);

// Reports, at startup, which bindable AAE keys no evdev code can produce.
// A gap here is a key the player cannot bind, and finding that at startup is
// considerably better than finding it halfway through a game.
void EvdevKeymapLogCoverage();
