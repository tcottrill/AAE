// pokey_timebase.h - CPU-cycle to POKEY-clock conversion for the AAE POKEY
// adapter (c012294_interface.cpp). Header-only so tests/test_pokey_timebase.cpp can
// build it without the engine.
//
// The adapter feeds the c012294 core by catch-up: at every register access
// and once per frame it computes how many CPU cycles passed since the last
// catch-up and converts them to POKEY master clocks. Boards where the two
// clocks differ (Food Fight and Quantum 68000s with a 600 kHz POKEY; the
// Centipede-family boards including Warlords clock CPU and POKEY at the
// same 1.512 MHz) need the fraction carried across
// calls, or the chip drifts against the CPU. `remainder` is that carried
// fraction, in units of 1/cpu_hz of a POKEY clock; start it at 0 and pass
// the same variable every call.
//
// CORRECTION (Warlords): the line above USED to say Warlords clocks CPU and
// POKEY at the same 1.512 MHz.  It does not.  6502 phi0 (pin 37) is a bare
// wire to P4 (74LS163A) pin 11 = dot/16 = 756 kHz, and POKEY phi2 (pin 7) is
// P4 pin 12 = dot/8 = 1.512 MHz -- adjacent bits of one free-running counter,
// so POKEY ticks exactly TWICE per CPU cycle.  [schematic sheets 01B, 02B]
// That ratio is what makes the ROM's POKEY PROTECT check work: its 4-CPU-cycle
// gap between STA SKCTL and LDY RANDOM is 8 POKEY clocks, enough for RANDOM to
// have filled to $FF.  Believing the two clocks were equal is what caused
// warlord.cpp's CPU freq to be "corrected" from 756000 to 1512000, which broke
// the check and prompted a fabricated bus-stall workaround.  Do not restore
// that assumption; this conversion handles unequal clocks correctly and
// Warlords depends on it doing so.
#ifndef POKEY_TIMEBASE_H
#define POKEY_TIMEBASE_H

#include <cstdint>

inline uint32_t pokey_clocks_elapsed(uint64_t elapsed_cpu, uint64_t pokey_hz,
                                     uint64_t cpu_hz, uint64_t* remainder)
{
    if (cpu_hz == 0) cpu_hz = 1;
    const uint64_t accum  = *remainder + elapsed_cpu * pokey_hz;
    const uint64_t clocks = accum / cpu_hz;
    *remainder = accum % cpu_hz;
    // ad_pokey_advance takes uint32_t; a delta this large cannot occur at
    // any real frame rate, but clamp rather than truncate if it ever does.
    return clocks > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)clocks;
}

#endif // POKEY_TIMEBASE_H
