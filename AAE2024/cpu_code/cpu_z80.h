/* z80stb.c
 * Zilog Z80 processor emulator in C. Modified to be API compliant with Neil
 * Bradley's MZ80 (for ported projects)
 * Ported to 'C' by Jeff Mitchell; original copyright follows:
 *
 * Abstract:
 *   Internal declarations for Zilog Z80 emulator.
 *
 * Revisions:
 *   18-Apr-97 EAM Created
 *   ??-???-97 EAM Released in MageX 0.5
 *   26-Jul-97 EAM Fixed Time Pilot slowdown bug and
 *      Gyruss hiscore bugs (opcode 0xC3)
 *
 * Original copyright (C) 1997 Edward Massey
 */

// Revisions continued for AAE by myself:

// 091519 Found this older CMZ80 code from Retrocade and converted it to a class. I wanted to use it since it was already compatible with my code.
// Time passes, life happens.
// 090124 Discovered it had a few issues (All repeat commands were in a while loop causing cycle counting issues, especially LDIR and cycle counts and decided to revamp and test what I could)
// 090924 Removed extra cycles commands. Why does removing the cycle counting in LDIR seem to resolve the ship speed issue in omegarace
// 091124 Revised the IRQ NMI code, simple IRQ Hold added, need to revise and make better. Makes Omega Race work properly
// 091424 added the ability to get last opcode for the sega vector encryption
// 091124 LDI/R Command fixed (Increment AFTER!!) , revised all repeat commands
// 091424 added the ability to get last opcode for the Sega vector encryption
// 092724 Added DAA Calculation so the Marat table could be dropped
// 092824 Removed all tables except parity and flags, made them static. Verified with Zexdoc, still issues with aloup tests but all add,sub,bit, etc calculations pass.
// 092924 Regression tested code with pacman, bosconian, galaga, space invaders, omega race and sega vector games, no obvious issues found
// 092924 Added missing mz80ClearPendingInterrupt() since I changed the interrupt handling
// 092924 Reorganized Public and Private members
// 093024 Moved interupt addresses back to public, added irq vector to int mode zero. Doscovered it still will not run rally x. :( 
// 100124 Revised Mode 0 interrupt code to use the correct IRQ vector, fixes Rally X. Still needs further work for other jump/call addresses
// 100924 Revised cpu halt handling to work properly with interleaving and count cycles. Main loop needs a major re-org
// 101624 Added fancy shifting method to IRQ Mode Zero to get the Rst jump location. 
// 110124 Discovered TacScan randomly resetting in level 2, very difficult to troubleshoot. 
// 110224 Rewrote the inc, dec, bit, cp, sub, sbc and adc routines using Vedder Bruno's z80 from the c++ emulator OldSpark as a guide.
// 110224 Now passes all Zexdoc tests. 
// 110324 Fixed and issue where flags were being set on 0xdb (In A,n). This was my fault, removed the offending line of code.
// 110424 Completely rewrote the main exec loop ala Superzazu. It's a lot cleaner to me. 
// 110424 Added address Rbit7 save code from OldSpark EMU (Vedder Bruno)
// 110424 Updated the Previous PC code and verified. Rewrote the Program Counter handler code, first pass. 
// 110524 Invalidating the Previous Program counter after an interrupt, setting it to -1; and trapping it like in M.A.M.E (tm) fixed the decryption issue in Tacscan. 
// 110524 LD A,nn Instructions after an interrupt are not decrypted. 
// 
//Most of my code verification is with:
//[superzazu / z80](https://github.com/superzazu/z80)

/*
Notes:
Still Need to Fix the EI
 // "When an EI instruction is executed, any pending interrupt request
  // is not accepted until after the instruction following EI is executed."

Not passing flag testing in ZEXDOC, every instruction flag calculation needs reviewed, I know some of them are not correct.
Especially the LDI, LDD, CPI , etc. 

Add interrupt pulse instead of just hold. So far everything I have tested doesn't seem to care. 

Currently any undocumented behavior is not emulated. (X and Y flags and undocumented opcodes). 

*/




#ifndef	_MZ80_H_
#define	_MZ80_H_

#pragma once

// REMOVE below if not needed for your code. 
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
/////////////////////////////////////////////

#include <cstdint>
#include "deftypes.h"

enum
{
	C_FLAG = 0x01,
	N_FLAG = 0x02,
	V_FLAG = 0x04, //P_FLAG Parity
	X_FLAG = 0x08,
	H_FLAG = 0x10,
	Y_FLAG = 0x20,
	Z_FLAG = 0x40,
	S_FLAG = 0x80
};

class cpu_z80
{
public:


	//Pointer to the handler structures
	MemoryReadByte* z80MemoryRead = nullptr;
	MemoryWriteByte* z80MemoryWrite = nullptr;
	z80PortRead* z80IoRead = nullptr;
	z80PortWrite* z80IoWrite = nullptr;
	
	//Fix, these need accessors.
	uint16_t z80intAddr = 0x38;
	uint16_t z80nmiAddr = 0x66;

	//PC Manipulations
	uint8_t  GetLastOpcode();
	uint16_t GetPC();
	uint16_t GetPPC();
	void SetPC(uint16_t wAddr);
	void AdjustPC(int8_t cb);

	UINT8	mz80GetMemory(uint16_t addr);
	void	mz80PutMemory(uint16_t addr, uint8_t byte);
	uint8_t In(uint8_t bPort);
	uint8_t InRaw(uint8_t bPort); //TODO : Remove this
	void    Out(uint8_t bPort, uint8_t bVal);
	void    MemWriteWord(UINT16 wAddr, UINT16 wVal);
	void    MemWriteByte(UINT16 wAddr, UINT8 bVal);
	UINT8   MemReadByte(UINT16 wAddr);
	UINT16  MemReadWord(UINT16 wAddr);

	//Main Routines
	unsigned mz80step();
	unsigned long int mz80exec(unsigned long int cCyclesArg);
	UINT32 mz80int(UINT32 bVal);
	UINT32 mz80nmi(void);
	void   mz80reset();
	UINT32 mz80GetElapsedTicks(UINT32 dwClearIt);
	void   mz80ReleaseTimeslice();
	void   mz80ClearPendingInterrupt();
	
	cpu_z80(uint8_t* MEM, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr, int num);
	~cpu_z80();

private:


	union {
		uint16_t m_regAF;
		struct {
			uint8_t m_regF;
			uint8_t m_regA;
		} regAFs;
	} regAF;
#define m_regAF regAF.m_regAF
#define m_regF regAF.regAFs.m_regF
#define m_regA regAF.regAFs.m_regA

	union {
		uint16_t m_regBC;
		struct {
			uint8_t m_regC;
			uint8_t m_regB;
		} regBCs;
	} regBC;
#define m_regBC regBC.m_regBC
#define m_regB regBC.regBCs.m_regB
#define m_regC regBC.regBCs.m_regC

	union {
		uint16_t m_regDE;
		struct {
			uint8_t m_regE;
			uint8_t m_regD;
		} regDEs;
	} regDE;
#define m_regDE regDE.m_regDE
#define m_regD regDE.regDEs.m_regD
#define m_regE regDE.regDEs.m_regE

	union {
		uint16_t m_regHL;
		struct {
			uint8_t m_regL;
			uint8_t m_regH;
		} regHLs;
	} regHL;
#define m_regHL regHL.m_regHL
#define m_regH regHL.regHLs.m_regH
#define m_regL regHL.regHLs.m_regL

	union {
		uint16_t m_regIX;
		struct {
			uint8_t m_regIXl;
			uint8_t m_regIXh;
		} regIXs;
	} regIX;
#define m_regIX regIX.m_regIX
#define m_regIXl regIX.regIXs.m_regIXl
#define m_regIXh regIX.regIXs.m_regIXh

	union {
		uint16_t m_regIY;
		struct {
			uint8_t m_regIYl;
			uint8_t m_regIYh;
		} regIYs;
	} regIY;
#define m_regIY regIY.m_regIY
#define m_regIYl regIY.regIYs.m_regIYl
#define m_regIYh regIY.regIYs.m_regIYh

	//const uint8_t* m_rgbOpcode;		// takes place of the PC register (Removed in place of actually using z80pc)
	uint8_t* m_rgbStack;			// takes place of the SP register
	uint8_t* m_rgbMemory;			// direct access to memory buffer (RAM)
	//const uint8_t* m_rgbOpcodeBase; // "base" pointer for m_rgbOpcode
	uint8_t* m_rgbStackBase;
	int cCycles;

	uint16_t z80pc;
	uint16_t z80ppc;
	uint8_t m_regR;
	uint8_t m_regI;

	int m_iff1, m_iff2;
	bool m_fHalt;
	int m_nIM;
	bool m_fPendingInterrupt = false;

	//New
	int pending_int;  //TODO: Swap this with m_fPendingInterrupt
	uint8_t iff_delay;
	// Contains the irq vector. 
	uint16_t irq_vector; 
	/* Use to store bit 7 of R  */
	uint8_t Rbit7;

	uint16_t m_regAF2;
	uint16_t m_regBC2;
	uint16_t m_regDE2;
	uint16_t m_regHL2;
	
	UINT32 dwElapsedTicks;
	//Memory Accessors
	uint8_t ImmedByte();
	UINT16 ImmedWord();

	int cpu_num;


	unsigned exec_opcode(uint8_t bOpcode);
	void Push(uint16_t wArg);
	uint16_t Pop();
	uint16_t GetSP();
	void SetSP(uint16_t wAddr);
		
	//Utils
	void swap(uint16_t& b1, uint16_t& b2);

	//Opcodes
	int Ret0(int f);
	int Ret1(int f);
	// Math
	void Add_1(uint8_t bArg);
	void Adc_1(uint8_t bArg);
	void Sub_1(uint8_t bArg);
	void Sbc_1(uint8_t bArg);
	uint16_t Adc_2(uint16_t wArg1, uint16_t wArg2);
	uint16_t Sbc_2(uint16_t wArg1, uint16_t wArg2);
	uint16_t Add_2(uint16_t wArg1, uint16_t wArg2);
	//
	void And(uint8_t bArg);
	void Or(uint8_t bArg);
	void Xor(uint8_t bArg);
	void Cp(uint8_t bArg);
	uint8_t Inc(uint8_t bArg);
	uint8_t Dec(uint8_t bArg);
	uint8_t Set(uint8_t bArg, int nBit);
	uint8_t Res(uint8_t bArg, int nBit);
	void Bit(uint8_t bArg, uint8_t nBit);
	//Rotate
	uint8_t Rlc(uint8_t bArg);
	uint8_t Rrc(uint8_t bArg);
	uint8_t Rl(uint8_t bArg);
	uint8_t Rr(uint8_t bArg);
	uint8_t Sll(uint8_t bArg);
	uint8_t Srl(uint8_t bArg);
	uint8_t Sla(uint8_t bArg);
	uint8_t Sra(uint8_t bArg);
	void Rlca();
	void Rrca();
	void Rla();
	void Rra();
	//Flow Control
	int Jr0(int f);
	int Jr1(int f);
	int Call0(int f);
	int Call1(int f);
	int Jp0(int f);
	int Jp1(int f);
	void Rst(uint16_t wAddr);
	//Prefixed Opcodes
	int HandleDDCB();
	int HandleFDCB();
	int HandleFD();
	uint16_t IndirectIY();
	int Ei();
	void Di();
	void HandleED();
	int HandleDD();
	uint16_t IndirectIX();
	void Exx();
	int HandleCB();
	void Daa();
};

#endif	// _MZ80_H_
