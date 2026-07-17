
#ifndef _i8088_H_
#define _i8088_H_

#pragma once

#include "..//deftypes.h"
#include <cstdint>

/*
  Intel 8088/8086 emulator core.

  CPU instruction emulation written by Mike Chambers as part of
  "Fake86: A portable, open-source 8086 PC emulator."
  Copyright (C)2010-2013 Mike Chambers
  Released under the GNU General Public License (version 2 or later).

  Adapted into a self-contained, Neil Bradley / AAE compatible emulator
  class for "Another Arcade Emulator" (AAE), following the same integration
  pattern as cpu_i8080 / cpu_i8085. The instruction-decode core in
  cpu_i8088.cpp is kept as close to the original Fake86 cpu.c as practical;
  the AAE-specific memory access (cpu_i8088_mem.cpp) and glue
  (cpu_i8088_glue.cpp) live in separate translation units.

  The original Fake86 source remains under its own terms; this notice and
  the original copyright acknowledgement must be preserved.
*/

// 8086/8088 register file: byte registers overlay the low/high halves of the
// word registers. Little-endian layout (AL = byte 0, AH = byte 1, ...).
union _bytewordregs_ {
	uint16_t wordregs[8];
	uint8_t  byteregs[8];
};

class cpu_i8088
{

public:

	// ---- memory map / handler pointers (AAE Neil Bradley style) ----
	// Pointer to a flat 1 MB (0x100000) physical memory buffer. Reads/writes
	// that fall outside the registered handlers fall through to this buffer
	// (unless MAME-style memory handling rejects them).
	uint8_t* MEM = nullptr;
	MemoryReadByte*  memory_read  = nullptr;
	MemoryWriteByte* memory_write = nullptr;
	z80PortRead*     z80IoRead    = nullptr;
	z80PortWrite*    z80IoWrite   = nullptr;

	// ---- architectural register state ----
	union _bytewordregs_ regs = { { 0,0,0,0,0,0,0,0 } };
	uint16_t segregs[4] = { 0,0,0,0 }; // ES, CS, SS, DS
	uint16_t ip = 0;

	// byteregtable maps a 3-bit reg encoding (AL,CL,DL,BL,AH,CH,DH,BH) to the
	// matching byte offset inside the word-register union.
	uint8_t byteregtable[8] = { 0,2,4,6,1,3,5,7 };

	// ---- flag bits (kept as individual bytes, matching Fake86) ----
	uint8_t cf = 0, pf = 0, af = 0, zf = 0, sf = 0, tf = 0, ifl = 0, df = 0, of = 0;

	//Constructor / destructor (signature mirrors cpu_i8080 for drop-in use)
	cpu_i8088(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem,
	          z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr);
	~cpu_i8088() {};

	// ---- AAE public CPU interface ----
	uint8_t In(uint16_t bPort);            // convenience alias for portin
	void    Out(uint16_t bPort, uint8_t bVal); // convenience alias for portout
	int     exec(int cycles);              // run ~cycles worth of work, return leftover
	void    interrupt(uint8_t vector);     // maskable hardware INTR (honours IF)
	void    interrupt_nmi();               // non-maskable interrupt (vector 2)
	void    reset();
	int     get_ticks(int reset);

	// Use MAME style memory handling: reject reads/writes that don't hit a handler.
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s)     { log_debug_rw = s; }

	// ---- memory / port access (routed through AAE handlers, see cpu_i8088_mem.cpp) ----
	uint8_t  read86(uint32_t addr32);
	void     write86(uint32_t addr32, uint8_t value);
	uint16_t readw86(uint32_t addr32);
	void     writew86(uint32_t addr32, uint16_t value);
	uint8_t  portin(uint16_t portnum);
	uint16_t portin16(uint16_t portnum);
	void     portout(uint16_t portnum, uint8_t value);
	void     portout16(uint16_t portnum, uint16_t value);

private:

	// ---- decoder / ALU scratch state (was file-scope globals in Fake86) ----
	uint16_t savecs = 0, saveip = 0, useseg = 0, oldsp = 0;
	uint8_t  opcode = 0, segoverride = 0, reptype = 0, hltstate = 0;
	uint8_t  mode = 0, reg = 0, rm = 0;
	uint8_t  oper1b = 0, oper2b = 0, res8 = 0, disp8 = 0, temp8 = 0, nestlev = 0, addrbyte = 0;
	uint8_t  tempcf = 0, oldcf = 0;
	uint16_t oper1 = 0, oper2 = 0, res16 = 0, disp16 = 0, temp16 = 0, dummy = 0, stacksize = 0, frametemp = 0;
	uint32_t temp1 = 0, temp2 = 0, temp3 = 0, temp4 = 0, temp5 = 0, temp32 = 0, tempaddr32 = 0, ea = 0;
	int32_t  result = 0;
	uint64_t totalexec = 0;
	uint16_t firstip = 0, trap_toggle = 0;

	// ---- instruction-core helpers (cpu_i8088.cpp) ----
	void flag_szp8(uint8_t value);
	void flag_szp16(uint16_t value);
	void flag_log8(uint8_t value);
	void flag_log16(uint16_t value);
	void flag_adc8(uint8_t v1, uint8_t v2, uint8_t v3);
	void flag_adc16(uint16_t v1, uint16_t v2, uint16_t v3);
	void flag_add8(uint8_t v1, uint8_t v2);
	void flag_add16(uint16_t v1, uint16_t v2);
	void flag_sbb8(uint8_t v1, uint8_t v2, uint8_t v3);
	void flag_sbb16(uint16_t v1, uint16_t v2, uint16_t v3);
	void flag_sub8(uint8_t v1, uint8_t v2);
	void flag_sub16(uint16_t v1, uint16_t v2);

	void op_adc8();  void op_adc16();
	void op_add8();  void op_add16();
	void op_and8();  void op_and16();
	void op_or8();   void op_or16();
	void op_xor8();  void op_xor16();
	void op_sub8();  void op_sub16();
	void op_sbb8();  void op_sbb16();

	void     getea(uint8_t rmval);
	void     push(uint16_t pushval);
	uint16_t pop();
	uint16_t readrm16(uint8_t rmval);
	uint8_t  readrm8(uint8_t rmval);
	void     writerm16(uint8_t rmval, uint16_t value);
	void     writerm8(uint8_t rmval, uint8_t value);

	uint8_t  op_grp2_8(uint8_t cnt);
	uint16_t op_grp2_16(uint8_t cnt);
	void     op_div8(uint16_t valdiv, uint8_t divisor);
	void     op_idiv8(uint16_t valdiv, uint8_t divisor);
	void     op_grp3_8();
	void     op_div16(uint32_t valdiv, uint16_t divisor);
	void     op_idiv16(uint32_t valdiv, uint16_t divisor);
	void     op_grp3_16();
	void     op_grp5();

	void intcall86(uint8_t intnum); // software / generic IVT interrupt
	void reset86();

	// ---- configuration ----
	bool mmem = false;          // MAME-style memory handling (reject unhandled rw)
	int  log_debug_rw = 0;      // log unhandled reads/writes & illegal opcodes
	int  clocktickstotal = 0;   // running, resettable tick total
	int  op_cycle_estimate = 8; // approximate cycles charged per executed op
	                            // (Fake86 has no per-op timing; this keeps the
	                            //  AAE cycle/exec contract consistent and tunable)
};

#endif
