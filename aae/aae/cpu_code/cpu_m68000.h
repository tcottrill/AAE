// -----------------------------------------------------------------------------
// Motorola MC68000 CPU Core (wrapper)
//
// This file is part of the AAE (Another Arcade Emulator) project. It wraps the
// Musashi 68000 engine (in cpu_code/68000/) in a per-instance C++ class that
// matches the shape of the other CPU cores in this folder (cpu_m6809, cpu_z80,
// cpu_i8080, cpu_i8085, cpu_6502).
//
// Design notes:
//   - Single-instance only. All three current 68000 games (quantum, aztarac,
//     cchasm) are single-CPU boards; Musashi keeps its state in C globals and a
//     second instance would require an unimplemented context-swap dance. The
//     constructor aborts if a second instance is created.
//   - Every m68k_* symbol reference in the project (outside cpu_code/68000/
//     itself) lives in cpu_m68000.cpp. 
//
// 2026-05-28  TC  Initial implementation.
// -----------------------------------------------------------------------------

#ifndef _CPU_M68000_H_
#define _CPU_M68000_H_

#pragma once

#include <cstdint>
#include "deftypes.h"   // MemoryReadByte, MemoryWriteByte, MemoryReadWord, MemoryWriteWord

// IRQ-ack callback sentinel: return this from a set_irq_ack_callback handler
// to tell the CPU to use the auto-vector for the IRQ level instead of a
// driver-supplied vector number. Mirrors Musashi's M68K_INT_ACK_AUTOVECTOR.
#define CPU_M68000_INT_ACK_AUTOVECTOR  ((int)0xffffffff)

class cpu_m68000
{
public:
    // ---- Construction / lifecycle ---------------------------------------
    // 8-bit + 16-bit handler tables plus the CPU slot. Stores the handler
    // pointers, installs *this as the singleton the static Musashi bridges
    // dispatch through, and pulses reset.
    cpu_m68000(MemoryReadByte*  read8,  MemoryWriteByte* write8,
               MemoryReadWord*  read16, MemoryWriteWord* write16,
               int cpu_num);
    ~cpu_m68000();

    // ---- Main entry points (mirror cpu_m6809) ---------------------------
    void reset();                  // 68000 RESET: SP from $000000, PC from $000004
    int  exec(int cycles);         // run ~cycles, return cycles actually run
    int  get_ticks(int reset);     // mid-slice cycle peek; always 0 on Musashi
                                   // (no non-destructive elapsed-cycles API)
    void end_timeslice();          // request early bail (used by lockup handlers)

    // ---- Interrupt input ------------------------------------------------
    // 68000 IPL pins encode level 0..7. Level 0 = no interrupt; 1..7 latches
    // until serviced. Level 7 is the NMI-like auto-vector.
    void irq_line(int level);

    // ---- Interrupt-acknowledge hook -------------------------------------
    // Optional callback the CPU invokes during IACK to read the vector.
    // Used by aztarac (returns 0xc). Mirrors Musashi's signature so call sites
    // don't change shape; when Musashi is replaced, our own core implements
    // the same hook.
    void set_irq_ack_callback(int (*cb)(int));

    // ---- Debug surface --------------------------------------------------
    uint32_t GetPC()     const;
    uint32_t GetSP()     const;
    uint16_t GetSR()     const;
    int      GetCpuNum() const { return m_cpu_num; }

    // ---- Legacy AAE / Musashi-style API ---------------------------------
    // Thin name-compat wrappers so older call sites keep working.
    void reset68k()                  { reset(); }
    int  exec68k(int cycles)         { return exec(cycles); }
    int  get68kticks(int r)          { return get_ticks(r); }
    void cause_interrupt(int level)  { irq_line(level); }
    void clear_pending_interrupts()  { irq_line(0); }

    // ---- Handler-table accessors (for the file-static memory bridges) ---
    // Public so the static m68k_read_memory_* free functions in the .cpp can
    // reach them via s_instance->read8_table() etc.
    MemoryReadByte*  read8_table()   const { return m_read8;  }
    MemoryWriteByte* write8_table()  const { return m_write8; }
    MemoryReadWord*  read16_table()  const { return m_read16; }
    MemoryWriteWord* write16_table() const { return m_write16;}

private:
    MemoryReadByte*  m_read8   = nullptr;
    MemoryWriteByte* m_write8  = nullptr;
    MemoryReadWord*  m_read16  = nullptr;
    MemoryWriteWord* m_write16 = nullptr;
    int              m_cpu_num = 0;
};

#endif // _CPU_M68000_H_
