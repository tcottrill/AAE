
#include "cpu_i8080.h"
#include "sys_log.h"
#include <cmath>

#pragma warning( disable : 4244 )

int cpu_i8080::get_ticks(int reset)
{
	int tmp;

	tmp = clocktickstotal;
	if (reset)
	{
		clocktickstotal = 0;
	}
	return tmp;
}


cpu_i8080::cpu_i8080(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	z80IoRead = port_read;
	z80IoWrite = port_write;

}


uint8_t cpu_i8080::i8080_read(uint16_t addr)
{
	uint8_t temp = 0;

	// Pointer to Beginning of our handler
	MemoryReadByte* MemRead = memory_read;

	while (MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
		{

			if (MemRead->memoryCall)
			{
				temp = MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
			}
			else
			{
				temp = *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
			}
			MemRead = nullptr;
			break;
		}
		++MemRead;
	}
	// Add blocking here
	if (MemRead && !mmem)
	{
		temp = MEM[addr];
	}
	if (MemRead && mmem)
	{
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Read at %x", addr);
	}

	return temp;
}


void cpu_i8080::i8080_write(uint16_t addr, uint8_t byte)
{

	// Pointer to Beginning of our handler
	MemoryWriteByte* MemWrite = memory_write;

	while (MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
		{
			if (MemWrite->memoryCall)
			{
				MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
			}
			else
			{
				*((uint8_t*)MemWrite->pUserArea + (addr - MemWrite->lowAddr)) = byte;
			}
			MemWrite = nullptr;
			break;
		}
		++MemWrite;
	}
	// Add blocking here
	if (MemWrite && !mmem)
	{
		MEM[addr] = (uint8_t)byte;
	}
	if (MemWrite && mmem)
	{
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Write at %x data: %x", addr, byte);
	}
}



uint8_t cpu_i8080::In(uint8_t bPort)
{
	uint8_t bVal = 0;
	struct z80PortRead* mr = z80IoRead;

	while (mr->lowIoAddr != 0xffff)
	{
		if (bPort >= mr->lowIoAddr && bPort <= mr->highIoAddr)
		{
			bVal = mr->IOCall(bPort, mr);
			break;
		}
		mr++;
	}

	return bVal;
}


void cpu_i8080::Out(uint8_t bPort, uint8_t bVal)
{
	
	struct z80PortWrite* mr = z80IoWrite;

	while (mr->lowIoAddr != 0xffff)
	{
		if (bPort >= mr->lowIoAddr && bPort <= mr->highIoAddr)
		{
			mr->IOCall(bPort, bVal, mr);
			return;
		}
		mr++;
	}
	
}


uint16_t cpu_i8080::read_RP(uint8_t rp)
{
	switch (rp) {
	case 0x00:
		return reg16_BC;
	case 0x01:
		return reg16_DE;
	case 0x02:
		return reg16_HL;
	case 0x03:
		return reg_SP;
	}
	return 0;
}

uint16_t cpu_i8080::read_RP_PUSHPOP(uint8_t rp)
{
	switch (rp) {
	case 0x00:
		return reg16_BC;
	case 0x01:
		return reg16_DE;
	case 0x02:
		return reg16_HL;
	case 0x03:
		return (reg16_PSW | 0x02) & 0xFFD7;
	}
	return 0;
}

void cpu_i8080::write_RP(uint8_t rp, uint8_t lb, uint8_t hb)
{
	switch (rp) {
	case 0x00:
		reg8[C] = lb;
		reg8[B] = hb;
		break;
	case 0x01:
		reg8[E] = lb;
		reg8[D] = hb;
		break;
	case 0x02:
		reg8[L] = lb;
		reg8[H] = hb;
		break;
	case 0x03:
		reg_SP = (uint16_t)lb | ((uint16_t)hb << 8);
		break;
	}
}

void cpu_i8080::write16_RP(uint8_t rp, uint16_t value)
{
	switch (rp) {
	case 0x00:
		reg8[C] = value & 0x00FF;
		reg8[B] = value >> 8;
		break;
	case 0x01:
		reg8[E] = value & 0x00FF;
		reg8[D] = value >> 8;
		break;
	case 0x02:
		reg8[L] = value & 0x00FF;
		reg8[H] = value >> 8;
		break;
	case 0x03:
		reg_SP = value;
		break;
	}
}

void cpu_i8080::write16_RP_PUSHPOP(uint8_t rp, uint16_t value)
{
	switch (rp) {
	case 0x00:
		reg8[C] = value & 0x00FF;
		reg8[B] = value >> 8;
		break;
	case 0x01:
		reg8[E] = value & 0x00FF;
		reg8[D] = value >> 8;
		break;
	case 0x02:
		reg8[L] = value & 0x00FF;
		reg8[H] = value >> 8;
		break;
	case 0x03:
		reg8[FLAGS] = ((value & 0x00FF) | 0x02) & 0xD7; // Always Clear Bit 5 and Bit 3, Set Bit 1
		reg8[A] = value >> 8;
		break;
	}
}

void cpu_i8080::calc_SZP(uint8_t value) {
	if (value == 0) set_Z(); else clear_Z();
	if (value & 0x80) set_S(); else clear_S();
	if (parity[value]) set_P(); else clear_P();
}

void cpu_i8080::calc_AC(uint8_t val1, uint8_t val2)
{
	if (((val1 & 0x0F) + (val2 & 0x0F)) > 0x0F) {
		set_AC();
	}
	else {
		clear_AC();
	}
}

void cpu_i8080::calc_AC_carry(uint8_t val1, uint8_t val2)
{
	if (((val1 & 0x0F) + (val2 & 0x0F)) >= 0x0F) {
		set_AC();
	}
	else {
		clear_AC();
	}
}

void cpu_i8080::calc_subAC(int8_t val1, uint8_t val2)
{
	if ((val2 & 0x0F) <= (val1 & 0x0F)) {
		set_AC();
	}
	else {
		clear_AC();
	}
}

void cpu_i8080::calc_subAC_borrow(int8_t val1, uint8_t val2)
{
	if ((val2 & 0x0F) < (val1 & 0x0F)) {
		set_AC();
	}
	else {
		clear_AC();
	}
}

uint8_t cpu_i8080::test_cond(uint8_t code)
{
	switch (code) {
	case 0: //Z not set
		if (!test_Z()) return 1; else return 0;
	case 1: //Z set
		if (test_Z()) return 1; else return 0;
	case 2: //C not set
		if (!test_C()) return 1; else return 0;
	case 3: //C set
		if (test_C()) return 1; else return 0;
	case 4: //P not set
		if (!test_P()) return 1; else return 0;
	case 5: //P set
		if (test_P()) return 1; else return 0;
	case 6: //S not set
		if (!test_S()) return 1; else return 0;
	case 7: //S set
		if (test_S()) return 1; else return 0;
	}
	return 0;
}

void cpu_i8080::i8080_push(uint16_t value)
{
	i8080_write(--reg_SP, value >> 8);
	i8080_write(--reg_SP, (uint8_t)value);
}

uint16_t cpu_i8080::i8080_pop()
{
	uint16_t temp;
	temp = i8080_read(reg_SP++);
	temp |= (uint16_t)i8080_read(reg_SP++) << 8;
	return temp;
}

void cpu_i8080::interrupt(uint8_t n)
{
	if (!INTE) return;
	i8080_push(reg_PC);   //Push Old PC on the stack.
	reg_PC = (uint16_t)n; //Set new PC
	//reg_PC = (uint16_t)n << 3;  //Why shift 3 ?
	INTE = 0; //Set Interrupts to off
}

void cpu_i8080::i8080_jump(uint16_t addr)
{
	reg_PC = addr;
}

void cpu_i8080::reset()
{
	reg_PC = reg_SP = 0x0000;
}

void cpu_i8080::write_reg8(uint8_t reg, uint8_t value)
{
	if (reg == M) {
		i8080_write(reg16_HL, value);
	}
	else {
		reg8[reg] = value;
	}
}

uint8_t cpu_i8080::read_reg8(uint8_t reg)
{
	if (reg == M) {
		return i8080_read(reg16_HL);
	}
	else {
		return reg8[reg];
	}
}

int cpu_i8080::exec(int cycles)
{
	uint8_t opcode, temp8, reg, reg2;
	uint16_t temp16;
	uint32_t temp32;

	while (cycles > 0) 	
	{

		int last_cycles = cycles;

		opcode = i8080_read(reg_PC++);
		//LOG_INFO("Opcode here :%x  pc:%x", opcode, reg_PC);
		switch (opcode) 
		{
		case 0x3A: //LDA a - load A from memory
			temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC + 1) << 8);
			reg8[A] = i8080_read(temp16);
			reg_PC += 2;
			cycles -= 13;
			break;
		case 0x32: //STA a - store A to memory
			temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC + 1) << 8);
			i8080_write(temp16, reg8[A]);
			reg_PC += 2;
			cycles -= 13;
			break;
		case 0x2A: //LHLD a - load H:L from memory
			temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC + 1) << 8);
			reg8[L] = i8080_read(temp16++);
			reg8[H] = i8080_read(temp16);
			reg_PC += 2;
			cycles -= 16;
			break;
		case 0x22: //SHLD a - store H:L to memory
			temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC + 1) << 8);
			i8080_write(temp16++, reg8[L]);
			i8080_write(temp16, reg8[H]);
			reg_PC += 2;
			cycles -= 16;
			break;
		case 0xEB: //XCHG - exchange DE and HL content
			temp8 = reg8[D];
			reg8[D] = reg8[H];
			reg8[H] = temp8;
			temp8 = reg8[E];
			reg8[E] = reg8[L];
			reg8[L] = temp8;
			cycles -= 5;
			break;
		case 0xC6: //ADI # - add immediate to A
			temp8 = i8080_read(reg_PC++);
			temp16 = (uint16_t)reg8[A] + (uint16_t)temp8;
			if (temp16 & 0xFF00) set_C(); else clear_C();
			calc_AC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			cycles -= 7;
			break;
		case 0xCE: //ACI # - add immediate to A with carry
			temp8 = i8080_read(reg_PC++);
			temp16 = (uint16_t)reg8[A] + (uint16_t)temp8 + (uint16_t)test_C();
			if (test_C()) calc_AC_carry(reg8[A], temp8); else calc_AC(reg8[A], temp8);
			if (temp16 & 0xFF00) set_C(); else clear_C();
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			cycles -= 7;
			break;
		case 0xD6: //SUI # - subtract immediate from A
			temp8 = i8080_read(reg_PC++);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
			if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
			calc_subAC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			cycles -= 7;
			break;
		case 0x27: //DAA - decimal adjust accumulator
			temp16 = reg8[A];
			if (((temp16 & 0x0F) > 0x09) || test_AC()) {
				if (((temp16 & 0x0F) + 0x06) & 0xF0) set_AC(); else clear_AC();
				temp16 += 0x06;
				if (temp16 & 0xFF00) set_C(); //can also cause carry to be set during addition to the low nibble
			}
			if (((temp16 & 0xF0) > 0x90) || test_C()) {
				temp16 += 0x60;
				if (temp16 & 0xFF00) set_C(); //doesn't clear it if this clause is false
			}
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			cycles -= 4;
			break;
		case 0xE6: //ANI # - AND immediate with A
			temp8 = i8080_read(reg_PC++);
			if ((reg8[A] | temp8) & 0x08) set_AC(); else clear_AC();
			reg8[A] &= temp8;
			clear_C();
			calc_SZP(reg8[A]);
			cycles -= 7;
			break;
		case 0xF6: //ORI # - OR immediate with A
			reg8[A] |= i8080_read(reg_PC++);
			clear_AC();
			clear_C();
			calc_SZP(reg8[A]);
			cycles -= 7;
			break;
		case 0xEE: //XRI # - XOR immediate with A
			reg8[A] ^= i8080_read(reg_PC++);
			clear_AC();
			clear_C();
			calc_SZP(reg8[A]);
			cycles -= 7;
			break;
		case 0xDE: //SBI # - subtract immediate from A with borrow
			temp8 = i8080_read(reg_PC++);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8 - (uint16_t)test_C();
			if (test_C()) calc_subAC_borrow(reg8[A], temp8); else calc_subAC(reg8[A], temp8);
			if (((temp16 & 0x00FF) >= reg8[A]) && (temp8 | test_C())) set_C(); else clear_C();
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			cycles -= 7;
			break;
		case 0xFE: //CPI # - compare immediate with A
			temp8 = i8080_read(reg_PC++);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
			if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
			calc_subAC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			cycles -= 7;
			break;
		case 0x07: //RLC - rotate A left
			if (reg8[A] & 0x80) set_C(); else clear_C();
			reg8[A] = (reg8[A] >> 7) | (reg8[A] << 1);
			cycles -= 4;
			break;
		case 0x0F: //RRC - rotate A right
			if (reg8[A] & 0x01) set_C(); else clear_C();
			reg8[A] = (reg8[A] << 7) | (reg8[A] >> 1);
			cycles -= 4;
			break;
		case 0x17: //RAL - rotate A left through carry
			temp8 = test_C();
			if (reg8[A] & 0x80) set_C(); else clear_C();
			reg8[A] = (reg8[A] << 1) | temp8;
			cycles -= 4;
			break;
		case 0x1F: //RAR - rotate A right through carry
			temp8 = test_C();
			if (reg8[A] & 0x01) set_C(); else clear_C();
			reg8[A] = (reg8[A] >> 1) | (temp8 << 7);
			cycles -= 4;
			break;
		case 0x2F: //CMA - compliment A
			reg8[A] = ~reg8[A];
			cycles -= 4;
			break;
		case 0x3F: //CMC - compliment carry flag
			reg8[FLAGS] ^= 1;
			cycles -= 4;
			break;
		case 0x37: //STC - set carry flag
			set_C();
			cycles -= 4;
			break;
		case 0xC7: //RST n - restart (call n*8)
		case 0xD7:
		case 0xE7:
		case 0xF7:
		case 0xCF:
		case 0xDF:
		case 0xEF:
		case 0xFF:
			i8080_push(reg_PC);
			reg_PC = (uint16_t)((opcode >> 3) & 7) << 3;
			cycles -= 11;
			break;
		case 0xE9: //PCHL - jump to address in H:L
			reg_PC = reg16_HL;
			cycles -= 5;
			break;
		case 0xE3: //XTHL - swap H:L with top word on stack
			temp16 = i8080_pop();
			i8080_push(reg16_HL);
			write16_RP(2, temp16);
			cycles -= 18;
			break;
		case 0xF9: //SPHL - set SP to content of HL
			reg_SP = reg16_HL;
			cycles -= 5;
			break;
		case 0xDB: //IN p - read input port into A
			reg8[A] = In(i8080_read(reg_PC++));
			cycles -= 10;
			break;
		case 0xD3: //OUT p - write A to output port
			Out(i8080_read(reg_PC++), reg8[A]);
			cycles -= 10;
			break;
		case 0xFB: //EI - enable interrupts
			INTE = 1;
			cycles -= 4;
			break;
		case 0xF3: //DI - disbale interrupts
			INTE = 0;
			cycles -= 4;
			break;
		case 0x76: //HLT - halt processor
			reg_PC--;
			cycles -= 7;
			break;
		case 0x00: //NOP - no operation
#ifdef ALLOW_UNDEFINED
		case 0x10:
		case 0x20:
		case 0x30:
		case 0x08:
		case 0x18:
		case 0x28:
		case 0x38:
#endif
			cycles -= 4;
			break;
		case 0x40: case 0x50: case 0x60: case 0x70: //MOV D,S - move register to register
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
			if ((reg == M) || (reg2 == M)) {
				cycles -= 7;
			}
			else {
				cycles -= 5;
			}
			break;
		case 0x06: //MVI D,# - move immediate to register
		case 0x16:
		case 0x26:
		case 0x36:
		case 0x0E:
		case 0x1E:
		case 0x2E:
		case 0x3E:
			reg = (opcode >> 3) & 7;
			write_reg8(reg, i8080_read(reg_PC++));
			if (reg == M) {
				cycles -= 10;
			}
			else {
				cycles -= 7;
			}
			break;
		case 0x01: //LXI RP,# - load register pair immediate
		case 0x11:
		case 0x21:
		case 0x31:
			reg = (opcode >> 4) & 3;
			write_RP(reg, i8080_read(reg_PC), i8080_read(reg_PC + 1));
			reg_PC += 2;
			cycles -= 10;
			break;
		case 0x0A: //LDAX BC - load A indirect through BC
			reg8[A] = i8080_read(reg16_BC);
			cycles -= 7;
			break;
		case 0x1A: //LDAX DE - load A indirect through DE
			reg8[A] = i8080_read(reg16_DE);
			cycles -= 7;
			break;
		case 0x02: //STAX BC - store A indirect through BC
			i8080_write(reg16_BC, reg8[A]);
			cycles -= 7;
			break;
		case 0x12: //STAX DE - store A indirect through DE
			i8080_write(reg16_DE, reg8[A]);
			cycles -= 7;
			break;
		case 0x04: //INR D - increment register
		case 0x14:
		case 0x24:
		case 0x34:
		case 0x0C:
		case 0x1C:
		case 0x2C:
		case 0x3C:
			reg = (opcode >> 3) & 7;
			temp8 = read_reg8(reg); //reg8[reg];
			calc_AC(temp8, 1);
			calc_SZP(temp8 + 1);
			write_reg8(reg, temp8 + 1); //reg8[reg]++;
			if (reg == M) {
				cycles -= 10;
			}
			else {
				cycles -= 5;
			}
			break;
		case 0x05: //DCR D - decrement register
		case 0x15:
		case 0x25:
		case 0x35:
		case 0x0D:
		case 0x1D:
		case 0x2D:
		case 0x3D:
			reg = (opcode >> 3) & 7;
			temp8 = read_reg8(reg); //reg8[reg];
			calc_subAC(temp8, 1);
			calc_SZP(temp8 - 1);
			write_reg8(reg, temp8 - 1); //reg8[reg]--;
			if (reg == M) {
				cycles -= 10;
			}
			else {
				cycles -= 5;
			}
			break;
		case 0x03: //INX RP - increment register pair
		case 0x13:
		case 0x23:
		case 0x33:
			reg = (opcode >> 4) & 3;
			write16_RP(reg, read_RP(reg) + 1);
			cycles -= 5;
			break;
		case 0x0B: //DCX RP - decrement register pair
		case 0x1B:
		case 0x2B:
		case 0x3B:
			reg = (opcode >> 4) & 3;
			write16_RP(reg, read_RP(reg) - 1);
			cycles -= 5;
			break;
		case 0x09: //DAD RP - add register pair to HL
		case 0x19:
		case 0x29:
		case 0x39:
			reg = (opcode >> 4) & 3;
			temp32 = (uint32_t)reg16_HL + (uint32_t)read_RP(reg);
			write16_RP(2, (uint16_t)temp32);
			if (temp32 & 0xFFFF0000) set_C(); else clear_C();
			cycles -= 10;
			break;
		case 0x80: //ADD S - add register or memory to A
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			temp16 = (uint16_t)reg8[A] + (uint16_t)temp8;
			if (temp16 & 0xFF00) set_C(); else clear_C();
			calc_AC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0x88: //ADC S - add register or memory to A with carry
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:
		case 0x8F:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			temp16 = (uint16_t)reg8[A] + (uint16_t)temp8 + (uint16_t)test_C();
			if (test_C()) calc_AC_carry(reg8[A], temp8); else calc_AC(reg8[A], temp8);
			if (temp16 & 0xFF00) set_C(); else clear_C();
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0x90: //SUB S - subtract register or memory from A
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
			if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
			calc_subAC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0x98: //SBB S - subtract register or memory from A with borrow
		case 0x99:
		case 0x9A:
		case 0x9B:
		case 0x9C:
		case 0x9D:
		case 0x9E:
		case 0x9F:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8 - (uint16_t)test_C();
			if (test_C()) calc_subAC_borrow(reg8[A], temp8); else calc_subAC(reg8[A], temp8);
			if (((temp16 & 0x00FF) >= reg8[A]) && (temp8 | test_C())) set_C(); else clear_C();
			calc_SZP((uint8_t)temp16);
			reg8[A] = (uint8_t)temp16;
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0xA0: //ANA S - AND register with A
		case 0xA1:
		case 0xA2:
		case 0xA3:
		case 0xA4:
		case 0xA5:
		case 0xA6:
		case 0xA7:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			if ((reg8[A] | temp8) & 0x08) set_AC(); else clear_AC();
			reg8[A] &= temp8;
			clear_C();
			calc_SZP(reg8[A]);
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0xB0: //ORA S - OR register with A
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:
		case 0xB7:
			reg = opcode & 7;
			reg8[A] |= read_reg8(reg);
			clear_AC();
			clear_C();
			calc_SZP(reg8[A]);
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0xA8: //XRA S - XOR register with A
		case 0xA9:
		case 0xAA:
		case 0xAB:
		case 0xAC:
		case 0xAD:
		case 0xAE:
		case 0xAF:
			reg = opcode & 7;
			reg8[A] ^= read_reg8(reg);
			clear_AC();
			clear_C();
			calc_SZP(reg8[A]);
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0xB8: //CMP S - compare register with A
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:
		case 0xBF:
			reg = opcode & 7;
			temp8 = read_reg8(reg);
			temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
			if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
			calc_subAC(reg8[A], temp8);
			calc_SZP((uint8_t)temp16);
			if (reg == M) {
				cycles -= 7;
			}
			else {
				cycles -= 4;
			}
			break;
		case 0xC3: //JMP a - unconditional jump
#ifdef ALLOW_UNDEFINED
		case 0xCB:
#endif
			temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
			reg_PC = temp16;
			cycles -= 10;
			break;
		case 0xC2: //Jccc - conditional jumps
		case 0xCA:
		case 0xD2:
		case 0xDA:
		case 0xE2:
		case 0xEA:
		case 0xF2:
		case 0xFA:
			temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
			if (test_cond((opcode >> 3) & 7)) reg_PC = temp16; else reg_PC += 2;
			cycles -= 10;
			break;
		case 0xCD: //CALL a - unconditional call
#ifdef ALLOW_UNDEFINED
		case 0xDD:
		case 0xED:
		case 0xFD:
#endif
			temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
			i8080_push(reg_PC + 2);
			reg_PC = temp16;
			cycles -= 17;
			break;
		case 0xC4: //Cccc - conditional calls
		case 0xCC:
		case 0xD4:
		case 0xDC:
		case 0xE4:
		case 0xEC:
		case 0xF4:
		case 0xFC:
			temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
			if (test_cond((opcode >> 3) & 7)) {
				i8080_push(reg_PC + 2);
				reg_PC = temp16;
				cycles -= 17;
			}
			else {
				reg_PC += 2;
				cycles -= 11;
			}
			break;
		case 0xC9: //RET - unconditional return
#ifdef ALLOW_UNDEFINED
		case 0xD9:
#endif
			reg_PC = i8080_pop();
			cycles -= 10;
			break;
		case 0xC0: //Rccc - conditional returns
		case 0xC8:
		case 0xD0:
		case 0xD8:
		case 0xE0:
		case 0xE8:
		case 0xF0:
		case 0xF8:
			if (test_cond((opcode >> 3) & 7)) {
				reg_PC = i8080_pop();
				cycles -= 11;
			}
			else {
				cycles -= 5;
			}
			break;
		case 0xC5: //PUSH RP - push register pair on the stack
		case 0xD5:
		case 0xE5:
		case 0xF5:
			reg = (opcode >> 4) & 3;
			i8080_push(read_RP_PUSHPOP(reg));
			cycles -= 11;
			break;
		case 0xC1: //POP RP - pop register pair from the stack
		case 0xD1:
		case 0xE1:
		case 0xF1:
			reg = (opcode >> 4) & 3;
			write16_RP_PUSHPOP(reg, i8080_pop());
			cycles -= 10;
			break;

#ifndef ALLOW_UNDEFINED
		default:
			LOG_INFO("UNRECOGNIZED INSTRUCTION @ %04Xh: %02X\n", reg_PC - 1, opcode);
			exit(0);
#endif
			
		}
		// update clock cycles
		clocktickstotal += abs((cycles - last_cycles));
	  if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
	}
	//cycles, clockticks, same diff
	return cycles;
}