
#ifndef _LR35902_H_
#define _LR35902_H_

#pragma once

#include "deftypes.h"
#include <cstdint>

// -----------------------------------------------------------------------------
// Sharp LR35902 (Game Boy CPU) Emulator
//
// A hybrid of the Intel 8080 and Zilog Z80 used in the Nintendo Game Boy (DMG)
// and Game Boy Color (DMG mode). The interface (constructor, exec, reset,
// get_ticks, In, Out, memory handler pointers) mirrors cpu_i8080 / cpu_i8085
// for drop-in compatibility with the AAE-style cpu_control framework.
//
// Differences from the 8080 / Z80:
//   - 8080-style register set (A, F, B, C, D, E, H, L, SP, PC).
//     No shadow registers, no IX / IY.
//   - F flag layout is Z(0x80), N(0x40), H(0x20), C(0x10). Bits 0..3 always 0.
//   - Z80-style 0xCB-prefix bit / rotate / shift block; adds SWAP (0xCB 30..37).
//   - No IN / OUT (no I/O port space).
//   - No DD / ED / FD prefixes; no LDI / LDD / LDIR / LDDR / EX / EXX.
//   - Adds LR35902-only ops:
//       0x08  LD (a16),SP        0xE0  LDH (a8),A     0xF0  LDH A,(a8)
//       0x22  LD (HL+),A         0xE2  LD (C),A       0xF2  LD A,(C)
//       0x2A  LD A,(HL+)         0xE8  ADD SP,r8      0xF8  LD HL,SP+r8
//       0x32  LD (HL-),A         0xEA  LD (a16),A     0xFA  LD A,(a16)
//       0x3A  LD A,(HL-)         0xD9  RETI
//       0x10  STOP                              CB 30..37  SWAP r
//   - DAA semantics differ from 8080 / Z80 (uses N and H flags only).
//   - Five fixed interrupt vectors gated by IF / IE / IME (not IM 0/1/2):
//       VBLANK 0x40, LCD 0x48, TIMER 0x50, SERIAL 0x58, JOYPAD 0x60
//   - IF (0xFF0F) and IE (0xFFFF) are owned inside the CPU; the host wires
//     its memory handlers for those addresses into get_IF / set_IF / get_IE
//     / set_IE.
// -----------------------------------------------------------------------------

class cpu_lr35902
{

public:

	// ----- Flag bit definitions (LR35902 layout - flags in high nibble) -----
	static constexpr uint8_t Z_FLAG = 0x80;  // Zero
	static constexpr uint8_t N_FLAG = 0x40;  // Subtract (used by DAA)
	static constexpr uint8_t H_FLAG = 0x20;  // Half-carry (from bit 3)
	static constexpr uint8_t C_FLAG = 0x10;  // Carry      (from bit 7)
	// Bits 0..3 of F are always read back as 0 on real hardware.

	// ----- Interrupt bit masks (matches Game Boy IF / IE bit layout) -----
	static constexpr uint8_t INT_VBLANK = 0x01;  // vector 0x40
	static constexpr uint8_t INT_LCD    = 0x02;  // vector 0x48
	static constexpr uint8_t INT_TIMER  = 0x04;  // vector 0x50
	static constexpr uint8_t INT_SERIAL = 0x08;  // vector 0x58
	static constexpr uint8_t INT_JOYPAD = 0x10;  // vector 0x60

	// ----- Registers -----
	uint8_t  reg8[9] = { 0,0,0,0,0,0,0,0,0 };
	uint16_t reg_SP = 0;
	uint16_t reg_PC = 0;

	#define lr_reg16_AF (((uint16_t)reg8[A] << 8) | (uint16_t)(reg8[FLAGS] & 0xF0))
	#define lr_reg16_BC (((uint16_t)reg8[B] << 8) | (uint16_t)reg8[C])
	#define lr_reg16_DE (((uint16_t)reg8[D] << 8) | (uint16_t)reg8[E])
	#define lr_reg16_HL (((uint16_t)reg8[H] << 8) | (uint16_t)reg8[L])

	enum {
		B, C, D, E, H, L, M, A, FLAGS
	};

	// ----- Interrupt / execution state (public so host can inspect) -----
	uint8_t IF          = 0;     // bottom 5 bits used; top 3 read back as 1
	uint8_t IE          = 0;     // full 8 bits R/W
	bool    IME         = false; // master interrupt enable
	bool    IME_pending = false; // EI delay one-shot
	bool    halted      = false; // set by HALT, cleared by any pending int
	bool    stopped     = false; // set by STOP, polled / cleared by host
	bool    halt_bug    = false; // HALT-bug one-shot (next fetch repeats PC)

	// ----- Memory map + handlers -----
	uint8_t          *MEM           = nullptr;
	MemoryReadByte   *memory_read   = nullptr;
	MemoryWriteByte  *memory_write  = nullptr;
	z80PortRead      *z80IoRead     = nullptr;  // unused
	z80PortWrite     *z80IoWrite    = nullptr;  // unused

	// Constructor / destructor - same signature as cpu_i8080 / cpu_i8085
	cpu_lr35902(uint8_t* mem,
	            MemoryReadByte*  read_mem,
	            MemoryWriteByte* write_mem,
	            z80PortRead*     port_read,
	            z80PortWrite*    port_write,
	            uint16_t         addr);

	~cpu_lr35902() {};

	// ----- Public interface (matches cpu_i8080 / cpu_i8085) -----
	uint8_t In(uint8_t bPort);                // no-op (returns 0xFF) - API parity
	void    Out(uint8_t bPort, uint8_t bVal); // no-op - API parity
	int     exec(int cycles);                 // returns residual cycles (negative)
	int     get_ticks(int reset);
	void    reset();

	// 8080 / cpu_control-compatible interrupt entry: alias of request_interrupt.
	void    interrupt(uint8_t mask) { request_interrupt(mask); }

	// ----- LR35902 / Game Boy interrupt API -----
	void    request_interrupt(uint8_t mask) { IF = (IF | mask) & 0x1F; }
	uint8_t get_IF() const                  { return IF | 0xE0; }
	void    set_IF(uint8_t v)               { IF = v & 0x1F; }
	uint8_t get_IE() const                  { return IE; }
	void    set_IE(uint8_t v)               { IE = v; }

	// MAME-style memory handling options
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s)     { log_debug_rw = s; }

private:

	// ----- Memory access (private) -----
	uint8_t  lr_read (uint16_t addr);
	void     lr_write(uint16_t addr, uint8_t v);
	uint16_t lr_read_word (uint16_t addr);
	void     lr_write_word(uint16_t addr, uint16_t v);

	// Register / pair access
	void     write_reg8(uint8_t reg, uint8_t value);
	uint8_t  read_reg8 (uint8_t reg);

	uint16_t read_RP        (uint8_t rp);   // BC, DE, HL, SP
	uint16_t read_RP_PUSHPOP(uint8_t rp);   // BC, DE, HL, AF (with F nibble mask)
	void     write_RP        (uint8_t rp, uint8_t lb, uint8_t hb);
	void     write16_RP       (uint8_t rp, uint16_t value);
	void     write16_RP_PUSHPOP(uint8_t rp, uint16_t value);

	// Stack
	void     push16(uint16_t v);
	uint16_t pop16 ();

	// Fetch (consumes / clears halt_bug)
	uint8_t  fetch_byte();
	uint16_t fetch_word();

	// Condition codes (cc: 0=NZ, 1=Z, 2=NC, 3=C)
	uint8_t  test_cond(uint8_t cc);

	// ----- 8-bit arithmetic helpers (set Z, N, H, C) -----
	void     Add_1(uint8_t v);
	void     Adc_1(uint8_t v);
	void     Sub_1(uint8_t v);
	void     Sbc_1(uint8_t v);
	void     And_1(uint8_t v);
	void     Or_1 (uint8_t v);
	void     Xor_1(uint8_t v);
	void     Cp_1 (uint8_t v);
	uint8_t  Inc_1(uint8_t v);
	uint8_t  Dec_1(uint8_t v);

	// ----- 16-bit arithmetic -----
	uint16_t Add_HL   (uint16_t v);          // N=0, H/C from bit 11/15; Z untouched
	uint16_t Add_SP_r8(int8_t r8);           // Z=0, N=0, H/C from low-byte add

	// ----- Decimal adjust (LR35902 variant) -----
	void     Daa();

	// ----- Rotate / shift helpers -----
	uint8_t  Rlc (uint8_t v);
	uint8_t  Rrc (uint8_t v);
	uint8_t  Rl  (uint8_t v);
	uint8_t  Rr  (uint8_t v);
	uint8_t  Sla (uint8_t v);
	uint8_t  Sra (uint8_t v);
	uint8_t  Srl (uint8_t v);
	uint8_t  Swap(uint8_t v);

	// CB BIT / RES / SET
	void     Bit(uint8_t v, int n);
	uint8_t  Res(uint8_t v, int n);
	uint8_t  Set(uint8_t v, int n);

	// Accumulator-only rotates (RLCA / RRCA / RLA / RRA): Z always 0
	void     Rlca();
	void     Rrca();
	void     Rla();
	void     Rra();

	// CB-prefix dispatcher. Returns T-states consumed by the CB op.
	int      HandleCB();

	// Service one pending interrupt if any. Returns T-states consumed (0 or 20).
	int      service_interrupt();

	// ----- Internal state -----
	bool     debug          = false;
	bool     mmem           = false;  // MAME-style memory blocking
	int      log_debug_rw   = 0;      // log unhandled R/W
	int      clocktickstotal = 0;     // running, resetable T-state total
};

#endif // _LR35902_H_
