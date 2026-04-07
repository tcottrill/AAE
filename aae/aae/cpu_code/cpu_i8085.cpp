
// -----------------------------------------------------------------------------
// Intel 8085A Emulator for AAE (Another Arcade Emulator)
//
// Based on the AAE cpu_i8080 emulator written by Mike Chambers, extended to full 8085A.
// Reference: MAME I8085 by Juergen Buchmueller, Intel 8085A datasheet.
//
// The 8085 is backward-compatible with the 8080 instruction set but adds:
//   - RIM (0x20) and SIM (0x30) instructions
//   - Vectored interrupt system: TRAP, RST 5.5, RST 6.5, RST 7.5, INTR
//   - SID/SOD serial I/O pins
//   - Different cycle timings (T-states)
//   - V (overflow) flag behavior on bit 2 for arithmetic ops
//     (parity for logical ops - same bit position, context-dependent)
//
// Integration:
//   Drop-in replacement for cpu_i8080 in AAE's cpu_control framework.
//   Same constructor signature, same exec/interrupt/reset/get_ticks interface.
// -----------------------------------------------------------------------------

#include "cpu_i8085.h"
#include "sys_log.h"
#include <cmath>

#pragma warning( disable : 4244 )


// ============================================================================
// Construction / Reset / Tick Counting
// ============================================================================

cpu_i8085::cpu_i8085(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem,
                     z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	z80IoRead = port_read;
	z80IoWrite = port_write;
	reset();
}

void cpu_i8085::reset()
{
	reg_PC = 0x0000;
	reg_SP = 0x0000;
	for (int i = 0; i < 9; i++) reg8[i] = 0;

	IM = 0;
	IREQ = 0;
	ISRV = 0;
	IRQ1 = 0;
	IRQ2 = 0;
	HALT = 0;
	clocktickstotal = 0;
}

int cpu_i8085::get_ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset) {
		clocktickstotal = 0;
	}
	return tmp;
}


// ============================================================================
// Memory / IO
// ============================================================================
uint8_t cpu_i8085::i8085_read(uint16_t addr)
{
	MemoryReadByte* MemRead = memory_read;
	while (MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
		{
			if (MemRead->memoryCall)
				return MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
			if (MemRead->pUserArea)
				return *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
		}
		++MemRead;
	}
	return MEM[addr];
}

void cpu_i8085::i8085_write(uint16_t addr, uint8_t byte)
{
	MemoryWriteByte* MemWrite = memory_write;
	while (MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
		{
			if (MemWrite->memoryCall) {
				MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
				return;
			}
			if (MemWrite->pUserArea) {
				*((uint8_t*)MemWrite->pUserArea + (addr - MemWrite->lowAddr)) = byte;
				return;
			}
		}
		++MemWrite;
	}
	MEM[addr] = byte;
}

uint8_t cpu_i8085::In(uint8_t bPort)
{
	uint8_t bVal = 0xFF;
	struct z80PortRead* mr = z80IoRead;
	while (mr->lowIoAddr != 0xffff)
	{
		if (bPort >= mr->lowIoAddr && bPort <= mr->highIoAddr) {
			bVal = mr->IOCall(bPort, mr);
			break;
		}
		mr++;
	}
	// 8085 Difference: IN affects Sign, Zero, and Parity flags. 
	// AC is cleared, CY is unaffected.
	reg8[FLAGS] = (reg8[FLAGS] & CF) | i8085_ZSP[bVal];
	return bVal;
}

void cpu_i8085::Out(uint8_t bPort, uint8_t bVal)
{
	struct z80PortWrite* mr = z80IoWrite;
	while (mr->lowIoAddr != 0xffff)
	{
		if (bPort >= mr->lowIoAddr && bPort <= mr->highIoAddr) {
			mr->IOCall(bPort, bVal, mr);
			return;
		}
		mr++;
	}
}

// ============================================================================
// Register Pair Helpers
// ============================================================================

uint16_t cpu_i8085::read_RP(uint8_t rp)
{
	switch (rp) {
	case 0x00: return i85_reg16_BC;
	case 0x01: return i85_reg16_DE;
	case 0x02: return i85_reg16_HL;
	case 0x03: return reg_SP;
	}
	return 0;
}

uint16_t cpu_i8085::read_RP_PUSHPOP(uint8_t rp)
{
	switch (rp) {
	case 0x00: return i85_reg16_BC;
	case 0x01: return i85_reg16_DE;
	case 0x02: return i85_reg16_HL;
	case 0x03: return i85_reg16_PSW;
	}
	return 0;
}

void cpu_i8085::write_RP(uint8_t rp, uint8_t lb, uint8_t hb)
{
	switch (rp) {
	case 0x00: reg8[C] = lb; reg8[B] = hb; break;
	case 0x01: reg8[E] = lb; reg8[D] = hb; break;
	case 0x02: reg8[L] = lb; reg8[H] = hb; break;
	case 0x03: reg_SP = (uint16_t)lb | ((uint16_t)hb << 8); break;
	}
}

void cpu_i8085::write16_RP(uint8_t rp, uint16_t value)
{
	switch (rp) {
	case 0x00: reg8[C] = value & 0xFF; reg8[B] = value >> 8; break;
	case 0x01: reg8[E] = value & 0xFF; reg8[D] = value >> 8; break;
	case 0x02: reg8[L] = value & 0xFF; reg8[H] = value >> 8; break;
	case 0x03: reg_SP = value; break;
	}
}

void cpu_i8085::write16_RP_PUSHPOP(uint8_t rp, uint16_t value)
{
	switch (rp) {
	case 0x00: reg8[C] = value & 0xFF; reg8[B] = value >> 8; break;
	case 0x01: reg8[E] = value & 0xFF; reg8[D] = value >> 8; break;
	case 0x02: reg8[L] = value & 0xFF; reg8[H] = value >> 8; break;
	case 0x03:
		reg8[FLAGS] = value & 0xFF;
		reg8[A] = value >> 8;
		break;
	}
}


// ============================================================================
// Register Read/Write (M register goes through memory at HL)
// ============================================================================

void cpu_i8085::write_reg8(uint8_t reg, uint8_t value)
{
	if (reg == M) {
		i8085_write(i85_reg16_HL, value);
	} else {
		reg8[reg] = value;
	}
}

uint8_t cpu_i8085::read_reg8(uint8_t reg)
{
	if (reg == M) {
		return i8085_read(i85_reg16_HL);
	} else {
		return reg8[reg];
	}
}


// ============================================================================
// Condition Code Testing
// ============================================================================

uint8_t cpu_i8085::test_cond(uint8_t code)
{
	switch (code) {
	case 0: return !(reg8[FLAGS] & ZF); // NZ
	case 1: return  (reg8[FLAGS] & ZF); // Z
	case 2: return !(reg8[FLAGS] & CF); // NC
	case 3: return  (reg8[FLAGS] & CF); // C
	case 4: return !(reg8[FLAGS] & VF); // PO (parity odd / no overflow)
	case 5: return  (reg8[FLAGS] & VF); // PE (parity even / overflow)
	case 6: return !(reg8[FLAGS] & SF); // P  (positive)
	case 7: return  (reg8[FLAGS] & SF); // M  (minus)
	}
	return 0;
}


// ============================================================================
// Stack Operations
// ============================================================================

void cpu_i8085::i8085_push(uint16_t value)
{
	i8085_write(--reg_SP, value >> 8);
	i8085_write(--reg_SP, (uint8_t)value);
}

uint16_t cpu_i8085::i8085_pop()
{
	uint16_t temp;
	temp = i8085_read(reg_SP++);
	temp |= (uint16_t)i8085_read(reg_SP++) << 8;
	return temp;
}


// ============================================================================
// 8085 Interrupt System
// ============================================================================

void cpu_i8085::set_TRAP(int state)
{
	if (state)
	{
		IREQ |= IM_TRAP;
		if (ISRV & IM_TRAP) return; // already servicing TRAP
		ISRV = IM_TRAP;
		IRQ2 = ADDR_TRAP;
	}
	else
	{
		IREQ &= ~IM_TRAP;
	}
}

void cpu_i8085::set_RST75(int state)
{
	if (state)
	{
		IREQ |= IM_RST75;
		if (IM & IM_RST75) return;    // masked
		if (!ISRV)                     // no higher priority active
		{
			ISRV = IM_RST75;
			IRQ2 = ADDR_RST75;
		}
	}
	// RST 7.5 is edge-triggered: only cleared by SIM or EI, not by signal going low
}

void cpu_i8085::set_RST65(int state)
{
	if (state)
	{
		IREQ |= IM_RST65;
		if (IM & IM_RST65) return;
		if (!ISRV)
		{
			ISRV = IM_RST65;
			IRQ2 = ADDR_RST65;
		}
	}
	else
	{
		IREQ &= ~IM_RST65;
	}
}

void cpu_i8085::set_RST55(int state)
{
	if (state)
	{
		IREQ |= IM_RST55;
		if (IM & IM_RST55) return;
		if (!ISRV)
		{
			ISRV = IM_RST55;
			IRQ2 = ADDR_RST55;
		}
	}
	else
	{
		IREQ &= ~IM_RST55;
	}
}

void cpu_i8085::set_INTR(int type)
{
	if (type)
	{
		IREQ |= IM_INTR;
		if (IM & IM_INTR) return;
		if (!ISRV)
		{
			ISRV = IM_INTR;
			IRQ2 = type;
		}
	}
	else
	{
		IREQ &= ~IM_INTR;
	}
}

// External interface: trigger an interrupt by type
void cpu_i8085::cause_interrupt(int type)
{
	switch (type)
	{
	case I8085_SID:   set_SID(1);    break;
	case I8085_TRAP:  set_TRAP(1);   break;
	case I8085_RST75: set_RST75(1);  break;
	case I8085_RST65: set_RST65(1);  break;
	case I8085_RST55: set_RST55(1);  break;
	default:          set_INTR(type); break;
	}
}

void cpu_i8085::clear_pending_interrupts()
{
	if (IREQ & IM_TRAP)  set_TRAP(I8085_NONE);
	if (IREQ & IM_RST75) set_RST75(I8085_NONE);
	if (IREQ & IM_RST65) set_RST65(I8085_NONE);
	if (IREQ & IM_RST55) set_RST55(I8085_NONE);
	if (IREQ & IM_INTR)  set_INTR(I8085_NONE);
}

void cpu_i8085::set_SID(int state)
{
	if (state)
		IM |= IM_SID;
	else
		IM &= ~IM_SID;
}

// 8080-compatible interrupt: push PC, vector to n, disable interrupts.
// This is the interface used by cpu_control's cpu_do_int_imm for CPU_8085.
void cpu_i8085::interrupt(uint8_t n)
{
	if (!(IM & IM_IEN)) return;  // interrupts disabled
	if (HALT) {
		reg_PC++;
		HALT = 0;
	}
	IM &= ~IM_IEN;
	i8085_push(reg_PC);
	reg_PC = (uint16_t)n;
}

// Internal interrupt dispatch - called from exec loop when IRQ1 is set
void cpu_i8085::do_interrupt()
{
	if (HALT)
	{
		reg_PC++;   // skip past the HLT instruction
		HALT = 0;
	}
	IM &= ~IM_IEN;  // disable interrupts

	// Dispatch based on interrupt type
	// TRAP and RST vectors: push PC and jump to fixed address
	// INTR can carry a CALL nnnn or JMP nnnn in the high bytes
	switch (IRQ1 & 0xFF0000)
	{
	case 0xCD0000:  // CALL nnnn from bus
		clocktickstotal += 7;
		i8085_push(reg_PC);
		// fall through to JMP
	case 0xC30000:  // JMP nnnn from bus
		clocktickstotal += 10;
		reg_PC = IRQ1 & 0xFFFF;
		break;
	default:
		// Standard RST vector or TRAP address
		switch (IRQ1)
		{
		case ADDR_TRAP:
		case ADDR_RST75:
		case ADDR_RST65:
		case ADDR_RST55:
			i8085_push(reg_PC);
			reg_PC = IRQ1;
			break;
		default:
			// Single-byte RST instruction on the bus - execute it inline
			// This handles the case where IRQ1 is an opcode like RST n
			{
				uint8_t rst_op = IRQ1 & 0xFF;
				i8085_push(reg_PC);
				reg_PC = (uint16_t)((rst_op >> 3) & 7) << 3;
			}
			break;
		}
		break;
	}
}


// ============================================================================
// Main Execution Loop
// ============================================================================

int cpu_i8085::exec(int cycles)
{
	uint8_t opcode, temp8, reg, reg2;
	uint16_t temp16;
	uint32_t temp32;

	while (cycles > 0)
	{
		int last_cycles = cycles;

		// ----- Interrupt check -----
		// Check if interrupts are enabled OR if TRAP is pending (TRAP is non-maskable)
		if ((IM & IM_IEN) || (IREQ & IM_TRAP))
		{
			IRQ1 = IRQ2;
			IRQ2 = 0;
			if (IRQ1) {
				do_interrupt();
			}
		}

		// ----- Fetch and execute -----
		opcode = i8085_read(reg_PC++);

		switch (opcode)
		{

		// ================================================================
		// NOP
		// ================================================================
		case 0x00: // NOP
			cycles -= 4;
			break;

		// ================================================================
		// LXI - Load register pair immediate
		// ================================================================
		case 0x01: // LXI B,nnnn
		case 0x11: // LXI D,nnnn
		case 0x21: // LXI H,nnnn
		case 0x31: // LXI SP,nnnn
			reg = (opcode >> 4) & 3;
			write_RP(reg, i8085_read(reg_PC), i8085_read(reg_PC + 1));
			reg_PC += 2;
			cycles -= 10;
			break;

		// ================================================================
		// STAX / LDAX
		// ================================================================
		case 0x02: // STAX B
			i8085_write(i85_reg16_BC, reg8[A]);
			cycles -= 7;
			break;
		case 0x12: // STAX D
			i8085_write(i85_reg16_DE, reg8[A]);
			cycles -= 7;
			break;
		case 0x0A: // LDAX B
			reg8[A] = i8085_read(i85_reg16_BC);
			cycles -= 7;
			break;
		case 0x1A: // LDAX D
			reg8[A] = i8085_read(i85_reg16_DE);
			cycles -= 7;
			break;

		// ================================================================
		// STA / LDA
		// ================================================================
		case 0x32: // STA nnnn
			temp16 = (uint16_t)i8085_read(reg_PC) | ((uint16_t)i8085_read(reg_PC + 1) << 8);
			i8085_write(temp16, reg8[A]);
			reg_PC += 2;
			cycles -= 13;
			break;
		case 0x3A: // LDA nnnn
			temp16 = (uint16_t)i8085_read(reg_PC) | ((uint16_t)i8085_read(reg_PC + 1) << 8);
			reg8[A] = i8085_read(temp16);
			reg_PC += 2;
			cycles -= 13;
			break;

		// ================================================================
		// SHLD / LHLD
		// ================================================================
		case 0x22: // SHLD nnnn
			temp16 = (uint16_t)i8085_read(reg_PC) | ((uint16_t)i8085_read(reg_PC + 1) << 8);
			i8085_write(temp16, reg8[L]);
			i8085_write(temp16 + 1, reg8[H]);
			reg_PC += 2;
			cycles -= 16;
			break;
		case 0x2A: // LHLD nnnn
			temp16 = (uint16_t)i8085_read(reg_PC) | ((uint16_t)i8085_read(reg_PC + 1) << 8);
			reg8[L] = i8085_read(temp16);
			reg8[H] = i8085_read(temp16 + 1);
			reg_PC += 2;
			cycles -= 16;
			break;

		// ================================================================
		// INX / DCX - Increment/Decrement register pair
		// ================================================================
		case 0x03: // INX B
		case 0x13: // INX D
		case 0x23: // INX H
		case 0x33: // INX SP
			reg = (opcode >> 4) & 3;
			write16_RP(reg, read_RP(reg) + 1);
			cycles -= 6;
			break;
		case 0x0B: // DCX B
		case 0x1B: // DCX D
		case 0x2B: // DCX H
		case 0x3B: // DCX SP
			reg = (opcode >> 4) & 3;
			write16_RP(reg, read_RP(reg) - 1);
			cycles -= 6;
			break;

		// ================================================================
		// INR - Increment register
		// 8085 sets V flag: overflow if result is 0x80 (was 0x7F)
		// ================================================================
		case 0x04: case 0x14: case 0x24: case 0x34:
		case 0x0C: case 0x1C: case 0x2C: case 0x3C:
		{
			reg = (opcode >> 3) & 7;
			temp8 = read_reg8(reg);
			uint8_t res = temp8 + 1;

			uint8_t f = (reg8[FLAGS] & CF); // Keep Carry
			f |= i8085_ZS[res];             // Get Zero and Sign
			if (res == 0x80) f |= VF;       // 8085 Overflow: 7F -> 80
			if ((res & 0x0F) == 0x00) f |= HF; // Aux Carry

			reg8[FLAGS] = f;
			write_reg8(reg, res);
			cycles -= (reg == M) ? 10 : 4;
			break;
		}

		// ================================================================
		// DCR - Decrement register
		// 8085 sets V flag: overflow if result is 0x7F (was 0x80)
		// ================================================================
		case 0x05: case 0x15: case 0x25: case 0x35:
		case 0x0D: case 0x1D: case 0x2D: case 0x3D:
		{
			reg = (opcode >> 3) & 7;
			temp8 = read_reg8(reg);
			uint8_t result = temp8 - 1;
			reg8[FLAGS] = (reg8[FLAGS] & CF) | NF
			            | i8085_ZS[result]
			            | ((temp8 == 0x80) ? VF : 0)
			            | ((temp8 & 0x0F) ? 0 : HF);
			write_reg8(reg, result);
			if (reg == M) { cycles -= 10; } else { cycles -= 4; }
			break;
		}

		// ================================================================
		// MVI - Move immediate to register
		// ================================================================
		case 0x06: case 0x16: case 0x26: case 0x36:
		case 0x0E: case 0x1E: case 0x2E: case 0x3E:
			reg = (opcode >> 3) & 7;
			write_reg8(reg, i8085_read(reg_PC++));
			if (reg == M) { cycles -= 10; } else { cycles -= 7; }
			break;

		// ================================================================
		// Rotate instructions
		// ================================================================
		case 0x07: // RLC
		{
			uint8_t carry = (reg8[A] >> 7) & 1;
			reg8[A] = (reg8[A] << 1) | carry;
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | NF | CF)) | carry;
			cycles -= 4;
			break;
		}
		case 0x0F: // RRC
		{
			uint8_t carry = reg8[A] & 1;
			reg8[A] = (reg8[A] >> 1) | (carry << 7);
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | NF | CF)) | carry;
			cycles -= 4;
			break;
		}
		case 0x17: // RAL
		{
			uint8_t carry = (reg8[FLAGS] & CF);
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | NF | CF)) | (reg8[A] >> 7);
			reg8[A] = (reg8[A] << 1) | carry;
			cycles -= 4;
			break;
		}
		case 0x1F: // RAR
		{
			uint8_t carry = (reg8[FLAGS] & CF);
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | NF | CF)) | (reg8[A] & 1);
			reg8[A] = (reg8[A] >> 1) | (carry << 7);
			cycles -= 4;
			break;
		}

		// ================================================================
		// DAD - Double Add (add register pair to HL)
		// 8085: also sets/clears HF based on carry out of bit 11
		// ================================================================
		case 0x09: case 0x19: case 0x29: case 0x39:
		{
			reg = (opcode >> 4) & 3;
			uint32_t hl = (uint32_t)i85_reg16_HL;
			uint32_t rp = (uint32_t)read_RP(reg);
			uint32_t result = hl + rp;
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | CF))
			            | (((hl ^ result ^ rp) >> 8) & HF)
			            | ((result >> 16) & CF);
			write16_RP(2, (uint16_t)result);
			cycles -= 10;
			break;
		}

		// ================================================================
		// RIM - Read Interrupt Mask (8085 only, opcode 0x20)
		// ================================================================
		case 0x20:
		{
			// Bit 7: SID Pin
			// Bit 6-4: Pending Interrupts (I7.5, I6.5, I5.5)
			// Bit 3: Interrupt Enable Flag
			// Bit 2-0: Current Interrupt Masks (M7.5, M6.5, M5.5)
			uint8_t res = (IM & 0x07);             // Mask bits
			if (IM & IM_IEN) res |= 0x08;          // IE status
			res |= (IREQ & 0x70);                  // Pending latches (mapped to bits 4,5,6)
			if (SID_state)   res |= 0x80;          // Actual SID pin state
			reg8[A] = res;
			cycles -= 4;
			break;
		}

		// ================================================================
		// DAA - Decimal Adjust Accumulator
		// ================================================================
		case 0x27: // DAA
		{
			temp16 = reg8[A];
			if (((temp16 & 0x0F) > 0x09) || (reg8[FLAGS] & HF)) {
				if (((temp16 & 0x0F) + 0x06) & 0xF0)
					reg8[FLAGS] |= HF;
				else
					reg8[FLAGS] &= ~HF;
				temp16 += 0x06;
				if (temp16 & 0xFF00)
					reg8[FLAGS] |= CF;
			}
			if (((temp16 & 0xF0) > 0x90) || (reg8[FLAGS] & CF)) {
				temp16 += 0x60;
				if (temp16 & 0xFF00)
					reg8[FLAGS] |= CF;
			}
			reg8[A] = (uint8_t)temp16;
			reg8[FLAGS] = (reg8[FLAGS] & (CF | HF)) | i8085_ZSP[reg8[A]];
			cycles -= 4;
			break;
		}

		// ================================================================
		// CMA - Complement Accumulator
		// 8085 sets HF and NF
		// ================================================================
		case 0x2F: // CMA
			reg8[A] = ~reg8[A];
			reg8[FLAGS] |= (HF | NF);
			cycles -= 4;
			break;

		// ================================================================
		// SIM - Set Interrupt Mask (8085 only, opcode 0x30)
		// ================================================================
		case 0x30:
		{
			uint8_t mask = reg8[A];

			// 1. Update SOD if Bit 6 (SOE) is set
			if (mask & 0x40) {
				SOD_state = (mask & 0x80) ? 1 : 0;
				if (SOD_callback) SOD_callback(SOD_state);
			}

			// 2. Reset RST 7.5 latch if Bit 4 is set
			if (mask & 0x10) {
				IREQ &= ~IM_RST75;
			}

			// 3. Update Interrupt Masks ONLY if Bit 3 (MSE) is set
			if (mask & 0x08) {
				// Only bits 0,1,2 are masks
				IM = (IM & ~0x07) | (mask & 0x07);
			}
			cycles -= 4;
			break;
		}

		// ================================================================
		// STC / CMC - Set/Complement Carry
		// ================================================================
		case 0x37: // STC
			reg8[FLAGS] = (reg8[FLAGS] & ~(HF | NF)) | CF;
			cycles -= 4;
			break;
		case 0x3F: // CMC
			reg8[FLAGS] = ((reg8[FLAGS] & ~(HF | NF)) | ((reg8[FLAGS] & CF) << 4)) ^ CF;
			cycles -= 4;
			break;

		// ================================================================
		// MOV - Register to Register moves (0x40-0x7F, except 0x76=HLT)
		// ================================================================
		case 0x40: case 0x50: case 0x60: case 0x70:
		case 0x41: case 0x51: case 0x61: case 0x71:
		case 0x42: case 0x52: case 0x62: case 0x72:
		case 0x43: case 0x53: case 0x63: case 0x73:
		case 0x44: case 0x54: case 0x64: case 0x74:
		case 0x45: case 0x55: case 0x65: case 0x75:
		case 0x46: case 0x56: case 0x66:
		case 0x47: case 0x57: case 0x67: case 0x77:
		case 0x48: case 0x58: case 0x68: case 0x78:
		case 0x49: case 0x59: case 0x69: case 0x79:
		case 0x4A: case 0x5A: case 0x6A: case 0x7A:
		case 0x4B: case 0x5B: case 0x6B: case 0x7B:
		case 0x4C: case 0x5C: case 0x6C: case 0x7C:
		case 0x4D: case 0x5D: case 0x6D: case 0x7D:
		case 0x4E: case 0x5E: case 0x6E: case 0x7E:
		case 0x4F: case 0x5F: case 0x6F: case 0x7F:
			reg = (opcode >> 3) & 7;
			reg2 = opcode & 7;
			write_reg8(reg, read_reg8(reg2));
			if ((reg == M) || (reg2 == M)) { cycles -= 7; } else { cycles -= 4; }
			break;

		// ================================================================
		// HLT - Halt
		// ================================================================
		case 0x76: // HLT
			reg_PC--;
			HALT = 1;
			cycles -= 4;
			if (cycles > 0) cycles = 0;  // stop execution until interrupt
			break;

		// ================================================================
		// ADD - Add register to A (with overflow flag)
		// ================================================================
		case 0x80: case 0x81: case 0x82: case 0x83:
		case 0x84: case 0x85: case 0x86: case 0x87:
		{
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			int q = reg8[A] + temp8;

			uint8_t f = i8085_ZS[q & 0xFF];
			if (q > 0xFF) f |= CF;
			if ((reg8[A] ^ q ^ temp8) & 0x10) f |= HF;
			// 8085 specific Overflow logic for bit 2
			if (((temp8 ^ reg8[A] ^ 0x80) & (temp8 ^ q) & 0x80)) f |= VF;

			reg8[FLAGS] = f;
			reg8[A] = (uint8_t)q;
			cycles -= (reg == M) ? 7 : 4;
			break;
		}

		// ================================================================
		// ADC - Add register to A with carry
		// ================================================================
		case 0x88: case 0x89: case 0x8A: case 0x8B:
		case 0x8C: case 0x8D: case 0x8E: case 0x8F:
		{
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			int q = reg8[A] + temp8 + (reg8[FLAGS] & CF);
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF)
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A] ^ SF) & (temp8 ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;
		}

		// ================================================================
		// SUB - Subtract register from A
		// ================================================================
		case 0x90: case 0x91: case 0x92: case 0x93:
		case 0x94: case 0x95: case 0x96: case 0x97:
		{
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			int q = reg8[A] - temp8;
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;
		}

		// ================================================================
		// SBB - Subtract register from A with borrow
		// ================================================================
		case 0x98: case 0x99: case 0x9A: case 0x9B:
		case 0x9C: case 0x9D: case 0x9E: case 0x9F:
		{
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			int q = reg8[A] - temp8 - (reg8[FLAGS] & CF);
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;
		}

		// ================================================================
		// ANA - AND register with A (logical: uses parity in VF)
		// ================================================================
		case 0xA0: case 0xA1: case 0xA2: case 0xA3:
		case 0xA4: case 0xA5: case 0xA6: case 0xA7:
			reg = opcode & 7;
			reg8[A] &= read_reg8(reg);
			reg8[FLAGS] = i8085_ZSP[reg8[A]] | HF;
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;

		// ================================================================
		// XRA - XOR register with A (logical: uses parity in VF)
		// ================================================================
		case 0xA8: case 0xA9: case 0xAA: case 0xAB:
		case 0xAC: case 0xAD: case 0xAE: case 0xAF:
			reg = opcode & 7;
			reg8[A] ^= read_reg8(reg);
			reg8[FLAGS] = i8085_ZSP[reg8[A]];
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;

		// ================================================================
		// ORA - OR register with A (logical: uses parity in VF)
		// ================================================================
		case 0xB0: case 0xB1: case 0xB2: case 0xB3:
		case 0xB4: case 0xB5: case 0xB6: case 0xB7:
			reg = opcode & 7;
			reg8[A] |= read_reg8(reg);
			reg8[FLAGS] = i8085_ZSP[reg8[A]];
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;

		// ================================================================
		// CMP - Compare register with A
		// ================================================================
		case 0xB8: case 0xB9: case 0xBA: case 0xBB:
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		{
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			int q = reg8[A] - temp8;
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			if (reg == M) { cycles -= 7; } else { cycles -= 4; }
			break;
		}

		// ================================================================
		// Conditional Returns
		// ================================================================
		case 0xC0: case 0xC8: case 0xD0: case 0xD8:
		case 0xE0: case 0xE8: case 0xF0: case 0xF8:
			if (test_cond((opcode >> 3) & 7)) {
				reg_PC = i8085_pop();
				cycles -= 12; // Taken
			}
			else {
				cycles -= 6;  // Not Taken
			}
			break;

		// ================================================================
		// POP
		// ================================================================
		case 0xC1: // POP B
		case 0xD1: // POP D
		case 0xE1: // POP H
		case 0xF1: // POP PSW
			reg = (opcode >> 4) & 3;
			write16_RP_PUSHPOP(reg, i8085_pop());
			cycles -= 10;
			break;

		// ================================================================
		// Conditional Jumps
		// ================================================================
		case 0xC2: case 0xCA: case 0xD2: case 0xDA:
		case 0xE2: case 0xEA: case 0xF2: case 0xFA:
			temp16 = (uint16_t)i8085_read(reg_PC) | (((uint16_t)i8085_read(reg_PC + 1)) << 8);
			if (test_cond((opcode >> 3) & 7)) {
				reg_PC = temp16;
				cycles -= 10; // Taken
			}
			else {
				reg_PC += 2;
				cycles -= 7;  // Not Taken (8085 specific timing)
			}
			break;

		// ================================================================
		// JMP - Unconditional Jump
		// ================================================================
		case 0xC3: // JMP
			temp16 = (uint16_t)i8085_read(reg_PC) | (((uint16_t)i8085_read(reg_PC + 1)) << 8);
			reg_PC = temp16;
			cycles -= 10;
			break;

		// ================================================================
		// Conditional Calls
		// ================================================================
		case 0xC4: // CNZ
		case 0xCC: // CZ
		case 0xD4: // CNC
		case 0xDC: // CC
		case 0xE4: // CPO
		case 0xEC: // CPE
		case 0xF4: // CP
		case 0xFC: // CM
			temp16 = (uint16_t)i8085_read(reg_PC) | (((uint16_t)i8085_read(reg_PC + 1)) << 8);
			if (test_cond((opcode >> 3) & 7)) {
				i8085_push(reg_PC + 2);
				reg_PC = temp16;
				cycles -= 18;
			} else {
				reg_PC += 2;
				cycles -= 9;
			}
			break;

		// ================================================================
		// PUSH
		// ================================================================
		case 0xC5: // PUSH B
		case 0xD5: // PUSH D
		case 0xE5: // PUSH H
		case 0xF5: // PUSH PSW
			reg = (opcode >> 4) & 3;
			i8085_push(read_RP_PUSHPOP(reg));
			cycles -= 12;
			break;

		// ================================================================
		// Immediate Arithmetic
		// ================================================================
		case 0xC6: // ADI
		{
			temp8 = i8085_read(reg_PC++);
			int q = reg8[A] + temp8;
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF)
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A] ^ SF) & (temp8 ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			cycles -= 7;
			break;
		}
		case 0xCE: // ACI
		{
			temp8 = i8085_read(reg_PC++);
			int q = reg8[A] + temp8 + (reg8[FLAGS] & CF);
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF)
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A] ^ SF) & (temp8 ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			cycles -= 7;
			break;
		}
		case 0xD6: // SUI
		{
			temp8 = i8085_read(reg_PC++);
			int q = reg8[A] - temp8;
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			cycles -= 7;
			break;
		}
		case 0xDE: // SBI
		{
			temp8 = i8085_read(reg_PC++);
			int q = reg8[A] - temp8 - (reg8[FLAGS] & CF);
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			reg8[A] = (uint8_t)q;
			cycles -= 7;
			break;
		}
		case 0xE6: // ANI
			temp8 = i8085_read(reg_PC++);
			reg8[A] &= temp8;
			reg8[FLAGS] = i8085_ZSP[reg8[A]] | HF;
			cycles -= 7;
			break;
		case 0xEE: // XRI
			reg8[A] ^= i8085_read(reg_PC++);
			reg8[FLAGS] = i8085_ZSP[reg8[A]];
			cycles -= 7;
			break;
		case 0xF6: // ORI
			reg8[A] |= i8085_read(reg_PC++);
			reg8[FLAGS] = i8085_ZSP[reg8[A]];
			cycles -= 7;
			break;
		case 0xFE: // CPI
		{
			temp8 = i8085_read(reg_PC++);
			int q = reg8[A] - temp8;
			reg8[FLAGS] = i8085_ZS[q & 0xFF]
			            | ((q >> 8) & CF) | NF
			            | ((reg8[A] ^ q ^ temp8) & HF)
			            | (((temp8 ^ reg8[A]) & (reg8[A] ^ q) & SF) >> 5);
			cycles -= 7;
			break;
		}

		// ================================================================
		// RST - Restart
		// ================================================================
		case 0xC7: case 0xCF: case 0xD7: case 0xDF:
		case 0xE7: case 0xEF: case 0xF7: case 0xFF:
			i8085_push(reg_PC);
			reg_PC = (uint16_t)((opcode >> 3) & 7) << 3;
			cycles -= 12;
			break;

		// ================================================================
		// RET - Unconditional Return
		// ================================================================
		case 0xC9: // RET
			reg_PC = i8085_pop();
			cycles -= 10;
			break;

		// ================================================================
		// CALL - Unconditional Call
		// ================================================================
		case 0xCD: // CALL
			temp16 = (uint16_t)i8085_read(reg_PC) | (((uint16_t)i8085_read(reg_PC + 1)) << 8);
			i8085_push(reg_PC + 2);
			reg_PC = temp16;
			cycles -= 18;
			break;

		// ================================================================
		// IN / OUT
		// 8085 IN sets flags (S, Z, P based on A); 8085 OUT does not
		// ================================================================
		case 0xDB: // IN
			temp8 = i8085_read(reg_PC++);
			reg8[A] = In(temp8);
			// 8085: IN sets S, Z, P flags based on data read
			reg8[FLAGS] = (reg8[FLAGS] & CF) | i8085_ZSP[reg8[A]];
			cycles -= 10;
			break;
		case 0xD3: // OUT
			Out(i8085_read(reg_PC++), reg8[A]);
			cycles -= 10;
			break;

		// ================================================================
		// XCHG - Exchange DE and HL
		// ================================================================
		case 0xEB: // XCHG
			temp8 = reg8[D]; reg8[D] = reg8[H]; reg8[H] = temp8;
			temp8 = reg8[E]; reg8[E] = reg8[L]; reg8[L] = temp8;
			cycles -= 4;
			break;

		// ================================================================
		// XTHL - Exchange top of stack with HL
		// ================================================================
		case 0xE3: // XTHL
			temp16 = i8085_pop();
			i8085_push(i85_reg16_HL);
			write16_RP(2, temp16);
			cycles -= 16;
			break;

		// ================================================================
		// PCHL - Jump to address in HL
		// ================================================================
		case 0xE9: // PCHL
			reg_PC = i85_reg16_HL;
			cycles -= 6;
			break;

		// ================================================================
		// SPHL - Set SP to HL
		// ================================================================
		case 0xF9: // SPHL
			reg_SP = i85_reg16_HL;
			cycles -= 6;
			break;

		// ================================================================
		// EI - Enable Interrupts
		// Also re-evaluates pending interrupts (8085 behavior)
		// ================================================================
		case 0xFB: // EI
			IM |= IM_IEN;
			// Clear serviced interrupt request and re-evaluate pending
			IREQ &= ~ISRV;
			ISRV = 0;
			// Find highest priority pending IREQ and schedule
			if (!(IM & IM_RST75) && (IREQ & IM_RST75)) {
				ISRV = IM_RST75;
				IRQ2 = ADDR_RST75;
			} else if (!(IM & IM_RST65) && (IREQ & IM_RST65)) {
				ISRV = IM_RST65;
				IRQ2 = ADDR_RST65;
			} else if (!(IM & IM_RST55) && (IREQ & IM_RST55)) {
				ISRV = IM_RST55;
				IRQ2 = ADDR_RST55;
			} else if (!(IM & IM_INTR) && (IREQ & IM_INTR)) {
				ISRV = IM_INTR;
				IRQ2 = ADDR_INTR;
			}
			cycles -= 4;
			break;

		// ================================================================
		// DI - Disable Interrupts
		// ================================================================
		case 0xF3: // DI
			IM &= ~IM_IEN;
			cycles -= 4;
			break;

		// ================================================================
		// Undefined opcodes on the 8085
		// These are NOPs or handled as illegal on real hardware.
		// Some are documented as undocumented 8085 instructions
		// (DSUB, ARHL, RDEL, etc.) but we treat them as NOPs for now.
		// ================================================================
		case 0x08: case 0x10: case 0x18: case 0x28: case 0x38:
		case 0xCB: case 0xD9: case 0xDD: case 0xED: case 0xFD:
			LOG_INFO("I8085 undefined opcode %02X at %04X", opcode, reg_PC - 1);
			cycles -= 4;
			break;

		default:
			LOG_INFO("I8085 UNRECOGNIZED INSTRUCTION @ %04Xh: %02X", reg_PC - 1, opcode);
			cycles -= 4;
			break;

		} // end switch(opcode)

		// Update clock tick total
		clocktickstotal += abs(cycles - last_cycles);
		if (clocktickstotal > 0x0FFFFFFF) clocktickstotal = 0;

	} // end while(cycles > 0)

	return cycles;
}
