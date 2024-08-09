// 6502 CPU Core, based on code by Neil Bradley, with many changes from sources across the web.
// ADC and SBC code from M.A.M.E. (tm)  Copyright (c) 1998,1999,2000, etc Juergen Buchmueller
// Timing table and debug code from FakeNes
// Code rewritten to c++ and updated by TC 2015-2024 explicitly written to work with with older M.A.M.E. source code for my testing.
// Currently does not handle IRQ after CLI correctly.
// TODO: fix issues with incorrect operand display in the debug code.
// Code has been verified to run multi-cpu correctly with Major Havoc.

//This code is freeware.
//Version 2024.7.14

#ifndef _6502_H_
#define _6502_H_

#pragma once

/*
 Only Required for testing with antique Allegro code.
#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef intptr_t
#undef uintptr_t
#undef int64_t
#undef uint64_t
 */

#include <cstdint>
#include <string>
#include "..\\deftypes.h"

class cpu_6502
{

public:

	//Register Enums
	enum
	{
		M6502_A = 0x01,
		M6502_X = 0x02,
		M6502_Y = 0x04,
		M6502_P = 0x08,
		M6502_S = 0x10,
	};

	// Pointer to the games 32 bit memory map
	uint8_t *MEM = nullptr;
	//Pointer to the handler structures
	MemoryReadByte  *memory_read = nullptr;
	MemoryWriteByte *memory_write = nullptr;

	//Constructors
	cpu_6502(uint8_t *mem, MemoryReadByte *read_mem, MemoryWriteByte *write_mem, uint16_t addr, int num);
	
	//Destructor
	~cpu_6502() {};

	// must be called first to initialise the 6502 instance, this is called in the primary constructor
	void init6502(uint16_t addrmaskval);
	//Irq Handler
	void irq6502();
	//Int Handler
	void nmi6502();
	// Sets all of the 6502 registers. 
	void reset6502();
	// Run the 6502 engine for specified number of clock cycles 
	int exec6502(int timerTicks);
	//Step the cpu 1 instruction at a time
	int  step6502();
	//Get elapsed ticks / reset to zero
	int  get6502ticks(int reset);
	//Get Register values
	uint8_t m6502_get_reg(int regnum);
	//Set Register Values
	void m6502_set_reg(int regnum, uint8_t val);
	//Get the PC
	uint16_t get_pc();
	//Force a jump to a different PC
	void set_pc(uint16_t pc);
	//Get the previous PC
	uint16_t get_ppc();
	//Return the string value of the last instruction
	std::string disam(uint8_t opcode);
	//Use Mame style memory handling, block/log read/writes that don't go through the handlers.
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s) { log_debug_rw = s; }
	//Enable verbose debug logging (slow)
	void enable_debug(bool s);

	void push16(uint16_t pushval);
	void push8(uint8_t pushval);
	uint16_t pull16();
	uint8_t pull8();

private:

    //CPU Registers 
	uint8_t A;                   // A register 
	uint8_t P;                   // Flags      
	uint16_t PC;                 // Program counter 
	uint16_t PPC;                // Previous Program counter 
	uint8_t X;                   // X register 
	uint8_t Y;                   // Y register 
	uint8_t S;                   // S register STACK

	//Memory handlers
	uint8_t get6502memory(uint16_t addr);
	void    put6502memory(uint16_t addr, uint8_t byte);

	// Function pointer arrays 
	void (cpu_6502::*adrmode[0x100])();
	void (cpu_6502::*instruction[0x100])();

	// Flags
	enum
	{
		F_C = 0x01, //Carry
		F_Z = 0x02, //Zero
		F_I = 0x04, //Interrupt Disable
		F_D = 0x08, //Deciomal 
		F_B = 0x10, //Break 
		F_T = 0x20, //Constant
		F_V = 0x40, //Overflow
		F_N = 0x80  //Negative
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////
    #define SET_NZ(n)	if ((n) == 0) P = (P & ~F_N) | F_Z; else P = (P & ~(F_N | F_Z)) | ((n) & F_N)
	#define SET_Z(n)	if ((n) == 0) P |= F_Z; else P &= ~F_Z
	#define BASE_STACK     0x100
    #define MAX_HANDLER 50
	

	uint16_t addrmask;     //Address Mask for < 0xffff top
	// internal registers 
	uint8_t opcode;
	int clockticks6502;
	int clocktickstotal;   //Runnning, resetable total of clockticks
	uint16_t reladdr;

	// help variables, could be refined.
	uint16_t savepc;       // Holds the PC of the last operation (EA) Effective Address
	uint16_t oldpc;        // Only use in B commands
	uint8_t  value;        //Helper for address calculation
	int saveflags;         //Used in rotate commands to save the flags
	uint16_t help;         //Just a temp variable
	
	bool debug = 1;
	bool mmem = 0;         //Use mame style memory handling, reject unhandled read/writes
	bool log_debug_rw = 0; //Log unhandled reads and writes
	
		
	//int num;
	int _irqPending;       //Non-zero if Irq Pending. Required only in this cpucore by Major Havoc.
	int cpu_num;           //For multicpu identification

	//Addressing
	void implied6502();
	void immediate6502();
	void abs6502();
	void relative6502();
	void indirect6502();
	void absx6502();
	void absy6502();
	void zp6502();
	void zpx6502();
	void zpy6502();
	void indx6502();
	void indy6502();
	void indabsx6502(); // A Register only version.
	void indzp6502();
	//Instructions.
	void adc6502();
	void and6502();
	void asla6502();   // A Register only version.
	void asl6502();
	void bcc6502();
	void bcs6502();
	void beq6502();
	void bit6502();
	void bmi6502();
	void bne6502();
	void bpl6502();
	void brk6502();
	void bvc6502();
	void bvs6502();
	void clc6502();
	void cld6502();
	void cli6502();
	void clv6502();
	void cmp6502();
	void cpx6502();
	void cpy6502();
	void dec6502();
	void dex6502();
	void dey6502();
	void eor6502();
	void inc6502();
	void inx6502();
	void iny6502();
	void jmp6502();
	void jsr6502();
	void lda6502();
	void ldx6502();
	void ldy6502();
	void lsr6502();
	void lsra6502();  // A Register only version.
	void nop6502();
	void ora6502();
	void pha6502();
	void php6502();
	void pla6502();
	void plp6502();
	void rol6502();
	void rola6502();  // A Register only version.
	void ror6502();
	void rora6502();  // A Register only version. 
	void rti6502();
	void rts6502();
	void sbc6502();
	void sec6502();
	void sed6502();
	void sei6502();
	void sta6502();
	void stx6502();
	void sty6502();
	void tax6502();
	void tay6502();
	void tsx6502();
	void txa6502();
	void txs6502();
	void tya6502();
	void bra6502();
	void dea6502();
	void ina6502();
	void phx6502();
	void plx6502();
	void phy6502();
	void ply6502();
	void stz6502();
	void tsb6502();
	void trb6502();
};

#endif 

