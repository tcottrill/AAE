
#ifndef _i8039_H_
#define _i8039_H_

#pragma once

#include "deftypes.h"
#include <cstdint>

// -----------------------------------------------------------------------------
// Intel 8039 / MCS-48 Emulator for AAE (Another Arcade Emulator)
//
// Clean-room implementation written from the published Intel MCS-48 User's
// Manual / 8039 datasheet instruction set. NOT derived from the MAME mcs48
// core - only the documented opcode semantics (which are facts, not
// expression) are reproduced here. The file *structure* (banner comments,
// switch-dispatched exec loop, cycles-decrement accounting, LOG_INFO on
// unrecognised opcodes, handler-list memory access) mirrors the AAE
// cpu_i8085 used as a template, so it slots into cpu_control the same way.
//
// The 8039 is the ROM-less member of the MCS-48 family (it is the 8049 with
// the on-chip ROM removed - 128 bytes of internal RAM, external program
// memory up to 4K via the A11 bank bit). Key architectural differences from
// the 8085:
//
//   - Harvard architecture: separate PROGRAM space (opcodes / MOVP / JMP) and
//     external DATA space (MOVX), plus a small internal data RAM.
//   - Eight working registers R0..R7 live *inside* the internal RAM. Two
//     banks (RB0 at RAM 0..7, RB1 at RAM 24..31) selected by PSW bit 4.
//   - The hardware stack (8 levels) also lives in internal RAM at 8..23, and
//     the 3-bit stack pointer is embedded in PSW bits 2..0.
//   - On-chip 8-bit timer / event counter with its own overflow interrupt.
//   - Two test inputs (T0, T1), an external interrupt pin, and bit flags
//     F0 (in PSW) and F1 (standalone).
//   - Paged program memory: JMP/CALL carry the low 8 bits in the operand, the
//     next 3 bits in the opcode, and A11 from the DBF (SEL MB0/MB1) latch.
//
// Memory / IO model chosen to fit the existing AAE framework:
//   - PROGRAM memory : read through the MemoryReadByte handler list, falling
//     back to MEM[] (the CPU's memory_region) - identical to cpu_i8085. This
//     is where opcodes, immediate operands, MOVP and MOVP3 fetch from.
//   - INTERNAL RAM   : private 128-byte array (registers + stack + @Rr).
//   - PORTS (BUS,P1,P2 and expander P4..P7) : the z80PortRead / z80PortWrite
//     interface via In()/Out(). BUS=port 0, P1=1, P2=2, P4..P7 = 4..7.
//   - EXTERNAL DATA  (MOVX) : optional ext_read_cb / ext_write_cb callbacks,
//     falling back to a private 256-byte buffer when no callback is set.
//
// The constructor signature, exec/reset/get_ticks/interrupt/In/Out interface
// and the public reg_PC member match cpu_i8080 / cpu_i8085 for drop-in use
// inside cpu_control's per-CPU switch statements.
// -----------------------------------------------------------------------------

class cpu_i8039
{

public:

	// ----- PSW (Program Status Word) bit definitions -----
	static constexpr uint8_t PSW_C   = 0x80; // Carry
	static constexpr uint8_t PSW_AC  = 0x40; // Auxiliary (half) carry
	static constexpr uint8_t PSW_F0  = 0x20; // User flag 0
	static constexpr uint8_t PSW_BS  = 0x10; // Register bank select
	static constexpr uint8_t PSW_X3  = 0x08; // Unused, always reads 1
	static constexpr uint8_t PSW_SP  = 0x07; // 3-bit stack pointer

	// ----- Native (simple) port numbers as seen by In()/Out() -----
	// Used when mame_compat is OFF: BUS=0, P1=1, P2=2, expanders 4..7, and the
	// T0/T1 test pins arrive via set_T0()/set_T1() rather than the port map.
	static constexpr uint8_t PORT_BUS = 0;
	static constexpr uint8_t PORT_P1  = 1;
	static constexpr uint8_t PORT_P2  = 2;
	// Expander ports P4..P7 use port numbers 4..7.

	// ----- MAME-style 16-bit port map (used when mame_compat is ON) -----
	// Matches the Buffoni/Boris i8039 core: the named ports live at 0x100+, the
	// test pins are read through the port map, and MOVX external-data accesses
	// use the raw 8-bit R0/R1 value (0x00..0xFF) as the port number, so the two
	// never overlap. Wire your driver's port handlers to these addresses.
	static constexpr uint16_t MPORT_P1  = 0x101;
	static constexpr uint16_t MPORT_P2  = 0x102;
	static constexpr uint16_t MPORT_P4  = 0x104; // P4..P7 = 0x104..0x107
	static constexpr uint16_t MPORT_T0  = 0x110;
	static constexpr uint16_t MPORT_T1  = 0x111;
	static constexpr uint16_t MPORT_BUS = 0x120;

	// ----- Interrupt vector addresses (in program memory) -----
	static constexpr uint16_t VEC_EXTERNAL = 0x0003;
	static constexpr uint16_t VEC_TIMER    = 0x0007;

	// ----- Interrupt type constants for cause_interrupt() -----
	static constexpr int I8039_EXTERNAL = 1;
	static constexpr int I8039_TIMER    = 2;
	static constexpr int I8039_NONE     = 0;

	// Address masks (8039 = 128 bytes RAM, up to 4K program space).
	static constexpr uint16_t PROG_MASK = 0x0FFF;

	// ----- Visible CPU state -----
	uint8_t  A   = 0;        // Accumulator
	uint8_t  PSW = PSW_X3;   // Program status word (bit3 always set)
	uint16_t reg_PC = 0;     // Program counter (12-bit effective)

	uint8_t  reg_T = 0;      // Timer / counter register
	bool     F1 = false;     // Standalone F1 flag
	bool     DBF = false;    // Memory bank flag -> supplies A11 to JMP/CALL

	// Internal data RAM (registers @0..7 / 24..31, stack @8..23, RAM @32..127)
	uint8_t  intRAM[128] = { 0 };

	// External data memory fallback (MOVX) when no callback supplied
	uint8_t  extRAM[256] = { 0 };

	// Pointer to the CPU memory map (program space backing store)
	uint8_t* MEM = nullptr;

	// Handler structures (shared framework types)
	MemoryReadByte*  memory_read  = nullptr;
	MemoryWriteByte* memory_write = nullptr;
	z80PortRead*     z80IoRead    = nullptr;
	z80PortWrite*    z80IoWrite   = nullptr;

	// Constructor - same signature as cpu_i8080 / cpu_i8085.
	cpu_i8039(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem,
	          z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr);

	~cpu_i8039() {};

	// ----- Public interface (matches cpu_i8080 / cpu_i8085) -----
	uint8_t In(uint8_t bPort);
	void    Out(uint8_t bPort, uint8_t bVal);
	int     exec(int cycles);
	int     get_ticks(int reset);
	void    reset();

	// 8080-compatible entry used by cpu_control::cpu_do_int_imm. On the MCS-48
	// the "INTR" equivalent is the external interrupt pin, which always vectors
	// to location 3, so the passed value is ignored.
	void interrupt(uint8_t n);

	// ----- MCS-48 specific external interface -----
	void cause_interrupt(int type);     // assert EXTERNAL or TIMER request
	void clear_pending_interrupts();

	void set_int_line(int state);       // external INT pin (1 = active)
	void set_T0(int state);             // test input T0
	void set_T1(int state);             // test input T1 (also counter clock)

	// Optional external-data-space (MOVX) hooks. If unset, extRAM[] is used.
	uint8_t (*ext_read_cb)(uint16_t addr) = nullptr;
	void    (*ext_write_cb)(uint16_t addr, uint8_t val) = nullptr;

	// Allow 8035/8048-style 64-byte RAM if a driver needs it (default 128).
	void set_ram_size(int bytes) { ram_mask = (bytes >= 128) ? 0x7F : 0x3F; }

	// Enable MAME-compatible behaviour (Buffoni/Boris i8039 core):
	//   - MOVX A,@Rr / MOVX @Rr,A route through the I/O port space (port = Rr)
	//     instead of the ext_read_cb/ext_write_cb data path.
	//   - The named ports use the 16-bit MAME map (P1=0x101 .. BUS=0x120) and
	//     the T0/T1 test pins are read through ports 0x110/0x111.
	//   - IN A,P1/P2 ANDs the pin read with the output latch (quasi-bidir port).
	//   - The timer is running at reset (some drivers, e.g. Mario Bros, rely
	//     on this without an explicit STRT T).
	//   - Event-counter mode samples T1 from the port and counts rising edges.
	void set_mame_compat(bool s) { mame_compat = s; }

	void log_unhandled_rw(bool s) { log_debug_rw = s; }

private:

	// ----- Program / external memory + IO helpers -----
	uint8_t prog_read(uint16_t addr);
	uint8_t fetch();                    // read at PC, advance PC (12-bit wrap)
	uint8_t ext_read(uint8_t addr);
	void    ext_write(uint8_t addr, uint8_t val);

	// 16-bit-capable port access (the public In/Out forward to these). The
	// MAME map uses port numbers above 0xFF, so the handler walk must compare
	// against 16-bit addresses.
	uint8_t io_in(uint16_t port);
	void    io_out(uint16_t port, uint8_t val);

	// Resolve a logical port to its active number for the current mode.
	uint16_t pnum_bus() { return mame_compat ? MPORT_BUS : (uint16_t)PORT_BUS; }
	uint16_t pnum_p1()  { return mame_compat ? MPORT_P1  : (uint16_t)PORT_P1; }
	uint16_t pnum_p2()  { return mame_compat ? MPORT_P2  : (uint16_t)PORT_P2; }
	uint16_t pnum_exp(int p) { return mame_compat ? (uint16_t)(MPORT_P4 + p) : (uint16_t)(4 + p); }
	int      read_test(int t);          // T0/T1 level for JT0/JT1/JNT0/JNT1

	// ----- Internal register file (lives in RAM) -----
	int     reg_base();                 // 0 or 24 depending on PSW_BS
	uint8_t get_R(int n);
	void    set_R(int n, uint8_t v);
	uint8_t ram_get(uint8_t addr);
	void    ram_set(uint8_t addr, uint8_t v);

	// ----- Stack (CALL / RET / RETR / interrupt entry) -----
	void     do_call(uint16_t addr);
	void     do_ret(bool restore_psw);

	// ----- ALU helpers -----
	void     op_add(uint8_t value, bool with_carry);
	void     daa();

	// ----- Timer / counter + interrupt servicing -----
	void     bump_counter();            // increment reg_T, handle overflow
	void     step_timer(int machine_cycles);
	void     service_interrupts();

	// Carry helpers
	bool     carry()    { return (PSW & PSW_C) != 0; }
	void     set_carry(bool c) { if (c) PSW |= PSW_C; else PSW &= ~PSW_C; }

	// ----- Internal state -----
	uint8_t  ram_mask = 0x7F;           // 128-byte RAM by default
	bool     mame_compat = true;       // MAME-compatible port/timer behaviour

	bool     irq_ext_enabled   = false; // EN I  / DIS I
	bool     irq_timer_enabled = false; // EN TCNTI / DIS TCNTI
	bool     irq_in_progress   = false; // suppress nesting until RETR
	bool     irq_ext_pending   = false; // external request latched
	bool     irq_timer_pending = false; // timer overflow request latched

	int      int_line = 0;              // current external INT pin state
	int      t0_line  = 0;              // current T0 pin state
	int      t1_line  = 0;              // current T1 pin state (edge-tracked)

	bool     timer_running   = false;   // STRT T
	bool     counter_running = false;   // STRT CNT
	bool     timer_flag      = false;   // TF - tested/cleared by JTF
	int      timer_prescaler = 0;       // divides machine cycles by 32
	uint8_t  old_t1_sample   = 0;       // last T1 sample (compat counter edge)

	uint8_t  port_latch[8] = { 0 };     // output latches for ANL/ORL on ports

	int      log_debug_rw = 0;
	int      clocktickstotal = 0;
};

#endif
