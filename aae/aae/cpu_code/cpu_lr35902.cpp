
#include "cpu_lr35902.h"
#include "sys_log.h"
#include <cmath>
#include <cstdlib>


// =============================================================================
// Construction / lifecycle
// =============================================================================

cpu_lr35902::cpu_lr35902(uint8_t* mem,
                         MemoryReadByte*  read_mem,
                         MemoryWriteByte* write_mem,
                         z80PortRead*     port_read,
                         z80PortWrite*    port_write,
                         uint16_t         addr)
{
	MEM           = mem;
	memory_read   = read_mem;
	memory_write  = write_mem;
	z80IoRead     = port_read;   // unused on LR35902 (no IN/OUT)
	z80IoWrite    = port_write;  // unused
	reg_PC        = addr;
}

void cpu_lr35902::reset()
{
	for (int i = 0; i < 9; ++i) reg8[i] = 0;
	reg_SP       = 0xFFFE;
	reg_PC       = 0x0000;
	IF           = 0;
	IE           = 0;
	IME          = false;
	IME_pending  = false;
	halted       = false;
	stopped      = false;
	halt_bug     = false;
}

int cpu_lr35902::get_ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset) clocktickstotal = 0;
	return tmp;
}

// =============================================================================
// Memory access (mirrors cpu_i8080::i8080_read / i8080_write)
// =============================================================================

uint8_t cpu_lr35902::lr_read(uint16_t addr)
{
	uint8_t temp = 0;
	MemoryReadByte* MemRead = memory_read;

	while (MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
		{
			if (MemRead->memoryCall)
				temp = MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
			else
				temp = *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
			MemRead = nullptr;
			break;
		}
		++MemRead;
	}
	if (MemRead && !mmem)
		temp = MEM[addr];
	if (MemRead && mmem)
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Read at %x", addr);

	return temp;
}

void cpu_lr35902::lr_write(uint16_t addr, uint8_t byte)
{
	MemoryWriteByte* MemWrite = memory_write;

	while (MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
		{
			if (MemWrite->memoryCall)
				MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
			else
				*((uint8_t*)MemWrite->pUserArea + (addr - MemWrite->lowAddr)) = byte;
			MemWrite = nullptr;
			break;
		}
		++MemWrite;
	}
	if (MemWrite && !mmem)
		MEM[addr] = byte;
	if (MemWrite && mmem)
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Write at %x data: %x", addr, byte);
}

uint16_t cpu_lr35902::lr_read_word(uint16_t addr)
{
	uint16_t lo = lr_read(addr);
	uint16_t hi = lr_read(addr + 1);
	return lo | (hi << 8);
}

void cpu_lr35902::lr_write_word(uint16_t addr, uint16_t v)
{
	lr_write(addr,     (uint8_t)(v & 0xFF));
	lr_write(addr + 1, (uint8_t)(v >> 8));
}

// LR35902 has no IN/OUT - exposed for cpu_control framework API parity.
uint8_t cpu_lr35902::In (uint8_t /*bPort*/)               { return 0xFF; }
void    cpu_lr35902::Out(uint8_t /*bPort*/, uint8_t /*v*/) { /* no-op */   }

// =============================================================================
// Register / pair access
// =============================================================================

uint8_t cpu_lr35902::read_reg8(uint8_t reg)
{
	if (reg == M) return lr_read(lr_reg16_HL);
	return reg8[reg];
}

void cpu_lr35902::write_reg8(uint8_t reg, uint8_t value)
{
	if (reg == M) lr_write(lr_reg16_HL, value);
	else          reg8[reg] = value;
}

uint16_t cpu_lr35902::read_RP(uint8_t rp)
{
	switch (rp) {
	case 0x00: return lr_reg16_BC;
	case 0x01: return lr_reg16_DE;
	case 0x02: return lr_reg16_HL;
	case 0x03: return reg_SP;
	}
	return 0;
}

uint16_t cpu_lr35902::read_RP_PUSHPOP(uint8_t rp)
{
	switch (rp) {
	case 0x00: return lr_reg16_BC;
	case 0x01: return lr_reg16_DE;
	case 0x02: return lr_reg16_HL;
	case 0x03: return lr_reg16_AF;   // F low nibble already masked to 0
	}
	return 0;
}

void cpu_lr35902::write_RP(uint8_t rp, uint8_t lb, uint8_t hb)
{
	switch (rp) {
	case 0x00: reg8[C] = lb; reg8[B] = hb; break;
	case 0x01: reg8[E] = lb; reg8[D] = hb; break;
	case 0x02: reg8[L] = lb; reg8[H] = hb; break;
	case 0x03: reg_SP  = (uint16_t)lb | ((uint16_t)hb << 8); break;
	}
}

void cpu_lr35902::write16_RP(uint8_t rp, uint16_t v)
{
	switch (rp) {
	case 0x00: reg8[C] = v & 0xFF; reg8[B] = v >> 8; break;
	case 0x01: reg8[E] = v & 0xFF; reg8[D] = v >> 8; break;
	case 0x02: reg8[L] = v & 0xFF; reg8[H] = v >> 8; break;
	case 0x03: reg_SP  = v; break;
	}
}

void cpu_lr35902::write16_RP_PUSHPOP(uint8_t rp, uint16_t v)
{
	switch (rp) {
	case 0x00: reg8[C] = v & 0xFF;        reg8[B] = v >> 8; break;
	case 0x01: reg8[E] = v & 0xFF;        reg8[D] = v >> 8; break;
	case 0x02: reg8[L] = v & 0xFF;        reg8[H] = v >> 8; break;
	case 0x03:
		// Low nibble of F is always 0 on LR35902 (no X / Y / V / N parity bits).
		reg8[FLAGS] = v & 0xF0;
		reg8[A]     = v >> 8;
		break;
	}
}

// =============================================================================
// Stack
// =============================================================================

void cpu_lr35902::push16(uint16_t v)
{
	lr_write(--reg_SP, (uint8_t)(v >> 8));
	lr_write(--reg_SP, (uint8_t)(v & 0xFF));
}

uint16_t cpu_lr35902::pop16()
{
	uint16_t lo = lr_read(reg_SP++);
	uint16_t hi = lr_read(reg_SP++);
	return lo | (hi << 8);
}

// =============================================================================
// Fetch (handles the HALT bug one-shot)
// =============================================================================

uint8_t cpu_lr35902::fetch_byte()
{
	uint8_t b = lr_read(reg_PC);
	if (halt_bug) {
		halt_bug = false;   // one-shot - do NOT advance PC this once
	} else {
		++reg_PC;
	}
	return b;
}

uint16_t cpu_lr35902::fetch_word()
{
	uint16_t lo = fetch_byte();
	uint16_t hi = fetch_byte();
	return lo | (hi << 8);
}

// =============================================================================
// Condition codes
// cc encoding (matches Z80/LR35902): 0=NZ, 1=Z, 2=NC, 3=C
// =============================================================================

uint8_t cpu_lr35902::test_cond(uint8_t cc)
{
	switch (cc & 0x03) {
	case 0: return  (reg8[FLAGS] & Z_FLAG) ? 0 : 1;  // NZ
	case 1: return  (reg8[FLAGS] & Z_FLAG) ? 1 : 0;  // Z
	case 2: return  (reg8[FLAGS] & C_FLAG) ? 0 : 1;  // NC
	case 3: return  (reg8[FLAGS] & C_FLAG) ? 1 : 0;  // C
	}
	return 0;
}

// =============================================================================
// 8-bit ALU helpers
// =============================================================================

void cpu_lr35902::Add_1(uint8_t v)
{
	uint8_t a = reg8[A];
	int     r = (int)a + (int)v;
	uint8_t f = 0;
	if (((a & 0x0F) + (v & 0x0F)) > 0x0F) f |= H_FLAG;
	if (r > 0xFF)                         f |= C_FLAG;
	reg8[A]     = (uint8_t)r;
	if (reg8[A] == 0) f |= Z_FLAG;
	reg8[FLAGS] = f;
}

void cpu_lr35902::Adc_1(uint8_t v)
{
	uint8_t a    = reg8[A];
	uint8_t cin  = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	int     r    = (int)a + (int)v + (int)cin;
	uint8_t f    = 0;
	if (((a & 0x0F) + (v & 0x0F) + cin) > 0x0F) f |= H_FLAG;
	if (r > 0xFF)                                f |= C_FLAG;
	reg8[A]      = (uint8_t)r;
	if (reg8[A] == 0) f |= Z_FLAG;
	reg8[FLAGS]  = f;
}

void cpu_lr35902::Sub_1(uint8_t v)
{
	uint8_t a = reg8[A];
	uint8_t r = a - v;
	uint8_t f = N_FLAG;
	if ((a & 0x0F) < (v & 0x0F)) f |= H_FLAG;
	if (a < v)                   f |= C_FLAG;
	if (r == 0)                  f |= Z_FLAG;
	reg8[A]     = r;
	reg8[FLAGS] = f;
}

void cpu_lr35902::Sbc_1(uint8_t v)
{
	uint8_t a    = reg8[A];
	uint8_t cin  = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	int     r    = (int)a - (int)v - (int)cin;
	uint8_t f    = N_FLAG;
	if (((a & 0x0F) - (v & 0x0F) - cin) < 0) f |= H_FLAG;
	if (r < 0)                                f |= C_FLAG;
	reg8[A]      = (uint8_t)r;
	if (reg8[A] == 0)                         f |= Z_FLAG;
	reg8[FLAGS]  = f;
}

void cpu_lr35902::And_1(uint8_t v)
{
	reg8[A] &= v;
	reg8[FLAGS] = H_FLAG | (reg8[A] == 0 ? Z_FLAG : 0);
}

void cpu_lr35902::Or_1(uint8_t v)
{
	reg8[A] |= v;
	reg8[FLAGS] = (reg8[A] == 0) ? Z_FLAG : 0;
}

void cpu_lr35902::Xor_1(uint8_t v)
{
	reg8[A] ^= v;
	reg8[FLAGS] = (reg8[A] == 0) ? Z_FLAG : 0;
}

void cpu_lr35902::Cp_1(uint8_t v)
{
	// Subtract but discard the result; flags only.
	uint8_t a = reg8[A];
	uint8_t r = a - v;
	uint8_t f = N_FLAG;
	if ((a & 0x0F) < (v & 0x0F)) f |= H_FLAG;
	if (a < v)                   f |= C_FLAG;
	if (r == 0)                  f |= Z_FLAG;
	reg8[FLAGS] = f;
}

uint8_t cpu_lr35902::Inc_1(uint8_t v)
{
	uint8_t r = v + 1;
	uint8_t f = reg8[FLAGS] & C_FLAG;          // C preserved
	if (r == 0)              f |= Z_FLAG;
	if ((v & 0x0F) == 0x0F)  f |= H_FLAG;
	// N = 0
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Dec_1(uint8_t v)
{
	uint8_t r = v - 1;
	uint8_t f = (reg8[FLAGS] & C_FLAG) | N_FLAG;
	if (r == 0)              f |= Z_FLAG;
	if ((v & 0x0F) == 0x00)  f |= H_FLAG;
	reg8[FLAGS] = f;
	return r;
}

// =============================================================================
// 16-bit ALU helpers
// =============================================================================

// ADD HL,rr - Z untouched; N=0; H from bit 11; C from bit 15
uint16_t cpu_lr35902::Add_HL(uint16_t v)
{
	uint16_t hl = lr_reg16_HL;
	uint32_t r  = (uint32_t)hl + (uint32_t)v;
	uint8_t  f  = reg8[FLAGS] & Z_FLAG;        // preserve Z
	if (((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF) f |= H_FLAG;
	if (r > 0xFFFF)                              f |= C_FLAG;
	// N = 0
	reg8[FLAGS] = f;
	return (uint16_t)r;
}

// ADD SP,r8 and LD HL,SP+r8 - Z=0, N=0, H/C from low-byte (unsigned) add
uint16_t cpu_lr35902::Add_SP_r8(int8_t r8)
{
	uint16_t sp  = reg_SP;
	uint16_t u   = (uint8_t)r8;               // unsigned byte for flag calc
	uint8_t  f   = 0;
	if (((sp & 0x0F) + (u & 0x0F)) > 0x0F)   f |= H_FLAG;
	if (((sp & 0xFF) + (u & 0xFF)) > 0xFF)   f |= C_FLAG;
	// Z=0, N=0
	reg8[FLAGS] = f;
	return (uint16_t)((int32_t)sp + (int32_t)r8);
}

// =============================================================================
// DAA - LR35902 variant (differs from 8080/Z80; uses only N and H flags)
// =============================================================================

void cpu_lr35902::Daa()
{
	uint8_t a = reg8[A];
	uint8_t f = reg8[FLAGS];
	if (!(f & N_FLAG)) {
		if ((f & C_FLAG) || a > 0x99) { a += 0x60; f |= C_FLAG; }
		if ((f & H_FLAG) || (a & 0x0F) > 0x09) { a += 0x06; }
	} else {
		if (f & C_FLAG) a -= 0x60;
		if (f & H_FLAG) a -= 0x06;
	}
	reg8[A] = a;
	f &= ~(Z_FLAG | H_FLAG);
	if (a == 0) f |= Z_FLAG;
	reg8[FLAGS] = f;
}

// =============================================================================
// Rotates / shifts (used by RLCA/RRCA/RLA/RRA and the CB-prefix block)
// =============================================================================

uint8_t cpu_lr35902::Rlc(uint8_t v)
{
	uint8_t c = (v >> 7) & 1;
	uint8_t r = (uint8_t)((v << 1) | c);
	uint8_t f = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Rrc(uint8_t v)
{
	uint8_t c = v & 1;
	uint8_t r = (uint8_t)((v >> 1) | (c << 7));
	uint8_t f = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Rl(uint8_t v)
{
	uint8_t cin = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	uint8_t c   = (v >> 7) & 1;
	uint8_t r   = (uint8_t)((v << 1) | cin);
	uint8_t f   = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Rr(uint8_t v)
{
	uint8_t cin = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	uint8_t c   = v & 1;
	uint8_t r   = (uint8_t)((v >> 1) | (cin << 7));
	uint8_t f   = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Sla(uint8_t v)
{
	uint8_t c = (v >> 7) & 1;
	uint8_t r = (uint8_t)(v << 1);
	uint8_t f = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Sra(uint8_t v)
{
	uint8_t c = v & 1;
	uint8_t r = (uint8_t)((v >> 1) | (v & 0x80));   // arithmetic - preserve sign
	uint8_t f = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Srl(uint8_t v)
{
	uint8_t c = v & 1;
	uint8_t r = (uint8_t)(v >> 1);
	uint8_t f = 0;
	if (r == 0) f |= Z_FLAG;
	if (c)      f |= C_FLAG;
	reg8[FLAGS] = f;
	return r;
}

uint8_t cpu_lr35902::Swap(uint8_t v)
{
	uint8_t r = (uint8_t)((v >> 4) | (v << 4));
	reg8[FLAGS] = (r == 0) ? Z_FLAG : 0;
	return r;
}

void cpu_lr35902::Bit(uint8_t v, int n)
{
	uint8_t f = (reg8[FLAGS] & C_FLAG) | H_FLAG;   // C preserved; H=1; N=0
	if (!(v & (1 << n))) f |= Z_FLAG;
	reg8[FLAGS] = f;
}

uint8_t cpu_lr35902::Res(uint8_t v, int n) { return (uint8_t)(v & ~(1 << n)); }
uint8_t cpu_lr35902::Set(uint8_t v, int n) { return (uint8_t)(v |  (1 << n)); }

// RLCA / RRCA / RLA / RRA: same math as Rlc/Rrc/Rl/Rr but Z always 0
void cpu_lr35902::Rlca()
{
	uint8_t v = reg8[A];
	uint8_t c = (v >> 7) & 1;
	reg8[A]     = (uint8_t)((v << 1) | c);
	reg8[FLAGS] = c ? C_FLAG : 0;
}

void cpu_lr35902::Rrca()
{
	uint8_t v = reg8[A];
	uint8_t c = v & 1;
	reg8[A]     = (uint8_t)((v >> 1) | (c << 7));
	reg8[FLAGS] = c ? C_FLAG : 0;
}

void cpu_lr35902::Rla()
{
	uint8_t v   = reg8[A];
	uint8_t cin = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	uint8_t c   = (v >> 7) & 1;
	reg8[A]     = (uint8_t)((v << 1) | cin);
	reg8[FLAGS] = c ? C_FLAG : 0;
}

void cpu_lr35902::Rra()
{
	uint8_t v   = reg8[A];
	uint8_t cin = (reg8[FLAGS] & C_FLAG) ? 1 : 0;
	uint8_t c   = v & 1;
	reg8[A]     = (uint8_t)((v >> 1) | (cin << 7));
	reg8[FLAGS] = c ? C_FLAG : 0;
}

// =============================================================================
// CB-prefix dispatcher
//
//   op layout: gg sss rrr
//     gg  (op >> 6)        00 = rotate/shift, 01 = BIT, 10 = RES, 11 = SET
//     sss ((op >> 3) & 7)  00..07 within the rotate group, or the bit index
//     rrr  (op & 7)        target register (B,C,D,E,H,L,(HL),A)
//
// Cycle costs:
//   - reg-direct ops:                    8
//   - BIT n,(HL):                       12
//   - RLC/RRC/RL/RR/SLA/SRA/SWAP/SRL/RES/SET (HL):  16
// =============================================================================

int cpu_lr35902::HandleCB()
{
	uint8_t op    = fetch_byte();
	uint8_t reg   = op & 0x07;
	uint8_t subop = (op >> 3) & 0x07;
	uint8_t group =  op >> 6;
	bool    is_hl = (reg == M);

	uint8_t v = read_reg8(reg);
	uint8_t r = 0;
	int     cycles = is_hl ? 16 : 8;

	switch (group) {
	case 0: // rotate/shift
		switch (subop) {
		case 0: r = Rlc (v); break;
		case 1: r = Rrc (v); break;
		case 2: r = Rl  (v); break;
		case 3: r = Rr  (v); break;
		case 4: r = Sla (v); break;
		case 5: r = Sra (v); break;
		case 6: r = Swap(v); break;
		case 7: r = Srl (v); break;
		}
		write_reg8(reg, r);
		break;
	case 1: // BIT n,r
		Bit(v, subop);
		cycles = is_hl ? 12 : 8;          // BIT n,(HL) is 12, not 16
		break;
	case 2: // RES n,r
		write_reg8(reg, Res(v, subop));
		break;
	case 3: // SET n,r
		write_reg8(reg, Set(v, subop));
		break;
	}
	return cycles;
}

// =============================================================================
// Interrupt dispatch
//
// Returns T-states consumed by the interrupt service (0 if none).
// =============================================================================

int cpu_lr35902::service_interrupt()
{
	uint8_t pending = (uint8_t)(IF & IE & 0x1F);
	if (pending == 0) return 0;

	// Any pending interrupt wakes HALT regardless of IME.
	if (halted) halted = false;

	if (!IME) return 0;

	// Lowest-set bit has highest priority (V-Blank > LCD > Timer > Serial > Joypad)
	int bit = 0;
	while (!(pending & (1 << bit))) ++bit;

	IF         &= (uint8_t)~(1 << bit);
	IME         = false;
	IME_pending = false;
	push16(reg_PC);
	reg_PC      = (uint16_t)(0x40 + bit * 8);

	return 20;  // 5 M-cycles = 20 T-states
}

// =============================================================================
// Main execution loop
// =============================================================================

int cpu_lr35902::exec(int cycles)
{
	while (cycles > 0)
	{
		int last_cycles = cycles;

		// 1. Service any pending interrupt (may push/jump, wakes HALT)
		cycles -= service_interrupt();

		// 2. If still halted or stopped, burn a 4-T-state slice
		if (halted || stopped) {
			cycles -= 4;
			clocktickstotal += abs(cycles - last_cycles);
			if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
			continue;
		}

		// 3. Fetch + dispatch
		uint8_t op = fetch_byte();

		switch (op)
		{
		// ===== 0x00..0x0F =====
		case 0x00: // NOP
			cycles -= 4; break;
		case 0x01: // LD BC,nn
			reg8[C] = fetch_byte(); reg8[B] = fetch_byte();
			cycles -= 12; break;
		case 0x02: // LD (BC),A
			lr_write(lr_reg16_BC, reg8[A]);
			cycles -= 8; break;
		case 0x03: // INC BC
			write16_RP(0x00, lr_reg16_BC + 1);
			cycles -= 8; break;
		case 0x04: // INC B
			reg8[B] = Inc_1(reg8[B]); cycles -= 4; break;
		case 0x05: // DEC B
			reg8[B] = Dec_1(reg8[B]); cycles -= 4; break;
		case 0x06: // LD B,n
			reg8[B] = fetch_byte(); cycles -= 8; break;
		case 0x07: // RLCA
			Rlca(); cycles -= 4; break;
		case 0x08: // LD (a16),SP
		{
			uint16_t addr = fetch_word();
			lr_write_word(addr, reg_SP);
			cycles -= 20;
			break;
		}
		case 0x09: // ADD HL,BC
			write16_RP(0x02, Add_HL(lr_reg16_BC));
			cycles -= 8; break;
		case 0x0A: // LD A,(BC)
			reg8[A] = lr_read(lr_reg16_BC); cycles -= 8; break;
		case 0x0B: // DEC BC
			write16_RP(0x00, lr_reg16_BC - 1); cycles -= 8; break;
		case 0x0C: // INC C
			reg8[C] = Inc_1(reg8[C]); cycles -= 4; break;
		case 0x0D: // DEC C
			reg8[C] = Dec_1(reg8[C]); cycles -= 4; break;
		case 0x0E: // LD C,n
			reg8[C] = fetch_byte(); cycles -= 8; break;
		case 0x0F: // RRCA
			Rrca(); cycles -= 4; break;

		// ===== 0x10..0x1F =====
		case 0x10: // STOP (2-byte: 0x10 0x00)
			(void)fetch_byte();   // consume the trailing 0x00
			stopped = true;
			cycles -= 4; break;
		case 0x11: // LD DE,nn
			reg8[E] = fetch_byte(); reg8[D] = fetch_byte();
			cycles -= 12; break;
		case 0x12: // LD (DE),A
			lr_write(lr_reg16_DE, reg8[A]); cycles -= 8; break;
		case 0x13: // INC DE
			write16_RP(0x01, lr_reg16_DE + 1); cycles -= 8; break;
		case 0x14: // INC D
			reg8[D] = Inc_1(reg8[D]); cycles -= 4; break;
		case 0x15: // DEC D
			reg8[D] = Dec_1(reg8[D]); cycles -= 4; break;
		case 0x16: // LD D,n
			reg8[D] = fetch_byte(); cycles -= 8; break;
		case 0x17: // RLA
			Rla(); cycles -= 4; break;
		case 0x18: // JR e
		{
			int8_t e = (int8_t)fetch_byte();
			reg_PC = (uint16_t)(reg_PC + e);
			cycles -= 12; break;
		}
		case 0x19: // ADD HL,DE
			write16_RP(0x02, Add_HL(lr_reg16_DE)); cycles -= 8; break;
		case 0x1A: // LD A,(DE)
			reg8[A] = lr_read(lr_reg16_DE); cycles -= 8; break;
		case 0x1B: // DEC DE
			write16_RP(0x01, lr_reg16_DE - 1); cycles -= 8; break;
		case 0x1C: // INC E
			reg8[E] = Inc_1(reg8[E]); cycles -= 4; break;
		case 0x1D: // DEC E
			reg8[E] = Dec_1(reg8[E]); cycles -= 4; break;
		case 0x1E: // LD E,n
			reg8[E] = fetch_byte(); cycles -= 8; break;
		case 0x1F: // RRA
			Rra(); cycles -= 4; break;

		// ===== 0x20..0x2F =====
		case 0x20: case 0x28: case 0x30: case 0x38: // JR cc,e
		{
			int8_t e = (int8_t)fetch_byte();
			if (test_cond((op >> 3) & 0x03)) {
				reg_PC = (uint16_t)(reg_PC + e);
				cycles -= 12;
			} else {
				cycles -= 8;
			}
			break;
		}
		case 0x21: // LD HL,nn
			reg8[L] = fetch_byte(); reg8[H] = fetch_byte();
			cycles -= 12; break;
		case 0x22: // LD (HL+),A
		{
			uint16_t hl = lr_reg16_HL;
			lr_write(hl, reg8[A]);
			++hl;
			reg8[H] = hl >> 8; reg8[L] = hl & 0xFF;
			cycles -= 8; break;
		}
		case 0x23: // INC HL
			write16_RP(0x02, lr_reg16_HL + 1); cycles -= 8; break;
		case 0x24: // INC H
			reg8[H] = Inc_1(reg8[H]); cycles -= 4; break;
		case 0x25: // DEC H
			reg8[H] = Dec_1(reg8[H]); cycles -= 4; break;
		case 0x26: // LD H,n
			reg8[H] = fetch_byte(); cycles -= 8; break;
		case 0x27: // DAA
			Daa(); cycles -= 4; break;
		case 0x29: // ADD HL,HL
			write16_RP(0x02, Add_HL(lr_reg16_HL)); cycles -= 8; break;
		case 0x2A: // LD A,(HL+)
		{
			uint16_t hl = lr_reg16_HL;
			reg8[A] = lr_read(hl);
			++hl;
			reg8[H] = hl >> 8; reg8[L] = hl & 0xFF;
			cycles -= 8; break;
		}
		case 0x2B: // DEC HL
			write16_RP(0x02, lr_reg16_HL - 1); cycles -= 8; break;
		case 0x2C: // INC L
			reg8[L] = Inc_1(reg8[L]); cycles -= 4; break;
		case 0x2D: // DEC L
			reg8[L] = Dec_1(reg8[L]); cycles -= 4; break;
		case 0x2E: // LD L,n
			reg8[L] = fetch_byte(); cycles -= 8; break;
		case 0x2F: // CPL
			reg8[A] = (uint8_t)~reg8[A];
			reg8[FLAGS] = (reg8[FLAGS] & (Z_FLAG | C_FLAG)) | N_FLAG | H_FLAG;
			cycles -= 4; break;

		// ===== 0x30..0x3F =====
		case 0x31: // LD SP,nn
			reg_SP = fetch_word(); cycles -= 12; break;
		case 0x32: // LD (HL-),A
		{
			uint16_t hl = lr_reg16_HL;
			lr_write(hl, reg8[A]);
			--hl;
			reg8[H] = hl >> 8; reg8[L] = hl & 0xFF;
			cycles -= 8; break;
		}
		case 0x33: // INC SP
			++reg_SP; cycles -= 8; break;
		case 0x34: // INC (HL)
			lr_write(lr_reg16_HL, Inc_1(lr_read(lr_reg16_HL)));
			cycles -= 12; break;
		case 0x35: // DEC (HL)
			lr_write(lr_reg16_HL, Dec_1(lr_read(lr_reg16_HL)));
			cycles -= 12; break;
		case 0x36: // LD (HL),n
			lr_write(lr_reg16_HL, fetch_byte()); cycles -= 12; break;
		case 0x37: // SCF
			reg8[FLAGS] = (reg8[FLAGS] & Z_FLAG) | C_FLAG;
			cycles -= 4; break;
		case 0x39: // ADD HL,SP
			write16_RP(0x02, Add_HL(reg_SP)); cycles -= 8; break;
		case 0x3A: // LD A,(HL-)
		{
			uint16_t hl = lr_reg16_HL;
			reg8[A] = lr_read(hl);
			--hl;
			reg8[H] = hl >> 8; reg8[L] = hl & 0xFF;
			cycles -= 8; break;
		}
		case 0x3B: // DEC SP
			--reg_SP; cycles -= 8; break;
		case 0x3C: // INC A
			reg8[A] = Inc_1(reg8[A]); cycles -= 4; break;
		case 0x3D: // DEC A
			reg8[A] = Dec_1(reg8[A]); cycles -= 4; break;
		case 0x3E: // LD A,n
			reg8[A] = fetch_byte(); cycles -= 8; break;
		case 0x3F: // CCF
			reg8[FLAGS] = (reg8[FLAGS] & (Z_FLAG | C_FLAG)) ^ C_FLAG;
			cycles -= 4; break;

		// ===== 0x40..0x7F: LD r,r' (HALT at 0x76) =====
		case 0x40: case 0x41: case 0x42: case 0x43:
		case 0x44: case 0x45: case 0x46: case 0x47:
		case 0x48: case 0x49: case 0x4A: case 0x4B:
		case 0x4C: case 0x4D: case 0x4E: case 0x4F:
		case 0x50: case 0x51: case 0x52: case 0x53:
		case 0x54: case 0x55: case 0x56: case 0x57:
		case 0x58: case 0x59: case 0x5A: case 0x5B:
		case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		case 0x60: case 0x61: case 0x62: case 0x63:
		case 0x64: case 0x65: case 0x66: case 0x67:
		case 0x68: case 0x69: case 0x6A: case 0x6B:
		case 0x6C: case 0x6D: case 0x6E: case 0x6F:
		case 0x70: case 0x71: case 0x72: case 0x73:
		case 0x74: case 0x75:               case 0x77:
		case 0x78: case 0x79: case 0x7A: case 0x7B:
		case 0x7C: case 0x7D: case 0x7E: case 0x7F:
		{
			uint8_t src = op & 0x07;
			uint8_t dst = (op >> 3) & 0x07;
			write_reg8(dst, read_reg8(src));
			cycles -= (src == M || dst == M) ? 8 : 4;
			break;
		}
		case 0x76: // HALT
			if (!IME && ((IF & IE & 0x1F) != 0)) {
				// HALT bug: do not halt; next fetch reads the same byte twice.
				halt_bug = true;
			} else {
				halted = true;
			}
			cycles -= 4; break;

		// ===== 0x80..0xBF: ALU A,r =====
		case 0x80: case 0x81: case 0x82: case 0x83:
		case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8A: case 0x8B:
		case 0x8C: case 0x8D: case 0x8E: case 0x8F:
		case 0x90: case 0x91: case 0x92: case 0x93:
		case 0x94: case 0x95: case 0x96: case 0x97:
		case 0x98: case 0x99: case 0x9A: case 0x9B:
		case 0x9C: case 0x9D: case 0x9E: case 0x9F:
		case 0xA0: case 0xA1: case 0xA2: case 0xA3:
		case 0xA4: case 0xA5: case 0xA6: case 0xA7:
		case 0xA8: case 0xA9: case 0xAA: case 0xAB:
		case 0xAC: case 0xAD: case 0xAE: case 0xAF:
		case 0xB0: case 0xB1: case 0xB2: case 0xB3:
		case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		case 0xB8: case 0xB9: case 0xBA: case 0xBB:
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		{
			uint8_t src = op & 0x07;
			uint8_t v   = read_reg8(src);
			switch ((op >> 3) & 0x07) {
			case 0: Add_1(v); break;
			case 1: Adc_1(v); break;
			case 2: Sub_1(v); break;
			case 3: Sbc_1(v); break;
			case 4: And_1(v); break;
			case 5: Xor_1(v); break;
			case 6: Or_1 (v); break;
			case 7: Cp_1 (v); break;
			}
			cycles -= (src == M) ? 8 : 4;
			break;
		}

		// ===== 0xC0..0xCF =====
		case 0xC0: case 0xC8: case 0xD0: case 0xD8: // RET cc
			if (test_cond((op >> 3) & 0x03)) {
				reg_PC = pop16();
				cycles -= 20;
			} else {
				cycles -= 8;
			}
			break;
		case 0xC1: // POP BC
			write16_RP_PUSHPOP(0x00, pop16()); cycles -= 12; break;
		case 0xC2: case 0xCA: case 0xD2: case 0xDA: // JP cc,nn
		{
			uint16_t addr = fetch_word();
			if (test_cond((op >> 3) & 0x03)) {
				reg_PC = addr;
				cycles -= 16;
			} else {
				cycles -= 12;
			}
			break;
		}
		case 0xC3: // JP nn
			reg_PC = fetch_word(); cycles -= 16; break;
		case 0xC4: case 0xCC: case 0xD4: case 0xDC: // CALL cc,nn
		{
			uint16_t addr = fetch_word();
			if (test_cond((op >> 3) & 0x03)) {
				push16(reg_PC);
				reg_PC = addr;
				cycles -= 24;
			} else {
				cycles -= 12;
			}
			break;
		}
		case 0xC5: // PUSH BC
			push16(lr_reg16_BC); cycles -= 16; break;
		case 0xC6: // ADD A,n
			Add_1(fetch_byte()); cycles -= 8; break;
		case 0xC7: case 0xCF: case 0xD7: case 0xDF:
		case 0xE7: case 0xEF: case 0xF7: case 0xFF: // RST n
			push16(reg_PC);
			reg_PC = (uint16_t)(op & 0x38);
			cycles -= 16; break;
		case 0xC9: // RET
			reg_PC = pop16(); cycles -= 16; break;
		case 0xCB: // CB prefix
			cycles -= HandleCB(); break;
		case 0xCD: // CALL nn
		{
			uint16_t addr = fetch_word();
			push16(reg_PC);
			reg_PC = addr;
			cycles -= 24; break;
		}
		case 0xCE: // ADC A,n
			Adc_1(fetch_byte()); cycles -= 8; break;

		// ===== 0xD0..0xDF =====
		case 0xD1: // POP DE
			write16_RP_PUSHPOP(0x01, pop16()); cycles -= 12; break;
		case 0xD5: // PUSH DE
			push16(lr_reg16_DE); cycles -= 16; break;
		case 0xD6: // SUB n
			Sub_1(fetch_byte()); cycles -= 8; break;
		case 0xD9: // RETI
			reg_PC      = pop16();
			IME         = true;
			IME_pending = false;
			cycles -= 16; break;
		case 0xDE: // SBC A,n
			Sbc_1(fetch_byte()); cycles -= 8; break;

		// ===== 0xE0..0xEF =====
		case 0xE0: // LDH (a8),A   ->  MEM[0xFF00 + a8] = A
			lr_write((uint16_t)(0xFF00 + fetch_byte()), reg8[A]);
			cycles -= 12; break;
		case 0xE1: // POP HL
			write16_RP_PUSHPOP(0x02, pop16()); cycles -= 12; break;
		case 0xE2: // LD (C),A   ->  MEM[0xFF00 + C] = A
			lr_write((uint16_t)(0xFF00 + reg8[C]), reg8[A]);
			cycles -= 8; break;
		case 0xE5: // PUSH HL
			push16(lr_reg16_HL); cycles -= 16; break;
		case 0xE6: // AND n
			And_1(fetch_byte()); cycles -= 8; break;
		case 0xE8: // ADD SP,r8
		{
			int8_t r8 = (int8_t)fetch_byte();
			reg_SP = Add_SP_r8(r8);
			cycles -= 16; break;
		}
		case 0xE9: // JP (HL)  (more accurately: JP HL — no indirection)
			reg_PC = lr_reg16_HL; cycles -= 4; break;
		case 0xEA: // LD (a16),A
		{
			uint16_t addr = fetch_word();
			lr_write(addr, reg8[A]);
			cycles -= 16; break;
		}
		case 0xEE: // XOR n
			Xor_1(fetch_byte()); cycles -= 8; break;

		// ===== 0xF0..0xFF =====
		case 0xF0: // LDH A,(a8)
			reg8[A] = lr_read((uint16_t)(0xFF00 + fetch_byte()));
			cycles -= 12; break;
		case 0xF1: // POP AF
			write16_RP_PUSHPOP(0x03, pop16()); cycles -= 12; break;
		case 0xF2: // LD A,(C)
			reg8[A] = lr_read((uint16_t)(0xFF00 + reg8[C]));
			cycles -= 8; break;
		case 0xF3: // DI
			IME         = false;
			IME_pending = false;
			cycles -= 4; break;
		case 0xF5: // PUSH AF
			push16(lr_reg16_AF); cycles -= 16; break;
		case 0xF6: // OR n
			Or_1(fetch_byte()); cycles -= 8; break;
		case 0xF8: // LD HL,SP+r8
		{
			int8_t r8 = (int8_t)fetch_byte();
			uint16_t v = Add_SP_r8(r8);
			reg8[H] = v >> 8; reg8[L] = v & 0xFF;
			cycles -= 12; break;
		}
		case 0xF9: // LD SP,HL
			reg_SP = lr_reg16_HL; cycles -= 8; break;
		case 0xFA: // LD A,(a16)
		{
			uint16_t addr = fetch_word();
			reg8[A] = lr_read(addr);
			cycles -= 16; break;
		}
		case 0xFB: // EI - IME is enabled AFTER the next instruction completes
			// IME_pending is committed in the post-dispatch step below.
			cycles -= 4; break;
		case 0xFE: // CP n
			Cp_1(fetch_byte()); cycles -= 8; break;

		// ===== Illegal opcodes - real hardware freezes; we log + NOP =====
		case 0xD3: case 0xDB: case 0xDD:
		case 0xE3: case 0xE4: case 0xEB: case 0xEC: case 0xED:
		case 0xF4: case 0xFC: case 0xFD:
			if (log_debug_rw)
				LOG_INFO("LR35902 illegal opcode %02X at %04X (treated as NOP)",
				         op, (uint16_t)(reg_PC - 1));
			cycles -= 4; break;
		}

		// 4. EI delay: commit IME_pending one instruction AFTER the EI.
		if (op == 0xFB) {
			IME_pending = true;          // arm; will commit on next non-EI op
		} else if (IME_pending) {
			IME         = true;
			IME_pending = false;
		}

		// 5. Cycle accounting
		clocktickstotal += abs(cycles - last_cycles);
		if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
	}
	return cycles;
}
