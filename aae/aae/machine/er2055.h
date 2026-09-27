//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// GI ER2055 EAROM (64 words x 8 bits), translated arithmetic-for-arithmetic
// from MAME 0.286's er2055_device (src/devices/machine/er2055.cpp/.h,
// BSD-3-Clause, copyright the MAME team / Aaron Giles). MAME's names are
// kept with an er2055_ prefix so the two can be compared side by side.
//
// The chip exposes five active-high pins - CS1, CS2, C1, C2 and CK - and
// an address/data bus. A control or clock change only does anything while
// BOTH chip selects are set, and only on a genuine transition: writing the
// same latch value twice is a no-op. Mode by C1/C2:
//     C1=0 C2=0  write  - rom[address] &= data (can only clear bits)
//     C1=0 C2=1  erase  - rom[address]  = 0xFF
//     C1=1 C2=0  read   - falling CK edge latches rom[address] into data
//     C1=1 C2=1  no-op
//
// Board-specific decoding of the CPU-visible control byte (which bit is
// which pin, and any inversion) belongs in the driver, exactly as MAME's
// per-driver earom_control_w() does - Centipede/Millipede wire C1 and C2
// to different bits than the vector boards. Drivers persist e->rom with
// nvram_set_region(); this module does no file I/O and has no AAE includes,
// so tests/er2055_tests.cpp builds it standalone.
#ifndef ER2055_H
#define ER2055_H

#include <stdint.h>

struct er2055
{
	uint8_t rom[64];     // MAME m_rom_data - the persisted image
	uint8_t address;     // set_address(), low 6 bits
	uint8_t data;        // set_data() latch / data() read register
	uint8_t control;     // internal CK|C1|C2|CS1|CS2 composite (MAME bit values)
};

// rom = 0x00 (every Atari user has ROM_REGION "earom" ROMREGION_ERASE00 "to
// suppress invalid high score display"); latches and control cleared.
void    er2055_init(er2055* e);

void    er2055_set_address(er2055* e, uint8_t addr);   // & 0x3f
void    er2055_set_data(er2055* e, uint8_t data);
uint8_t er2055_data(const er2055* e);

// set_control() then set_clk(), in that order, per control-byte write.
void    er2055_set_control(er2055* e, bool cs1, bool cs2, bool c1, bool c2);
void    er2055_set_clk(er2055* e, bool state);

#endif // ER2055_H
