// -----------------------------------------------------------------------------
// AAE (Another Arcade Emulator) - 6502 CPU Core
//
// This file is part of the AAE project and is released under The Unlicense.
// You are free to use, modify, and distribute this software without restriction.
// See <http://unlicense.org/> for details.
//
// Origins and Contributions:
//   - Original 6502 CPU core by Neil Bradley, adapted from unnamed web sources.
//   - ADC and SBC algorithms from M.A.M.E. (c) 1998-2000 Juergen Buchmueller.
//   - Timing tables and stack logic adapted from FakeNes.
//   - Modern C++ refactor and extensions by TC (2015-2025) for AAE compatibility.
//   - Additional documentation and cleanup assisted by ChatGPT and Gemini Pro 3 and Claude.ai.
//
// This version retains functional and timing accuracy, supports profiling and
// debugging, and integrates mostly with legacy MAME-style emulation systems.
// 6510 USAGE NOTE:
// 1. Create the CPU
// cpu = new cpu_6502(mem, read_handlers, write_handlers, 0xFFFF, 0, CPU_6510);
// 2. Define a static callback to handle ROM Banking
// static void c64_banking_callback(uint8_t data, uint8_t dir) {
// Bits 0, 1, 2 of 'data' usually control BASIC, KERNAL, and I/O visibility
// update_memory_map(data);
// 3. Register it
// cpu->set_6510_port_callback(c64_banking_callback);
// -----------------------------------------------------------------------------

// Notes
// 11/22/24 added undoucumented isb opcode, will work to add the rest later.
// 12/28/24 Rewrote the main loop to resolve an issue with the cycle counting being consistently undereported.
// 01/03/25 Discovered an edge case where clocktickstotal was not being set to zero at init, causing an immediate crash.
// 01/09/25 Changed clocktickstotal again to be set to zero at init, not reset. When a cpu was reset mid-frame, it was throwing the timing count off.
// 06/29/25 Added IRQ after CLI check, added new dissassembler, code cleanup
// 06/30/25 Added the most commonly used undocumented instructions, and hopefully adjusted the timing table to match. These are totally unverified.
// For your own usage, just undefine USING_AAE_EMU
// 07/01/25 Moved the stack operations back to not using the Memory Handlers. This was a good speed up and did not affect Major Havoc.
// I left the old code commented out, just in case.
// 09/01/25 Fixed a newly introduced BCD bug in SBC  if (hi & 0x0100) hi -= 0x60;  // <-- high-digit BCD adjust
// Updated ABC and SBC code to handle NMOS edge cases. 
// Rewrote the IRQ after CLI handling to work correctly. 
// Doubled down on my targeted NMOS first support, added some undocumented upcodes. 
// Updated (again) changed the ADC/SBC code.
// 01/18/2025 Partial Re-Write with claude.ai. Corrected all documented and undocumented instructions. Added 65C02 Support, 6510 Support
// and 2A03 support. This CPU Core now passes both NMOS and CMOS Klaus Tests, as well as most Lorentz tests, the rest need specific things that I do not want to add yet. 
// Added IRQ_HOLD for Emulators that need it like the Commodore PET. Default is IRQ_PULSE.
// TESTED with the Commodore 64 and NES, see AI generated test emulators on my GitHub. 

// 03/06/2026 Added readop() --an inline function that reads directly from the MEM[] array, bypassing the memory read handler chain.This separates instruction fetches from data fetches.
// Previously, all memory reads -- opcode fetches, operand fetches, and data reads -- went through get6502memory() and the full handler chain.
// This caused problems for drivers like Missile Command where the read handler inspects the currently executing opcode(via cpu_getppc()) to decide 
// whether to return video RAM data or ROM data.When an opcode fetch itself went through that handler, the handler would misidentify it as a data access and return 
// video RAM instead of ROM, corrupting the instruction stream. readop() is now used for the opcode fetch in step6502() and for all operand fetches from PC in the addressing modes : 
// abs, absx, absy, relative, indirect, zp, zpx, zpy, indx, indy, indzp, indabsx, and zprel.Data reads from the effective address(the actual LDA / STA / CMP targets) remain routed 
// through get6502memory() and the memory handlers as before.
// Also moved PPC = PC to execute before the opcode fetch so that get_ppc() returns the address of the opcode itself, not the byte after it.
// Both changes are MOSTLY! backward - compatible.For all existing drivers, MEM[addr] and get6502memory(addr) return the same value for ROM / RAM regions since the MRA_RAM / MRA_ROM handlers just 
// read from MEM.Only drivers with custom read handlers that inspect execution context(like Missile Command) are affected. 
// AND Major Havoc, which needed an override to be able to read banked rom data.m_cpu_6502[CPU0]->opfetch_through_handlers(true);


#ifndef _6502_H_
#define _6502_H_

#pragma once

#include <cstdint>
#include <string>
#include "deftypes.h"

// undefine USING_AAE_EMU to skip the timer code.  
#define USING_AAE_EMU

enum irqmode
{
	IRQ_PULSE,
	IRQ_HOLD
};


enum CpuModel {
	CPU_NMOS_6502,
	CPU_CMOS_65C02,
	CPU_NES_2A03,
	CPU_6510
};

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
	cpu_6502(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num, CpuModel model = CPU_NMOS_6502);
	~cpu_6502() = default;

	void init6502(uint16_t addrmaskval, CpuModel model = CPU_NMOS_6502);
	void reset6502();
	void execute_irq();
	void irq6502(int irqmode = IRQ_PULSE);
	void nmi6502();
	int exec6502(int timerTicks);
	int step6502();
	int get6502ticks(int reset);

	// 2. Add a callback setter for the 6510 Port
	// The emulator calls this to set a function that triggers when the port changes.
	typedef void (*PortCallback)(uint8_t data, uint8_t direction);
	void set_6510_port_callback(PortCallback cb) { port_cb = cb; }


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
	// Opcode fetch routing
	// -------------------------------------------------------------------------
	// When enabled, opcode and operand fetches go through the memory handler
	// chain (get6502memory) instead of reading directly from MEM[].
	// Required for games with bank-switched ROM (e.g. Major Havoc) where the
	// flat MEM array does not reflect the currently selected bank.
	// Default is false (direct MEM access, matching MAME cpu_readop behavior).
	void opfetch_through_handlers(bool s) { use_handler_for_opfetch = s; }

	// -------------------------------------------------------------------------
	// Stack operations
	// -------------------------------------------------------------------------
	void push16(uint16_t val);
	void push8(uint8_t val);
	uint16_t pull16();
	uint8_t pull8();

	uint8_t A = 0, P = 0, X = 0, Y = 0, S = 0xFF;
	uint16_t PC = 0, PPC = 0;


private:
	// Store the model
	CpuModel cpu_model = CPU_NMOS_6502;
	// -------------------------------------------------------------------------
	// CPU internal flags and registers
	// -------------------------------------------------------------------------
	bool direct_zero_page = false;
	bool direct_stack_page = false;
	// -------------------------------------------------------------------------
	// CPU internal state
	// -------------------------------------------------------------------------
	uint16_t addrmask = 0;
	uint8_t opcode = 0;
	uint16_t savepc = 0, oldpc = 0, reladdr = 0, help = 0;
	uint8_t value = 0, saveflags = 0;

	int clockticks6502 = 0;
	int clocktickstotal = 0;
	// IRQ Handling
	int _irqMode = 0;
	int _irqPending = 0;
	uint8_t irq_inhibit_one = 0;
	int cpu_num = 0;

	bool debug = false;
	bool mmem = false;
	bool log_debug_rw = false;
	bool use_handler_for_opfetch = false;

	// 6510 Internal State
	uint8_t io_port_data = 0; // $0001
	uint8_t io_port_dir = 0;  // $0000
	PortCallback port_cb = nullptr;

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

	// Direct read from MEM[] array, bypassing memory handlers.
	// Used for opcode and operand fetches (equivalent to MAME cpu_readop).
	// Data reads still go through get6502memory() and the handler chain.
	inline uint8_t readop(uint16_t addr);

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
	OpEntry opcode_table[256]; // The instance table

	// ADD THIS LINE HERE:
	static const OpEntry initial_opcode_table[256];

	// -------------------------------------------------------------------------
	// Addressing modes
	// -------------------------------------------------------------------------
	void implied6502(); void immediate6502(); void abs6502(); void relative6502();
	void indirect6502(); void absx6502(); void absy6502(); void zp6502();
	void zpx6502(); void zpy6502(); void indx6502(); void indy6502();
	void indabsx6502(); void indzp6502(); void zprel6502(); // For BBR/BBS

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
	// CMOS specific ALU helpers
	void adc65c02();
	void sbc65c02();
	// NES 2A03 specific ALU helpers (BCD Disabled)
	void adc_2a03();
	void sbc_2a03();
	void rra_2a03();
	void isc_2a03();
	// -------------------------------------------------------------------------
	// NMOS Undocumented Instructions
	// -------------------------------------------------------------------------
	void lax6502(); void sax6502(); void dcp6502(); void isc6502();
	void slo6502(); void rra6502(); void rla6502(); void sre6502();
	void anc6502(); void alr6502(); void arr6502(); void axs6502();
	//Lorentz Tests
	void ane6502(); void lxa6502(); void shs6502(); void shy6502(); 
	void shx6502(); void ahx6502(); void las6502();
	// C6502 Special Instructions
	void rmb_smb_6502(); // Handles RMB0-7 and SMB0-7
	void bbr_bbs_6502(); // Handles BBR0-7 and BBS0-7


};

#endif // _6502_H_