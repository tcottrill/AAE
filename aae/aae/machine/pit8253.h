// -----------------------------------------------------------------------------
// Programmable Interval Timer 8253 / 8254  -  AAE port
//
// Legacy MAME-Derived Module
// Adapted from the M.A.M.E.(TM) src/machine/pit8253.c / .h (v0.109),
//   "Programmable Interval Timer 8253/8254", originally by Peter Trauner,
//   Nathan Woods and Andrew Jenner.
//
// The counter behaviour (modes 0-5, BCD counting, read / read-back, gate
// handling) is preserved verbatim. Only the host-integration layer has been
// retargeted from MAME's mame_timer / save-state / READ8_HANDLER plumbing to
// the AAE timer.* subsystem and the AAE MemoryReadByte / MemoryWriteByte
// handler-table interface.
//
// Portions remain copyright the original MAME authors and contributors; this
// file is distributed under the GNU General Public License v3 or later, the
// same terms as the rest of AAE. See cpu_control.h for the full notice.
// -----------------------------------------------------------------------------

#ifndef PIT8253_AAE_H
#define PIT8253_AAE_H

#pragma once

#include "deftypes.h"   // UINT8/16/32, MemoryReadByte/WriteByte, MemoryReadWord/WriteWord

typedef enum { TYPE8253, TYPE8254 } PIT8253_TYPE;

// One PIT chip = three counters.
//   clockin         - per-counter input clock frequency, in Hz.
//   output_callback - fired when the OUT pin changes (state = 0 / 1).
//   clock_callback  - fired when the output frequency changes (periodic modes).
struct pit8253_config
{
    PIT8253_TYPE type;
    struct
    {
        double clockin;
        void (*output_callback)(int state);
        void (*clock_callback)(double clockout);
    } timer[3];
};

// ---- Lifecycle -------------------------------------------------------------
int  pit8253_init(int count, const struct pit8253_config *config);
void pit8253_reset(int which);

// ---- Sub-cycle clock hook (optional) ---------------------------------------
// AAE's timer clock only advances at CPU time-slice boundaries, so a counter
// read in a tight loop within one slice returns a constant. Install a callback
// returning the host CPU's cycles consumed since the last timer_update and the
// PIT folds it into its time source, so such reads (e.g. a game using a counter
// as an RNG) see the count advancing. Pass NULL (the default) for coarse mode.
void pit8253_set_subcycle_source(int (*cb)(void));

// ---- Core register access --------------------------------------------------
// offset selects the counter (0..2); a write with offset 3 targets the control
// word register (matches the 8253/8254 A1/A0 pin decode).
UINT8 pit8253_read (int which, UINT32 offset);
void  pit8253_write(int which, UINT32 offset, int data);

// ---- AAE 8-bit handlers (one register per byte address) --------------------
UINT8 pit8253_0_r(UINT32 offset, struct MemoryReadByte  *mem);
UINT8 pit8253_1_r(UINT32 offset, struct MemoryReadByte  *mem);
void  pit8253_0_w(UINT32 offset, UINT8 data, struct MemoryWriteByte *mem);
void  pit8253_1_w(UINT32 offset, UINT8 data, struct MemoryWriteByte *mem);

// ---- AAE 16-bit LSB handlers (68000-style word bus) ------------------------
// Registers are word-spaced and live in the low byte (the AAE equivalent of
// MAME's *_lsb_r / *_lsb_w). 'offset' is the byte offset within the mapped
// range, so the register index is offset >> 1.
UINT16 pit8253_0_lsb_r(UINT32 offset, struct MemoryReadWord  *mem);
UINT16 pit8253_1_lsb_r(UINT32 offset, struct MemoryReadWord  *mem);
void   pit8253_0_lsb_w(UINT32 offset, UINT16 data, struct MemoryWriteWord *mem);
void   pit8253_1_lsb_w(UINT32 offset, UINT16 data, struct MemoryWriteWord *mem);

// ---- Gate control + queries ------------------------------------------------
void pit8253_gate_write   (int which, int offset, int data);
int  pit8253_get_frequency(int which, int timer);
int  pit8253_get_output   (int which, int timer);
void pit8253_set_clockin  (int which, int timer, double new_clockin);

#endif // PIT8253_AAE_H
