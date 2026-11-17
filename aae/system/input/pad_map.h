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

// ---------------------------------------------------------------------------
// aae.ini [input] joystick_ignore = <tokens>
//
// Comma-separated list of devices that must never become a joy[] slot. Some
// peripherals (gaming keyboards, mice, RGB controllers) expose a genuine
// virtual gamepad node that no capability check can tell from a real pad, so
// the user has to be able to name them. Token forms, whitespace around a
// token ignored:
//   vvvv:pppp      USB vendor:product, hex, case-insensitive ("046d:c21d")
//   vvvv:*         every product of that vendor
//   name=<text>    case-insensitive substring of the device name
// A malformed token is skipped rather than rejecting the whole list, so one
// typo cannot un-ignore the other entries. Pure, platform-neutral: shared by
// the evdev backend and the DirectInput one.
bool pad_ignore_match(const char* list, uint16_t vid, uint16_t pid, const char* name);

// Steam's own device hiding. When Steam launches a game it exports
// SDL_GAMECONTROLLER_IGNORE_DEVICES (the PHYSICAL pads it has virtualized, as
// comma-separated "0xVVVV/0xPPPP" pairs) and sometimes
// SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT (an allow-list: everything else is
// hidden). AAE reads raw evdev rather than SDL, so without honouring them it
// sees the physical pad AND Steam's virtual pad: two players for one person.
// Returns true when the device must be skipped. Either argument may be null or
// empty; an empty/unset EXCEPT hides nothing, and so does an EXCEPT list with
// no well-formed pair. Malformed pairs are skipped. The caller must apply this
// to PHYSICAL devices only: Steam's virtual pads can share a vid:pid with a
// hidden physical pad (045e:028e), see evdev_joystick.cpp.
// IGNORE is checked first, then EXCEPT (SDL's order).
bool pad_env_ignore_match(const char* ignore, const char* except,
                          uint16_t vid, uint16_t pid);

// Pure node classification over capability booleans (the evdev code reads the
// bitsets and is only the caller), so the decision is testable without a
// device and without <linux/input.h>.
struct PadNodeCaps {
    bool hasEvKey  = false;
    bool btnGamepad  = false;   // BTN_GAMEPAD / BTN_SOUTH
    bool btnJoystick = false;   // BTN_JOYSTICK / BTN_TRIGGER
    bool btnLeft   = false;     // BTN_LEFT
    bool relX = false, relY = false;
    bool absX = false, absY = false;
    int  letters   = 0;         // letter keys present (a real keyboard has 26)
};
enum PadNodeClass { PADNODE_UNKNOWN, PADNODE_KEYBOARD, PADNODE_MOUSE, PADNODE_GAMEPAD };

// If the node looked pad-like but was refused, *vetoReason (when non-null) is
// set to a short reason; otherwise it is set to nullptr.
//
// A node is never a Gamepad if it is also a keyboard (>= 20 letter keys), has
// REL_X/REL_Y, or has BTN_LEFT: gaming keyboards and mice expose extra nodes
// with ABS_X/ABS_Y or stray BTN_* bits that the plain pad rules would take.
// Steam's virtual pads (28de:11ff, and the X360 emulation 045e:028e) carry
// none of those, so they are unaffected.
PadNodeClass pad_classify_node(const PadNodeCaps& caps, const char** vetoReason);

// What an evdev node advertises that decides its joy[] slot order. Plain
// booleans so the ranking is unit-testable without <linux/input.h>.
struct PadCaps {
    bool btnSouth  = false;   // BTN_GAMEPAD / BTN_SOUTH / BTN_A
    bool btnJoystick = false; // BTN_JOYSTICK / BTN_TRIGGER (flight stick)
    bool absRx = false, absRy = false;
    bool absZ = false, absRz = false;
    bool absHat0X = false;
    bool btnDpadUp = false;
};

// Slot ranking, lowest first:
//   0 full pad (south button + right stick + a trigger/hat/d-pad)
//   1 any other BTN_GAMEPAD device
//   2 BTN_JOYSTICK device
//   3 axis-only
// Real pads sort ahead of the odd gamepad-class nodes keyboards and mice
// expose, so Player 1 lands on the pad the user is actually holding.
int pad_rank(const PadCaps& caps);

// Combos must be held this many consecutive polls to fire.
constexpr int PAD_COMBO_CONFIRM_FRAMES = 2;

// Advance one (device, combo) hold counter. Returns true exactly once, on
// the poll where the count reaches PAD_COMBO_CONFIRM_FRAMES; the combo must
// be fully released before it can fire again.
bool pad_map_combo_step(bool held, int* holdFrames);
