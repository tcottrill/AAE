//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// mathbox.h - the Atari Math Box (Battlezone / Red Baron / Tempest): four
// Am2901 bit slices driven by a 32 x 8 mapping PROM and 256 x 24-bit
// microcode PROMs. See mathbox.cpp for the word layout and PROM order.
//
// The driver loads the PROMs into two regions and hands them to
// mathbox_init() from its init; the four memory handlers below sit at the
// board's status / result / GO addresses (Tempest $6040 / $6060 / $6070 /
// $6080-$609F, Battlezone $1800 / $1810 / $1818 / $1860-$187F).
#ifndef MATHBOX_H
#define MATHBOX_H

#include <cstdint>
#include "deftypes.h"   // UINT8/32, MemoryReadByte / MemoryWriteByte

struct mathbox_state {
    uint16_t r[16];       // 2901 RAM registers (16-bit, four slices)
    uint16_t q;           // Q register
    uint16_t y;           // Y bus as left by the last instruction
    uint8_t  jt;          // jump-target latch (LDAB)
    uint8_t  q0;          // Q0 pin latch for the MULT (CADD) trick
    // diagnostics
    uint32_t starts;      // writes that ran microcode
    uint32_t steps;       // microinstructions executed
    uint32_t runaway;     // writes that hit the step cap without STALL
    uint32_t xor_sign;    // SIGN used after EXOR/EXNOR (OVR undocumented)
};

// map:    32 bytes, the mapping PROM (write offset -> starting uPC).
// planes: 0x300 bytes, three 256-byte planes of the microcode:
//         planes[0x000+i] = word bits 7-0, [0x100+i] = bits 15-8,
//         [0x200+i] = bits 23-16 (see the ROM tables in tempest.cpp / bzone.cpp).
// Packs the words, resets the state and returns true; logs and returns
// false when either pointer is null (the GO handler then does nothing).
bool mathbox_init(const uint8_t* map, const uint8_t* planes);
void mathbox_reset();

// Diagnostics / tests.
const mathbox_state* mathbox_get_state();
uint32_t mathbox_ucode_word(int upc);

UINT8 MathboxStatusRead(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 MathboxLowbitRead(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 MathboxHighbitRead(UINT32 address, struct MemoryReadByte* psMemRead);
void  MathboxGo(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

#endif // MATHBOX_H
