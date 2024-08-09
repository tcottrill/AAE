
#ifndef _i8080_H_
#define _i8080_H_

#pragma once


#include "..//deftypes.h"
#include <cstdint>

/*
  Intel 8080 emulator in C
  Written by Mike Chambers, April 2018

  Thanks to deramp5113, gslick, and Chuck(G) from the Vintage Computer Forum for their insight.
  Especially to gslick for help in the final mile with flag calculation issues.

  Use this code for whatever you want. I don't care. It's officially public domain.
  Credit would be appreciated.

  Converted to a Neil Bradley compatible emulator class for use in AAE, July 2020 TC
*/

#define ALLOW_UNDEFINED


static const uint8_t parity[0x100] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};


class cpu_i8080
{

public:

	uint8_t reg8[9] = { 0,0,0,0,0,0,0,0,0 };
	uint8_t INTE = 0;
	uint16_t reg_SP = 0;
	uint16_t reg_PC = 0;

	#define reg16_PSW (((uint16_t)reg8[A] << 8) | (uint16_t)reg8[FLAGS])
	#define reg16_BC (((uint16_t)reg8[B] << 8) | (uint16_t)reg8[C])
	#define reg16_DE (((uint16_t)reg8[D] << 8) | (uint16_t)reg8[E])
	#define reg16_HL (((uint16_t)reg8[H] << 8) | (uint16_t)reg8[L])

	enum {

		B,
		C,
		D,
		E,
		H,
		L,
		M,
		A,
		FLAGS
	};

	//#define B 0
	//#define C 1
	//#define D 2
	//#define E 3
	//#define H 4
	//#define L 5
	//#define M 6
	//#define A 7
	//#define FLAGS 8

	#define set_S() reg8[FLAGS] |= 0x80
	#define set_Z() reg8[FLAGS] |= 0x40
	#define set_AC() reg8[FLAGS] |= 0x10
	#define set_P() reg8[FLAGS] |= 0x04
	#define set_C() reg8[FLAGS] |= 0x01
	#define clear_S() reg8[FLAGS] &= 0x7F
	#define clear_Z() reg8[FLAGS] &= 0xBF
	#define clear_AC() reg8[FLAGS] &= 0xEF
	#define clear_P() reg8[FLAGS] &= 0xFB
	#define clear_C() reg8[FLAGS] &= 0xFE
	#define test_S() (reg8[FLAGS] & 0x80)
	#define test_Z() (reg8[FLAGS] & 0x40)
	#define test_AC() (reg8[FLAGS] & 0x10)
	#define test_P() (reg8[FLAGS] & 0x04)
	#define test_C() (reg8[FLAGS] & 0x01)

	// Pointer to the cpu memory map (32 bit)
	uint8_t *MEM;
	//Pointer to the handler structures
	MemoryReadByte *memory_read = nullptr;
	MemoryWriteByte *memory_write = nullptr;
	z80PortRead *z80IoRead = nullptr;
	z80PortWrite *z80IoWrite = nullptr;

	//Constructors
	cpu_i8080(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr);
	
	//Destructor
	~cpu_i8080() {};

		
	uint8_t In(uint8_t bPort);
	void Out(uint8_t bPort, uint8_t bVal);
	int  exec(int cycles);
	void interrupt(uint8_t n);
	void reset();
	int get_ticks(int reset);
			
	//Return the string value of the last instruction
	//Use Mame style memory handling, block read/writes that don't go through the handlers.
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s) { log_debug_rw = s; }

private:

	//Internal Memory handlers
	uint8_t i8080_read(uint16_t addr);
	void    i8080_write(uint16_t addr, uint8_t byte);

	uint16_t read_RP(uint8_t rp);
	uint16_t read_RP_PUSHPOP(uint8_t rp);
	void write_RP(uint8_t rp, uint8_t lb, uint8_t hb);
	void write16_RP(uint8_t rp, uint16_t value);
	void write16_RP_PUSHPOP(uint8_t rp, uint16_t value);
	void calc_SZP(uint8_t value);
	void calc_AC(uint8_t val1, uint8_t val2);
	void calc_AC_carry(uint8_t val1, uint8_t val2);
	void calc_subAC(int8_t val1, uint8_t val2);
	void calc_subAC_borrow(int8_t val1, uint8_t val2);
	uint8_t test_cond(uint8_t code);
	void i8080_push(uint16_t value);
	uint16_t i8080_pop();
	void i8080_jump(uint16_t addr);
	void write_reg8(uint8_t reg, uint8_t value);
	uint8_t read_reg8(uint8_t reg);
	

	bool debug = 0;
	bool mmem = 0; //Use mame style memory handling, reject unhandled read/writes
	int log_debug_rw = 0; //Log unhandled reads and writes
	int clocktickstotal=0; //Runnning, resetable total of clockticks
	int clockticks6502=0;
};

#endif 

