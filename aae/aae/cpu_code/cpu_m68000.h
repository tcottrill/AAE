// -----------------------------------------------------------------------------
// Motorola MC68000 CPU Core
//
// This file is part of the AAE (Another Arcade Emulator) project. It is a
// from-scratch, per-instance C++ implementation of the original Motorola MC68000
// processor, written to replace the Musashi engine while matching the shape of
// the other AAE CPU cores (cpu_m6809, cpu_z80, cpu_i8080, cpu_i8085, cpu_6502).
//
// Design notes
// ------------
//   - ORIGINAL MC68000 ONLY. No 68008/010/020/030/040 instructions, addressing
//     modes, or registers (no VBR / SFC / DFC / caches / MSP).
//   - FULLY PER-INSTANCE. All CPU state lives in class members, so any number of
//     cores can run independently (cf. the old Musashi wrapper, which kept state
//     in C globals and aborted on a second instance). Construct as many as you
//     like.
//   - DROP-IN API. The public surface matches the previous Musashi wrapper
//     (ctor, reset/exec/get_ticks/end_timeslice, irq_line, set_irq_ack_callback,
//     GetPC/GetSP/GetSR/GetCpuNum) plus the legacy reset68k/exec68k/... aliases,
//     so existing AAE call sites compile unchanged.
//   - MEMORY. Uses the standard AAE handler-table interface (MemoryReadByte etc.
//     from deftypes.h). The core walks the caller-supplied tables; addresses are
//     masked to the 68000's 24-bit bus. 32-bit accesses are split into two 16-bit
//     accesses (correct for the 68000's 16-bit data bus). The core relays exactly
//     what the handlers return and never byte-swaps internally.
//   - DISPATCH. exec() indexes a shared, generated 65536-entry function-pointer
//     table (s_optable in m68k_ops.inc, produced by tools/gen68k.py). The table is
//     const and shared by all instances; per-instance state arrives through the
//     cpu_m68000* argument to each handler.
//   - TIMING. Per-instruction nominal cycle counts (Musashi-level). The 'odometer'
//     accumulates total cycles run; get_ticks() exposes a resettable view.
//   - LOGGING. Routes through the engine logger (system/util/sys_log.h): a one-time
//     LOG_INFO init line, a periodic LOG_INFO heartbeat from exec(), and LOG_DEBUG
//     traces on unhandled memory. Define CPU_M68000_NO_AAE_LOG to stub the logger
//     (used by the standalone test build).
//
// Known limitation
// ----------------
//   - The group-0 ADDRESS-ERROR exception (odd-address word/long access, vector 3)
//     is not yet emulated. This matches Musashi, which also omits it; correct,
//     aligned 68000 software never triggers it. 
//
// 2026-05-28  TC  Initial Musashi wrapper.
// 2026-05-30  TC  Replaced wrapper with from-scratch per-instance core.
// -----------------------------------------------------------------------------

#ifndef _CPU_M68000_H_
#define _CPU_M68000_H_

#pragma once

#include <cstdint>
#include "deftypes.h"   // MemoryReadByte / MemoryWriteByte / MemoryReadWord / MemoryWriteWord

// IRQ-ack callback sentinel: return this from a set_irq_ack_callback handler to
// tell the CPU to use the auto-vector for the IRQ level instead of a driver-
// supplied vector number. Mirrors Musashi's M68K_INT_ACK_AUTOVECTOR so existing
// call sites (e.g. aztarac) keep working.
#define CPU_M68000_INT_ACK_AUTOVECTOR ((int)0xffffffff)

class cpu_m68000;

// Opcode handler signature. The generated dispatch table (m68k_ops.inc) is an
// array of these; each handler receives the live instance.
typedef void (*cpuop_func)(cpu_m68000*);

class cpu_m68000 {
public:
    // ---- Construction / lifecycle ---------------------------------------
    // 8-bit + 16-bit AAE handler tables plus the CPU slot. Stores the handler
    // pointers and pulses reset (which fetches SP from $000000 and PC from
    // $000004 through the supplied tables, so the memory map must be live before
    // construction). Unlike the old wrapper, creating multiple instances is fine.
    cpu_m68000(MemoryReadByte*  read8,  MemoryWriteByte* write8,
               MemoryReadWord*  read16, MemoryWriteWord* write16,
               int cpu_num);
    ~cpu_m68000();

    // ---- Main entry points (mirror cpu_m6809) ---------------------------
    void reset();              // 68000 RESET: SP from $000000, PC from $000004, S=1, I=7
    int  exec(int cycles);     // run ~cycles worth of instructions; returns cycles actually run
    int  get_ticks(int reset); // running, resettable elapsed-cycle count (real, unlike Musashi)
    void end_timeslice();      // request an early bail out of exec() (zeroes the budget)

    // Cycles consumed so far within the CURRENT exec() slice (slice budget minus
    // what remains). odometer / the AAE timer clock only advance at slice
    // boundaries; this exposes the per-instruction progress *inside* a slice so a
    // slice-granular clock (e.g. the PIT) can read an accurate mid-slice value.
    // Reads 0 between slices and during the end-of-slice timer_update (the base
    // has already absorbed the slice by then), so callers never double-count.
    int  cycles_run_this_slice() const { return m_slice_cycles - cycles_left; }

    // ---- Interrupt input ------------------------------------------------
    // 68000 IPL pins encode level 0..7. Level 0 = no interrupt; 1..6 are masked
    // by the SR interrupt level; level 7 is the NMI-like, always-serviced input.
    // The level latches until serviced (autovector path) or cleared via level 0.
    void irq_line(int level);

    // ---- Interrupt-acknowledge hook -------------------------------------
    // Optional callback invoked during IACK to read the vector number. Return a
    // vector 0x02..0xFF, or CPU_M68000_INT_ACK_AUTOVECTOR to take the autovector
    // (0x18 + level). Used by aztarac (returns 0xc).
    void set_irq_ack_callback(int (*cb)(int)) { m_int_ack_cb = cb; }

    // ---- Debug surface --------------------------------------------------
    uint32_t GetPC()     const { return pc; }
    uint32_t GetSP()     const { return dar[15]; }   // active stack pointer (A7)
    uint16_t GetSR()     const;                       // packed status register
    int      GetCpuNum() const { return m_cpu_num; }

    // ---- Legacy AAE / Musashi-style API ---------------------------------
    // Thin name-compat wrappers so older call sites keep working unchanged.
    void reset68k()                 { reset(); }
    int  exec68k(int c)             { return exec(c); }
    int  get68kticks(int r)         { return get_ticks(r); }
    void cause_interrupt(int level) { irq_line(level); }
    void clear_pending_interrupts() { irq_line(0); }

    // ---- Architectural register / flag state ----------------------------
    // Public because the generated opcode handlers (free functions in the .cpp)
    // operate on it directly. Treat as read-only from driver code except via the
    // accessors above; the test harness also seeds these.
    uint32_t dar[16];     // data/address file: dar[0..7]=D0..D7, dar[8..15]=A0..A7 (dar[15]=active SP)
    uint32_t sp_other;    // the inactive stack pointer (USP while supervisor, SSP while user)
    uint32_t pc, ppc;     // program counter, and previous PC (instruction start; for exceptions/debug)
    uint16_t ir;          // current instruction word (opcode being executed)
    uint8_t  flag_n, flag_z, flag_v, flag_c, flag_x;  // condition flags, each 0 or 1
    uint8_t  int_mask;    // SR interrupt mask, bits I2..I0 (0..7)
    bool     s_flag;      // supervisor mode
    bool     t_flag;      // trace mode (set, but trace exception not yet generated)
    bool     stopped;     // halted by the STOP instruction until an eligible interrupt
    bool     halted;      // hard-halted (e.g. unrecoverable condition)
    int      cycles_left; // remaining budget within the current exec() slice
    uint64_t odometer;    // total cycles executed since construction (drives get_ticks)
    uint8_t  irq_pending; // latched IPL level 0..7 (0 = none)

    // TODO(address-error): the MC68000 raises a group-0 address-error exception
    // (vector 3) on any word/long access at an odd address (incl. odd instruction
    // fetch / jump target). Not yet emulated -- matches Musashi, which also omits
    // it. The 14-byte frame's SSW/fault-addr/IR/SR and the fetch-fault stacked PC
    // are exactly reproducible; the DATA-fault stacked PC depends on prefetch-queue
    // position (deltas 0/2/4/6 per addressing mode) and needs prefetch modeling to
    // match byte-exact.

    // ---- Memory access --------------------------------------------------
    // Public so the generated handlers can call them. Each walks the appropriate
    // AAE handler table; addresses are masked to 24 bits. read32/write32 are
    // composed from two 16-bit accesses (high word first), matching the 16-bit bus.
    uint32_t read8 (uint32_t a);
    uint32_t read16(uint32_t a);
    uint32_t read32(uint32_t a);
    void     write8 (uint32_t a, uint32_t v);
    void     write16(uint32_t a, uint32_t v);
    void     write32(uint32_t a, uint32_t v);
    // Instruction-stream fetches (advance PC). Used by the decode loop and EA engine.
    uint32_t fetch16() { uint32_t v = read16(pc); pc += 2; return v; }
    uint32_t fetch32() { uint32_t v = read32(pc); pc += 4; return v; }

    // ---- Status register / condition codes ------------------------------
    void     set_sr(uint16_t sr);  // load full SR (mode bits + CCR); swaps the active SP if S changes
    void     set_ccr(uint8_t ccr); // load just the condition codes (low byte: X N Z V C)
    uint16_t make_sr() const;      // pack the split flags + mode bits into the 16-bit SR

    // ---- Exceptions / interrupts ----------------------------------------
    void take_exception(int vector); // build the group-1/2 frame, enter supervisor, vector via the table
    void check_interrupts();         // service a pending IRQ if its level outranks the mask (or is 7)

    // ---- Effective-address engine ---------------------------------------
    // mode/reg are the 3-bit fields from the opcode; size is 0=byte, 1=word, 2=long.
    // ea_addr advances PC over any extension words and returns the computed address
    // (0 for register-direct / immediate modes, which have no address). ea_read/
    // ea_write handle all modes including register-direct and immediate. Read-
    // modify-write callers must compute the address once via ea_addr, then use
    // ea_read_addr + a sized write, to avoid advancing PC/registers twice.
    uint32_t ea_addr(int mode, int reg, int size);                       // compute EA, advancing PC
    uint32_t ea_read(int mode, int reg, int size);                       // operand value (zero-extended)
    void     ea_write(int mode, int reg, int size, uint32_t v);          // store to the operand
    uint32_t ea_read_addr(int mode, int reg, int size, uint32_t addr);   // value at a precomputed address
    int      ea_cycles(int mode, int reg, int size);                     // EA-calculation cycle penalty

private:
    // ---- Caller-supplied AAE memory handler tables ----------------------
    MemoryReadByte*  m_read8   = nullptr;
    MemoryWriteByte* m_write8  = nullptr;
    MemoryReadWord*  m_read16  = nullptr;
    MemoryWriteWord* m_write16 = nullptr;

    int  m_cpu_num   = 0;          // CPU slot index (for logging / GetCpuNum)
    int  m_tick_base = 0;          // get_ticks() baseline (subtracted from odometer)
    int  m_slice_cycles = 0;       // current exec() slice budget snapshot (for cycles_run_this_slice())
    int  (*m_int_ack_cb)(int) = nullptr;  // optional IACK vector callback
};

#endif // _CPU_M68000_H_
