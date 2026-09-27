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
// cpu_6502.cpp
// 01/18/2025 Partial Re-Write with claude.ai. Corrected all documented and undocumented instructions. Added 65C02 Support, 6510 Support
// and 2A03 support. Passes both NMOS and CMOS Klaus Tests. Added IRQ_HOLD for Emulators that need it like the Commodore PET. Default is pulse.

#ifndef _6502_H_
#define _6502_H_

#pragma once

#include <cstdint>
#include <string>

// Define USING_AAE_EMU for the AAE build: the core then drives AAE's timers
// from step6502(), takes its memory-handler types from deftypes.h and fetches
// opcodes directly from MEM by default. This line is the only difference
// between the AAE cpu_code copy and this file.
#define USING_AAE_EMU

#ifdef USING_AAE_EMU
#include "deftypes.h"
#else
#include "cpu_fw.h"
#endif

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
	// Hardware reset sequence, preserving A/X/Y and decrementing the current S.
	// The original reset6502() remains the host's untimed initialization API.
	void reset_bus6502();
	// Immediate IRQ entry: call at a CPU boundary only, never from a memory
	// handler, cycle_cb or a timer callback. irq6502() only latches a request.
	void execute_irq();
	void irq6502(int irqmode = IRQ_PULSE);
	// NMI request. At an instruction boundary the seven-cycle sequence runs
	// immediately (and reaches the AAE timers). While the core is busy - from
	// a memory handler, cycle_cb or a timer callback fired by the step - the
	// edge is latched and taken at the next instruction boundary, as hardware
	// does with an NMI edge during an instruction. Ignored while STP/JAM parked.
	void nmi6502();
	// Pin APIs are safe from cycle_cb: true means asserted (electrically low).
	// IRQ is level-sensitive; NMI latches an inactive-to-active edge.
	void set_irq_line(bool asserted);
	void set_nmi_line(bool asserted);
	// Runs at least timerTicks machine cycles and returns the count actually
	// spent. A parked CPU still lets time pass, one cycle per step.
	int exec6502(int timerTicks);
	// A parked WAI/STP/JAM step advances the optional host clock once, counts
	// one machine cycle in get6502ticks() and the AAE timers, reports one
	// elapsed cycle and returns zero CPU cycles.
	int step6502();
	enum class RunState { Running, Waiting, Stopped, Jammed };
	RunState get_run_state() const { return run_state; }
	int get6502ticks(int reset);

	// Snapshot of the most recent step6502(), nmi6502(), or reset_bus6502().
	// Direct reset6502()/execute_irq() calls are outside this report.
	// step6502() and get6502ticks() retain their legacy CPU-cycle semantics.
	// Emitted bus cycles are the only cycle source; there is no per-opcode
	// table any more. The external SingleStepTests vectors are the reference.
	struct CycleReport {
		int cpu_cycles = 0;       // bus cycles actually performed, including without a callback
		uint64_t elapsed_cycles = 0; // callback ticks, including stalled attempts
		uint64_t stall_cycles = 0;
		bool bus_timing_active = false;
	};
	CycleReport get_last_cycle_report() const { return last_cycle_report; }

	// 2. Add a callback setter for the 6510 Port
	// The emulator calls this to set a function that triggers when the port changes.
	typedef void (*PortCallback)(uint8_t data, uint8_t direction);
	void set_6510_port_callback(PortCallback cb) { port_cb = cb; }

	// -------------------------------------------------------------------------
	// Cycle-accurate mode (opt-in). The hook advances the rest of the machine
	// by one cycle and returns true if the bus is available to the CPU this
	// cycle (false -> a read stalls, e.g. VIC bad line). When the hook is null
	// the core still counts bus cycles but does not advance an external machine.
	// -------------------------------------------------------------------------
	typedef bool (*CycleCallback)(void* user);
	void set_cycle_callback(CycleCallback cb, void* user) { cycle_cb = cb; cycle_user = user; }
	// Observe completed CPU bus transfers without repeating memory-mapped reads.
	typedef void (*BusCallback)(uint8_t value, void* user);
	void set_bus_callback(BusCallback cb, void* user) { bus_cb=cb; bus_user=user; }
	// Wait states inserted by a memory handler, for boards without a cycle
	// callback: the board held the CPU (RDY low or a stretched phi0) for
	// `cycles` before completing the access the handler is servicing.  They
	// count as machine time everywhere - this instruction's cycle count (so
	// the frame budget and the AAE timers see them) and the pending-tick
	// readers such as the POKEY adapter's catch-up.  Call only from a memory
	// handler while an instruction is executing.  No-op for cycles <= 0.
	void bus_wait(int cycles);

	// Allow the emulator to set input pins (like cassette sense)
	void set_6510_port_in(uint8_t val) { port_in = val; }

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
	// True while the cycle callback is running for a WRITE bus cycle. The
	// machine needs it because ANTIC DMA and a WSYNC wait do not treat writes
	// the same way - see cpu_cycle_tick() in atari800.cpp.
	bool cycle_is_write = false;
	BusCallback bus_cb = nullptr;
	void* bus_user = nullptr;
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
	// Bypass handlers for stack operations only, preserving bus timing/observers.
	// Defaults to false. The RAM address still obeys the CPU address mask.
	void set_direct_stack_access(bool enabled) { direct_stack_page = enabled; }
	// AAE-compatible fetch policy; this core defaults to handlers for banked hosts.
	void opfetch_through_handlers(bool enabled) { use_handler_for_opfetch = enabled; }

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
#ifdef USING_AAE_EMU
	// MAME cpu_readop behavior; Major Havoc opts into handlers for banked ROM.
	bool use_handler_for_opfetch = false;
#else
	bool use_handler_for_opfetch = true;
#endif
	// -------------------------------------------------------------------------
	// CPU internal state
	// -------------------------------------------------------------------------
	uint16_t addrmask = 0;
	uint8_t opcode = 0;
	uint16_t savepc = 0, oldpc = 0, reladdr = 0, help = 0;
	uint8_t value = 0, saveflags = 0;

	int clockticks6502 = 0;
	int clocktickstotal = 0;

	// Cycle-accurate mode state + primitives (see set_cycle_callback).
	CycleCallback cycle_cb = nullptr;
	void*         cycle_user = nullptr;
	int           cycles_emitted = 0;   // explicit cycle primitives spent this instruction
	uint64_t      elapsed_cycles = 0;
	CycleReport   last_cycle_report;
	inline void   wait_for_bus_cycle();
	void          finish_cycle_report();
	bool          indexed_access_is_read() const;
	void          adc_nmos_value(uint8_t value);
	void          sbc_nmos_value(uint8_t value);
	void          interrupt_entry(uint16_t vector);
	void          push_cycle(uint8_t value);
	uint8_t       pull_cycle();
	inline uint8_t read_cycle(uint16_t addr, bool stack = false);
	inline void    write_cycle(uint16_t addr, uint8_t v, bool stack = false);
	uint8_t stack_read(uint16_t addr);
	void stack_write(uint16_t addr, uint8_t value);
	uint8_t readop(uint16_t addr);
	uint8_t fetch_cycle(uint16_t addr);
	uint8_t operand_cycle();
	inline void    idle_cycle();                          // tick world for an internal/dummy cycle
	// IRQ Handling
	int _irqMode = 0;
	int _irqPending = 0;
	uint8_t irq_inhibit_one = 0;
	bool pin_irq_enabled = false, irq_line = false, irq_polled = false;
	bool nmi_line = false, nmi_latched = false, nmi_polled = false;
	bool nmi_immediate = false;  // reentrant nmi6502(): take at the next boundary
	// True while the core is busy (an instruction, a parked cycle, or the
	// timer update that ends a step): nmi6502() must latch, not execute.
	bool in_instruction = false;
	RunState run_state = RunState::Running;
	uint16_t irq_history = 0, nmi_history = 0;
	void poll_interrupt_pins();
	void take_nmi();
	int cpu_num = 0;

	bool debug = false;
	bool mmem = false;
	bool log_debug_rw = false;
	bool kil_logged = false;   // one-shot log when a KIL/JAM opcode halts the CPU

	// 6510 Emulation State
	uint8_t ddr = 0x00;       // $0000 Data Direction
	uint8_t port_out = 0x00;  // $0001 Output Latch (What CPU wrote)
	uint8_t port_in = 0xFF;   // $0001 Input Pins (External hardware, default high)
	bool skip_poll = false;   // BRK: no interrupt poll at the end of the sequence

	// Callback for banking
	PortCallback port_cb = nullptr;

	// Helper to check for changes
	void check_and_notify_6510(uint8_t old_ddr, uint8_t old_port);

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
	OpEntry opcode_table[256]; // The instance table

	// ADD THIS LINE HERE:
	static const OpEntry initial_opcode_table[256];

	// -------------------------------------------------------------------------
	// Addressing modes
	// -------------------------------------------------------------------------
	void implied6502(); void immediate6502(); void abs6502(); void abs_jsr6502();
	void relative6502();
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
	void kil6502();                       // NMOS JAM/KIL: freezes the PC
	void wai6502();
	void stp6502();
	uint16_t sh_target(uint16_t base, uint8_t store_value);  // SHx page-cross target
	// C6502 Special Instructions
	void rmb_smb_6502(); // Handles RMB0-7 and SMB0-7
	void bbr_bbs_6502(); // Handles BBR0-7 and BBS0-7
};

#endif // _6502_H_
