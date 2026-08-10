// -----------------------------------------------------------------------------
// Motorola MC6800 / MC6802 / MC6808 CPU Core
//
// This file is part of the AAE (Another Arcade Emulator) project and follows the
// same Neil-Bradley-compatible memory-handler interface used by the other CPU
// cores in this folder (cpu_m6809, cpu_i8080, cpu_i8085, cpu_z80).
//
// Design notes:
//   - ONE core covers all three chips: the 6800, 6802 and 6808 are
//     instruction-set identical. They differ only in packaging -- the 6802 adds
//     an on-chip clock and 128 bytes of internal RAM at $0000-$007F, and the
//     6808 is a 6802 with that RAM disabled. Nothing about those differences is
//     visible to the instruction decoder, so a 6802's internal RAM is mapped by
//     the driver like any other RAM region rather than being built into the core.
//   - Documented MC6800 instruction set only. The 6801/6803 additions (ABX, MUL,
//     LDD/STD, ADDD/SUBD, PSHX/PULX, BRN, JSR-direct, the 16-bit shifts) are NOT
//     part of this core, and the two "halt and catch fire" test opcodes ($9D,
//     $DD) are treated as illegal rather than locking the CPU up.
//   - Style modeled on cpu_m6809: a flat class with a big switch in step().
//     The 6800's four addressing modes (immediate / direct / indexed / extended)
//     are far simpler than the 6809's postbyte scheme, so the accumulator opcode
//     block $80-$FF decodes as a mode x operation grid instead of a case per
//     opcode.
//   - Interrupts are EDGE/ONE-SHOT, not level-sensitive: nmi_line()/irq_line()
//     latch a request; the core LOWERS the latch when the interrupt is taken, so
//     one assertion = one interrupt. A still-masked request stays pending until
//     the mask clears, then fires exactly once. This matches cpu_m6809 and the
//     way the AAE scheduler pulses cpu_do_int_imm() once per interrupt period.
//     A device that holds its line asserted on real hardware must RE-ASSERT here.
//   - Per-instruction cycle counts from the MC6800 datasheet.
//   - The 6800 is BIG-ENDIAN; all 16-bit accesses go through read16()/write16().
//   - CPX is the classic 6800 gotcha: it affects N, Z and V but NOT carry, so
//     the unsigned branches are unusable after it. That behaviour is deliberate
//     here -- the 6801 changed it, the 6800 did not, and MAME implements the
//     6801/6803 version for the whole family.
//
// 2026-08-09  TC  Initial implementation.
// -----------------------------------------------------------------------------

#ifndef _CPU_M6800_H_
#define _CPU_M6800_H_

#pragma once

#include <cstdint>
#include "deftypes.h"

// Interrupt-type bitmask used by m6800_Cause_Interrupt(). The level inputs
// nmi_line() / irq_line() are the preferred portable API; this enum is kept for
// the AAE cpu_control layer that requests interrupts by type.
enum {
    M6800_INT_NONE = 0x00,
    M6800_INT_IRQ  = 0x01,
    M6800_INT_NMI  = 0x04,
};

class cpu_m6800
{
public:

    // Condition Code register bit masks (hardware bit order). Bits 6 and 7 read
    // as 1 on real hardware and are held set here.
    enum {
        CC_C = 0x01,  // carry / borrow
        CC_V = 0x02,  // two's-complement overflow
        CC_Z = 0x04,  // zero
        CC_N = 0x08,  // negative
        CC_I = 0x10,  // IRQ mask
        CC_H = 0x20,  // half carry (decimal)
        CC_UNUSED = 0xC0,
    };

    // Pointer to the cpu memory map (raw 64 KiB fallback).
    uint8_t* MEM = nullptr;
    // Pointer to the handler structures.
    MemoryReadByte*  memory_read  = nullptr;
    MemoryWriteByte* memory_write = nullptr;

    // Constructor / destructor.
    cpu_m6800(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, int cpu_num);
   ~cpu_m6800() {}

    // Main entry points (mirror cpu_m6809: step / exec).
    void reset();                 // load PC from the RESET vector $FFFE/$FFFF
    int  step();                  // run exactly ONE step; return cycles consumed
    int  exec(int cycles);        // run >= cycles by looping step(); return total run
    int  get_ticks(int reset);    // running, resettable cycle total

    // Interrupt lines (see the edge/one-shot note in the header comment).
    void nmi_line(bool asserted) { m_nmi_line = asserted; }
    void irq_line(bool asserted) { m_irq_line = asserted; }

    // Debug surface.
    uint16_t GetPC()         const { return m_PC; }
    uint16_t GetPPC()        const { return m_PPC; }
    uint8_t  GetLastOpcode() const { return m_last_opcode; }
    int      GetCpuNum()     const { return cpu_num; }
    void mame_memory_handling(bool s) { mmem = s; }
    void log_unhandled_rw(bool s)     { log_debug_rw = s; }

    // ---- AAE cpu_control bridge --------------------------------------------
    // Thin name-compatibility wrappers used by the AAE scheduler.
    void     reset6800()          { reset(); }
    int      exec6800(int cycles) { return exec(cycles); }
    int      get6800ticks(int r)  { return get_ticks(r); }
    uint16_t get_pc()  const      { return m_PC; }
    uint16_t get_ppc() const      { return m_PPC; }
    uint8_t  get_last_ireg() const { return m_last_opcode; }
    // One-shot interrupt request (edge style), matching how the AAE scheduler
    // drives every other CPU core. Raises the corresponding level input; the
    // core lowers it again when the interrupt is taken (see step()).
    void     m6800_Cause_Interrupt(int type);
    void     m6800_Clear_Pending_Interrupts();

    // Test/debug register accessors (read-only snapshots).
    uint8_t  GetA()  const { return m_A; }
    uint8_t  GetB()  const { return m_B; }
    uint16_t GetX()  const { return m_X; }
    uint16_t GetSP() const { return m_SP; }
    uint8_t  GetCC() const { return m_CC; }

    // Test/debug register setters (used by a test harness to seed state).
    void SetA(uint8_t v)   { m_A = v; }
    void SetB(uint8_t v)   { m_B = v; }
    void SetX(uint16_t v)  { m_X = v; }
    void SetSP(uint16_t v) { m_SP = v; }
    void SetCC(uint8_t v)  { m_CC = (uint8_t)(v | CC_UNUSED); }
    void SetPC(uint16_t v) { m_PC = v; }

private:

    // ---- Register file -----------------------------------------------------
    uint8_t  m_A  = 0;   // accumulator A
    uint8_t  m_B  = 0;   // accumulator B
    uint16_t m_X  = 0;   // index register
    uint16_t m_SP = 0;   // stack pointer (points at the next FREE byte)
    uint16_t m_PC = 0;   // program counter
    uint16_t m_PPC = 0;  // previous PC (debug)
    uint8_t  m_CC = CC_UNUSED;  // condition codes

    uint8_t  m_last_opcode = 0;
    int      cpu_num = 0;

    // ---- Interrupt / wait state -------------------------------------------
    bool m_nmi_line = false;
    bool m_irq_line = false;
    bool m_wai      = false;  // WAI: registers pre-stacked, waiting for an interrupt

    // ---- Debug / memory options -------------------------------------------
    bool mmem = false;            // MAME-style memory handling (block unhandled)
    bool log_debug_rw = false;    // log unhandled reads/writes
    int  clocktickstotal = 0;

    // ---- Memory access -----------------------------------------------------
    uint8_t  read8 (uint16_t addr);
    void     write8(uint16_t addr, uint8_t v);
    uint16_t read16(uint16_t addr);            // big-endian: hi @ addr, lo @ addr+1
    void     write16(uint16_t addr, uint16_t v);
    uint8_t  fetch8()  { return read8(m_PC++); }
    uint16_t fetch16() { uint16_t hi = read8(m_PC++); uint16_t lo = read8(m_PC++); return (uint16_t)((hi << 8) | lo); }

    // ---- Addressing-mode helpers ------------------------------------------
    // The 6800 has no direct-page register: "direct" is always page zero, and
    // "indexed" is X plus an UNSIGNED 8-bit offset (no auto inc/dec, no indirect).
    uint16_t ea_direct()   { return fetch8(); }
    uint16_t ea_indexed()  { return (uint16_t)(m_X + fetch8()); }
    uint16_t ea_extended() { return fetch16(); }

    // ---- Flag helpers ------------------------------------------------------
    inline void set_flag(uint8_t mask, bool cond) { if (cond) m_CC |= mask; else m_CC &= (uint8_t)~mask; }
    inline bool get_flag(uint8_t mask) const { return (m_CC & mask) != 0; }
    inline void set_NZ8(uint8_t v)   { set_flag(CC_N, (v & 0x80) != 0);   set_flag(CC_Z, v == 0); }
    inline void set_NZ16(uint16_t v) { set_flag(CC_N, (v & 0x8000) != 0); set_flag(CC_Z, v == 0); }

    // ---- 8-bit operation helpers ------------------------------------------
    // add8/sub8 carry the H flag and the borrow convention for ADD/ADC/ABA and
    // SUB/SBC/CMP/SBA/CBA respectively; sub8 returns the result so the compare
    // forms can discard it.
    uint8_t add8(uint8_t a, uint8_t v, uint8_t carry_in);
    uint8_t sub8(uint8_t a, uint8_t v, uint8_t carry_in);
    void    op_and8(uint8_t& dst, uint8_t v);
    void    op_or8(uint8_t& dst, uint8_t v);
    void    op_eor8(uint8_t& dst, uint8_t v);
    void    op_bit8(uint8_t a, uint8_t v);
    void    op_ld8(uint8_t& dst, uint8_t v);
    void    op_st8(uint16_t ea, uint8_t v);
    uint8_t op_neg8(uint8_t v);
    uint8_t op_com8(uint8_t v);
    uint8_t op_clr8();
    uint8_t op_inc8(uint8_t v);
    uint8_t op_dec8(uint8_t v);
    void    op_tst8(uint8_t v);
    uint8_t op_lsr8(uint8_t v);
    uint8_t op_asr8(uint8_t v);
    uint8_t op_asl8(uint8_t v);
    uint8_t op_rol8(uint8_t v);
    uint8_t op_ror8(uint8_t v);
    void    op_daa();

    // ---- 16-bit operation helpers -----------------------------------------
    void    op_ld16(uint16_t& dst, uint16_t v);
    void    op_st16(uint16_t ea, uint16_t v);
    void    op_cpx(uint16_t v);    // CPX: sets N/Z/V, leaves C alone (6800 quirk)

    // ---- Stack helpers -----------------------------------------------------
    // SP points at the next FREE byte: push writes then decrements, pull
    // increments then reads.
    void     push8(uint8_t v)   { write8(m_SP--, v); }
    uint8_t  pull8()            { return read8(++m_SP); }
    void     push16(uint16_t v) { push8((uint8_t)v); push8((uint8_t)(v >> 8)); }
    uint16_t pull16()           { uint16_t h = pull8(); return (uint16_t)((h << 8) | pull8()); }

    // ---- Branch / interrupt helpers ---------------------------------------
    bool     test_branch_cond(uint8_t code); // low-nibble branch condition (0..F)
    void     branch(bool taken);             // signed 8-bit relative (fetch + maybe take)
    // Stacks the register frame (unless WAI already did) and vectors. Returns
    // the entry cycle cost.
    int      service_interrupt(uint16_t vector);

    // Decode helper for the $80-$FF accumulator grid.
    void     exec_accumulator(uint8_t op, int& cycles);

    // Charge `c` cycles to clocktickstotal and the AAE timer; returns `c`.
    // Called once per step() so interrupt timing is cycle-accurate.
    int      charge_cycles(int c);
};

#endif // _CPU_M6800_H_
