//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// Standalone assert-runner for pad_map. Build & run (from repo root, WSL):
//   g++ -std=c++17 -I aae/system/input aae/system/input/tests/pad_map_tests.cpp aae/system/input/pad_map.cpp -o /tmp/pad_map_tests
//   /tmp/pad_map_tests
#include "pad_map.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
} while (0)

// Sony raw rgbButtons order (DirectInput): 0=Square 1=Cross 2=Circle
// 3=Triangle 4=L1 5=R1 6=L2 7=R2 8=Create 9=Options 10=L3 11=R3 12=PS
// 13=Touchpad 14=Mute
enum { RAW_SQUARE = 0, RAW_CROSS, RAW_CIRCLE, RAW_TRIANGLE, RAW_L1, RAW_R1,
       RAW_L2, RAW_R2, RAW_CREATE, RAW_OPTIONS, RAW_L3, RAW_R3, RAW_PS,
       RAW_TOUCHPAD, RAW_MUTE };

static void test_is_sony()
{
    CHECK(pad_map_is_sony(0x054C, 0x0CE6));   // DualSense
    CHECK(pad_map_is_sony(0x054C, 0x0DF2));   // DualSense Edge
    CHECK(pad_map_is_sony(0x054C, 0x05C4));   // DualShock 4 v1
    CHECK(pad_map_is_sony(0x054C, 0x09CC));   // DualShock 4 v2
    CHECK(!pad_map_is_sony(0x054C, 0x0BA0));  // Sony VID, unknown PID (DS4 dongle)
    CHECK(!pad_map_is_sony(0xD209, 0x0501));  // Ultimarc
    CHECK(!pad_map_is_sony(0x045E, 0x02FF));  // Microsoft
    CHECK(!pad_map_is_sony(0x045E, 0x0CE6));  // non-Sony VID with a Sony PID
}

static void test_sony_buttons()
{
    uint8_t raw[PAD_SONY_RAW_BUTTONS];
    uint8_t out[PAD_BTN_COUNT];

    // Every raw button lands in its canonical slot.
    static const struct { int raw; int canon; } pairs[] = {
        { RAW_CROSS,    PAD_BTN_A },      { RAW_CIRCLE,  PAD_BTN_B },
        { RAW_SQUARE,   PAD_BTN_X },      { RAW_TRIANGLE,PAD_BTN_Y },
        { RAW_L1,       PAD_BTN_LB },     { RAW_R1,      PAD_BTN_RB },
        { RAW_CREATE,   PAD_BTN_BACK },   { RAW_OPTIONS, PAD_BTN_START },
        { RAW_L3,       PAD_BTN_LTHUMB }, { RAW_R3,      PAD_BTN_RTHUMB },
        { RAW_L2,       PAD_BTN_LT },     { RAW_R2,      PAD_BTN_RT },
    };
    for (const auto& p : pairs) {
        std::memset(raw, 0, sizeof(raw));
        raw[p.raw] = 0x80;
        std::memset(out, 0xFF, sizeof(out));  // catch slots the impl fails to write
        pad_map_sony_buttons(raw, out);
        for (int b = 0; b < PAD_BTN_COUNT; ++b)
            CHECK(out[b] == (b == p.canon ? 1 : 0));
    }

    // PS, touchpad, mute are dropped entirely.
    std::memset(raw, 0, sizeof(raw));
    raw[RAW_PS] = raw[RAW_TOUCHPAD] = raw[RAW_MUTE] = 0x80;
    std::memset(out, 0xFF, sizeof(out));
    pad_map_sony_buttons(raw, out);
    for (int b = 0; b < PAD_BTN_COUNT; ++b)
        CHECK(out[b] == 0);
}

static void test_chord_mask()
{
    uint8_t c[PAD_BTN_COUNT] = {};
    CHECK(pad_map_chord_mask(c) == 0);

    c[PAD_BTN_BACK] = 1; c[PAD_BTN_START] = 1;
    CHECK(pad_map_chord_mask(c) == (AAE_JOYBTN_BACK | AAE_JOYBTN_START));

    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_LTHUMB] = 1; c[PAD_BTN_START] = 1;
    CHECK(pad_map_chord_mask(c) == JOY_COMBO_MENU);

    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_A] = 1; c[PAD_BTN_DPAD_LEFT] = 1;
    CHECK(pad_map_chord_mask(c) == (AAE_JOYBTN_A | AAE_JOYBTN_DPAD_LEFT));

    // Table-driven: pressing exactly one canonical button must yield exactly
    // its expected AAE_JOYBTN_* bit (0 for LT/RT, which have no chord bit).
    // This pins every entry of the BIT[] table against swaps/drops.
    static const struct { int canon; uint16_t bit; } expected[PAD_BTN_COUNT] = {
        { PAD_BTN_A,          AAE_JOYBTN_A },
        { PAD_BTN_B,          AAE_JOYBTN_B },
        { PAD_BTN_X,          AAE_JOYBTN_X },
        { PAD_BTN_Y,          AAE_JOYBTN_Y },
        { PAD_BTN_LB,         AAE_JOYBTN_LEFT_SHOULDER },
        { PAD_BTN_RB,         AAE_JOYBTN_RIGHT_SHOULDER },
        { PAD_BTN_BACK,       AAE_JOYBTN_BACK },
        { PAD_BTN_START,      AAE_JOYBTN_START },
        { PAD_BTN_LTHUMB,     AAE_JOYBTN_LEFT_THUMB },
        { PAD_BTN_RTHUMB,     AAE_JOYBTN_RIGHT_THUMB },
        { PAD_BTN_DPAD_UP,    AAE_JOYBTN_DPAD_UP },
        { PAD_BTN_DPAD_DOWN,  AAE_JOYBTN_DPAD_DOWN },
        { PAD_BTN_DPAD_LEFT,  AAE_JOYBTN_DPAD_LEFT },
        { PAD_BTN_DPAD_RIGHT, AAE_JOYBTN_DPAD_RIGHT },
        { PAD_BTN_LT,         0 },
        { PAD_BTN_RT,         0 },
    };
    for (const auto& e : expected) {
        std::memset(c, 0, sizeof(c));
        c[e.canon] = 1;
        CHECK(pad_map_chord_mask(c) == e.bit);
    }

    // All 16 canonical buttons pressed at once must yield exactly the OR of
    // all 14 real chord bits (LT/RT contribute nothing).
    for (int b = 0; b < PAD_BTN_COUNT; ++b) c[b] = 1;
    uint16_t allBits = 0;
    for (const auto& e : expected) allBits |= e.bit;
    CHECK(pad_map_chord_mask(c) == allBits);

    // LT/RT have no chord bits; they must not leak into the mask.
    std::memset(c, 0, sizeof(c));
    c[PAD_BTN_LT] = 1; c[PAD_BTN_RT] = 1;
    CHECK(pad_map_chord_mask(c) == 0);
}

static void test_combo_step()
{
    int frames = 0;
    CHECK(!pad_map_combo_step(true,  &frames));  // frame 1: arming
    CHECK( pad_map_combo_step(true,  &frames));  // frame 2: fires
    CHECK(!pad_map_combo_step(true,  &frames));  // still held: no refire
    CHECK(!pad_map_combo_step(false, &frames));  // released
    CHECK(!pad_map_combo_step(true,  &frames));  // re-pressed: arming again
    CHECK( pad_map_combo_step(true,  &frames));  // fires again

    // Partial arm (1 held frame) followed by a release must reset the
    // counter -- it then needs 2 fresh held frames to fire, not 1.
    frames = 0;
    CHECK(!pad_map_combo_step(true,  &frames));  // 1 held frame: arming
    CHECK(!pad_map_combo_step(false, &frames));  // released before confirm: reset
    CHECK(!pad_map_combo_step(true,  &frames));  // fresh frame 1: arming, not fired
    CHECK( pad_map_combo_step(true,  &frames));  // fresh frame 2: fires
}

int main()
{
    test_is_sony();
    test_sony_buttons();
    test_chord_mask();
    test_combo_step();
    if (g_failures) { std::printf("%d FAILURE(S)\n", g_failures); return 1; }
    std::printf("pad_map_tests: all passed\n");
    return 0;
}
