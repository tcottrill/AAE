//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#include "pad_map.h"

#include <cstddef>

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

namespace {

char lower_ch(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Parse 1..4 hex digits spanning [b,e). False on empty, too long or a non-hex.
bool parse_hex16(const char* b, const char* e, unsigned* out)
{
    if (b == e || e - b > 4) return false;
    unsigned v = 0;
    for (; b != e; ++b) {
        const char c = lower_ch(*b);
        unsigned d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10;
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

// Case-insensitive "does hay contain needle" for a non-empty needle [nb,ne).
bool contains_nocase(const char* hay, const char* nb, const char* ne)
{
    const size_t nlen = (size_t)(ne - nb);
    if (nlen == 0 || !hay) return false;
    for (; *hay; ++hay) {
        size_t i = 0;
        while (i < nlen && hay[i] && lower_ch(hay[i]) == lower_ch(nb[i])) ++i;
        if (i == nlen) return true;
    }
    return false;
}

// One trimmed token [b,e) against the device.
bool ignore_token_matches(const char* b, const char* e,
                          uint16_t vid, uint16_t pid, const char* name)
{
    if (e - b >= 5 && lower_ch(b[0]) == 'n' && lower_ch(b[1]) == 'a' &&
        lower_ch(b[2]) == 'm' && lower_ch(b[3]) == 'e' && b[4] == '=') {
        b += 5;
        while (b < e && is_ws(*b)) ++b;
        return contains_nocase(name, b, e);
    }

    const char* colon = b;
    while (colon < e && *colon != ':') ++colon;
    if (colon == e) return false;                      // no ':' -> malformed

    unsigned v = 0, p = 0;
    if (!parse_hex16(b, colon, &v)) return false;
    if (v != vid) return false;
    if (e - (colon + 1) == 1 && colon[1] == '*') return true;   // vvvv:*
    if (!parse_hex16(colon + 1, e, &p)) return false;
    return p == pid;
}

} // namespace

bool pad_ignore_match(const char* list, uint16_t vid, uint16_t pid, const char* name)
{
    if (!list) return false;
    const char* s = list;
    while (*s) {
        const char* end = s;
        while (*end && *end != ',') ++end;

        const char* b = s;
        const char* e = end;
        while (b < e && is_ws(*b)) ++b;
        while (e > b && is_ws(e[-1])) --e;
        if (b < e && ignore_token_matches(b, e, vid, pid, name))
            return true;

        s = *end ? end + 1 : end;
    }
    return false;
}

PadNodeClass pad_classify_node(const PadNodeCaps& c, const char** vetoReason)
{
    if (vetoReason) *vetoReason = nullptr;

    const bool padButtons   = c.hasEvKey && (c.btnGamepad || c.btnJoystick);
    const bool padAxes      = c.absX && c.absY;
    const bool mouseAxes    = c.relX && c.relY;
    const bool mouseButtons = c.hasEvKey && c.btnLeft;

    const bool padLike = padButtons || (padAxes && c.hasEvKey && !mouseAxes);

    const char* veto = nullptr;
    if (c.letters >= 20)          veto = "keyboard";
    else if (c.relX || c.relY)    veto = "has REL_X/REL_Y";
    else if (c.hasEvKey && c.btnLeft) veto = "has BTN_LEFT";

    if (padLike && !veto) return PADNODE_GAMEPAD;
    if (padLike && vetoReason) *vetoReason = veto;

    if (mouseAxes && mouseButtons) return PADNODE_MOUSE;
    if (c.letters >= 20)           return PADNODE_KEYBOARD;   // a real keyboard has all 26
    return PADNODE_UNKNOWN;
}

namespace {

// Does a "0xVVVV/0xPPPP,..." list contain vid/pid? 0x prefix optional.
// *anyValid (optional) reports whether the list held at least one well-formed
// pair, so a garbage EXCEPT list can be told apart from a real allow-list.
bool env_list_has(const char* list, uint16_t vid, uint16_t pid, bool* anyValid = nullptr)
{
    if (anyValid) *anyValid = false;
    if (!list) return false;
    const char* s = list;
    while (*s) {
        const char* end = s;
        while (*end && *end != ',') ++end;

        const char* b = s;
        const char* e = end;
        while (b < e && is_ws(*b)) ++b;
        while (e > b && is_ws(e[-1])) --e;

        const char* slash = b;
        while (slash < e && *slash != '/') ++slash;
        if (slash < e) {
            const char* vb = b;
            const char* pb = slash + 1;
            if (e - vb >= 0 && slash - vb >= 2 && vb[0] == '0' && lower_ch(vb[1]) == 'x') vb += 2;
            if (e - pb >= 2 && pb[0] == '0' && lower_ch(pb[1]) == 'x') pb += 2;
            unsigned v = 0, p = 0;
            if (parse_hex16(vb, slash, &v) && parse_hex16(pb, e, &p)) {
                if (anyValid) *anyValid = true;
                if (v == vid && p == pid)
                    return true;
            }
        }
        s = *end ? end + 1 : end;
    }
    return false;
}

} // namespace

bool pad_env_ignore_match(const char* ignore, const char* except,
                          uint16_t vid, uint16_t pid)
{
    if (ignore && env_list_has(ignore, vid, pid)) return true;
    if (except && except[0]) {
        // An allow-list with no well-formed pair is treated as unset: hiding
        // every pad because of a garbled variable would leave the player with
        // no controller at all.
        bool anyValid = false;
        const bool listed = env_list_has(except, vid, pid, &anyValid);
        if (anyValid && !listed) return true;
    }
    return false;
}

int pad_rank(const PadCaps& c)
{
    if (c.btnSouth && c.absRx && c.absRy &&
        (c.absZ || c.absRz || c.absHat0X || c.btnDpadUp))
        return 0;
    if (c.btnSouth)    return 1;
    if (c.btnJoystick) return 2;
    return 3;
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
