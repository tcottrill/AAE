// -----------------------------------------------------------------------------
// AAE (Another Arcade Emulator) - 6502 CPU Core
//
// This file is part of the AAE project and is released under The Unlicense.
// You are free to use, modify, and distribute this software without restriction.
// See <http://unlicense.org/> for details.
//
// Origins and Contributions:
//   - Original 6502 CPU core by Neil Bradley, adapted from unnamed web sources.
//   - ADC and SBC algorithms from M.A.M.E.™ (c) 1998–2000 Juergen Buchmueller.
//   - Timing tables and stack logic adapted from FakeNes.
//   - Modern C++ refactor and extensions by TC (2015–2025) for AAE compatibility.
//   - Additional documentation and cleanup assisted by ChatGPT.
//
// This version retains functional and timing accuracy, supports profiling and
// debugging, and integrates mostly with legacy MAME-style emulation systems.
// -----------------------------------------------------------------------------

#ifndef _6502_H_
#define _6502_H_

#pragma once

#include <cstdint>
#include <string>
#include "deftypes.h"

#define USING_AAE_EMU

class cpu_6502
{
public:
	enum
	{
		M6502_A = 0x01,
		M6502_X = 0x02,
		M6502_Y = 0x04,
		M6502_P = 0x08,
		M6502_S = 0x10,
	};

	// -------------------------------------------------------------------------
	// External memory interface
	// -------------------------------------------------------------------------
	uint8_t* MEM = nullptr;
	MemoryReadByte* memory_read = nullptr;
	MemoryWriteByte* memory_write = nullptr;

	// -------------------------------------------------------------------------
	// Construction and execution
	// -------------------------------------------------------------------------
	cpu_6502(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num);
	~cpu_6502() = default;

	void init6502(uint16_t addrmaskval);
	void reset6502();
	void irq6502();
	void nmi6502();
	int exec6502(int timerTicks);
	int step6502();
	int get6502ticks(int reset);

	// -------------------------------------------------------------------------
	// Instruction Usage Profiler
	// -------------------------------------------------------------------------
	uint64_t instruction_count[256] = { 0 };
	bool instruction_profile_enabled = false;

	void log_instruction_usage();
	void reset_instruction_counts();

	// -------------------------------------------------------------------------
	// Register access
	// -------------------------------------------------------------------------
	uint8_t m6502_get_reg(int regnum);
	void m6502_set_reg(int regnum, uint8_t val);
	uint16_t get_pc();
	uint16_t get_ppc();
	void set_pc(uint16_t pc);

	// -------------------------------------------------------------------------
	// Interrupts and memory handling
	// -------------------------------------------------------------------------
	void m6502clearpendingint();
	void check_interrupts_after_cli();
	bool is_irq_pending() const { return _irqPending != 0; }

	// -------------------------------------------------------------------------
	// Debugging and disassembly
	// -------------------------------------------------------------------------
	void enable_debug(bool s) { debug = s; }
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s) { log_debug_rw = s; }
	std::string disassemble(uint16_t pc, int* bytesUsed = nullptr);

	// -------------------------------------------------------------------------
	// Stack operations
	// -------------------------------------------------------------------------
	void push16(uint16_t val);
	void push8(uint8_t val);
	uint16_t pull16();
	uint8_t pull8();

private:
	// -------------------------------------------------------------------------
	// CPU internal flags and registers
	// -------------------------------------------------------------------------
	bool direct_zero_page = false;
	bool direct_stack_page = false;

	uint8_t A = 0, P = 0, X = 0, Y = 0, S = 0xFF;
	uint16_t PC = 0, PPC = 0;

	// -------------------------------------------------------------------------
	// CPU internal state
	// -------------------------------------------------------------------------
	uint16_t addrmask = 0;
	uint8_t opcode = 0;
	uint16_t savepc = 0, oldpc = 0, reladdr = 0, help = 0;
	uint8_t value = 0, saveflags = 0;

	int clockticks6502 = 0;
	int clocktickstotal = 0;
	int _irqPending = 0;
	int cpu_num = 0;

	bool debug = false;
	bool mmem = false;
	bool log_debug_rw = false;

	// -------------------------------------------------------------------------
	// Opcode function dispatch tables
	// -------------------------------------------------------------------------
	void (cpu_6502::* adrmode[0x100])();
	void (cpu_6502::* instruction[0x100])();

	// -------------------------------------------------------------------------
	// Processor status flags
	// -------------------------------------------------------------------------
	enum
	{
		F_C = 0x01, // Carry
		F_Z = 0x02, // Zero
		F_I = 0x04, // Interrupt Disable
		F_D = 0x08, // Decimal Mode
		F_B = 0x10, // Break
		F_T = 0x20, // Unused (always set)
		F_V = 0x40, // Overflow
		F_N = 0x80  // Negative
	};

	// -------------------------------------------------------------------------
	// Stack base address
	// -------------------------------------------------------------------------
	static constexpr uint16_t BASE_STACK = 0x100;

	// -------------------------------------------------------------------------
	// Memory access
	// -------------------------------------------------------------------------
	uint8_t get6502memory(uint16_t addr);
	void put6502memory(uint16_t addr, uint8_t byte);

	// -------------------------------------------------------------------------
	// IRQ helper
	// -------------------------------------------------------------------------
	void maybe_take_irq();

	// -------------------------------------------------------------------------
	// Inline flag logic
	// -------------------------------------------------------------------------
	inline void set_nz(uint8_t n)
	{
		if (n == 0)
			P = (P & ~F_N) | F_Z;
		else
			P = (P & ~(F_N | F_Z)) | (n & F_N);
	}

	inline void set_z(uint8_t n)
	{
		if (n == 0)
			P |= F_Z;
		else
			P &= ~F_Z;
	}

	// -------------------------------------------------------------------------
	// Opcode table entry structure
	// -------------------------------------------------------------------------
	struct OpEntry {
		void (cpu_6502::* instruction)();
		void (cpu_6502::* addressing_mode)();
	};
	static const OpEntry opcode_table[256];

	// -------------------------------------------------------------------------
	// Addressing modes
	// -------------------------------------------------------------------------
	void implied6502(); void immediate6502(); void abs6502(); void relative6502();
	void indirect6502(); void absx6502(); void absy6502(); void zp6502();
	void zpx6502(); void zpy6502(); void indx6502(); void indy6502();
	void indabsx6502(); void indzp6502();

	// -------------------------------------------------------------------------
	// Instruction implementations
	// -------------------------------------------------------------------------
	void adc6502(); void and6502(); void asl6502(); void asla6502();
	void bcc6502(); void bcs6502(); void beq6502(); void bit6502();
	void bmi6502(); void bne6502(); void bpl6502(); void brk6502();
	void bvc6502(); void bvs6502(); void clc6502(); void cld6502();
	void cli6502(); void clv6502(); void cmp6502(); void cpx6502();
	void cpy6502(); void dec6502(); void dex6502(); void dey6502();
	void eor6502(); void inc6502(); void inx6502(); void iny6502();
	void jmp6502(); void jsr6502(); void lda6502(); void ldx6502();
	void ldy6502(); void lsr6502(); void lsra6502(); void nop6502();
	void ora6502(); void pha6502(); void php6502(); void pla6502();
	void plp6502(); void rol6502(); void rola6502(); void ror6502();
	void rora6502(); void rti6502(); void rts6502(); void sbc6502();
	void sec6502(); void sed6502(); void sei6502(); void sta6502();
	void stx6502(); void sty6502(); void tax6502(); void tay6502();
	void tsx6502(); void txa6502(); void txs6502(); void tya6502();
	void bra6502(); void dea6502(); void ina6502(); void phx6502();
	void plx6502(); void phy6502(); void ply6502(); void stz6502();
	void tsb6502(); void trb6502(); 
	// -------------------------------------------------------------------------
	// Undocumented Instructions
	// -------------------------------------------------------------------------
	void lax6502(); void sax6502(); void dcp6502(); void isc6502();
	void slo6502(); void rra6502(); void rla6502(); void sre6502();
};

#endif // _6502_H_