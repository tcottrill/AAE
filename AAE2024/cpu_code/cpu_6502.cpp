// 6502 CPU Core, based on code by Neil Bradley, with many changes from sources across the web.
// ADC and SBC code from M.A.M.E. (tm)  Copyright (c) 1998,1999,2000, etc Juergen Buchmueller
// Timing table and debug code from FakeNes
// Code rewritten to c++ and updated by TC 2015-2024 explicitly written to work with with older M.A.M.E. source code for my testing.
// Currently does not handle IRQ after CLI correctly.
// TODO, fix issues with incorrect operand display in the debug code.

// Notes
// 11/22/24 added undoucumented isb opcode, will work to add the rest later.
// 12/28/24 Rewrote the main loop to resolve an issue with the cycle counting being consistently undereported.
// 01/03/25 Discovered an edge case where clocktickstotal was not being set to zero at init, causing an immediate crash. 
// 01/09/25 Changed clocktickstotal again to be set to zero at init, not reset. When a cpu was reset mid-frame, it was throwing the timing count off. 

#include <stdio.h>
#include "cpu_6502.h"
#include "log.h"
#include <string>
#include "timer.h"

//Version 2025.1.09

#define bget(p,m) ((p) & (m))

static const uint32_t ticks[256] = {
	/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
	/* 0 */      7,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    4,    4,    6,    6,  /* 0 */
	/* 1 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 1 */
	/* 2 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    6,  /* 2 */
	/* 3 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 3 */
	/* 4 */      6,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    6,  /* 4 */
	/* 5 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 5 */
	/* 6 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    5,    4,    6,    6,  /* 6 */
	/* 7 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 7 */
	/* 8 */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* 8 */
	/* 9 */      2,    6,    2,    6,    4,    4,    4,    4,    2,    5,    2,    5,    5,    5,    5,    5,  /* 9 */
	/* A */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* A */
	/* B */      2,    5,    2,    5,    4,    4,    4,    4,    2,    4,    2,    4,    4,    4,    4,    4,  /* B */
	/* C */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* C */
	/* D */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* D */
	/* E */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* E */
	/* F */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7   /* F */
};

cpu_6502::cpu_6502(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	cpu_num = num;
	log_debug_rw = 1;
	debug = 0;
	init6502(addr);
}

void cpu_6502::enable_debug(bool s)
{
	debug = 1;
}

int cpu_6502::get6502ticks(int reset)
{
	int tmp;

	tmp = clocktickstotal;
	if (reset)
	{
		clocktickstotal = 0;
	}
	return tmp;
}

uint8_t cpu_6502::get6502memory(uint16_t addr)
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
				//Note the MAME style addressing here!
				temp = MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
				//temp = MemRead->memoryCall(addr, MemRead);
			}
			else
			{
				//Note the MAME style addressing here!
				temp = *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
				//temp = *((uint8_t*)MemRead->pUserArea + (addr )); //Note the addressing here!
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
		if (log_debug_rw) wrlog("Warning! Unhandled Read at %x", addr);
	}

	return temp;
}

void cpu_6502::put6502memory(uint16_t addr, uint8_t byte)
{
	// Pointer to Beginning of our handler
	MemoryWriteByte* MemWrite = memory_write;

	while (MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
		{
			if (MemWrite->memoryCall)
			{
				//Note the MAME style addressing here!
				MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
			}
			else
			{
				//Note the MAME style addressing here!
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
		if (log_debug_rw) wrlog("Warning! Unhandled Write at %x data: %x", addr, byte);
	}
}

// ================================================= Addressing Modes ========================================================

//Accumulator opcodes are handled with their own separate functions.
//{}

// Absolute
void cpu_6502::abs6502()
{
	savepc = get6502memory(PC) + (get6502memory(PC + 1) << 8);
	PC += 2;
}

// Implied
void cpu_6502::implied6502()
{
	//NOP
}

// #Immediate
void cpu_6502::immediate6502()
{
	savepc = PC++;
}

// Branch
void cpu_6502::relative6502()
{
	savepc = get6502memory(PC++);
	if (savepc & F_N) savepc |= 0xFF00;
}

// Indirect JMP
void cpu_6502::indirect6502()
{
	uint16_t temp;
	help = get6502memory(PC) + (get6502memory(PC + 1) << 8);
	temp = (help & 0xFF00) | ((help + 1) & 0x00FF);  //replicate 6502 page-boundary wraparound bug
	savepc = (get6502memory(help) | get6502memory(temp) << 8);
	PC += 2;
}

// Absolute X Indexed
void cpu_6502::absx6502()
{
	savepc = get6502memory(PC) + (get6502memory(PC + 1) << 8);

	if (ticks[opcode] == 4)
		if ((savepc >> 8) != ((savepc + X) >> 8))
			clockticks6502++;//one cycle penalty for page-crossing on some opcodes
	savepc += X;
	PC += 2;
}

// Absolute Y Indexed
void cpu_6502::absy6502()
{
	savepc = get6502memory(PC) + (get6502memory(PC + 1) << 8);

	if (ticks[opcode] == 4)
		if ((savepc >> 8) != ((savepc + Y) >> 8))
			clockticks6502++;//one cycle penalty for page-crossing on some opcodes
	savepc += Y;
	PC += 2;
}

//NOTE: ZERO PAGE and STACK READS/WRITES ARE GOING THROUGH A MEM HANDLER. THIS SLOWS DOWN EMULATION BUT WAS REQUIRED FOR MAJOR HAVOC
// ZeroPage
void cpu_6502::zp6502()
{
	savepc = (uint16_t)get6502memory((uint16_t)PC++);
}

// ZeroPage, X Indexed
void cpu_6502::zpx6502()
{
	savepc = ((uint16_t)get6502memory((uint16_t)PC++) + (uint16_t)X) & 0xFF; //zero-page wraparound
}

// ZeroPage, Y Indexed
void cpu_6502::zpy6502()
{
	savepc = ((uint16_t)get6502memory((uint16_t)PC++) + (uint16_t)Y) & 0xFF; //zero-page wraparound
}

// (X Indexed, Indirect)
void cpu_6502::indx6502()
{
	value = (uint16_t)(((uint16_t)get6502memory(PC++) + (uint16_t)X) & 0xFF);
	savepc = (uint16_t)get6502memory(value & 0x00FF) | ((uint16_t)get6502memory((value + 1) & 0x00FF) << 8);
}

// (Y Indexed, Indirect)
void cpu_6502::indy6502()
{
	uint16_t temp;
	value = (uint16_t)get6502memory(PC++);
	temp = (value & 0xFF00) | ((value + 1) & 0x00FF);  //zero-page wraparound
	savepc = (uint16_t)get6502memory(value) | ((uint16_t)get6502memory(temp) << 8);
	if (ticks[opcode] == 5)
		if ((savepc >> 8) != ((savepc + Y) >> 8))
			clockticks6502++; //one cycle penlty for page-crossing on some opcodes
	savepc += Y;
}

// (ABS,X) - Used by JMP only.
void cpu_6502::indabsx6502()
{
	help = get6502memory(PC) + (get6502memory(PC + 1) << 8) + X;
	savepc = get6502memory(help) + (get6502memory(help + 1) << 8);
}

// (ZP)
void cpu_6502::indzp6502()
{
	value = get6502memory(PC++);
	savepc = get6502memory(value) + (get6502memory(value + 1) << 8);
}

// ================================================= Stack Operations========================================================
// It would be faster not doing zero page and stack access through handles, but Major Havoc requires this.
void cpu_6502::push16(uint16_t pushval)
{
	put6502memory(BASE_STACK + S, (pushval >> 8) & 0xFF);
	put6502memory(BASE_STACK + ((S - 1) & 0xFF), pushval & 0xFF);
	S -= 2;
}

void cpu_6502::push8(uint8_t pushval)
{
	put6502memory(BASE_STACK + S--, pushval);
}

uint16_t cpu_6502::pull16()
{
	uint16_t temp16;
	temp16 = get6502memory(BASE_STACK + ((S + 1) & 0xFF)) | ((uint16_t)get6502memory(BASE_STACK + ((S + 2) & 0xFF)) << 8);
	S += 2;
	return(temp16);
}

uint8_t cpu_6502::pull8()
{
	return (get6502memory(BASE_STACK + ++S));
}

// ================================================= Instructions ========================================================

//ADC
//Code taken from M.A.M.E (tm)
void cpu_6502::adc6502()
{
	int tmp = get6502memory(savepc);

	if (P & F_D)
	{
		int c = (P & F_C);
		int lo = (A & 0x0f) + (tmp & 0x0f) + c;
		int hi = (A & 0xf0) + (tmp & 0xf0);
		P &= ~(F_V | F_C);
		if (lo > 0x09)
		{
			hi += 0x10;
			lo += 0x06;
		}
		if (~(A ^ tmp) & (A ^ hi) & F_N)
			P |= F_V;
		if (hi > 0x90)
			hi += 0x60;
		if (hi & 0xff00)
			P |= F_C;
		A = (lo & 0x0f) + (hi & 0xf0);
	}
	else
	{
		int c = (P & F_C);
		int sum = A + tmp + c;
		P &= ~(F_V | F_C);
		if (~(A ^ tmp) & (A ^ sum) & F_N)
			P |= F_V;
		if (sum & 0xff00)
			P |= F_C;
		A = (uint8_t)sum;
	}
	SET_NZ(A);
}
//AND
void cpu_6502::and6502()
{
	value = get6502memory(savepc);
	A &= value;
	SET_NZ(A);
}
//ASL
void cpu_6502::asl6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value = value << 1;
	put6502memory(savepc, value);
	SET_NZ(value);
}

void cpu_6502::asla6502()
{
	P = (P & ~F_C) | ((A >> 7) & F_C);
	A = A << 1;
	SET_NZ(A);
}

void cpu_6502::bcc6502()
{
	if ((P & F_C) == 0)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::bcs6502()
{
	if (P & F_C)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::beq6502()
{
	if (P & F_Z)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::bit6502()
{
	value = get6502memory(savepc);

	P &= ~(F_N | F_V | F_Z);
	P |= value & (F_N | F_V);
	if ((value & A) == 0)	P |= F_Z;
}

void cpu_6502::bmi6502()
{
	if (P & F_N)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::bne6502()
{
	if ((P & F_Z) == 0)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::bpl6502()
{
	if ((P & F_N) == 0)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::brk6502()
{
	PC++;
	push16(PC); //push next instruction address onto stack
	push8(P | F_B); //push CPU status to stack
	P = (P | F_I) & ~F_D;
	PC = MEM[0xfffe & addrmask] + (MEM[0xffff & addrmask] << 8);
}

void cpu_6502::bvc6502()
{
	if (!(P & F_V))
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::bvs6502()
{
	if (P & F_V)
	{
		oldpc = PC;
		PC += savepc;
		if ((oldpc & 0xFF00) != (PC & 0xFF00)) clockticks6502 += 2;
		else clockticks6502++;
	}
}

void cpu_6502::clc6502()
{
	P &= ~F_C;
}

void cpu_6502::cld6502()
{
	P &= ~F_D;
}

void cpu_6502::cli6502()
{
	//if (_irqPending) irq6502();

	P &= ~F_I;
}

void cpu_6502::clv6502()
{
	P &= ~F_V;
}

void cpu_6502::cmp6502()
{
	value = get6502memory(savepc);
	if (A + 0x100 - value > 0xff) P |= F_C; else P &= ~F_C;
	value = A + 0x100 - value;
	SET_NZ(value);
}

void cpu_6502::cpx6502()
{
	value = get6502memory(savepc);
	if (X + 0x100 - value > 0xff) P |= F_C; else P &= ~F_C;
	value = X + 0x100 - value;
	SET_NZ(value);
}

void cpu_6502::cpy6502()
{
	value = get6502memory(savepc);
	if (Y + 0x100 - value > 0xff) P |= F_C; else P &= ~F_C;
	value = Y + 0x100 - value;
	SET_NZ(value);
}

void cpu_6502::dec6502()
{
	value = get6502memory(savepc);
	uint8_t result = value - 1;
	SET_NZ(result);
	value = result; // Evaluate if value needs to be saved and used elsewhere
	put6502memory(savepc, result);
}

void cpu_6502::dex6502()
{
	X--;
	SET_NZ(X);
}

void cpu_6502::dey6502()
{
	Y--;
	SET_NZ(Y);
}

void cpu_6502::eor6502()
{
	A ^= get6502memory(savepc);
	SET_NZ(A);
}

void cpu_6502::inc6502()
{
	value = get6502memory(savepc);
	uint8_t result = value + 1;
	SET_NZ(result);
	value = result; // Evaluate if value needs to be saved and used elsewhere
	put6502memory(savepc, result);
}

void cpu_6502::inx6502()
{
	X++;
	SET_NZ(X);
}

void cpu_6502::iny6502()
{
	Y++;
	SET_NZ(Y);
}

void cpu_6502::jmp6502()
{
	PC = savepc;
}

void cpu_6502::jsr6502()
{
	PC--;
	push16(PC);
	PC = savepc;
}

void cpu_6502::lda6502()
{
	A = get6502memory(savepc);
	SET_NZ(A);
}

void cpu_6502::ldx6502()
{
	X = get6502memory(savepc);
	SET_NZ(X);
}

void cpu_6502::ldy6502()
{
	Y = get6502memory(savepc);
	SET_NZ(Y);
}

void cpu_6502::lsr6502()
{
	value = get6502memory(savepc);
	// set carry flag if shifting right causes a bit to be lost
	P = (P & ~F_C) | (value & F_C);
	//zero and negative Flag Calculations
	value = value >> 1;
	SET_NZ(value);
	put6502memory(savepc, value);
}

void cpu_6502::lsra6502()
{
	P = (P & ~F_C) | (A & F_C);
	A = A >> 1;
	SET_NZ(A);
}

void cpu_6502::nop6502()
{
	if (opcode != 0xea) {
		wrlog("!!!!WARNING UNHANDLED NO - OP CALLED: %x CPU: %x", opcode, cpu_num);
	}
	switch (opcode) {
	case 0x1C:
	case 0x3C:
	case 0x5C:
	case 0x7C:
	case 0xDC:
	case 0xFC:
		clockticks6502++;
		break;
	}
}

void cpu_6502::ora6502()
{
	A |= get6502memory(savepc);
	SET_NZ(A);
}

void cpu_6502::pha6502()
{
	push8(A);
}

void cpu_6502::php6502()
{
	push8(P | F_B);
}

void cpu_6502::pla6502()
{
	A = pull8();
	SET_NZ(A);
}

void cpu_6502::plp6502()
{
	P = pull8() | F_T;
}

void cpu_6502::rol6502()
{
	saveflags = (P & F_C);
	value = get6502memory(savepc);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value = value << 1;
	value |= saveflags;
	put6502memory(savepc, value);
	SET_NZ(value);
}

void cpu_6502::rola6502()
{
	saveflags = (P & F_C);
	P = (P & ~F_C) | ((A >> 7) & F_C);
	A = A << 1;
	A |= saveflags;
	SET_NZ(A);
}

void cpu_6502::ror6502()
{
	saveflags = (P & F_C);
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value & F_C);
	value = value >> 1;
	if (saveflags) value |= F_N;
	put6502memory(savepc, value);
	SET_NZ(value);
}

void cpu_6502::rora6502()
{
	saveflags = (P & F_C);
	P = (P & ~F_C) | (A & F_C);
	A = A >> 1;
	if (saveflags) A |= F_N;
	SET_NZ(A);
}

void cpu_6502::rti6502()
{
	//if (_irqPending) irq6502();

	P = pull8();
	P |= F_T | F_B;
	PC = pull16();
}

void cpu_6502::rts6502()
{
	PC = pull16();
	PC++;
}

void cpu_6502::sbc6502() //Code taken from M.A.M.E (tm)
{
	int tmp = get6502memory(savepc);

	if (P & F_D)
	{
		int c = (P & F_C) ^ F_C;
		int sum = A - tmp - c;
		int lo = (A & 0x0f) - (tmp & 0x0f) - c;
		int hi = (A & 0xf0) - (tmp & 0xf0);
		if (lo & 0x10)
		{
			lo -= 6;
			hi--;
		}
		P &= ~(F_V | F_C | F_Z | F_N);
		if ((A ^ tmp) & (A ^ sum) & F_N)
			P |= F_V;
		if (hi & 0x0100)
			hi -= 0x60;
		if ((sum & 0xff00) == 0)
			P |= F_C;
		if (!((A - tmp - c) & 0xff))
			P |= F_Z;
		if ((A - tmp - c) & 0x80)
			P |= F_N;
		A = (lo & 0x0f) | (hi & 0xf0);
	}
	else
	{
		int c = (P & F_C) ^ F_C;
		int sum = A - tmp - c;
		P &= ~(F_V | F_C);
		if ((A ^ tmp) & (A ^ sum) & F_N)
			P |= F_V;
		if ((sum & 0xff00) == 0)
			P |= F_C;
		A = (uint8_t)sum;
		SET_NZ(A);
	}
}

void cpu_6502::sec6502()
{
	P |= F_C;  //Set Carry
}

void cpu_6502::sed6502()
{
	P |= F_D; //Set Decimal
}

void cpu_6502::sei6502()
{
	P |= F_I; //Set Interrupt
}

void cpu_6502::sta6502()
{
	put6502memory(savepc, A);
}

void cpu_6502::stx6502()
{
	put6502memory(savepc, X);
}

void cpu_6502::sty6502()
{
	put6502memory(savepc, Y);
}

void cpu_6502::tax6502()
{
	X = A;
	SET_NZ(X);
}

void cpu_6502::tay6502()
{
	Y = A;
	SET_NZ(Y);
}

void cpu_6502::tsx6502()
{
	X = S;
	SET_NZ(X);
}

void cpu_6502::txa6502()
{
	A = X;
	SET_NZ(A);
}

void cpu_6502::txs6502()
{
	S = X;
}

void cpu_6502::tya6502()
{
	A = Y;
	SET_NZ(A);
}
//BRA - Branch Always
void cpu_6502::bra6502()
{
	PC += savepc;
	clockticks6502++;
}
//DEC A Decrement A
void cpu_6502::dea6502()
{
	A--;
	SET_NZ(A);
}
//INC A Increment A
void cpu_6502::ina6502()
{
	A++;
	SET_NZ(A);
}

void cpu_6502::phx6502()
{
	put6502memory(0x100 + S--, X);
}

void cpu_6502::plx6502()
{
	X = pull8();
	SET_NZ(X);
}

void cpu_6502::phy6502()
{
	put6502memory(0x100 + S--, Y);
}

void cpu_6502::ply6502()
{
	Y = pull8();
	SET_NZ(Y);
}

void cpu_6502::stz6502()
{
	put6502memory(savepc, 0);
}

//65C02 OPS, need work.

void cpu_6502::isb6502()
{
	inc6502();
	sbc6502();
	//if (penaltyop && penaltyaddr) clockticks6502--;
}

void cpu_6502::tsb6502()
{
	MEM[savepc] |= A;
	SET_Z(MEM[savepc]);
}

void cpu_6502::trb6502()
{
	MEM[savepc] = MEM[savepc] & (A ^ 0xff);
	SET_Z(MEM[savepc]);
}

// Init MyCpu
void cpu_6502::init6502(uint16_t addrmaskval)
{
	int Iperiod = 0;
	int otherTicks = 0;
	PC = 0;
	PPC = 0;
	addrmask = addrmaskval;
	_irqPending = 0;
	clocktickstotal = 0;
	
	instruction[0x00] = &cpu_6502::brk6502; adrmode[0x00] = &cpu_6502::implied6502;
	instruction[0x01] = &cpu_6502::ora6502; adrmode[0x01] = &cpu_6502::indx6502;
	instruction[0x02] = &cpu_6502::nop6502; adrmode[0x02] = &cpu_6502::implied6502;
	instruction[0x03] = &cpu_6502::nop6502; adrmode[0x03] = &cpu_6502::implied6502;
	instruction[0x04] = &cpu_6502::tsb6502; adrmode[0x04] = &cpu_6502::zp6502;
	instruction[0x05] = &cpu_6502::ora6502; adrmode[0x05] = &cpu_6502::zp6502;
	instruction[0x06] = &cpu_6502::asl6502; adrmode[0x06] = &cpu_6502::zp6502;
	instruction[0x07] = &cpu_6502::nop6502; adrmode[0x07] = &cpu_6502::implied6502;
	instruction[0x08] = &cpu_6502::php6502; adrmode[0x08] = &cpu_6502::implied6502;
	instruction[0x09] = &cpu_6502::ora6502; adrmode[0x09] = &cpu_6502::immediate6502;
	instruction[0x0a] = &cpu_6502::asla6502; adrmode[0x0a] = &cpu_6502::implied6502;
	instruction[0x0b] = &cpu_6502::nop6502; adrmode[0x0b] = &cpu_6502::implied6502;
	instruction[0x0c] = &cpu_6502::tsb6502; adrmode[0x0c] = &cpu_6502::abs6502;
	instruction[0x0d] = &cpu_6502::ora6502; adrmode[0x0d] = &cpu_6502::abs6502;
	instruction[0x0e] = &cpu_6502::asl6502; adrmode[0x0e] = &cpu_6502::abs6502;
	instruction[0x0f] = &cpu_6502::nop6502; adrmode[0x0f] = &cpu_6502::implied6502;
	instruction[0x10] = &cpu_6502::bpl6502; adrmode[0x10] = &cpu_6502::relative6502;
	instruction[0x11] = &cpu_6502::ora6502; adrmode[0x11] = &cpu_6502::indy6502;
	instruction[0x12] = &cpu_6502::ora6502; adrmode[0x12] = &cpu_6502::indzp6502;
	instruction[0x13] = &cpu_6502::nop6502; adrmode[0x13] = &cpu_6502::implied6502;
	instruction[0x14] = &cpu_6502::trb6502; adrmode[0x14] = &cpu_6502::zp6502;
	instruction[0x15] = &cpu_6502::ora6502; adrmode[0x15] = &cpu_6502::zpx6502;
	instruction[0x16] = &cpu_6502::asl6502; adrmode[0x16] = &cpu_6502::zpx6502;
	instruction[0x17] = &cpu_6502::nop6502; adrmode[0x17] = &cpu_6502::implied6502;
	instruction[0x18] = &cpu_6502::clc6502; adrmode[0x18] = &cpu_6502::implied6502;
	instruction[0x19] = &cpu_6502::ora6502; adrmode[0x19] = &cpu_6502::absy6502;
	instruction[0x1a] = &cpu_6502::ina6502; adrmode[0x1a] = &cpu_6502::implied6502;
	instruction[0x1b] = &cpu_6502::nop6502; adrmode[0x1b] = &cpu_6502::implied6502;
	instruction[0x1c] = &cpu_6502::trb6502; adrmode[0x1c] = &cpu_6502::abs6502;
	instruction[0x1d] = &cpu_6502::ora6502; adrmode[0x1d] = &cpu_6502::absx6502;
	instruction[0x1e] = &cpu_6502::asl6502; adrmode[0x1e] = &cpu_6502::absx6502;
	instruction[0x1f] = &cpu_6502::nop6502; adrmode[0x1f] = &cpu_6502::implied6502;
	instruction[0x20] = &cpu_6502::jsr6502; adrmode[0x20] = &cpu_6502::abs6502;
	instruction[0x21] = &cpu_6502::and6502; adrmode[0x21] = &cpu_6502::indx6502;
	instruction[0x22] = &cpu_6502::nop6502; adrmode[0x22] = &cpu_6502::implied6502;
	instruction[0x23] = &cpu_6502::nop6502; adrmode[0x23] = &cpu_6502::implied6502;
	instruction[0x24] = &cpu_6502::bit6502; adrmode[0x24] = &cpu_6502::zp6502;
	instruction[0x25] = &cpu_6502::and6502; adrmode[0x25] = &cpu_6502::zp6502;
	instruction[0x26] = &cpu_6502::rol6502; adrmode[0x26] = &cpu_6502::zp6502;
	instruction[0x27] = &cpu_6502::nop6502; adrmode[0x27] = &cpu_6502::implied6502;
	instruction[0x28] = &cpu_6502::plp6502; adrmode[0x28] = &cpu_6502::implied6502;
	instruction[0x29] = &cpu_6502::and6502; adrmode[0x29] = &cpu_6502::immediate6502;
	instruction[0x2a] = &cpu_6502::rola6502; adrmode[0x2a] = &cpu_6502::implied6502;
	instruction[0x2b] = &cpu_6502::nop6502; adrmode[0x2b] = &cpu_6502::implied6502;
	instruction[0x2c] = &cpu_6502::bit6502; adrmode[0x2c] = &cpu_6502::abs6502;
	instruction[0x2d] = &cpu_6502::and6502; adrmode[0x2d] = &cpu_6502::abs6502;
	instruction[0x2e] = &cpu_6502::rol6502; adrmode[0x2e] = &cpu_6502::abs6502;
	instruction[0x2f] = &cpu_6502::nop6502; adrmode[0x2f] = &cpu_6502::implied6502;
	instruction[0x30] = &cpu_6502::bmi6502; adrmode[0x30] = &cpu_6502::relative6502;
	instruction[0x31] = &cpu_6502::and6502; adrmode[0x31] = &cpu_6502::indy6502;
	instruction[0x32] = &cpu_6502::and6502; adrmode[0x32] = &cpu_6502::indzp6502;
	instruction[0x33] = &cpu_6502::nop6502; adrmode[0x33] = &cpu_6502::implied6502;
	instruction[0x34] = &cpu_6502::bit6502; adrmode[0x34] = &cpu_6502::zpx6502;
	instruction[0x35] = &cpu_6502::and6502; adrmode[0x35] = &cpu_6502::zpx6502;
	instruction[0x36] = &cpu_6502::rol6502; adrmode[0x36] = &cpu_6502::zpx6502;
	instruction[0x37] = &cpu_6502::nop6502; adrmode[0x37] = &cpu_6502::implied6502;
	instruction[0x38] = &cpu_6502::sec6502; adrmode[0x38] = &cpu_6502::implied6502;
	instruction[0x39] = &cpu_6502::and6502; adrmode[0x39] = &cpu_6502::absy6502;
	instruction[0x3a] = &cpu_6502::dea6502; adrmode[0x3a] = &cpu_6502::implied6502;
	instruction[0x3b] = &cpu_6502::nop6502; adrmode[0x3b] = &cpu_6502::implied6502;
	instruction[0x3c] = &cpu_6502::bit6502; adrmode[0x3c] = &cpu_6502::absx6502;
	instruction[0x3d] = &cpu_6502::and6502; adrmode[0x3d] = &cpu_6502::absx6502;
	instruction[0x3e] = &cpu_6502::rol6502; adrmode[0x3e] = &cpu_6502::absx6502;
	instruction[0x3f] = &cpu_6502::nop6502; adrmode[0x3f] = &cpu_6502::implied6502;
	instruction[0x40] = &cpu_6502::rti6502; adrmode[0x40] = &cpu_6502::implied6502;
	instruction[0x41] = &cpu_6502::eor6502; adrmode[0x41] = &cpu_6502::indx6502;
	instruction[0x42] = &cpu_6502::nop6502; adrmode[0x42] = &cpu_6502::implied6502;
	instruction[0x43] = &cpu_6502::nop6502; adrmode[0x43] = &cpu_6502::implied6502;
	instruction[0x44] = &cpu_6502::nop6502; adrmode[0x44] = &cpu_6502::implied6502;
	instruction[0x45] = &cpu_6502::eor6502; adrmode[0x45] = &cpu_6502::zp6502;
	instruction[0x46] = &cpu_6502::lsr6502; adrmode[0x46] = &cpu_6502::zp6502;
	instruction[0x47] = &cpu_6502::nop6502; adrmode[0x47] = &cpu_6502::implied6502;
	instruction[0x48] = &cpu_6502::pha6502; adrmode[0x48] = &cpu_6502::implied6502;
	instruction[0x49] = &cpu_6502::eor6502; adrmode[0x49] = &cpu_6502::immediate6502;
	instruction[0x4a] = &cpu_6502::lsra6502; adrmode[0x4a] = &cpu_6502::implied6502;
	instruction[0x4b] = &cpu_6502::nop6502; adrmode[0x4b] = &cpu_6502::implied6502;
	instruction[0x4c] = &cpu_6502::jmp6502; adrmode[0x4c] = &cpu_6502::abs6502;
	instruction[0x4d] = &cpu_6502::eor6502; adrmode[0x4d] = &cpu_6502::abs6502;
	instruction[0x4e] = &cpu_6502::lsr6502; adrmode[0x4e] = &cpu_6502::abs6502;
	instruction[0x4f] = &cpu_6502::nop6502; adrmode[0x4f] = &cpu_6502::implied6502;
	instruction[0x50] = &cpu_6502::bvc6502; adrmode[0x50] = &cpu_6502::relative6502;
	instruction[0x51] = &cpu_6502::eor6502; adrmode[0x51] = &cpu_6502::indy6502;
	instruction[0x52] = &cpu_6502::eor6502; adrmode[0x52] = &cpu_6502::indzp6502;
	instruction[0x53] = &cpu_6502::nop6502; adrmode[0x53] = &cpu_6502::implied6502;
	instruction[0x54] = &cpu_6502::nop6502; adrmode[0x54] = &cpu_6502::implied6502;
	instruction[0x55] = &cpu_6502::eor6502; adrmode[0x55] = &cpu_6502::zpx6502;
	instruction[0x56] = &cpu_6502::lsr6502; adrmode[0x56] = &cpu_6502::zpx6502;
	instruction[0x57] = &cpu_6502::nop6502; adrmode[0x57] = &cpu_6502::implied6502;
	instruction[0x58] = &cpu_6502::cli6502; adrmode[0x58] = &cpu_6502::implied6502;
	instruction[0x59] = &cpu_6502::eor6502; adrmode[0x59] = &cpu_6502::absy6502;
	instruction[0x5a] = &cpu_6502::phy6502; adrmode[0x5a] = &cpu_6502::implied6502;
	instruction[0x5b] = &cpu_6502::nop6502; adrmode[0x5b] = &cpu_6502::implied6502;
	instruction[0x5c] = &cpu_6502::nop6502; adrmode[0x5c] = &cpu_6502::implied6502;
	instruction[0x5d] = &cpu_6502::eor6502; adrmode[0x5d] = &cpu_6502::absx6502;
	instruction[0x5e] = &cpu_6502::lsr6502; adrmode[0x5e] = &cpu_6502::absx6502;
	instruction[0x5f] = &cpu_6502::nop6502; adrmode[0x5f] = &cpu_6502::implied6502;
	instruction[0x60] = &cpu_6502::rts6502; adrmode[0x60] = &cpu_6502::implied6502;
	instruction[0x61] = &cpu_6502::adc6502; adrmode[0x61] = &cpu_6502::indx6502;
	instruction[0x62] = &cpu_6502::nop6502; adrmode[0x62] = &cpu_6502::implied6502;
	instruction[0x63] = &cpu_6502::nop6502; adrmode[0x63] = &cpu_6502::implied6502;
	instruction[0x64] = &cpu_6502::stz6502; adrmode[0x64] = &cpu_6502::zp6502;
	instruction[0x65] = &cpu_6502::adc6502; adrmode[0x65] = &cpu_6502::zp6502;
	instruction[0x66] = &cpu_6502::ror6502; adrmode[0x66] = &cpu_6502::zp6502;
	instruction[0x67] = &cpu_6502::nop6502; adrmode[0x67] = &cpu_6502::implied6502;
	instruction[0x68] = &cpu_6502::pla6502; adrmode[0x68] = &cpu_6502::implied6502;
	instruction[0x69] = &cpu_6502::adc6502; adrmode[0x69] = &cpu_6502::immediate6502;
	instruction[0x6a] = &cpu_6502::rora6502; adrmode[0x6a] = &cpu_6502::implied6502;
	instruction[0x6b] = &cpu_6502::nop6502; adrmode[0x6b] = &cpu_6502::implied6502;
	instruction[0x6c] = &cpu_6502::jmp6502; adrmode[0x6c] = &cpu_6502::indirect6502;
	instruction[0x6d] = &cpu_6502::adc6502; adrmode[0x6d] = &cpu_6502::abs6502;
	instruction[0x6e] = &cpu_6502::ror6502; adrmode[0x6e] = &cpu_6502::abs6502;
	instruction[0x6f] = &cpu_6502::nop6502; adrmode[0x6f] = &cpu_6502::implied6502;
	instruction[0x70] = &cpu_6502::bvs6502; adrmode[0x70] = &cpu_6502::relative6502;
	instruction[0x71] = &cpu_6502::adc6502; adrmode[0x71] = &cpu_6502::indy6502;
	instruction[0x72] = &cpu_6502::adc6502; adrmode[0x72] = &cpu_6502::indzp6502;
	instruction[0x73] = &cpu_6502::nop6502; adrmode[0x73] = &cpu_6502::implied6502;
	instruction[0x74] = &cpu_6502::stz6502; adrmode[0x74] = &cpu_6502::zpx6502;
	instruction[0x75] = &cpu_6502::adc6502; adrmode[0x75] = &cpu_6502::zpx6502;
	instruction[0x76] = &cpu_6502::ror6502; adrmode[0x76] = &cpu_6502::zpx6502;
	instruction[0x77] = &cpu_6502::nop6502; adrmode[0x77] = &cpu_6502::implied6502;
	instruction[0x78] = &cpu_6502::sei6502; adrmode[0x78] = &cpu_6502::implied6502;
	instruction[0x79] = &cpu_6502::adc6502; adrmode[0x79] = &cpu_6502::absy6502;
	instruction[0x7a] = &cpu_6502::ply6502; adrmode[0x7a] = &cpu_6502::implied6502;
	instruction[0x7b] = &cpu_6502::nop6502; adrmode[0x7b] = &cpu_6502::implied6502;
	instruction[0x7c] = &cpu_6502::jmp6502; adrmode[0x7c] = &cpu_6502::indabsx6502;
	instruction[0x7d] = &cpu_6502::adc6502; adrmode[0x7d] = &cpu_6502::absx6502;
	instruction[0x7e] = &cpu_6502::ror6502; adrmode[0x7e] = &cpu_6502::absx6502;
	instruction[0x7f] = &cpu_6502::nop6502; adrmode[0x7f] = &cpu_6502::implied6502;
	instruction[0x80] = &cpu_6502::bra6502; adrmode[0x80] = &cpu_6502::relative6502;
	instruction[0x81] = &cpu_6502::sta6502; adrmode[0x81] = &cpu_6502::indx6502;
	instruction[0x82] = &cpu_6502::nop6502; adrmode[0x82] = &cpu_6502::implied6502;
	instruction[0x83] = &cpu_6502::nop6502; adrmode[0x83] = &cpu_6502::implied6502;
	instruction[0x84] = &cpu_6502::sty6502; adrmode[0x84] = &cpu_6502::zp6502;
	instruction[0x85] = &cpu_6502::sta6502; adrmode[0x85] = &cpu_6502::zp6502;
	instruction[0x86] = &cpu_6502::stx6502; adrmode[0x86] = &cpu_6502::zp6502;
	instruction[0x87] = &cpu_6502::nop6502; adrmode[0x87] = &cpu_6502::implied6502;
	instruction[0x88] = &cpu_6502::dey6502; adrmode[0x88] = &cpu_6502::implied6502;
	instruction[0x89] = &cpu_6502::bit6502; adrmode[0x89] = &cpu_6502::immediate6502;
	instruction[0x8a] = &cpu_6502::txa6502; adrmode[0x8a] = &cpu_6502::implied6502;
	instruction[0x8b] = &cpu_6502::nop6502; adrmode[0x8b] = &cpu_6502::implied6502;
	instruction[0x8c] = &cpu_6502::sty6502; adrmode[0x8c] = &cpu_6502::abs6502;
	instruction[0x8d] = &cpu_6502::sta6502; adrmode[0x8d] = &cpu_6502::abs6502;
	instruction[0x8e] = &cpu_6502::stx6502; adrmode[0x8e] = &cpu_6502::abs6502;
	instruction[0x8f] = &cpu_6502::nop6502; adrmode[0x8f] = &cpu_6502::implied6502;
	instruction[0x90] = &cpu_6502::bcc6502; adrmode[0x90] = &cpu_6502::relative6502;
	instruction[0x91] = &cpu_6502::sta6502; adrmode[0x91] = &cpu_6502::indy6502;
	instruction[0x92] = &cpu_6502::sta6502; adrmode[0x92] = &cpu_6502::indzp6502;
	instruction[0x93] = &cpu_6502::nop6502; adrmode[0x93] = &cpu_6502::implied6502;
	instruction[0x94] = &cpu_6502::sty6502; adrmode[0x94] = &cpu_6502::zpx6502;
	instruction[0x95] = &cpu_6502::sta6502; adrmode[0x95] = &cpu_6502::zpx6502;
	instruction[0x96] = &cpu_6502::stx6502; adrmode[0x96] = &cpu_6502::zpy6502;
	instruction[0x97] = &cpu_6502::nop6502; adrmode[0x97] = &cpu_6502::implied6502;
	instruction[0x98] = &cpu_6502::tya6502; adrmode[0x98] = &cpu_6502::implied6502;
	instruction[0x99] = &cpu_6502::sta6502; adrmode[0x99] = &cpu_6502::absy6502;
	instruction[0x9a] = &cpu_6502::txs6502; adrmode[0x9a] = &cpu_6502::implied6502;
	instruction[0x9b] = &cpu_6502::nop6502; adrmode[0x9b] = &cpu_6502::implied6502;
	instruction[0x9c] = &cpu_6502::stz6502; adrmode[0x9c] = &cpu_6502::abs6502;
	instruction[0x9d] = &cpu_6502::sta6502; adrmode[0x9d] = &cpu_6502::absx6502;
	instruction[0x9e] = &cpu_6502::stz6502; adrmode[0x9e] = &cpu_6502::absx6502;
	instruction[0x9f] = &cpu_6502::nop6502; adrmode[0x9f] = &cpu_6502::implied6502;
	instruction[0xa0] = &cpu_6502::ldy6502; adrmode[0xa0] = &cpu_6502::immediate6502;
	instruction[0xa1] = &cpu_6502::lda6502; adrmode[0xa1] = &cpu_6502::indx6502;
	instruction[0xa2] = &cpu_6502::ldx6502; adrmode[0xa2] = &cpu_6502::immediate6502;
	instruction[0xa3] = &cpu_6502::nop6502; adrmode[0xa3] = &cpu_6502::implied6502;
	instruction[0xa4] = &cpu_6502::ldy6502; adrmode[0xa4] = &cpu_6502::zp6502;
	instruction[0xa5] = &cpu_6502::lda6502; adrmode[0xa5] = &cpu_6502::zp6502;
	instruction[0xa6] = &cpu_6502::ldx6502; adrmode[0xa6] = &cpu_6502::zp6502;
	instruction[0xa7] = &cpu_6502::nop6502; adrmode[0xa7] = &cpu_6502::implied6502;
	instruction[0xa8] = &cpu_6502::tay6502; adrmode[0xa8] = &cpu_6502::implied6502;
	instruction[0xa9] = &cpu_6502::lda6502; adrmode[0xa9] = &cpu_6502::immediate6502;
	instruction[0xaa] = &cpu_6502::tax6502; adrmode[0xaa] = &cpu_6502::implied6502;
	instruction[0xab] = &cpu_6502::nop6502; adrmode[0xab] = &cpu_6502::implied6502;
	instruction[0xac] = &cpu_6502::ldy6502; adrmode[0xac] = &cpu_6502::abs6502;
	instruction[0xad] = &cpu_6502::lda6502; adrmode[0xad] = &cpu_6502::abs6502;
	instruction[0xae] = &cpu_6502::ldx6502; adrmode[0xae] = &cpu_6502::abs6502;
	instruction[0xaf] = &cpu_6502::nop6502; adrmode[0xaf] = &cpu_6502::implied6502;
	instruction[0xb0] = &cpu_6502::bcs6502; adrmode[0xb0] = &cpu_6502::relative6502;
	instruction[0xb1] = &cpu_6502::lda6502; adrmode[0xb1] = &cpu_6502::indy6502;
	instruction[0xb2] = &cpu_6502::nop6502; adrmode[0xb2] = &cpu_6502::implied6502;
	instruction[0xb3] = &cpu_6502::nop6502; adrmode[0xb3] = &cpu_6502::implied6502;
	instruction[0xb4] = &cpu_6502::ldy6502; adrmode[0xb4] = &cpu_6502::zpx6502;
	instruction[0xb5] = &cpu_6502::lda6502; adrmode[0xb5] = &cpu_6502::zpx6502;
	instruction[0xb6] = &cpu_6502::ldx6502; adrmode[0xb6] = &cpu_6502::zpy6502;
	instruction[0xb7] = &cpu_6502::nop6502; adrmode[0xb7] = &cpu_6502::implied6502;
	instruction[0xb8] = &cpu_6502::clv6502; adrmode[0xb8] = &cpu_6502::implied6502;
	instruction[0xb9] = &cpu_6502::lda6502; adrmode[0xb9] = &cpu_6502::absy6502;
	instruction[0xba] = &cpu_6502::tsx6502; adrmode[0xba] = &cpu_6502::implied6502;
	instruction[0xbb] = &cpu_6502::nop6502; adrmode[0xbb] = &cpu_6502::implied6502;
	instruction[0xbc] = &cpu_6502::ldy6502; adrmode[0xbc] = &cpu_6502::absx6502;
	instruction[0xbd] = &cpu_6502::lda6502; adrmode[0xbd] = &cpu_6502::absx6502;
	instruction[0xbe] = &cpu_6502::ldx6502; adrmode[0xbe] = &cpu_6502::absy6502;
	instruction[0xbf] = &cpu_6502::nop6502; adrmode[0xbf] = &cpu_6502::implied6502;
	instruction[0xc0] = &cpu_6502::cpy6502; adrmode[0xc0] = &cpu_6502::immediate6502;
	instruction[0xc1] = &cpu_6502::cmp6502; adrmode[0xc1] = &cpu_6502::indx6502;
	instruction[0xc2] = &cpu_6502::nop6502; adrmode[0xc2] = &cpu_6502::implied6502;
	instruction[0xc3] = &cpu_6502::nop6502; adrmode[0xc3] = &cpu_6502::implied6502;
	instruction[0xc4] = &cpu_6502::cpy6502; adrmode[0xc4] = &cpu_6502::zp6502;
	instruction[0xc5] = &cpu_6502::cmp6502; adrmode[0xc5] = &cpu_6502::zp6502;
	instruction[0xc6] = &cpu_6502::dec6502; adrmode[0xc6] = &cpu_6502::zp6502;
	instruction[0xc7] = &cpu_6502::nop6502; adrmode[0xc7] = &cpu_6502::implied6502;
	instruction[0xc8] = &cpu_6502::iny6502; adrmode[0xc8] = &cpu_6502::implied6502;
	instruction[0xc9] = &cpu_6502::cmp6502; adrmode[0xc9] = &cpu_6502::immediate6502;
	instruction[0xca] = &cpu_6502::dex6502; adrmode[0xca] = &cpu_6502::implied6502;
	instruction[0xcb] = &cpu_6502::nop6502; adrmode[0xcb] = &cpu_6502::implied6502;
	instruction[0xcc] = &cpu_6502::cpy6502; adrmode[0xcc] = &cpu_6502::abs6502;
	instruction[0xcd] = &cpu_6502::cmp6502; adrmode[0xcd] = &cpu_6502::abs6502;
	instruction[0xce] = &cpu_6502::dec6502; adrmode[0xce] = &cpu_6502::abs6502;
	instruction[0xcf] = &cpu_6502::nop6502; adrmode[0xcf] = &cpu_6502::implied6502;
	instruction[0xd0] = &cpu_6502::bne6502; adrmode[0xd0] = &cpu_6502::relative6502;
	instruction[0xd1] = &cpu_6502::cmp6502; adrmode[0xd1] = &cpu_6502::indy6502;
	instruction[0xd2] = &cpu_6502::cmp6502; adrmode[0xd2] = &cpu_6502::indzp6502;
	instruction[0xd3] = &cpu_6502::nop6502; adrmode[0xd3] = &cpu_6502::implied6502;
	instruction[0xd4] = &cpu_6502::nop6502; adrmode[0xd4] = &cpu_6502::implied6502;
	instruction[0xd5] = &cpu_6502::cmp6502; adrmode[0xd5] = &cpu_6502::zpx6502;
	instruction[0xd6] = &cpu_6502::dec6502; adrmode[0xd6] = &cpu_6502::zpx6502;
	instruction[0xd7] = &cpu_6502::nop6502; adrmode[0xd7] = &cpu_6502::implied6502;
	instruction[0xd8] = &cpu_6502::cld6502; adrmode[0xd8] = &cpu_6502::implied6502;
	instruction[0xd9] = &cpu_6502::cmp6502; adrmode[0xd9] = &cpu_6502::absy6502;
	instruction[0xda] = &cpu_6502::phx6502; adrmode[0xda] = &cpu_6502::implied6502;
	instruction[0xdb] = &cpu_6502::nop6502; adrmode[0xdb] = &cpu_6502::implied6502;
	instruction[0xdc] = &cpu_6502::nop6502; adrmode[0xdc] = &cpu_6502::implied6502;
	instruction[0xdd] = &cpu_6502::cmp6502; adrmode[0xdd] = &cpu_6502::absx6502;
	instruction[0xde] = &cpu_6502::dec6502; adrmode[0xde] = &cpu_6502::absx6502;
	instruction[0xdf] = &cpu_6502::nop6502; adrmode[0xdf] = &cpu_6502::implied6502;
	instruction[0xe0] = &cpu_6502::cpx6502; adrmode[0xe0] = &cpu_6502::immediate6502;
	instruction[0xe1] = &cpu_6502::sbc6502; adrmode[0xe1] = &cpu_6502::indx6502;
	instruction[0xe2] = &cpu_6502::nop6502; adrmode[0xe2] = &cpu_6502::implied6502;
	instruction[0xe3] = &cpu_6502::isb6502; adrmode[0xe3] = &cpu_6502::indx6502;
	instruction[0xe4] = &cpu_6502::cpx6502; adrmode[0xe4] = &cpu_6502::zp6502;
	instruction[0xe5] = &cpu_6502::sbc6502; adrmode[0xe5] = &cpu_6502::zp6502;
	instruction[0xe6] = &cpu_6502::inc6502; adrmode[0xe6] = &cpu_6502::zp6502;
	instruction[0xe7] = &cpu_6502::isb6502; adrmode[0xe7] = &cpu_6502::zp6502;
	instruction[0xe8] = &cpu_6502::inx6502; adrmode[0xe8] = &cpu_6502::implied6502;
	instruction[0xe9] = &cpu_6502::sbc6502; adrmode[0xe9] = &cpu_6502::immediate6502;
	instruction[0xea] = &cpu_6502::nop6502; adrmode[0xea] = &cpu_6502::implied6502;  //Real NOP
	instruction[0xeb] = &cpu_6502::nop6502; adrmode[0xeb] = &cpu_6502::implied6502;
	instruction[0xec] = &cpu_6502::cpx6502; adrmode[0xec] = &cpu_6502::abs6502;
	instruction[0xed] = &cpu_6502::sbc6502; adrmode[0xed] = &cpu_6502::abs6502;
	instruction[0xee] = &cpu_6502::inc6502; adrmode[0xee] = &cpu_6502::abs6502;
	instruction[0xef] = &cpu_6502::isb6502; adrmode[0xef] = &cpu_6502::abs6502;
	instruction[0xf0] = &cpu_6502::beq6502; adrmode[0xf0] = &cpu_6502::relative6502;
	instruction[0xf1] = &cpu_6502::sbc6502; adrmode[0xf1] = &cpu_6502::indy6502;
	instruction[0xf2] = &cpu_6502::sbc6502; adrmode[0xf2] = &cpu_6502::indzp6502;
	instruction[0xf3] = &cpu_6502::isb6502; adrmode[0xf3] = &cpu_6502::indy6502;
	instruction[0xf4] = &cpu_6502::nop6502; adrmode[0xf4] = &cpu_6502::implied6502;
	instruction[0xf5] = &cpu_6502::sbc6502; adrmode[0xf5] = &cpu_6502::zpx6502;
	instruction[0xf6] = &cpu_6502::inc6502; adrmode[0xf6] = &cpu_6502::zpx6502;
	instruction[0xf7] = &cpu_6502::isb6502; adrmode[0xf7] = &cpu_6502::zpx6502;
	instruction[0xf8] = &cpu_6502::sed6502; adrmode[0xf8] = &cpu_6502::implied6502;
	instruction[0xf9] = &cpu_6502::sbc6502; adrmode[0xf9] = &cpu_6502::absy6502;
	instruction[0xfa] = &cpu_6502::plx6502; adrmode[0xfa] = &cpu_6502::implied6502;
	instruction[0xfb] = &cpu_6502::isb6502; adrmode[0xfb] = &cpu_6502::absy6502;
	instruction[0xfc] = &cpu_6502::nop6502; adrmode[0xfc] = &cpu_6502::absx6502;
	instruction[0xfd] = &cpu_6502::sbc6502; adrmode[0xfd] = &cpu_6502::absx6502;
	instruction[0xfe] = &cpu_6502::inc6502; adrmode[0xfe] = &cpu_6502::absx6502;
	instruction[0xff] = &cpu_6502::isb6502; adrmode[0xff] = &cpu_6502::absx6502;
}
//Register Operations, need refinement
uint8_t cpu_6502::m6502_get_reg(int regnum)
{
	switch (regnum)
	{
	case M6502_S: return S;
	case M6502_P: return P;
	case M6502_A: return A;
	case M6502_X: return X;
	case M6502_Y: return Y;
	}
	return 0;
}

void cpu_6502::m6502_set_reg(int regnum, uint8_t val)
{
	switch (regnum)
	{
	case M6502_S: S = val; break;
	case M6502_P: P = val; break;
	case M6502_A: A = val; break;
	case M6502_X: X = val; break;
	case M6502_Y: Y = val; break;
	default:break;
	}
}

uint16_t cpu_6502::get_pc()
{
	return PC;
}

uint16_t cpu_6502::get_ppc()
{
	return PPC;
}

void cpu_6502::set_pc(uint16_t pc)
{
	PC = pc;
}

void cpu_6502::m6502clearpendingint()
{
	if (_irqPending) _irqPending = 0;
}

// Reset MyCpu
void cpu_6502::reset6502()
{
	A = 0;
	X = 0;
	Y = 0;
	P |= F_T | F_I | F_Z;
	_irqPending = 0;
	S = 0xff;
	PC = get6502memory(0xFFFC & addrmask);
	PC |= get6502memory(0xFFFD & addrmask) << 8;
	if (debug) { wrlog("reset: PC is %X", PC); }
	clockticks6502 += 6;
}

// Non maskerable interrupt
void cpu_6502::nmi6502()
{
	push16(PC);
	push8(P & ~F_B);
	P |= F_I;		// set I flag
	PC = get6502memory(0xFFFA & addrmask);
	PC |= get6502memory(0xFFFB & addrmask) << 8;
	clockticks6502 += 7;
	clocktickstotal += 7;
}

// Maskerable Interrupt
void cpu_6502::irq6502()
{
	_irqPending = 1; //Set IRQ Pending.

	if (!(P & F_I))
	{
		//wrlog("6502 IRQ Taken on CPU %d", cpu_num);
		push16(PC);
		push8(P & ~F_B);
		P |= F_I;		// set I flag
		PC = get6502memory(0xFFFE & addrmask);
		PC |= get6502memory(0xFFFF & addrmask) << 8;
		_irqPending = 0;
		clockticks6502 += 7;
		clocktickstotal += 7;
	}
}

// Execute a Single Instruction.
int cpu_6502::step6502()
{
	// Set the current number of cycles to zero.
	clockticks6502 = 0;
	// Make sure the flag is set.
	P |= F_T;
	// If there is an IRQ pending, take it.
	if (_irqPending) irq6502();
	// fetch instruction
	opcode = get6502memory(PC++);
	// Trap any Opcode errors.
	if (opcode > 0xff)
	{
		wrlog("Invalid Opcode called!!!: opcode %x  Memory %X ", opcode, PC);
		return 0x00000000;
	}
	// Debug Logging if needed. This code needs lots of help.
	debug = 0;
	if (debug)
	{
		std::string op = disam(opcode);

		int c = bget(P, F_C) ? 1 : 0;
		int z = bget(P, F_Z) ? 1 : 0;
		int i = bget(P, F_I) ? 1 : 0;
		int d = bget(P, F_D) ? 1 : 0;
		int b = bget(P, F_B) ? 1 : 0;
		int t = bget(P, F_T) ? 1 : 0;
		int v = bget(P, F_V) ? 1 : 0;
		int n = bget(P, F_N) ? 1 : 0;
		wrlog("%x: OP:%s DATA:%02x%02x F: C:%d Z:%d I:%d D:%d B:%d V:%d N:%d REG A:%x X:%x Y:%x S:%x ", PC - 1, op.c_str(), MEM[PC - 1], MEM[PC], c, z, i, d, b, v, n, A, X, Y, S);
	}
	//Backup PC
	PPC = PC;
	//set addressing mode
	(this->*(adrmode[opcode]))();
	// execute instruction
	(this->*(instruction[opcode]))();
	// update clock cycles
	clockticks6502 += ticks[opcode];
	// Update the running counter for clock cycles executed.
	clocktickstotal += clockticks6502;
	// Update Timer if being used
	timer_update(clockticks6502, cpu_num);
	// Keep the total # of ticks from exceeding the limit.
	if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
	return clockticks6502;
}

// Execute Multiple Instructions
int cpu_6502::exec6502(int timerTicks)
{
	int  cyc = 0;

	while (cyc < timerTicks) 
	{
		cyc += step6502();
	}

	return 0x80000000;
}

std::string cpu_6502::disam(uint8_t opcode)
{
	char opstr[64];

	std::string rval;

	switch (opcode)
	{
	case 0x00: sprintf_s(opstr, "BRK"); break;
	case 0x01: sprintf_s(opstr, "ORA ($%02x,X)", MEM[PC]);  break;
	case 0x04: sprintf_s(opstr, "TSB"); break;
	case 0x05: sprintf_s(opstr, "ORA $%02x", MEM[PC]);  break;
	case 0x06: sprintf_s(opstr, "ASL $%02x", MEM[PC]);  break;
	case 0x08: sprintf_s(opstr, "PHP"); break;
	case 0x09: sprintf_s(opstr, "ORA #$%02x", MEM[PC]);  break;
	case 0x0a: sprintf_s(opstr, "ASL A"); break;
	case 0x0d: sprintf_s(opstr, "ORA $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x0e: sprintf_s(opstr, "ASL $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x10: sprintf_s(opstr, "BPL $%02x", MEM[PC]);  break;
	case 0x11: sprintf_s(opstr, "ORA ($%02x),Y", MEM[PC]);  break;
	case 0x15: sprintf_s(opstr, "ORA $%02x,X", MEM[PC]);  break;
	case 0x16: sprintf_s(opstr, "ASL $%02x,X", MEM[PC]);  break;
	case 0x18: sprintf_s(opstr, "CLC"); break;
	case 0x19: sprintf_s(opstr, "ORA $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0x1d: sprintf_s(opstr, "ORA $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x1e: sprintf_s(opstr, "ASL $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x20: sprintf_s(opstr, "JSR $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x21: sprintf_s(opstr, "AND ($%02x,X)", MEM[PC]);  break;
	case 0x24: sprintf_s(opstr, "BIT $%02x", MEM[PC]);  break;
	case 0x25: sprintf_s(opstr, "AND $%02x", MEM[PC]);  break;
	case 0x26: sprintf_s(opstr, "ROL $%02x", MEM[PC]);  break;
	case 0x28: sprintf_s(opstr, "PLP"); break;
	case 0x29: sprintf_s(opstr, "AND #$%02x", MEM[PC]);  break;
	case 0x2a: sprintf_s(opstr, "ROL A"); break;
	case 0x2c: sprintf_s(opstr, "BIT $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x2d: sprintf_s(opstr, "AND $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x2e: sprintf_s(opstr, "ROL $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x30: sprintf_s(opstr, "BMI $%02x", MEM[PC]);  break;
	case 0x31: sprintf_s(opstr, "AND ($%02x),Y", MEM[PC]);  break;
	case 0x35: sprintf_s(opstr, "AND $%02x,X", MEM[PC]);  break;
	case 0x36: sprintf_s(opstr, "ROL $%02x,X", MEM[PC]);  break;
	case 0x38: sprintf_s(opstr, "SEC"); break;
	case 0x39: sprintf_s(opstr, "AND $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0x3d: sprintf_s(opstr, "AND $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x3e: sprintf_s(opstr, "ROL $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x40: sprintf_s(opstr, "RTI"); break;
	case 0x41: sprintf_s(opstr, "EOR ($%02x,X)", MEM[PC]);  break;
	case 0x45: sprintf_s(opstr, "EOR $%02x", MEM[PC]);  break;
	case 0x46: sprintf_s(opstr, "LSR $%02x", MEM[PC]);  break;
	case 0x48: sprintf_s(opstr, "PHA"); break;
	case 0x49: sprintf_s(opstr, "EOR #$%02x", MEM[PC]);  break;
	case 0x4a: sprintf_s(opstr, "LSR A"); break;
	case 0x4c: sprintf_s(opstr, "JMP $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x4d: sprintf_s(opstr, "EOR $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x4e: sprintf_s(opstr, "LSR $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x50: sprintf_s(opstr, "BVC $%02x", MEM[PC]);  break;
	case 0x51: sprintf_s(opstr, "EOR ($%02x),Y", MEM[PC]);  break;
	case 0x55: sprintf_s(opstr, "EOR $%02x,X", MEM[PC]);  break;
	case 0x56: sprintf_s(opstr, "LSR $%02x,X", MEM[PC]);  break;
	case 0x58: sprintf_s(opstr, "CLI"); break;
	case 0x59: sprintf_s(opstr, "EOR $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0x5d: sprintf_s(opstr, "EOR $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x5e: sprintf_s(opstr, "LSR $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x60: sprintf_s(opstr, "RTS"); break;
	case 0x61: sprintf_s(opstr, "ADC ($%02x,X)", MEM[PC]);  break;
	case 0x65: sprintf_s(opstr, "ADC $%02x", MEM[PC]);  break;
	case 0x66: sprintf_s(opstr, "ROR $%02x", MEM[PC]);  break;
	case 0x68: sprintf_s(opstr, "PLA"); break;
	case 0x69: sprintf_s(opstr, "ADC #$%02x", MEM[PC]);  break;
	case 0x6a: sprintf_s(opstr, "ROR A"); break;
	case 0x6c: sprintf_s(opstr, "JMP ($%02x%02x)", MEM[PC + 1], MEM[PC]); break;
	case 0x6d: sprintf_s(opstr, "ADC $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x6e: sprintf_s(opstr, "ROR $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x70: sprintf_s(opstr, "BVS $%02x", MEM[PC]);  break;
	case 0x71: sprintf_s(opstr, "ADC ($%02x),Y", MEM[PC]);  break;
	case 0x75: sprintf_s(opstr, "ADC $%02x,X", MEM[PC]);  break;
	case 0x76: sprintf_s(opstr, "ROR $%02x,X", MEM[PC]);  break;
	case 0x78: sprintf_s(opstr, "SEI"); break;
	case 0x79: sprintf_s(opstr, "ADC $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0x7d: sprintf_s(opstr, "ADC $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0x7e: sprintf_s(opstr, "ROR $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x81: sprintf_s(opstr, "STA ($%02x,X)", MEM[PC]);  break;
	case 0x84: sprintf_s(opstr, "STY $%02x", MEM[PC]);  break;
	case 0x85: sprintf_s(opstr, "STA $%02x", MEM[PC]);  break;
	case 0x86: sprintf_s(opstr, "STX $%02x", MEM[PC]);  break;
	case 0x88: sprintf_s(opstr, "DEY"); break;
	case 0x8a: sprintf_s(opstr, "TXA"); break;
	case 0x8c: sprintf_s(opstr, "STY $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x8d: sprintf_s(opstr, "STA $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x8e: sprintf_s(opstr, "STX $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0x90: sprintf_s(opstr, "BCC $%04x", MEM[PC]);  break; //PLUS SAVEpc MEM[PC]
	case 0x91: sprintf_s(opstr, "STA ($%02x),Y", MEM[PC]);  break;
	case 0x94: sprintf_s(opstr, "STY $%02x,X", MEM[PC]);  break;
	case 0x95: sprintf_s(opstr, "STA $%02x,X", MEM[PC]);  break;
	case 0x96: sprintf_s(opstr, "STX $%02x,Y", MEM[PC]);  break;
	case 0x98: sprintf_s(opstr, "TYA"); break;
	case 0x99: sprintf_s(opstr, "STA $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0x9a: sprintf_s(opstr, "TXS"); break;
	case 0x9d: sprintf_s(opstr, "STA $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xa0: sprintf_s(opstr, "LDY #$%02x", MEM[PC]);  break;
	case 0xa1: sprintf_s(opstr, "LDA ($%02x,X)", MEM[PC]);  break;
	case 0xa2: sprintf_s(opstr, "LDX #$%02x", MEM[PC]);  break;
	case 0xa4: sprintf_s(opstr, "LDY $%02x", MEM[PC]);  break;
	case 0xa5: sprintf_s(opstr, "LDA $%02x", MEM[PC]);  break;
	case 0xa6: sprintf_s(opstr, "LDX $%02x", MEM[PC]);  break;
	case 0xa8: sprintf_s(opstr, "TAY"); break;
	case 0xa9: sprintf_s(opstr, "LDA #$%02x", MEM[PC]);  break;
	case 0xaa: sprintf_s(opstr, "TAX"); break;
	case 0xac: sprintf_s(opstr, "LDY $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xad: sprintf_s(opstr, "LDA $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xae: sprintf_s(opstr, "LDX $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xb0: sprintf_s(opstr, "BCS $%02x", MEM[PC]);  break;
	case 0xb1: sprintf_s(opstr, "LDA ($%02x),Y", MEM[PC]);  break;
	case 0xb4: sprintf_s(opstr, "LDY $%02x,X", MEM[PC]);  break;
	case 0xb5: sprintf_s(opstr, "LDA $%02x,X", MEM[PC]);  break;
	case 0xb6: sprintf_s(opstr, "LDX $%02x,Y", MEM[PC]);  break;
	case 0xb8: sprintf_s(opstr, "CLV"); break;
	case 0xb9: sprintf_s(opstr, "LDA $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0xba: sprintf_s(opstr, "TSX"); break;
	case 0xbc: sprintf_s(opstr, "LDY $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xbd: sprintf_s(opstr, "LDA $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xbe: sprintf_s(opstr, "LDX $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0xc0: sprintf_s(opstr, "CPY #$%02x", MEM[PC]);  break;
	case 0xc1: sprintf_s(opstr, "CMP ($%02x,X)", MEM[PC]);  break;
	case 0xc4: sprintf_s(opstr, "CPY $%02x", MEM[PC]);  break;
	case 0xc5: sprintf_s(opstr, "CMP $%02x", MEM[PC]);  break;
	case 0xc6: sprintf_s(opstr, "DEC $%02x", MEM[PC]);  break;
	case 0xc8: sprintf_s(opstr, "INY"); break;
	case 0xc9: sprintf_s(opstr, "CMP #$%02x", MEM[PC]);  break;
	case 0xca: sprintf_s(opstr, "DEX"); break;
	case 0xcc: sprintf_s(opstr, "CPY $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xcd: sprintf_s(opstr, "CMP $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xce: sprintf_s(opstr, "DEC $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xd0: sprintf_s(opstr, "BNE $%02x%02x", MEM[PC + 2], MEM[PC + 1]);  break;
	case 0xd1: sprintf_s(opstr, "CMP ($%02x),Y", MEM[PC]);  break;
	case 0xd5: sprintf_s(opstr, "CMP $%02x,X", MEM[PC]);  break;
	case 0xd6: sprintf_s(opstr, "DEC $%02x,X", MEM[PC]);  break;
	case 0xd8: sprintf_s(opstr, "CLD"); break;
	case 0xd9: sprintf_s(opstr, "CMP $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0xdd: sprintf_s(opstr, "CMP $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xde: sprintf_s(opstr, "DEC $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xe0: sprintf_s(opstr, "CPX #$%02x", MEM[PC]);  break;
	case 0xe1: sprintf_s(opstr, "SBC ($%02x,X)", MEM[PC]);  break;
	case 0xe4: sprintf_s(opstr, "CPX $%02x", MEM[PC]);  break;
	case 0xe5: sprintf_s(opstr, "SBC $%02x", MEM[PC]);  break;
	case 0xe6: sprintf_s(opstr, "INC $%02x", MEM[PC]);  break;
	case 0xe8: sprintf_s(opstr, "INX"); break;
	case 0xe9: sprintf_s(opstr, "SBC #$%02x", MEM[PC]);  break;
	case 0xea: sprintf_s(opstr, "NOP"); break;
	case 0xec: sprintf_s(opstr, "CPX $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xed: sprintf_s(opstr, "SBC $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xee: sprintf_s(opstr, "INC $%02x%02x", MEM[PC + 1], MEM[PC]); break;
	case 0xf0: sprintf_s(opstr, "BEQ $%02x", MEM[PC]);  break;
	case 0xf1: sprintf_s(opstr, "SBC ($%02x),Y", MEM[PC]);  break;
	case 0xf5: sprintf_s(opstr, "SBC $%02x,X", MEM[PC]);  break;
	case 0xf6: sprintf_s(opstr, "INC $%02x,X", MEM[PC]);  break;
	case 0xf8: sprintf_s(opstr, "SED"); break;
	case 0xf9: sprintf_s(opstr, "SBC $%02x%02x,Y", MEM[PC + 1], MEM[PC]); break;
	case 0xfd: sprintf_s(opstr, "SBC $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;
	case 0xfe: sprintf_s(opstr, "INC $%02x%02x,X", MEM[PC + 1], MEM[PC]); break;

	default: sprintf_s(opstr, "UNKNOWN 6502 OPCODE: %x", opcode); break;
	}
	rval = opstr;

	return rval;
}