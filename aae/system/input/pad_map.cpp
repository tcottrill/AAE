//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#include "pad_map.h"

bool pad_map_is_sony(uint16_t vid, uint16_t pid)
{
    if (vid != 0x054C) return false;
    switch (pid) {
    case 0x0CE6:  // DualSense
    case 0x0DF2:  // DualSense Edge
    case 0x05C4:  // DualShock 4 v1
    case 0x09CC:  // DualShock 4 v2
        return true;
    default:
        return false;
    }
}

void pad_map_sony_buttons(const uint8_t raw[PAD_SONY_RAW_BUTTONS],
                          uint8_t canonical[PAD_BTN_COUNT])
{
    // canonical slot -> raw Sony index; -1 = no raw source (d-pad slots,
    // which the caller fills from the POV hat).
    static const int SRC[PAD_BTN_COUNT] = {
        1,   // A      <- Cross
        2,   // B      <- Circle
        0,   // X      <- Square
        3,   // Y      <- Triangle
        4,   // LB     <- L1
        5,   // RB     <- R1
        8,   // BACK   <- Create
        9,   // START  <- Options
        10,  // LTHUMB <- L3
        11,  // RTHUMB <- R3
        -1, -1, -1, -1,   // d-pad: from POV hat
        6,   // LT     <- L2 digital
        7,   // RT     <- R2 digital
    };
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        canonical[b] = (SRC[b] >= 0 && raw[SRC[b]]) ? 1 : 0;
}

uint16_t pad_map_chord_mask(const uint8_t canonical[PAD_BTN_COUNT])
{
    // canonical index -> AAE_JOYBTN_* bit; LT/RT have no XInput button bit.
    static const uint16_t BIT[PAD_BTN_COUNT] = {
        AAE_JOYBTN_A, AAE_JOYBTN_B, AAE_JOYBTN_X, AAE_JOYBTN_Y,
        AAE_JOYBTN_LEFT_SHOULDER, AAE_JOYBTN_RIGHT_SHOULDER,
        AAE_JOYBTN_BACK, AAE_JOYBTN_START,
        AAE_JOYBTN_LEFT_THUMB, AAE_JOYBTN_RIGHT_THUMB,
        AAE_JOYBTN_DPAD_UP, AAE_JOYBTN_DPAD_DOWN,
        AAE_JOYBTN_DPAD_LEFT, AAE_JOYBTN_DPAD_RIGHT,
        0, 0,
    };
    uint16_t mask = 0;
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        if (canonical[b]) mask |= BIT[b];
    return mask;
}

bool pad_map_combo_step(bool held, int* holdFrames)
{
    if (!held) { *holdFrames = 0; return false; }
    ++*holdFrames;
    return *holdFrames == PAD_COMBO_CONFIRM_FRAMES;
}
