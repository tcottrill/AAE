//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
//==============================================================================
// pad_map -- pure gamepad-normalization logic shared by the OS joystick
// backends. No OS headers: unit-tested standalone (tests/pad_map_tests.cpp).
//
// "Canonical" means the XInput layout documented in the XInput button
// layout block in joystick.h; every gamepad-class device fills joy[] in
// this order regardless of how the OS reports it.
//==============================================================================
#include <cstdint>

#include "joystick.h"   // AAE_JOYBTN_* chord bits (Win32-free header)

// Canonical pad button indices (XInput ordering).
enum {
    PAD_BTN_A = 0, PAD_BTN_B, PAD_BTN_X, PAD_BTN_Y,
    PAD_BTN_LB, PAD_BTN_RB, PAD_BTN_BACK, PAD_BTN_START,
    PAD_BTN_LTHUMB, PAD_BTN_RTHUMB,
    PAD_BTN_DPAD_UP, PAD_BTN_DPAD_DOWN, PAD_BTN_DPAD_LEFT, PAD_BTN_DPAD_RIGHT,
    PAD_BTN_LT, PAD_BTN_RT,
    PAD_BTN_COUNT
};

// Number of buttons the Sony pad reports over DirectInput (DualSense: 15;
// the DS4's 14 are a safe prefix of the same order). The remap itself only
// reads up to index 11 -- PS/touchpad/mute (12-14) are dropped.
constexpr int PAD_SONY_RAW_BUTTONS = 15;

// True for Sony pads served by the DirectInput path: DualSense (0CE6),
// DualSense Edge (0DF2), DualShock 4 v1/v2 (05C4/09CC).
bool pad_map_is_sony(uint16_t vid, uint16_t pid);

// Reorder Sony raw DirectInput rgbButtons order into canonical slots.
// raw[i] nonzero = pressed. Writes all PAD_BTN_COUNT entries (0/1); the
// d-pad slots (10..13) are always 0 here -- the d-pad arrives via the POV
// hat and is filled by the caller. PS / touchpad / mute are dropped.
void pad_map_sony_buttons(const uint8_t raw[PAD_SONY_RAW_BUTTONS],
                          uint8_t canonical[PAD_BTN_COUNT]);

// Chord mask (AAE_JOYBTN_* bits) from canonical button states.
uint16_t pad_map_chord_mask(const uint8_t canonical[PAD_BTN_COUNT]);

// Combos must be held this many consecutive polls to fire.
constexpr int PAD_COMBO_CONFIRM_FRAMES = 2;

// Advance one (device, combo) hold counter. Returns true exactly once, on
// the poll where the count reaches PAD_COMBO_CONFIRM_FRAMES; the combo must
// be fully released before it can fire again.
bool pad_map_combo_step(bool held, int* holdFrames);
