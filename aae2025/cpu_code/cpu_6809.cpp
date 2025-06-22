/*** m6809: Portable 6809 emulator ******************************************

	Copyright (C) John Butler 1997

	References:

		6809 Simulator V09, By L.C. Benschop, Eidnhoven The Netherlands.

		m6809: Portable 6809 emulator, DS (6809 code in MAME, derived from
			the 6809 Simulator V09)

		6809 Microcomputer Programming & Interfacing with Experiments"
			by Andrew C. Staugaard, Jr.; Howard W. Sams & Co., Inc.

	System dependencies:	word must be 16 bit unsigned int
							byte must be 8 bit unsigned int
							long must be more than 16 bits
							arrays up to 65536 bytes must be supported
							machine must be twos complement

*****************************************************************************/

#include <cstdint>
#include "cpu_6809.h"
#include "log.h"
#include "timer.h"
#include "aae_mame_driver.h"

int m6809_slapstic = 1;

extern int catch_nextBranch;

extern int slapstic_en;

#pragma warning( disable : 4305 4244 )

const char* m6809_opcodes[256] = {
"NEG", "ILLEGAL", "ILLEGAL", "COM", "LSR", "ILLEGAL", "ROR", "ASR",                     // 0x00-0x07
"ASL", "ROL", "DEC", "ILLEGAL", "INC", "TST", "JMP", "CLR",                             // 0x08-0x0F
"PREF10", "PREF11", "NOP", "SYNC", "ILLEGAL", "ILLEGAL", "LBRA", "LBSR",             // 0x10-0x17
"ILLEGAL", "DAA", "ORCC", "ILLEGAL", "ANDCC", "SEX", "EXG", "TFR",                      // 0x18-0x1F
"BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",                                 // 0x20-0x27
"BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE",                                 // 0x28-0x2F
"LEAX", "LEAY", "LEAS", "LEAU", "PSHS", "PULS", "PSHU", "PULU",                         // 0x30-0x37
"ILLEGAL", "RTS", "ABX", "RTI", "CWAI", "MUL", "ILLEGAL", "SWI",                        // 0x38-0x3F
"NEGA", "ILLEGAL", "ILLEGAL", "COMA", "LSRA", "ILLEGAL", "RORA", "ASRA",               // 0x40-0x47
"ASLA", "ROLA", "DECA", "ILLEGAL", "INCA", "TSTA", "ILLEGAL", "CLRA",                  // 0x48-0x4F
"NEGB", "ILLEGAL", "ILLEGAL", "COMB", "LSRB", "ILLEGAL", "RORB", "ASRB",               // 0x50-0x57
"ASLB", "ROLB", "DECB", "ILLEGAL", "INCB", "TSTB", "ILLEGAL", "CLRB",                  // 0x58-0x5F
"NEG", "ILLEGAL", "ILLEGAL", "COM", "LSR", "ILLEGAL", "ROR", "ASR",                     // 0x60-0x67
"ASL", "ROL", "DEC", "ILLEGAL", "INC", "TST", "JMP", "CLR",                             // 0x68-0x6F
"NEG", "ILLEGAL", "ILLEGAL", "COM", "LSR", "ILLEGAL", "ROR", "ASR",                     // 0x70-0x77
"ASL", "ROL", "DEC", "ILLEGAL", "INC", "TST", "JMP", "CLR",                             // 0x78-0x7F
"SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",                           // 0x80-0x87
"EORA", "ADCA", "ORA", "ADDA", "CMPX", "BSR", "LDX", "STX",                             // 0x88-0x8F
"SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",                           // 0x90-0x97
"EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX",                             // 0x98-0x9F
"SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",                           // 0xA0-0xA7
"EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX",                             // 0xA8-0xAF
"SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",                           // 0xB0-0xB7
"EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX",                             // 0xB8-0xBF
"SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",                           // 0xC0-0xC7
"EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU",                              // 0xC8-0xCF
"SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",                           // 0xD0-0xD7
"EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU",                              // 0xD8-0xDF
"SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",                           // 0xE0-0xE7
"EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU",                              // 0xE8-0xEF
"SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",                           // 0xF0-0xF7
"EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU"                               // 0xF8-0xFF
};

const char* m6809_0111_opcodes[] = {
	"LBRN",  // 0x1021
	"LBHI",  // 0x1022
	"LBLS",  // 0x1023
	"LBCC",  // 0x1024
	"LBCS",  // 0x1025
	"LBNE",  // 0x1026
	"LBEQ",  // 0x1027
	"LBVC",  // 0x1028
	"LBVS",  // 0x1029
	"LBPL",  // 0x102A
	"LBMI",  // 0x102B
	"LBGE",  // 0x102C
	"LBLT",  // 0x102D
	"LBGT",  // 0x102E
	"LBLE",  // 0x102F
	"SWI2",  // 0x103F
	"CMPD",  // 0x1083
	"CMPY",  // 0x108C
	"LDY",   // 0x108E
	"STY",   // 0x108F
	"CMPD",  // 0x1093
	"CMPY",  // 0x109C
	"LDY",   // 0x109E
	"STY",   // 0x109F
	"CMPD",  // 0x10A3
	"CMPY",  // 0x10AC
	"LDY",   // 0x10AE
	"STY",   // 0x10AF
	"CMPD",  // 0x10B3
	"CMPY",  // 0x10BC
	"LDY",   // 0x10BE
	"STY",   // 0x10BF
	"LDS",   // 0x10CE
	"STS",   // 0x10CF
	"LDS",   // 0x10DE
	"STS",   // 0x10DF
	"LDS",   // 0x10EE
	"STS",   // 0x10EF
	"LDS",   // 0x10FE
	"STS",   // 0x10FF
	"SWI3",  // 0x113F
	"CMPU",  // 0x1183
	"CMPS",  // 0x118C
	"CMPU",  // 0x1193
	"CMPS",  // 0x119C
	"CMPU",  // 0x11A3
	"CMPS",  // 0x11AC
	"CMPU",  // 0x11B3
	"CMPS",  // 0x11BC
	"ILLEGAL" // default
};

cpu_6809::cpu_6809(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	cpu_num = num;
	m6809_ICount = 0;
	init6809(addr);
	logging = 0;
}

int cpu_6809::get6809ticks(int reset)
{
	int tmp;

	tmp = clocktickstotal;
	if (reset)
	{
		clocktickstotal = 0;
	}
	return tmp;
}

uint8_t cpu_6809::get6809memory(uint16_t addr)
{
	uint8_t temp = 0;
	// Pointer to Beginning of our handler
	MemoryReadByte* MemRead = memory_read;

	while (MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
		{
			//if  (cpu_num) LOG_INFO("Reading from address: %x", addr);
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
		//LOG_INFO("Reading from address: %x", addr);
		temp = MEM[addr];
	}
	if (MemRead && mmem)
	{
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Read at %x", addr);
	}

	return temp;
}

void cpu_6809::put6809memory(uint16_t addr, uint8_t byte)
{
	// Pointer to Beginning of our handler
	MemoryWriteByte* MemWrite = memory_write;

	while (MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
		{
			//if (cpu_num) LOG_INFO("Writing to address: %x data: %x", addr, byte);
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

// Init MyCpu
void cpu_6809::init6809(uint16_t addrmaskval)
{
	int Iperiod = 0;
	int otherTicks = 0;
	clockticks6809 = 0;
	clocktickstotal = 0;
	pcreg = 0;
	addrmask = addrmaskval;
}

/****************************************************************************/
/* Return program counter                                                   */
/****************************************************************************/

uint16_t cpu_6809::get_pc()
{
	return pcreg;
}

uint16_t cpu_6809::get_ppc()
{
	return ppc;
}

void cpu_6809::set_pc(uint16_t newpc)
{
	//LOG_INFO(" Current PCREG %X Setting new PC %x", pcreg, newpc);
	pcreg = newpc;
}

void cpu_6809::change_pc(uint16_t pcreg)
{
	if (cpu_num > 0) return;

	//LOG_INFO("PC at ChangePC is %x", pcreg);
	//change_pc_called = 1;
	slapstic_en = 1;
	if (m6809_slapstic) cpu_setOPbase16(pcreg);
	// I don't have any other reason to change the PC? No banking? I am cluelessa bout how mame does the banking properly.
	//else
	//	change_pc16(pcreg);

	// Placeholder
	// This is just for the slapstic code.
	// Since all I care about is ESB, I'll just hard code the banks and switch between them here based on the slapstic
}

uint8_t cpu_6809::M_RDMEM(uint16_t A)
{
	slapstic_en = 1;
	return get6809memory(A);
}

void cpu_6809::M_WRMEM(uint16_t A, uint8_t V)
{
	slapstic_en = 1;
	put6809memory(A, V);
}
//////////////////////////////////////////////////////////////////////////
unsigned int cpu_6809::M_RDMEM_WORD(uint32_t A)
{
	int i;

	slapstic_en = 1;
	i = get6809memory(A) << 8;
	slapstic_en = 1;
	i |= get6809memory(((A)+1) & 0xFFFF);

	return i;
}

void cpu_6809::M_WRMEM_WORD(uint32_t A, uint16_t V)
{
	slapstic_en = 1;
	put6809memory(A, V >> 8);
	slapstic_en = 1;
	put6809memory(((A)+1) & 0xFFFF, V & 255);
}
//THESE GO THROUGH THE HANDLERS

int32_t  cpu_6809::rd_slow(int32_t addr)
{
	slapstic_en = 0;
	return get6809memory(addr);
}

int32_t  cpu_6809::rd_slow_wd(int32_t addr)
{
	int i;
	slapstic_en = 0;
	i = get6809memory(addr) << 8;
	slapstic_en = 0;
	i |= get6809memory((addr + 1) & 0xFFFF);
	return i;
	//return((get6809memory(addr) << 8) | (get6809memory((addr + 1) & 0xffff)));
}

void cpu_6809::wr_slow(int32_t addr, int32_t v)
{
	slapstic_en = 0;
	put6809memory(addr, v);
}

void cpu_6809::wr_slow_wd(int32_t addr, int32_t v)
{
	slapstic_en = 0;
	put6809memory(addr, v >> 8);
	slapstic_en = 0;
	put6809memory(((addr)+1) & 0xFFFF, v & 255);
}

////////////////////////////////////

/* macros to access memory */
#define IMMBYTE(b)	b = rd_slow(pcreg++);
#define IMMWORD(w)	w = rd_slow_wd(pcreg); pcreg+=2;
#define EXTENDED IMMWORD(eaddr)

#define PUSHBYTE(b) {--sreg;M_WRMEM(sreg,b);}
#define PUSHWORD(w) {sreg-=2;M_WRMEM_WORD(sreg,w);}

#define PULLBYTE(b) {b=M_RDMEM(sreg);sreg++;}
#define PULLWORD(w) {w=M_RDMEM_WORD(sreg);sreg+=2;}

#define PSHUBYTE(b) {--ureg;M_WRMEM(ureg,b);}
#define PSHUWORD(w) {ureg-=2;M_WRMEM_WORD(ureg,w);}

#define PULUBYTE(b) {b=M_RDMEM(ureg);ureg++;}
#define PULUWORD(w) {w=M_RDMEM_WORD(ureg);ureg+=2;}

/* flag bits in the cc register */
#define CC_C    0x01        /* Carry */
#define CC_V    0x02        /* Overflow */
#define CC_Z    0x04        /* Zero */
#define CC_N    0x08        /* Negative */
#define CC_II   0x10        /* Inhibit IRQ */
#define CC_H    0x20        /* Half (auxiliary) carry */
#define CC_IF   0x40        /* Inhibit FIRQ */
#define CC_E    0x80        /* entire state pushed */
#define CC  	cc

/* macros to set status flags */
#define SEC CC|=CC_C
#define CLC CC&=~CC_C
#define SEZ CC|=CC_Z
#define CLZ CC&=~CC_Z
#define SEN CC|=CC_N
#define CLN CC&=~CC_N
#define SEV CC|=CC_V
#define CLV CC&=~CC_V
#define SEH CC|=CC_H
#define CLH CC&=~CC_H

#define CLR_HNZVC	CC&=~(CC_H|CC_N|CC_Z|CC_V|CC_C)
#define CLR_NZV 	CC&=~(CC_N|CC_Z|CC_V)
#define CLR_HNZC	CC&=~(CC_H|CC_N|CC_Z|CC_C)
#define CLR_NZVC	CC&=~(CC_N|CC_Z|CC_V|CC_C)
#define CLR_Z		CC&=~(CC_Z)
#define CLR_NZC 	CC&=~(CC_N|CC_Z|CC_C)
#define CLR_ZC		CC&=~(CC_Z|CC_C)

/* macros for CC -- CC bits affected should be reset before calling */
#define SET_Z(a)		if(!a)SEZ
#define SET_Z8(a)		SET_Z((uint8_t)a)
#define SET_Z16(a)		SET_Z((uint16_t)a)
#define SET_N8(a)		CC|=((a&0x80)>>4)
#define SET_N16(a)		CC|=((a&0x8000)>>12)
#define SET_H(a,b,r)	CC|=(((a^b^r)&0x10)<<1)
#define SET_C8(a)		CC|=((a&0x100)>>8)
#define SET_C16(a)		CC|=((a&0x10000)>>16)
#define SET_V8(a,b,r)	CC|=(((a^b^r^(r>>1))&0x80)>>6)
#define SET_V16(a,b,r)	CC|=(((a^b^r^(r>>1))&0x8000)>>14)
/* combos */
#define SET_NZ8(a)			{SET_N8(a);SET_Z(a);}
#define SET_NZ16(a)			{SET_N16(a);SET_Z(a);}
#define SET_FLAGS8(a,b,r)	{SET_N8(r);SET_Z8(r);SET_V8(a,b,r);SET_C8(r);}
#define SET_FLAGS16(a,b,r)	{SET_N16(r);SET_Z16(r);SET_V16(a,b,r);SET_C16(r);}

/* for treating an unsigned uint8_t as a signed uint16_t */
#define SIGNED(b) ((UINT16)(b&0x80?b|0xff00:b))

/* macros to access dreg */
#define GETDREG ((areg<<8)|breg)
#define SETDREG(n) {areg=(n)>>8;breg=(n);}

#define DIRECT {eaddr=0;IMMBYTE(eaddr);eaddr|=(dpreg<<8);}
#define IMM8 eaddr=pcreg++
#define IMM16 {eaddr=pcreg;pcreg+=2;}
#define EXTENDED IMMWORD(eaddr)

/* macros for addressing modes (postbytes have their own code) */
#define DIRBYTE(b) { DIRECT;    b=M_RDMEM(eaddr);}
#define DIRWORD(w) { DIRECT;    w=M_RDMEM_WORD(eaddr);}
#define EXTBYTE(b) { EXTENDED;  b=M_RDMEM(eaddr);}
#define EXTWORD(w) { EXTENDED;  w=M_RDMEM_WORD(eaddr);}


/* macros for branch instructions */

#define BRANCH(f) { 					\
	UINT8 t;							\
	IMMBYTE(t); 						\
	if( f ) 							\
	{									\
		pcreg += SIGNED(t);				\
		change_pc(pcreg); 	/* TS 971002 */ \
 	}									\
}

#define LBRANCH(f) {                    \
	uint16_t t; 						\
	IMMWORD(t); 						\
	if( f ) 							\
	{	m6809_ICount -= 1;				\
		pcreg+=t;					    \
		change_pc(pcreg);	/* TS 971002 */ \
	}									\
}

#define NXORV  ((cc&0x08)^((cc&0x02)<<2))

/* macros for setting/getting registers in TFR/EXG instructions */
#define GETREG(val,reg) switch(reg) {\
                         case 0: val=GETDREG;break;\
                         case 1: val=xreg;break;\
                         case 2: val=yreg;break;\
                    	 case 3: val=ureg;break;\
                    	 case 4: val=sreg;break;\
                    	 case 5: val=pcreg;break;\
                    	 case 8: val=areg;break;\
                    	 case 9: val=breg;break;\
                    	 case 10: val=cc;break;\
                    	 case 11: val=dpreg;break;}

#define SETREG(val,reg) switch(reg) {\
			 case 0: SETDREG(val); break;\
			 case 1: xreg=val;break;\
			 case 2: yreg=val;break;\
			 case 3: ureg=val;break;\
			 case 4: sreg=val;break;\
			 case 5: pcreg=val;break;\
			 case 8: areg=val;break;\
			 case 9: breg=val;break;\
			 case 10: cc=val;break;\
			 case 11: dpreg=val;break;}

///////////////////////////////////////////////////////////////////
/*

HNZVC

? = undefined
* = affected
- = unaffected
0 = cleared
1 = set
# = ccr directly affected by instruction
@ = special - carry set if bit 7 is set

*/

void cpu_6809::illegal()
{
	//if (errorlog)fprintf(errorlog, "M6809: illegal opcode\n");
}

/* $00 NEG direct ?**** */
void cpu_6809::neg_di()
{
	uint16_t r, t;
	DIRBYTE(t);
	r = -t;
	CLR_NZVC; SET_FLAGS8(0, t, r);
	M_WRMEM(eaddr, r);
}

/* $01 ILLEGAL */

/* $02 ILLEGAL */

/* $03 COM direct -**01 */
void cpu_6809::com_di()
{
	uint8_t t = 0;
	DIRBYTE(t); t = ~t;
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $04 LSR direct -0*-* */
void cpu_6809::lsr_di()
{
	uint8_t t = 0;
	DIRBYTE(t); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $05 ILLEGAL */

/* $06 ROR direct -**-* */
void cpu_6809::ror_di()
{
	uint8_t t = 0;
	uint8_t r = 0;
	DIRBYTE(t); r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $07 ASR direct ?**-* */
void cpu_6809::asr_di()
{
	uint8_t t = 0;
	DIRBYTE(t); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; t |= ((t & 0x40) << 1);
	SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $08 ASL direct ?**** */
void cpu_6809::asl_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $09 ROL direct -**** */
void cpu_6809::rol_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $0A DEC direct -***- */
void cpu_6809::dec_di()
{
	uint8_t t = 0;
	DIRBYTE(t);
	--t;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $0B ILLEGAL */

/* $OC INC direct -***- */
void cpu_6809::inc_di()
{
	uint8_t t = 0;
	DIRBYTE(t);
	++t;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $OD TST direct -**0- */
void cpu_6809::tst_di()
{
	uint8_t t = 0;
	DIRBYTE(t); CLR_NZV; SET_NZ8(t);
}

/* $0E JMP direct ----- */
void cpu_6809::jmp_di()
{
	DIRECT; pcreg = eaddr;
	change_pc(pcreg);
}

/* $0F CLR direct -0100 */
void cpu_6809::clr_di()
{
	DIRECT; M_WRMEM(eaddr, 0);
	CLR_NZVC; SEZ;
}

/* $10 FLAG */

/* $11 FLAG */

/* $12 NOP inherent ----- */
void cpu_6809::nop()
{
	;
}

/* $13 SYNC inherent ----- */
void cpu_6809::sync()
{
	/* SYNC should stop processing instructions until an interrupt occurs.
	   A decent fake is probably to force an immediate IRQ. */
	if (m6809_ICount > 0) m6809_ICount = 0;
	pending_interrupts |= M6809_SYNC;
}

/* $14 ILLEGAL */

/* $15 ILLEGAL */

/* $16 LBRA relative ----- */
void cpu_6809::lbra()
{
	IMMWORD(eaddr);
	pcreg += eaddr;
	change_pc(pcreg);

	if (eaddr == 0xfffd) /* EHC 980508 speed up busy loop */
		if (m6809_ICount > 0)
			m6809_ICount = 0;
}

/* $17 LBSR relative ----- */
void cpu_6809::lbsr()
{
	IMMWORD(eaddr); PUSHWORD(pcreg); pcreg += eaddr;
	change_pc(pcreg);
}

/* $18 ILLEGAL */

/* $19 DAA inherent (areg) -**0* */
void cpu_6809::daa()
{
	uint8_t msn, lsn;
	uint16_t t, cf = 0;
	msn = areg & 0xf0; lsn = areg & 0x0f;
	if (lsn > 0x09 || cc & 0x20) cf |= 0x06;
	if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
	if (msn > 0x90 || cc & 0x01) cf |= 0x60;
	t = cf + areg;
	CLR_NZV; /* keep carry from previous operation */
	SET_NZ8(t); SET_C8(t);
	//SET_NZ8((UINT8)t); SET_C8(t);
	areg = t;
}

/* $1A ORCC immediate ##### */
void cpu_6809::orcc()
{
	uint8_t t = 0;
	IMMBYTE(t); cc |= t;
}

/* $1B ILLEGAL */

/* $1C ANDCC immediate ##### */
void cpu_6809::andcc()
{
	uint8_t t = 0;
	IMMBYTE(t); cc &= t;
}

/* $1D SEX inherent -**0- */
void cpu_6809::sex()
{
	uint16_t t;
	t = SIGNED(breg); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $1E EXG inherent ----- */
void cpu_6809::exg()
{
	UINT16 t1, t2;
	UINT8 tb;

	IMMBYTE(tb);
	if ((tb ^ (tb >> 4)) & 0x08)
	{
		t1 = t2 = 0xff;
	}
	else
	{
		switch (tb >> 4) {
		case  0: t1 = GETDREG;  break;
		case  1: t1 = xreg;  break;
		case  2: t1 = yreg;  break;
		case  3: t1 = ureg;  break;
		case  4: t1 = sreg;  break;
		case  5: t1 = pcreg; break;
		case  8: t1 = areg;  break;
		case  9: t1 = breg;  break;
		case 10: t1 = cc; break;
		case 11: t1 = dpreg; break;
		default: t1 = 0xff;
		}
		switch (tb & 15) {
		case  0: t2 = GETDREG;  break;
		case  1: t2 = xreg;  break;
		case  2: t2 = yreg;  break;
		case  3: t2 = ureg;  break;
		case  4: t2 = sreg;  break;
		case  5: t2 = pcreg; break;
		case  8: t2 = areg;  break;
		case  9: t2 = breg;  break;
		case 10: t2 = cc; break;
		case 11: t2 = dpreg; break;
		default: t2 = 0xff;
		}
	}
	switch (tb >> 4) {
	case  0: SETDREG(t2);  break;
	case  1: xreg = t2;  break;
	case  2: yreg = t2;  break;
	case  3: ureg = t2;  break;
	case  4: sreg = t2;  break;
	case  5: pcreg = t2;  change_pc(pcreg);  break;
	case  8: areg = t2;  break;
	case  9: breg = t2;  break;
	case 10: cc = t2; break;
	case 11: dpreg = t2; break;
	}
	switch (tb & 15) {
	case  0: SETDREG(t1);  break;
	case  1: xreg = t1;  break;
	case  2: yreg = t1;  break;
	case  3: ureg = t1;  break;
	case  4: sreg = t1;  break;
	case  5: pcreg = t1; change_pc(pcreg); break;
	case  8: areg = t1;  break;
	case  9: breg = t1;  break;
	case 10: cc = t1; break;
	case 11: dpreg = t1; break;
	}
}

/* $1F TFR inherent ----- */
void cpu_6809::tfr()
{
	UINT8 tb;
	UINT16 t = 0;

	IMMBYTE(tb);
	if ((tb ^ (tb >> 4)) & 0x08)
	{
		t = 0xff;
	}
	else
	{
		switch (tb >> 4)
		{
		case  0: t = GETDREG; break;
		case  1: t = xreg;  break;
		case  2: t = yreg;  break;
		case  3: t = ureg;  break;
		case  4: t = sreg;  break;
		case  5: t = pcreg; break;
		case  8: t = areg;  break;
		case  9: t = breg;  break;
		case 10: t = cc; break;
		case 11: t = dpreg; break;
		default: t = 0xff;
		}
	}
	switch (tb & 15) {
	case  0: SETDREG(t);  break;
	case  1: xreg = t;  break;
	case  2: yreg = t;  break;
	case  3: ureg = t;  break;
	case  4: sreg = t;  break;
	case  5: pcreg = t; change_pc(pcreg);  break;
	case  8: areg = t;  break;
	case  9: breg = t;  break;
	case 10: cc = t; break;
	case 11: dpreg = t; break;
	}
}

/* $20 BRA relative ----- */
void cpu_6809::bra()
{
	byte t;
	IMMBYTE(t);
	pcreg += SIGNED(t);
	change_pc(pcreg);
	if (t == 0xfe)
		if (m6809_ICount > 0) m6809_ICount = 0;
}

/* $21 BRN relative ----- */
void cpu_6809::brn()
{
	byte t;
	IMMBYTE(t);
}

/* $1021 LBRN relative ----- */
void cpu_6809::lbrn()
{
	uint16_t t;
	IMMWORD(t);
}

/* $22 BHI relative ----- */
void cpu_6809::bhi()
{
	BRANCH(!(CC & (CC_Z | CC_C)));
}

/* $1022 LBHI relative ----- */
void cpu_6809::lbhi()
{
	LBRANCH(!(CC & (CC_Z | CC_C)));
}

/* $23 BLS relative ----- */
void cpu_6809::bls()
{
	BRANCH((CC & (CC_Z | CC_C)));
}

/* $1023 LBLS relative ----- */
void cpu_6809::lbls()
{
	LBRANCH((CC & (CC_Z | CC_C)));
}

/* $24 BCC relative ----- */
void cpu_6809::bcc()
{
	BRANCH(!(CC & CC_C));
}

/* $1024 LBCC relative ----- */
void cpu_6809::lbcc()
{
	LBRANCH(!(CC & CC_C));
}

/* $25 BCS relative ----- */
void cpu_6809::bcs()
{
	BRANCH((CC & CC_C));
}

/* $1025 LBCS relative ----- */
void cpu_6809::lbcs()
{
	LBRANCH((CC & CC_C));
}

/* $26 BNE relative ----- */
void cpu_6809::bne()
{
	BRANCH(!(CC & CC_Z));
}

/* $1026 LBNE relative ----- */
void cpu_6809::lbne()
{
	LBRANCH(!(CC & CC_Z));
}

/* $27 BEQ relative ----- */
void cpu_6809::beq()
{
	BRANCH((CC & CC_Z));
}

/* $1027 LBEQ relative ----- */
void cpu_6809::lbeq()
{
	LBRANCH((CC & CC_Z));
}

/* $28 BVC relative ----- */
void cpu_6809::bvc()
{
	BRANCH(!(CC & CC_V));
}

/* $1028 LBVC relative ----- */
void cpu_6809::lbvc()
{
	LBRANCH(!(CC & CC_V));
}

/* $29 BVS relative ----- */
void cpu_6809::bvs()
{
	BRANCH((CC & CC_V));
}

/* $1029 LBVS relative ----- */
void cpu_6809::lbvs()
{
	LBRANCH((CC & CC_V));
}

/* $2A BPL relative ----- */
void cpu_6809::bpl()
{
	BRANCH(!(CC & CC_N));
}

/* $102A LBPL relative ----- */
void cpu_6809::lbpl()
{
	LBRANCH(!(CC & CC_N));
}

/* $2B BMI relative ----- */
void cpu_6809::bmi(void)
{
	BRANCH((CC & CC_N));
}

/* $102B LBMI relative ----- */
void cpu_6809::lbmi(void)
{
	LBRANCH((CC & CC_N));
}

/* $2C BGE relative ----- */
void cpu_6809::bge(void)
{
	BRANCH(!NXORV);
}

/* $102C LBGE relative ----- */
void cpu_6809::lbge(void)
{
	LBRANCH(!NXORV);
}

/* $2D BLT relative ----- */
void cpu_6809::blt(void)
{
	BRANCH(NXORV);
}

/* $102D LBLT relative ----- */
void cpu_6809::lblt(void)
{
	LBRANCH(NXORV);
}

/* $2E BGT relative ----- */
void cpu_6809::bgt(void)
{
	BRANCH(!(NXORV || (CC & CC_Z)));
}

/* $102E LBGT relative ----- */
void cpu_6809::lbgt(void)
{
	LBRANCH(!(NXORV || (CC & CC_Z)));
}

/* $2F BLE relative ----- */
void cpu_6809::ble(void)
{
	BRANCH((NXORV || (CC & CC_Z)));
}

/* $102F LBLE relative ----- */
void cpu_6809::lble(void)
{
	LBRANCH((NXORV || (CC & CC_Z)));
}

/* $30 LEAX indexed --*-- */
void cpu_6809::leax()
{
	fetch_effective_address();
	xreg = eaddr;
	CLR_Z;
	SET_Z(xreg);
}

/* $31 LEAY indexed --*-- */
void cpu_6809::leay()
{
	fetch_effective_address();
	yreg = eaddr; CLR_Z; SET_Z(yreg);
}

/* $32 LEAS indexed ----- */
void cpu_6809::leas()
{
	fetch_effective_address();
	sreg = eaddr;
}

/* $33 LEAU indexed ----- */
void cpu_6809::leau()
{
	fetch_effective_address();
	ureg = eaddr;
}

/* $34 PSHS inherent ----- */
void cpu_6809::pshs()
{
	uint8_t t = 0;
	IMMBYTE(t);
	if (t & 0x80) PUSHWORD(pcreg);
	if (t & 0x40) PUSHWORD(ureg);
	if (t & 0x20) PUSHWORD(yreg);
	if (t & 0x10) PUSHWORD(xreg);
	if (t & 0x08) PUSHBYTE(dpreg);
	if (t & 0x04) PUSHBYTE(breg);
	if (t & 0x02) PUSHBYTE(areg);
	if (t & 0x01) PUSHBYTE(cc);
}

/* 35 PULS inherent ----- */
void cpu_6809::puls()
{
	uint8_t t = 0;
	IMMBYTE(t);
	if (t & 0x01) PULLBYTE(cc);
	if (t & 0x02) PULLBYTE(areg);
	if (t & 0x04) PULLBYTE(breg);
	if (t & 0x08) PULLBYTE(dpreg);
	if (t & 0x10) PULLWORD(xreg);
	if (t & 0x20) PULLWORD(yreg);
	if (t & 0x40) PULLWORD(ureg);
	if (t & 0x80) { PULLWORD(pcreg);  change_pc(pcreg); m6809_ICount -= 2; }
}

/* $36 PSHU inherent ----- */
void cpu_6809::pshu()
{
	uint8_t t = 0;
	IMMBYTE(t);
	if (t & 0x80) PSHUWORD(pcreg);
	if (t & 0x40) PSHUWORD(sreg);
	if (t & 0x20) PSHUWORD(yreg);
	if (t & 0x10) PSHUWORD(xreg);
	if (t & 0x08) PSHUBYTE(dpreg);
	if (t & 0x04) PSHUBYTE(breg);
	if (t & 0x02) PSHUBYTE(areg);
	if (t & 0x01) PSHUBYTE(cc);
}

/* 37 PULU inherent ----- */
void cpu_6809::pulu()
{
	uint8_t t = 0;
	IMMBYTE(t);
	if (t & 0x01) PULUBYTE(cc);
	if (t & 0x02) PULUBYTE(areg);
	if (t & 0x04) PULUBYTE(breg);
	if (t & 0x08) PULUBYTE(dpreg);
	if (t & 0x10) PULUWORD(xreg);
	if (t & 0x20) PULUWORD(yreg);
	if (t & 0x40) PULUWORD(sreg);
	if (t & 0x80) { PULUWORD(pcreg); change_pc(pcreg); m6809_ICount -= 2; }
}

/* $38 ILLEGAL */

/* $39 RTS inherent ----- */
void cpu_6809::rts()
{
	PULLWORD(pcreg);
	change_pc(pcreg);
}

/* $3A ABX inherent ----- */
void cpu_6809::abx()
{
	xreg += breg;
}

/* $3B RTI inherent ##### */
void cpu_6809::rti()
{
	uint8_t t = 0;
	t = cc & 0x80;
	PULLBYTE(cc);
	if (t)
	{
		clockticks6809 += 9;
		PULLBYTE(areg);
		PULLBYTE(breg);
		PULLBYTE(dpreg);
		PULLWORD(xreg);
		PULLWORD(yreg);
		PULLWORD(ureg);
	}
	PULLWORD(pcreg);
	change_pc(pcreg);
}

/* $3C CWAI inherent ----1 */
void cpu_6809::cwai()
{
	uint8_t t = 0;
	IMMBYTE(t);
	cc &= t;
	/* CWAI should stack the entire machine state on the hardware stack,
		then wait for an interrupt. A poor fake is to force an IRQ. */
	m6809_ICount = 0;
}

/* $3D MUL inherent --*-@ */
void cpu_6809::mul()
{
	uint16_t t;
	t = areg * breg;
	CLR_ZC; SET_Z16(t); if (t & 0x80) SEC;
	SETDREG(t);
}

/* $3E ILLEGAL */

/* $3F SWI (SWI2 SWI3) absolute indirect ----- */
void cpu_6809::swi()
{
	CC |= CC_E;
	PUSHWORD(pcreg);
	PUSHWORD(ureg);
	PUSHWORD(yreg);
	PUSHWORD(xreg);
	PUSHBYTE(dpreg);
	PUSHBYTE(breg);
	PUSHBYTE(areg);
	PUSHBYTE(cc);
	CC |= CC_IF | CC_II;	/* inhibit FIRQ and IRQ */
	pcreg = M_RDMEM_WORD(0xfffa);
	change_pc(pcreg);
}

/* $103F SWI2 absolute indirect ----- */
void cpu_6809::swi2()
{
	cc |= 0x80;
	PUSHWORD(pcreg);
	PUSHWORD(ureg);
	PUSHWORD(yreg);
	PUSHWORD(xreg);
	PUSHBYTE(dpreg);
	PUSHBYTE(breg);
	PUSHBYTE(areg);
	PUSHBYTE(cc);
	pcreg = M_RDMEM_WORD(0xfff4);
	change_pc(pcreg);
}

/* $113F SWI3 absolute indirect ----- */
void cpu_6809::swi3()
{
	cc |= 0x80;
	PUSHWORD(pcreg);
	PUSHWORD(ureg);
	PUSHWORD(yreg);
	PUSHWORD(xreg);
	PUSHBYTE(dpreg);
	PUSHBYTE(breg);
	PUSHBYTE(areg);
	PUSHBYTE(cc);
	pcreg = M_RDMEM_WORD(0xfff2);
	change_pc(pcreg);
}

/* $40 NEGA inherent ?**** */
void cpu_6809::nega()
{
	uint16_t r;
	r = -areg;
	CLR_NZVC; SET_FLAGS8(0, areg, r);
	areg = r;
}

/* $41 ILLEGAL */

/* $42 ILLEGAL */

/* $43 COMA inherent -**01 */
void cpu_6809::coma()
{
	areg = ~areg;
	CLR_NZV; SET_NZ8(areg); SEC;
}

/* $44 LSRA inherent -0*-* */
void cpu_6809::lsra()
{
	CLR_NZC; cc |= (areg & 0x01);
	areg >>= 1; SET_Z8(areg);
}

/* $45 ILLEGAL */

/* $46 RORA inherent -**-* */
void cpu_6809::rora()
{
	uint8_t r;
	r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (areg & 0x01);
	r |= areg >> 1; SET_NZ8(r);
	areg = r;
}

/* $47 ASRA inherent ?**-* */
void cpu_6809::asra()
{
	CLR_NZC; cc |= (areg & 0x01);
	areg >>= 1; areg |= ((areg & 0x40) << 1);
	SET_NZ8(areg);
}

/* $48 ASLA inherent ?**** */
void cpu_6809::asla()
{
	uint16_t r;
	r = areg << 1;
	CLR_NZVC; SET_FLAGS8(areg, areg, r);
	areg = r;
}

/* $49 ROLA inherent -**** */
void cpu_6809::rola()
{
	uint16_t t, r;
	t = areg; r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	areg = r;
}

/* $4A DECA inherent -***- */
void cpu_6809::deca()
{
	--areg;
	CLR_NZV; if (areg == 0x7F) SEV; SET_NZ8(areg);
}

/* $4B ILLEGAL */

/* $4C INCA inherent -***- */
void cpu_6809::inca()
{
	++areg;
	CLR_NZV; if (areg == 0x80) SEV; SET_NZ8(areg);
}

/* $4D TSTA inherent -**0- */
void cpu_6809::tsta()
{
	CLR_NZV; SET_NZ8(areg);
}

/* $4E ILLEGAL */

/* $4F CLRA inherent -0100 */
void cpu_6809::clra()
{
	areg = 0;
	CLR_NZVC; SEZ;
}

/* $50 NEGB inherent ?**** */
void cpu_6809::negb()
{
	uint16_t r;
	r = -breg;
	CLR_NZVC; SET_FLAGS8(0, breg, r);
	breg = r;
}

/* $51 ILLEGAL */

/* $52 ILLEGAL */

/* $53 COMB inherent -**01 */
void cpu_6809::comb()
{
	breg = ~breg;
	CLR_NZV; SET_NZ8(breg); SEC;
}

/* $54 LSRB inherent -0*-* */
void cpu_6809::lsrb()
{
	CLR_NZC; cc |= (breg & 0x01);
	breg >>= 1; SET_Z8(breg);
}

/* $55 ILLEGAL */

/* $56 RORB inherent -**-* */
void cpu_6809::rorb()
{
	uint8_t r;
	r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (breg & 0x01);
	r |= breg >> 1; SET_NZ8(r);
	breg = r;
}

/* $57 ASRB inherent ?**-* */
void cpu_6809::asrb()
{
	CLR_NZC; cc |= (breg & 0x01);
	breg >>= 1; breg |= ((breg & 0x40) << 1);
	SET_NZ8(breg);
}

/* $58 ASLB inherent ?**** */
void cpu_6809::aslb()
{
	uint16_t r;
	r = breg << 1;
	CLR_NZVC; SET_FLAGS8(breg, breg, r);
	breg = r;
}

/* $59 ROLB inherent -**** */
void cpu_6809::rolb()
{
	uint16_t t, r;
	t = breg; r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	breg = r;
}

/* $5A DECB inherent -***- */
void cpu_6809::decb()
{
	--breg;
	CLR_NZV; if (breg == 0x7F) SEV; SET_NZ8(breg);
}

/* $5B ILLEGAL */

/* $5C INCB inherent -***- */
void cpu_6809::incb()
{
	++breg;
	CLR_NZV; if (breg == 0x80) SEV; SET_NZ8(breg);
}

/* $5D TSTB inherent -**0- */
void cpu_6809::tstb()
{
	CLR_NZV; SET_NZ8(breg);
}

/* $5E ILLEGAL */

/* $5F CLRB inherent -0100 */
void cpu_6809::clrb()
{
	breg = 0;
	CLR_NZVC; SEZ;
}

/* $60 NEG indexed ?**** */
void cpu_6809::neg_ix()
{
	uint16_t r, t;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = -t;
	CLR_NZVC; SET_FLAGS8(0, t, r);
	M_WRMEM(eaddr, r);
}

/* $61 ILLEGAL */

/* $62 ILLEGAL */

/* $63 COM indexed -**01 */
void cpu_6809::com_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = ~M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $64 LSR indexed -0*-* */
void cpu_6809::lsr_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $65 ILLEGAL */

/* $66 ROR indexed -**-* */
void cpu_6809::ror_ix()
{
	uint8_t t = 0; uint8_t r = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $67 ASR indexed ?**-* */
void cpu_6809::asr_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; t |= ((t & 0x40) << 1);
	SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $68 ASL indexed ?**** */
void cpu_6809::asl_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $69 ROL indexed -**** */
void cpu_6809::rol_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $6A DEC indexed -***- */
void cpu_6809::dec_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr) - 1;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $6B ILLEGAL */

/* $6C INC indexed -***- */
void cpu_6809::inc_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr) + 1;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $6D TST indexed -**0- */
void cpu_6809::tst_ix()
{
	uint8_t t = 0;
	fetch_effective_address();
	t = M_RDMEM(eaddr); CLR_NZV; SET_NZ8(t);
}

/* $6E JMP indexed ----- */
void cpu_6809::jmp_ix()
{
	fetch_effective_address();
	pcreg = eaddr;
	change_pc(pcreg);
}

/* $6F CLR indexed -0100 */
void cpu_6809::clr_ix()
{
	fetch_effective_address();
	M_WRMEM(eaddr, 0);
	CLR_NZVC; SEZ;
}

/* $70 NEG extended ?**** */
void cpu_6809::neg_ex()
{
	uint16_t r, t;
	EXTBYTE(t);
	r = -t;
	CLR_NZVC; SET_FLAGS8(0, t, r);
	M_WRMEM(eaddr, r);
}

/* $71 ILLEGAL */

/* $72 ILLEGAL */

/* $73 COM extended -**01 */
void cpu_6809::com_ex()
{
	uint8_t t = 0;
	EXTBYTE(t);
	t = ~t;
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $74 LSR extended -0*-* */
void cpu_6809::lsr_ex()
{
	uint8_t t = 0;
	EXTBYTE(t);
	CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $75 ILLEGAL */

/* $76 ROR extended -**-* */
void cpu_6809::ror_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	EXTBYTE(t);
	r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $77 ASR extended ?**-* */
void cpu_6809::asr_ex()
{
	uint8_t t = 0;
	eaddr = M_RDMEM_WORD(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	CLR_NZC; cc |= (t & 0x01);
	t >>= 1; t |= ((t & 0x40) << 1);
	SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $78 ASL extended ?**** */
void cpu_6809::asl_ex()
{
	uint16_t t, r;
	EXTBYTE(t);
	r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $79 ROL extended -**** */
void cpu_6809::rol_ex()
{
	uint16_t t, r;
	EXTBYTE(t);
	r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $7A DEC extended -***- */
void cpu_6809::dec_ex()
{
	uint8_t t = 0;
	EXTBYTE(t);
	--t;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $7B ILLEGAL */

/* $7C INC extended -***- */
void cpu_6809::inc_ex()
{
	uint8_t t = 0;
	EXTBYTE(t);
	++t;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $7D TST extended -**0- */
void cpu_6809::tst_ex()
{
	uint8_t t = 0;
	EXTBYTE(t);
	CLR_NZV; SET_NZ8(t);
}

/* $7E JMP extended ----- */
void cpu_6809::jmp_ex()
{
	EXTENDED;
	pcreg = eaddr;
	change_pc(pcreg);
}

/* $7F CLR extended -0100 */
void cpu_6809::clr_ex()
{
	EXTENDED; M_WRMEM(eaddr, 0);
	CLR_NZVC; SEZ;
}

/* $80 SUBA immediate ?**** */
void cpu_6809::suba_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $81 CMPA immediate ?**** */
void cpu_6809::cmpa_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $82 SBCA immediate ?**** */
void cpu_6809::sbca_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $83 SUBD (CMPD CMPU) immediate -**** */
void cpu_6809::subd_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $1083 CMPD immediate -**** */
void cpu_6809::cmpd_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $1183 CMPU immediate -**** */
void cpu_6809::cmpu_im()
{
	unsigned int r, b;
	IMMWORD(b);
	r = ureg - b;
	CLR_NZVC;
	SET_FLAGS16(ureg, b, r);
}

/* $84 ANDA immediate -**0- */
void cpu_6809::anda_im()
{
	uint8_t t = 0;
	IMMBYTE(t);
	areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $85 BITA immediate -**0- */
void cpu_6809::bita_im()
{
	uint8_t t = 0;
	uint8_t r = 0;
	IMMBYTE(t);
	r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $86 LDA immediate -**0- */
void cpu_6809::lda_im()
{
	IMMBYTE(areg);
	CLR_NZV;
	SET_NZ8(areg);
}

/* is this a legal instruction? */
/* $87 STA immediate -**0- */
void cpu_6809::sta_im()
{
	CLR_NZV; SET_NZ8(areg);
	IMM8; M_WRMEM(eaddr, areg);
}

/* $88 EORA immediate -**0- */
void cpu_6809::eora_im()
{
	uint8_t t = 0;
	IMMBYTE(t); areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $89 ADCA immediate ***** */
void cpu_6809::adca_im()
{
	uint16_t t, r;
	IMMBYTE(t); r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $8A ORA immediate -**0- */
void cpu_6809::ora_im()
{
	uint8_t t = 0;
	IMMBYTE(t); areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $8B ADDA immediate ***** */
void cpu_6809::adda_im()
{
	uint16_t t, r;
	IMMBYTE(t); r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $8C CMPX (CMPY CMPS) immediate -**** */
void cpu_6809::cmpx_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $108C CMPY immediate -**** */
void cpu_6809::cmpy_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $118C CMPS immediate -**** */
void cpu_6809::cmps_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $8D BSR ----- */
void cpu_6809::bsr()
{
	uint8_t t = 0;
	IMMBYTE(t); PUSHWORD(pcreg);
	pcreg += SIGNED(t); change_pc(pcreg);
}

/* $8E LDX (LDY) immediate -**0- */
void cpu_6809::ldx_im()
{
	IMMWORD(xreg);
	CLR_NZV; SET_NZ16(xreg);
}

/* $108E LDY immediate -**0- */
void cpu_6809::ldy_im()
{
	IMMWORD(yreg);
	CLR_NZV; SET_NZ16(yreg);
}

/* is this a legal instruction? */
/* $8F STX (STY) immediate -**0- */
void cpu_6809::stx_im()
{
	CLR_NZV; SET_NZ16(xreg);
	IMM16;
	M_WRMEM_WORD(eaddr, xreg);
}

/* is this a legal instruction? */
/* $108F STY immediate -**0- */
void cpu_6809::sty_im()
{
	CLR_NZV; SET_NZ16(yreg);
	IMM16; M_WRMEM_WORD(eaddr, yreg);
}

/* $90 SUBA direct ?**** */
void cpu_6809::suba_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $91 CMPA direct ?**** */
void cpu_6809::cmpa_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $92 SBCA direct ?**** */
void cpu_6809::sbca_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $93 SUBD (CMPD CMPU) direct -**** */
void cpu_6809::subd_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $1093 CMPD direct -**** */
void cpu_6809::cmpd_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $1193 CMPU direct -**** */
void cpu_6809::cmpu_di()
{
	uint32_t r, b;
	DIRWORD(b); r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $94 ANDA direct -**0- */
void cpu_6809::anda_di()
{
	uint8_t t = 0;
	DIRBYTE(t); areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $95 BITA direct -**0- */
void cpu_6809::bita_di()
{
	uint8_t t = 0; uint8_t r = 0;
	DIRBYTE(t); r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $96 LDA direct -**0- */
void cpu_6809::lda_di()
{
	DIRBYTE(areg);
	CLR_NZV; SET_NZ8(areg);
}

/* $97 STA direct -**0- */
void cpu_6809::sta_di()
{
	CLR_NZV; SET_NZ8(areg);
	DIRECT; M_WRMEM(eaddr, areg);
}

/* $98 EORA direct -**0- */
void cpu_6809::eora_di()
{
	uint8_t t = 0;
	DIRBYTE(t); areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $99 ADCA direct ***** */
void cpu_6809::adca_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $9A ORA direct -**0- */
void cpu_6809::ora_di()
{
	uint8_t t = 0;
	DIRBYTE(t); areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $9B ADDA direct ***** */
void cpu_6809::adda_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $9C CMPX (CMPY CMPS) direct -**** */
void cpu_6809::cmpx_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $109C CMPY direct -**** */
void cpu_6809::cmpy_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $119C CMPS direct -**** */
void cpu_6809::cmps_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $9D JSR direct ----- */
void cpu_6809::jsr_di()
{
	DIRECT; PUSHWORD(pcreg);
	pcreg = eaddr; change_pc(pcreg);
}

/* $9E LDX (LDY) direct -**0- */
void cpu_6809::ldx_di()
{
	DIRWORD(xreg);
	CLR_NZV; SET_NZ16(xreg);
}

/* $109E LDY direct -**0- */
void cpu_6809::ldy_di()
{
	DIRWORD(yreg);
	CLR_NZV; SET_NZ16(yreg);
}

/* $9F STX (STY) direct -**0- */
void cpu_6809::stx_di()
{
	CLR_NZV; SET_NZ16(xreg);
	DIRECT; M_WRMEM_WORD(eaddr, xreg);
}

/* $109F STY direct -**0- */
void cpu_6809::sty_di()
{
	CLR_NZV; SET_NZ16(yreg);
	DIRECT; M_WRMEM_WORD(eaddr, yreg);
}

/* $a0 SUBA indexed ?**** */
void cpu_6809::suba_ix()
{
	uint16_t	t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $a1 CMPA indexed ?**** */
void cpu_6809::cmpa_ix()
{
	uint16_t	t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $a2 SBCA indexed ?**** */
void cpu_6809::sbca_ix()
{
	uint16_t	t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $a3 SUBD (CMPD CMPU) indexed -**** */
void cpu_6809::subd_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $10a3 CMPD indexed -**** */
void cpu_6809::cmpd_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11a3 CMPU indexed -**** */
void cpu_6809::cmpu_ix()
{
	uint32_t r, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $a4 ANDA indexed -**0- */
void cpu_6809::anda_ix()
{
	fetch_effective_address();
	areg &= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a5 BITA indexed -**0- */
void cpu_6809::bita_ix()
{
	uint8_t r;
	fetch_effective_address();
	r = areg & M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(r);
}

/* $a6 LDA indexed -**0- */
void cpu_6809::lda_ix()
{
	fetch_effective_address();
	areg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a7 STA indexed -**0- */
void cpu_6809::sta_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ8(areg);
	M_WRMEM(eaddr, areg);
}

/* $a8 EORA indexed -**0- */
void cpu_6809::eora_ix()
{
	fetch_effective_address();
	areg ^= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a9 ADCA indexed ***** */
void cpu_6809::adca_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $aA ORA indexed -**0- */
void cpu_6809::ora_ix()
{
	fetch_effective_address();
	areg |= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $aB ADDA indexed ***** */
void cpu_6809::adda_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $aC CMPX (CMPY CMPS) indexed -**** */
void cpu_6809::cmpx_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $10aC CMPY indexed -**** */
void cpu_6809::cmpy_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11aC CMPS indexed -**** */
void cpu_6809::cmps_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $aD JSR indexed ----- */
void cpu_6809::jsr_ix()
{
	fetch_effective_address();
	PUSHWORD(pcreg);
	pcreg = eaddr;
	change_pc(pcreg);
}

/* $aE LDX (LDY) indexed -**0- */
void cpu_6809::ldx_ix()
{
	fetch_effective_address();
	xreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(xreg);
}

/* $10aE LDY indexed -**0- */
void cpu_6809::ldy_ix()
{
	fetch_effective_address();
	yreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(yreg);
}

/* $aF STX (STY) indexed -**0- */
void cpu_6809::stx_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ16(xreg);
	M_WRMEM_WORD(eaddr, xreg);
}

/* $10aF STY indexed -**0- */
void cpu_6809::sty_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ16(yreg);
	M_WRMEM_WORD(eaddr, yreg);
}

/* $b0 SUBA extended ?**** */
void cpu_6809::suba_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $b1 CMPA extended ?**** */
void cpu_6809::cmpa_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $b2 SBCA extended ?**** */
void cpu_6809::sbca_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $b3 SUBD (CMPD CMPU) extended -**** */
void cpu_6809::subd_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $10b3 CMPD extended -**** */
void cpu_6809::cmpd_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11b3 CMPU extended -**** */
void cpu_6809::cmpu_ex()
{
	uint32_t r, b;
	EXTWORD(b); r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $b4 ANDA extended -**0- */
void cpu_6809::anda_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $b5 BITA extended -**0- */
void cpu_6809::bita_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	EXTBYTE(t); r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $b6 LDA extended -**0- */
void cpu_6809::lda_ex()
{
	EXTBYTE(areg);
	CLR_NZV; SET_NZ8(areg);
}

/* $b7 STA extended -**0- */
void cpu_6809::sta_ex()
{
	CLR_NZV; SET_NZ8(areg);
	EXTENDED; M_WRMEM(eaddr, areg);
}

/* $b8 EORA extended -**0- */
void cpu_6809::eora_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $b9 ADCA extended ***** */
void cpu_6809::adca_ex()
{
	uint16_t t, r;
	EXTBYTE(t); r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $bA ORA extended -**0- */
void cpu_6809::ora_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $bB ADDA extended ***** */
void cpu_6809::adda_ex()
{
	uint16_t t, r;
	EXTBYTE(t); r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $bC CMPX (CMPY CMPS) extended -**** */
void cpu_6809::cmpx_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $10bC CMPY extended -**** */
void cpu_6809::cmpy_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11bC CMPS extended -**** */
void cpu_6809::cmps_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $bD JSR extended ----- */
void cpu_6809::jsr_ex()
{
	EXTENDED; PUSHWORD(pcreg);
	pcreg = eaddr; change_pc(pcreg);
}

/* $bE LDX (LDY) extended -**0- */
void cpu_6809::ldx_ex()
{
	EXTWORD(xreg);
	CLR_NZV; SET_NZ16(xreg);
}

/* $10bE LDY extended -**0- */
void cpu_6809::ldy_ex()
{
	EXTWORD(yreg);
	CLR_NZV; SET_NZ16(yreg);
}

/* $bF STX (STY) extended -**0- */
void cpu_6809::stx_ex()
{
	CLR_NZV; SET_NZ16(xreg);
	EXTENDED; M_WRMEM_WORD(eaddr, xreg);
}

/* $10bF STY extended -**0- */
void cpu_6809::sty_ex()
{
	CLR_NZV; SET_NZ16(yreg);
	EXTENDED; M_WRMEM_WORD(eaddr, yreg);
}

/* $c0 SUBB immediate ?**** */
void cpu_6809::subb_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $c1 CMPB immediate ?**** */
void cpu_6809::cmpb_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $c2 SBCB immediate ?**** */
void cpu_6809::sbcb_im()
{
	uint16_t	t, r;
	IMMBYTE(t); r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $c3 ADDD immediate -**** */
void cpu_6809::addd_im()
{
	uint32_t r, d, b;
	IMMWORD(b); d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $c4 ANDB immediate -**0- */
void cpu_6809::andb_im()
{
	uint8_t t = 0;
	IMMBYTE(t); breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $c5 BITB immediate -**0- */
void cpu_6809::bitb_im()
{
	uint8_t t = 0; uint8_t r = 0;
	IMMBYTE(t); r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $c6 LDB immediate -**0- */
void cpu_6809::ldb_im()
{
	IMMBYTE(breg);
	CLR_NZV; SET_NZ8(breg);
}

/* is this a legal instruction? */
/* $c7 STB immediate -**0- */
void cpu_6809::stb_im()
{
	CLR_NZV; SET_NZ8(breg);
	IMM8; M_WRMEM(eaddr, breg);
}

/* $c8 EORB immediate -**0- */
void cpu_6809::eorb_im()
{
	uint8_t t = 0;
	IMMBYTE(t); breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $c9 ADCB immediate ***** */
void cpu_6809::adcb_im()
{
	uint16_t t, r;
	IMMBYTE(t); r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $cA ORB immediate -**0- */
void cpu_6809::orb_im()
{
	uint8_t t = 0;
	IMMBYTE(t); breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $cB ADDB immediate ***** */
void cpu_6809::addb_im()
{
	uint16_t t, r;
	IMMBYTE(t); r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $cC LDD immediate -**0- */
void cpu_6809::ldd_im()
{
	uint16_t t;
	IMMWORD(t); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* is this a legal instruction? */
/* $cD STD immediate -**0- */
void cpu_6809::std_im()
{
	uint16_t t;
	IMM16; t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $cE LDU (LDS) immediate -**0- */
void cpu_6809::ldu_im()
{
	IMMWORD(ureg);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10cE LDS immediate -**0- */
void cpu_6809::lds_im()
{
	IMMWORD(sreg);
	CLR_NZV; SET_NZ16(sreg);
}

/* is this a legal instruction? */
/* $cF STU (STS) immediate -**0- */
void cpu_6809::stu_im()
{
	CLR_NZV; SET_NZ16(ureg);
	IMM16; M_WRMEM_WORD(eaddr, ureg);
}

/* is this a legal instruction? */
/* $10cF STS immediate -**0- */
void cpu_6809::sts_im()
{
	CLR_NZV; SET_NZ16(sreg);
	IMM16; M_WRMEM_WORD(eaddr, sreg);
}

/* $d0 SUBB direct ?**** */
void cpu_6809::subb_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $d1 CMPB direct ?**** */
void cpu_6809::cmpb_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $d2 SBCB direct ?**** */
void cpu_6809::sbcb_di()
{
	uint16_t	t, r;
	DIRBYTE(t); r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $d3 ADDD direct -**** */
void cpu_6809::addd_di()
{
	uint32_t r, d, b;
	DIRWORD(b); d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $d4 ANDB direct -**0- */
void cpu_6809::andb_di()
{
	uint8_t t = 0;
	DIRBYTE(t); breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $d5 BITB direct -**0- */
void cpu_6809::bitb_di()
{
	uint8_t t = 0; uint8_t r = 0;
	DIRBYTE(t); r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $d6 LDB direct -**0- */
void cpu_6809::ldb_di()
{
	DIRBYTE(breg);
	CLR_NZV; SET_NZ8(breg);
}

/* $d7 STB direct -**0- */
void cpu_6809::stb_di()
{
	CLR_NZV; SET_NZ8(breg);
	DIRECT; M_WRMEM(eaddr, breg);
}

/* $d8 EORB direct -**0- */
void cpu_6809::eorb_di()
{
	uint8_t t = 0;
	DIRBYTE(t); breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $d9 ADCB direct ***** */
void cpu_6809::adcb_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $dA ORB direct -**0- */
void cpu_6809::orb_di()
{
	uint8_t t = 0;
	DIRBYTE(t); breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $dB ADDB direct ***** */
void cpu_6809::addb_di()
{
	uint16_t t, r;
	DIRBYTE(t); r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $dC LDD direct -**0- */
void cpu_6809::ldd_di()
{
	uint16_t t;
	DIRWORD(t); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $dD STD direct -**0- */
void cpu_6809::std_di()
{
	uint16_t t;
	DIRECT; t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $dE LDU (LDS) direct -**0- */
void cpu_6809::ldu_di()
{
	DIRWORD(ureg);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10dE LDS direct -**0- */
void cpu_6809::lds_di()
{
	DIRWORD(sreg);
	CLR_NZV; SET_NZ16(sreg);
}

/* $dF STU (STS) direct -**0- */
void cpu_6809::stu_di()
{
	CLR_NZV;
	SET_NZ16(ureg);
	DIRECT;	M_WRMEM_WORD(eaddr, ureg);
}

/* $10dF STS direct -**0- */
void cpu_6809::sts_di()
{
	CLR_NZV;
	CLR_NZV; SET_NZ16(sreg);
	DIRECT; M_WRMEM_WORD(eaddr, sreg);
}

/* $e0 SUBB indexed ?**** */
void cpu_6809::subb_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $e1 CMPB indexed ?**** */
void cpu_6809::cmpb_ix()
{
	uint16_t	t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $e2 SBCB indexed ?**** */
void cpu_6809::sbcb_ix()
{
	uint16_t	t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $e3 ADDD indexed -**** */
void cpu_6809::addd_ix()
{
	uint32_t r, d, b;
	fetch_effective_address();
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $e4 ANDB indexed -**0- */
void cpu_6809::andb_ix()
{
	fetch_effective_address();
	breg &= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e5 BITB indexed -**0- */
void cpu_6809::bitb_ix()
{
	uint8_t r;
	fetch_effective_address();
	r = breg & M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(r);
}

/* $e6 LDB indexed -**0- */
void cpu_6809::ldb_ix()
{
	fetch_effective_address();
	breg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e7 STB indexed -**0- */
void cpu_6809::stb_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ8(breg);
	M_WRMEM(eaddr, breg);
}

/* $e8 EORB indexed -**0- */
void cpu_6809::eorb_ix()
{
	fetch_effective_address();
	breg ^= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e9 ADCB indexed ***** */
void cpu_6809::adcb_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $eA ORB indexed -**0- */
void cpu_6809::orb_ix()
{
	fetch_effective_address();
	breg |= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $eB ADDB indexed ***** */
void cpu_6809::addb_ix()
{
	uint16_t t, r;
	fetch_effective_address();
	t = M_RDMEM(eaddr); r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $eC LDD indexed -**0- */
void cpu_6809::ldd_ix()
{
	uint16_t t;
	fetch_effective_address();
	t = M_RDMEM_WORD(eaddr); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $eD STD indexed -**0- */
void cpu_6809::std_ix()
{
	uint16_t t;
	fetch_effective_address();
	t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $eE LDU (LDS) indexed -**0- */
void cpu_6809::ldu_ix()
{
	fetch_effective_address();
	ureg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10eE LDS indexed -**0- */
void cpu_6809::lds_ix()
{
	fetch_effective_address();
	sreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(sreg);
}

/* $eF STU (STS) indexed -**0- */
void cpu_6809::stu_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ16(ureg);
	M_WRMEM_WORD(eaddr, ureg);
}

/* $10eF STS indexed -**0- */
void cpu_6809::sts_ix()
{
	fetch_effective_address();
	CLR_NZV; SET_NZ16(sreg);
	M_WRMEM_WORD(eaddr, sreg);
}

/* $f0 SUBB extended ?**** */
void cpu_6809::subb_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $f1 CMPB extended ?**** */
void cpu_6809::cmpb_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $f2 SBCB extended ?**** */
void cpu_6809::sbcb_ex()
{
	uint16_t	t, r;
	EXTBYTE(t); r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $f3 ADDD extended -**** */
void cpu_6809::addd_ex()
{
	uint32_t r, d, b;
	EXTWORD(b); d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $f4 ANDB extended -**0- */
void cpu_6809::andb_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $f5 BITB extended -**0- */
void cpu_6809::bitb_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	EXTBYTE(t); r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $f6 LDB extended -**0- */
void cpu_6809::ldb_ex()
{
	EXTBYTE(breg);
	CLR_NZV; SET_NZ8(breg);
}

/* $f7 STB extended -**0- */
void cpu_6809::stb_ex()
{
	CLR_NZV; SET_NZ8(breg);
	EXTENDED; M_WRMEM(eaddr, breg);
}

/* $f8 EORB extended -**0- */
void cpu_6809::eorb_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $f9 ADCB extended ***** */
void cpu_6809::adcb_ex()
{
	uint16_t t, r;
	EXTBYTE(t); r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $fA ORB extended -**0- */
void cpu_6809::orb_ex()
{
	uint8_t t = 0;
	EXTBYTE(t); breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $fB ADDB extended ***** */
void cpu_6809::addb_ex()
{
	uint16_t t, r;
	EXTBYTE(t); r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $fC LDD extended -**0- */
void cpu_6809::ldd_ex()
{
	uint16_t t;
	EXTWORD(t); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $fD STD extended -**0- */
void cpu_6809::std_ex()
{
	uint16_t t;
	EXTENDED; t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $fE LDU (LDS) extended -**0- */
void cpu_6809::ldu_ex()
{
	EXTWORD(ureg);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10fE LDS extended -**0- */
void cpu_6809::lds_ex()
{
	EXTWORD(sreg);
	CLR_NZV; SET_NZ16(sreg);
}

/* $fF STU (STS) extended -**0- */
void cpu_6809::stu_ex()
{
	CLR_NZV; SET_NZ16(ureg);
	EXTENDED;
	M_WRMEM_WORD(eaddr, ureg);
}

/* $10fF STS extended -**0- */
void cpu_6809::sts_ex()
{
	CLR_NZV; SET_NZ16(sreg);
	EXTENDED;
	M_WRMEM_WORD(eaddr, sreg);
}

/****************************************************************************/
/* Set all registers to given values                                        */
/****************************************************************************/
void cpu_6809::m6809_SetRegs(m6809_Regs* Regs)
{
	pcreg = Regs->pc; //change_pc(pcreg);
	ureg = Regs->u;
	sreg = Regs->s;
	xreg = Regs->x;
	yreg = Regs->y;
	dpreg = Regs->dp;
	areg = Regs->a;
	breg = Regs->b;
	cc = Regs->cc;
	pending_interrupts = Regs->pending_interrupts;
}

/****************************************************************************/
/* Get all registers in given buffer                                        */
/****************************************************************************/
void cpu_6809::m6809_GetRegs(m6809_Regs* Regs)
{
	Regs->pc = pcreg;
	Regs->u = ureg;
	Regs->s = sreg;
	Regs->x = xreg;
	Regs->y = yreg;
	Regs->dp = dpreg;
	Regs->a = areg;
	Regs->b = breg;
	Regs->cc = cc;
	Regs->pending_interrupts = pending_interrupts;
}

// Reset MyCpu
void cpu_6809::reset6809()
{
	ppc = -1;
	pcreg = M_RDMEM_WORD(0xfffe);
	slapstic_en = 0;	change_pc(pcreg);

	dpreg = 0x0;		/* Direct page register = 0x00 */
	cc = 0x00;			/* Clear all flags */
	cc |= 0x10;			/* IRQ disabled */
	cc |= 0x40;			/* FIRQ disabled */
	areg = 0x00;		/* clear accumulator a */
	breg = 0x00;		/* clear accumulator b */
	clockticks6809 = 0;// m6809_IPeriod;
	m6809_Clear_Pending_Interrupts();	/* NS 970908 */
	m6809.extra_cycles = 0;
	//m6809_IRequest = INT_NONE;
	m6809_ICount = 0;
}

void cpu_6809::m6809_Cause_Interrupt(int type)	/* NS 970908 */
{
	pending_interrupts |= type;
	if (type & (M6809_INT_NMI | M6809_INT_IRQ | M6809_INT_FIRQ))
	{
		pending_interrupts &= ~M6809_SYNC;
		if (pending_interrupts & M6809_CWAI)
		{
			if ((pending_interrupts & M6809_INT_NMI) != 0)
				pending_interrupts &= ~M6809_CWAI;
			else if ((pending_interrupts & M6809_INT_IRQ) != 0 && (cc & 0x10) == 0)
				pending_interrupts &= ~M6809_CWAI;
			else if ((pending_interrupts & M6809_INT_FIRQ) != 0 && (cc & 0x40) == 0)
				pending_interrupts &= ~M6809_CWAI;
		}
	}
}
void cpu_6809::m6809_Clear_Pending_Interrupts()	/* NS 970908 */
{
	pending_interrupts &= ~(M6809_INT_IRQ | M6809_INT_FIRQ | M6809_INT_NMI);
}

/* Generate interrupts */
void cpu_6809::Interrupt()	/* NS 970909 */
{
	if ((pending_interrupts & M6809_INT_NMI) != 0)
	{
		pending_interrupts &= ~M6809_INT_NMI;

		/* NMI */
		cc |= 0x80;	/* ASG 971016 */
		PUSHWORD(pcreg);
		PUSHWORD(ureg);
		PUSHWORD(yreg);
		PUSHWORD(xreg);
		PUSHBYTE(dpreg);
		PUSHBYTE(breg);
		PUSHBYTE(areg);
		PUSHBYTE(cc);
		cc |= 0xd0;
		pcreg = M_RDMEM_WORD(0xfffc);
		change_pc(pcreg);	/* TS 971002 */
		m6809.extra_cycles += 19;
	}
	else if ((pending_interrupts & M6809_INT_IRQ) != 0 && (cc & 0x10) == 0)
	{
		pending_interrupts &= ~M6809_INT_IRQ;

		/* standard IRQ */
		cc |= 0x80;	/* ASG 971016 */
		PUSHWORD(pcreg);
		PUSHWORD(ureg);
		PUSHWORD(yreg);
		PUSHWORD(xreg);
		PUSHBYTE(dpreg);
		PUSHBYTE(breg);
		PUSHBYTE(areg);
		PUSHBYTE(cc);
		cc |= 0x90;
		pcreg = M_RDMEM_WORD(0xfff8);
		change_pc(pcreg);	/* TS 971002 */
		m6809.extra_cycles += 19;
	}
	else if ((pending_interrupts & M6809_INT_FIRQ) != 0 && (cc & 0x40) == 0)
	{
		pending_interrupts &= ~M6809_INT_FIRQ;

		/* fast IRQ */
		PUSHWORD(pcreg);
		cc &= 0x7f;	/* ASG 971016 */
		PUSHBYTE(cc);
		cc |= 0x50;
		pcreg = M_RDMEM_WORD(0xfff6);
		change_pc(pcreg);	/* TS 971002 */
		m6809.extra_cycles += 10;
	}
}

void cpu_6809::fetch_effective_address()
{
	uint8_t postbyte = rd_slow(pcreg++);

	switch (postbyte)
	{
	case 0x00: eaddr = xreg; break;
	case 0x01: eaddr = xreg + 1; m6809_ICount -= 1; break;
	case 0x02: eaddr = xreg + 2; m6809_ICount -= 1; break;
	case 0x03: eaddr = xreg + 3; m6809_ICount -= 1; break;
	case 0x04: eaddr = xreg + 4; m6809_ICount -= 1; break;
	case 0x05: eaddr = xreg + 5; m6809_ICount -= 1; break;
	case 0x06: eaddr = xreg + 6; m6809_ICount -= 1; break;
	case 0x07: eaddr = xreg + 7; m6809_ICount -= 1;  break;
	case 0x08: eaddr = xreg + 8; m6809_ICount -= 1; break;
	case 0x09: eaddr = xreg + 9; m6809_ICount -= 1; break;
	case 0x0A: eaddr = xreg + 10; m6809_ICount -= 1; break;
	case 0x0B: eaddr = xreg + 11; m6809_ICount -= 1; break;
	case 0x0C: eaddr = xreg + 12; m6809_ICount -= 1; break;
	case 0x0D: eaddr = xreg + 13; m6809_ICount -= 1; break;
	case 0x0E: eaddr = xreg + 14; m6809_ICount -= 1; break;
	case 0x0F: eaddr = xreg + 15; m6809_ICount -= 1; break;
	case 0x10: eaddr = xreg - 16; m6809_ICount -= 1;  break;
	case 0x11: eaddr = xreg - 15; m6809_ICount -= 1;  break;
	case 0x12: eaddr = xreg - 14; m6809_ICount -= 1; break;
	case 0x13: eaddr = xreg - 13; m6809_ICount -= 1;  break;
	case 0x14: eaddr = xreg - 12; m6809_ICount -= 1;  break;
	case 0x15: eaddr = xreg - 11; m6809_ICount -= 1; break;
	case 0x16: eaddr = xreg - 10; m6809_ICount -= 1; break;
	case 0x17: eaddr = xreg - 9; m6809_ICount -= 1; break;
	case 0x18: eaddr = xreg - 8; m6809_ICount -= 1; break;
	case 0x19: eaddr = xreg - 7; m6809_ICount -= 1; break;
	case 0x1A: eaddr = xreg - 6; m6809_ICount -= 1; break;
	case 0x1B: eaddr = xreg - 5; m6809_ICount -= 1; break;
	case 0x1C: eaddr = xreg - 4; m6809_ICount -= 1; break;
	case 0x1D: eaddr = xreg - 3; m6809_ICount -= 1; break;
	case 0x1E: eaddr = xreg - 2; m6809_ICount -= 1; break;
	case 0x1F: eaddr = xreg - 1; m6809_ICount -= 1; break;
	case 0x20: eaddr = yreg; m6809_ICount -= 1; break;
	case 0x21: eaddr = yreg + 1; m6809_ICount -= 1;  break;
	case 0x22: eaddr = yreg + 2; m6809_ICount -= 1; break;
	case 0x23: eaddr = yreg + 3; m6809_ICount -= 1;  break;
	case 0x24: eaddr = yreg + 4; m6809_ICount -= 1; break;
	case 0x25: eaddr = yreg + 5; m6809_ICount -= 1;  break;
	case 0x26: eaddr = yreg + 6; m6809_ICount -= 1; break;
	case 0x27: eaddr = yreg + 7; m6809_ICount -= 1; break;
	case 0x28: eaddr = yreg + 8; m6809_ICount -= 1; break;
	case 0x29: eaddr = yreg + 9; m6809_ICount -= 1; break;
	case 0x2A: eaddr = yreg + 10; m6809_ICount -= 1; break;
	case 0x2B: eaddr = yreg + 11; m6809_ICount -= 1; break;
	case 0x2C: eaddr = yreg + 12; m6809_ICount -= 1; break;
	case 0x2D: eaddr = yreg + 13; m6809_ICount -= 1;  break;
	case 0x2E: eaddr = yreg + 14; m6809_ICount -= 1;  break;
	case 0x2F: eaddr = yreg + 15; m6809_ICount -= 1; break;
	case 0x30: eaddr = yreg - 16; m6809_ICount -= 1; break;
	case 0x31: eaddr = yreg - 15; m6809_ICount -= 1;  break;
	case 0x32: eaddr = yreg - 14; m6809_ICount -= 1;  break;
	case 0x33: eaddr = yreg - 13; m6809_ICount -= 1;  break;
	case 0x34: eaddr = yreg - 12; m6809_ICount -= 1;  break;
	case 0x35: eaddr = yreg - 11; m6809_ICount -= 1;  break;
	case 0x36: eaddr = yreg - 10; m6809_ICount -= 1; break;
	case 0x37: eaddr = yreg - 9; m6809_ICount -= 1;  break;
	case 0x38: eaddr = yreg - 8; m6809_ICount -= 1;  break;
	case 0x39: eaddr = yreg - 7; m6809_ICount -= 1;  break;
	case 0x3A: eaddr = yreg - 6; m6809_ICount -= 1; break;
	case 0x3B: eaddr = yreg - 5; m6809_ICount -= 1;  break;
	case 0x3C: eaddr = yreg - 4; m6809_ICount -= 1; break;
	case 0x3D: eaddr = yreg - 3; m6809_ICount -= 1; break;
	case 0x3E: eaddr = yreg - 2; m6809_ICount -= 1; break;
	case 0x3F: eaddr = yreg - 1; m6809_ICount -= 1;  break;
	case 0x40: eaddr = ureg; m6809_ICount -= 1; break;
	case 0x41: eaddr = ureg + 1; m6809_ICount -= 1; break;
	case 0x42: eaddr = ureg + 2; m6809_ICount -= 1; break;
	case 0x43: eaddr = ureg + 3; m6809_ICount -= 1; break;
	case 0x44: eaddr = ureg + 4; m6809_ICount -= 1; break;
	case 0x45: eaddr = ureg + 5; m6809_ICount -= 1; break;
	case 0x46: eaddr = ureg + 6; m6809_ICount -= 1;  break;
	case 0x47: eaddr = ureg + 7; m6809_ICount -= 1; break;
	case 0x48: eaddr = ureg + 8; m6809_ICount -= 1;  break;
	case 0x49: eaddr = ureg + 9; m6809_ICount -= 1; break;
	case 0x4A: eaddr = ureg + 10; m6809_ICount -= 1;  break;
	case 0x4B: eaddr = ureg + 11; m6809_ICount -= 1; break;
	case 0x4C: eaddr = ureg + 12; m6809_ICount -= 1;  break;
	case 0x4D: eaddr = ureg + 13; m6809_ICount -= 1; break;
	case 0x4E: eaddr = ureg + 14; m6809_ICount -= 1;  break;
	case 0x4F: eaddr = ureg + 15; m6809_ICount -= 1;  break;
	case 0x50: eaddr = ureg - 16; m6809_ICount -= 1;  break;
	case 0x51: eaddr = ureg - 15; m6809_ICount -= 1;  break;
	case 0x52: eaddr = ureg - 14; m6809_ICount -= 1; break;
	case 0x53: eaddr = ureg - 13; m6809_ICount -= 1;  break;
	case 0x54: eaddr = ureg - 12; m6809_ICount -= 1; break;
	case 0x55: eaddr = ureg - 11; m6809_ICount -= 1; break;
	case 0x56: eaddr = ureg - 10; m6809_ICount -= 1; break;
	case 0x57: eaddr = ureg - 9; m6809_ICount -= 1; break;
	case 0x58: eaddr = ureg - 8; m6809_ICount -= 1; break;
	case 0x59: eaddr = ureg - 7; m6809_ICount -= 1; break;
	case 0x5A: eaddr = ureg - 6; m6809_ICount -= 1; break;
	case 0x5B: eaddr = ureg - 5; m6809_ICount -= 1;  break;
	case 0x5C: eaddr = ureg - 4; m6809_ICount -= 1; break;
	case 0x5D: eaddr = ureg - 3; m6809_ICount -= 1;  break;
	case 0x5E: eaddr = ureg - 2; m6809_ICount -= 1;  break;
	case 0x5F: eaddr = ureg - 1; m6809_ICount -= 1;  break;
	case 0x60: eaddr = sreg; m6809_ICount -= 1;  break;
	case 0x61: eaddr = sreg + 1; m6809_ICount -= 1; break;
	case 0x62: eaddr = sreg + 2; m6809_ICount -= 1; break;
	case 0x63: eaddr = sreg + 3; m6809_ICount -= 1; break;
	case 0x64: eaddr = sreg + 4; m6809_ICount -= 1;  break;
	case 0x65: eaddr = sreg + 5; m6809_ICount -= 1; break;
	case 0x66: eaddr = sreg + 6; m6809_ICount -= 1; break;
	case 0x67: eaddr = sreg + 7; m6809_ICount -= 1; break;
	case 0x68: eaddr = sreg + 8; m6809_ICount -= 1; break;
	case 0x69: eaddr = sreg + 9; m6809_ICount -= 1; break;
	case 0x6A: eaddr = sreg + 10; m6809_ICount -= 1; break;
	case 0x6B: eaddr = sreg + 11; m6809_ICount -= 1; break;
	case 0x6C: eaddr = sreg + 12; m6809_ICount -= 1; break;
	case 0x6D: eaddr = sreg + 13; m6809_ICount -= 1; break;
	case 0x6E: eaddr = sreg + 14; m6809_ICount -= 1; break;
	case 0x6F: eaddr = sreg + 15; m6809_ICount -= 1; break;
	case 0x70: eaddr = sreg - 16; m6809_ICount -= 1; break;
	case 0x71: eaddr = sreg - 15; m6809_ICount -= 1; break;
	case 0x72: eaddr = sreg - 14; m6809_ICount -= 1;  break;
	case 0x73: eaddr = sreg - 13; m6809_ICount -= 1;  break;
	case 0x74: eaddr = sreg - 12; m6809_ICount -= 1; break;
	case 0x75: eaddr = sreg - 11; m6809_ICount -= 1; break;
	case 0x76: eaddr = sreg - 10; m6809_ICount -= 1; break;
	case 0x77: eaddr = sreg - 9; m6809_ICount -= 1;  break;
	case 0x78: eaddr = sreg - 8; m6809_ICount -= 1;  break;
	case 0x79: eaddr = sreg - 7; m6809_ICount -= 1;  break;
	case 0x7A: eaddr = sreg - 6; m6809_ICount -= 1;  break;
	case 0x7B: eaddr = sreg - 5; m6809_ICount -= 1;  break;
	case 0x7C: eaddr = sreg - 4; m6809_ICount -= 1;  break;
	case 0x7D: eaddr = sreg - 3; m6809_ICount -= 1;  break;
	case 0x7E: eaddr = sreg - 2; m6809_ICount -= 1; break;
	case 0x7F: eaddr = sreg - 1; m6809_ICount -= 1; break;
	case 0x80: eaddr = xreg; xreg++; m6809_ICount -= 2; break;
	case 0x81: eaddr = xreg; xreg += 2; m6809_ICount -= 3; break;
	case 0x82: xreg--; eaddr = xreg; m6809_ICount -= 3; break;
	case 0x83: xreg -= 2; eaddr = xreg; m6809_ICount -= 3; break;
	case 0x84: eaddr = xreg; break;
	case 0x85: eaddr = xreg + SIGNED(breg); m6809_ICount -= 1;  break;
	case 0x86: eaddr = xreg + SIGNED(areg); m6809_ICount -= 1; break;
	case 0x87: eaddr = 0; break; /*ILLEGAL*/
	case 0x88: IMMBYTE(eaddr); eaddr = xreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0x89: IMMWORD(eaddr); eaddr += xreg; m6809_ICount -= 4;  break;
	case 0x8A: eaddr = 0; break; /*ILLEGAL*/
	case 0x8B: eaddr = xreg + GETDREG; m6809_ICount -= 4; break;
	case 0x8C: IMMBYTE(eaddr); pcreg++; eaddr = pcreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0x8D: IMMWORD(eaddr); eaddr += pcreg; m6809_ICount -= 5; break;
	case 0x8E: eaddr = 0; break; /*ILLEGAL*/
	case 0x8F: eaddr = M_RDMEM_WORD(pcreg); pcreg += 2; m6809_ICount -= 5; break;
	case 0x90: eaddr = xreg; xreg++; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0x91: eaddr = xreg; xreg += 2; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0x92: xreg--; eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0x93: xreg -= 2; eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0x94: eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0x95: eaddr = xreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0x96: eaddr = xreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0x97: eaddr = 0; break; /*ILLEGAL*/
	case 0x98: IMMBYTE(eaddr); eaddr = xreg + SIGNED(eaddr);	eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0x99: IMMWORD(eaddr); eaddr += xreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0x9A: eaddr = 0; break; /*ILLEGAL*/
	case 0x9B: eaddr = xreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0x9C: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr);	eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0x9D: IMMWORD(eaddr); eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;
	case 0x9E: eaddr = 0; break; /*ILLEGAL*/
	case 0x9F: IMMWORD(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;

	case 0xA0: eaddr = yreg; yreg++; m6809_ICount -= 2; break;
	case 0xA1: eaddr = yreg; yreg += 2; m6809_ICount -= 3; break;
	case 0xA2: yreg--; eaddr = yreg; m6809_ICount -= 2; break;
	case 0xA3: yreg -= 2; eaddr = yreg; m6809_ICount -= 3; break;
	case 0xA4: eaddr = yreg; break;
	case 0xA5: eaddr = yreg + SIGNED(breg); m6809_ICount -= 1; break;
	case 0xA6: eaddr = yreg + SIGNED(areg); m6809_ICount -= 1; break;
	case 0xA7: eaddr = 0; break; /*ILLEGAL*/
	case 0xA8: IMMBYTE(eaddr); eaddr = yreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xA9: IMMWORD(eaddr); eaddr += yreg; m6809_ICount -= 4; break;
	case 0xAA: eaddr = 0; break; /*ILLEGAL*/
	case 0xAB: eaddr = yreg + GETDREG; m6809_ICount -= 4; break;
	case 0xAC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xAD: IMMWORD(eaddr); eaddr += pcreg; m6809_ICount -= 5; break;
	case 0xAE: eaddr = 0; break; /*ILLEGAL*/
	case 0xAF: IMMWORD(eaddr); m6809_ICount -= 5; break;

	case 0xB0: eaddr = yreg; yreg++; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB1: eaddr = yreg; yreg += 2; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB2: yreg--; eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB3: yreg -= 2; eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB4: eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB5: eaddr = yreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB6: eaddr = yreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB7: eaddr = 0; break; /*ILLEGAL*/
	case 0xB8: IMMBYTE(eaddr); eaddr = yreg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xB9: eaddr = M_RDMEM_WORD(pcreg); pcreg += 2; eaddr += yreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xBA: eaddr = 0; break; /*ILLEGAL*/
	case 0xBB: eaddr = yreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xBC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xBD: IMMWORD(eaddr);  eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xBE: eaddr = 0; break; /*ILLEGAL*/
	case 0xBF: IMMWORD(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;

	case 0xC0: eaddr = ureg; ureg++; m6809_ICount -= 2; break;
	case 0xC1: eaddr = ureg; ureg += 2; m6809_ICount -= 3; break;
	case 0xC2: ureg--; eaddr = ureg; m6809_ICount -= 2; break;
	case 0xC3: ureg -= 2; eaddr = ureg; m6809_ICount -= 3; break;
	case 0xC4: eaddr = ureg; m6809_ICount -= 3; break;
	case 0xC5: eaddr = ureg + SIGNED(breg); m6809_ICount -= 1; break;
	case 0xC6: eaddr = ureg + SIGNED(areg); m6809_ICount -= 1; break;
	case 0xC7: eaddr = 0; break; /*ILLEGAL*/
	case 0xC8: IMMBYTE(eaddr); eaddr = ureg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xC9: IMMWORD(eaddr); eaddr += ureg; m6809_ICount -= 4; break;
	case 0xCA: eaddr = 0; break; /*ILLEGAL*/
	case 0xCB: eaddr = ureg + GETDREG; m6809_ICount -= 4; break;
	case 0xCC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xCD: IMMWORD(eaddr); eaddr += pcreg; m6809_ICount -= 5; break;
	case 0xCE: eaddr = 0; break; /*ILLEGAL*/
	case 0xCF: IMMWORD(eaddr); m6809_ICount -= 5; break;

	case 0xD0: eaddr = ureg; ureg++; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0xD1: eaddr = ureg; ureg += 2; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0xD2: ureg--; eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0xD3: ureg -= 2; eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0xD4: eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xD5: eaddr = ureg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xD6: eaddr = ureg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xD7: eaddr = 0; break; /*ILLEGAL*/
	case 0xD8: IMMBYTE(eaddr); eaddr = ureg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xD9: IMMWORD(eaddr); eaddr += ureg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0xDA: eaddr = 0; break; /*ILLEGAL*/
	case 0xDB: eaddr = ureg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0xDC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xDD: IMMWORD(eaddr); eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;
	case 0xDE: eaddr = 0; break; /*ILLEGAL*/
	case 0xDF: IMMWORD(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;

	case 0xE0: eaddr = sreg; sreg++; m6809_ICount -= 2; break;
	case 0xE1: eaddr = sreg; sreg += 2; m6809_ICount -= 3; break;
	case 0xE2: sreg--; eaddr = sreg; m6809_ICount -= 2; break;
	case 0xE3: sreg -= 2; eaddr = sreg; m6809_ICount -= 3; break;
	case 0xE4: eaddr = sreg; break;
	case 0xE5: eaddr = sreg + SIGNED(breg); m6809_ICount -= 1; break;
	case 0xE6: eaddr = sreg + SIGNED(areg); m6809_ICount -= 1; break;
	case 0xE7: eaddr = 0; break; /*ILLEGAL*/
	case 0xE8: IMMBYTE(eaddr); eaddr = sreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xE9: IMMWORD(eaddr); eaddr += sreg; m6809_ICount -= 4; break;
	case 0xEA: eaddr = 0; break; /*ILLEGAL*/
	case 0xEB: eaddr = sreg + GETDREG; m6809_ICount -= 4; break;
	case 0xEC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); m6809_ICount -= 1; break;
	case 0xED: IMMWORD(eaddr); eaddr += pcreg; m6809_ICount -= 5; break;
	case 0xEE: eaddr = 0; break; /*ILLEGAL*/
	case 0xEF: IMMWORD(eaddr); m6809_ICount -= 5; break;

	case 0xF0: eaddr = sreg; sreg++; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0xF1: eaddr = sreg; sreg += 2; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0xF2: sreg--; eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 5; break;
	case 0xF3: sreg -= 2; eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 6; break;
	case 0xF4: eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 3; break;
	case 0xF5: eaddr = sreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xF6: eaddr = sreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xF7: eaddr = 0; break; /*ILLEGAL*/
	case 0xF8: IMMBYTE(eaddr); eaddr = sreg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xF9: IMMWORD(eaddr); eaddr += sreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0xFA: eaddr = 0; break; /*ILLEGAL*/
	case 0xFB: eaddr = sreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 7; break;
	case 0xFC: IMMBYTE(eaddr); eaddr = pcreg + SIGNED(eaddr); eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 4; break;
	case 0xFD: IMMWORD(eaddr); eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;
	case 0xFE: eaddr = 0; break; /*ILLEGAL*/
	case 0xFF: IMMWORD(eaddr); pcreg += 2; eaddr = M_RDMEM_WORD(eaddr); m6809_ICount -= 8; break;
	}
}

/* $10xx opcodes */
void cpu_6809::pref10()
{
	slapstic_en = 0;
	ireg2 = rd_slow(pcreg++);

	switch (ireg2)
	{
	case 0x21: lbrn();		m6809_ICount -= 5;	break;
	case 0x22: lbhi();		m6809_ICount -= 5;	break;
	case 0x23: lbls();		m6809_ICount -= 5;	break;
	case 0x24: lbcc();		m6809_ICount -= 5;	break;
	case 0x25: lbcs();		m6809_ICount -= 5;	break;
	case 0x26: lbne();		m6809_ICount -= 5;	break;
	case 0x27: lbeq();		m6809_ICount -= 5;	break;
	case 0x28: lbvc();		m6809_ICount -= 5;	break;
	case 0x29: lbvs();		m6809_ICount -= 5;	break;
	case 0x2a: lbpl();		m6809_ICount -= 5;	break;
	case 0x2b: lbmi();		m6809_ICount -= 5;	break;
	case 0x2c: lbge();		m6809_ICount -= 5;	break;
	case 0x2d: lblt();		m6809_ICount -= 5;	break;
	case 0x2e: lbgt();		m6809_ICount -= 5;	break;
	case 0x2f: lble();		m6809_ICount -= 5;	break;

	case 0x3f: swi2();		m6809_ICount -= 20;	break;

	case 0x83: cmpd_im();	m6809_ICount -= 5;	break;
	case 0x8c: cmpy_im();	m6809_ICount -= 5;	break;
	case 0x8e: ldy_im();	m6809_ICount -= 4;	break;
	case 0x8f: sty_im();	m6809_ICount -= 4;	break;

	case 0x93: cmpd_di();	m6809_ICount -= 7;	break;
	case 0x9c: cmpy_di();	m6809_ICount -= 7;	break;
	case 0x9e: ldy_di();	m6809_ICount -= 6;	break;
	case 0x9f: sty_di();	m6809_ICount -= 6;	break;

	case 0xa3: cmpd_ix();	m6809_ICount -= 7;	break;
	case 0xac: cmpy_ix();	m6809_ICount -= 7;	break;
	case 0xae: ldy_ix();	m6809_ICount -= 6;	break;
	case 0xaf: sty_ix();	m6809_ICount -= 6;	break;

	case 0xb3: cmpd_ex();	m6809_ICount -= 8;	break;
	case 0xbc: cmpy_ex();	m6809_ICount -= 8;	break;
	case 0xbe: ldy_ex();	m6809_ICount -= 7;	break;
	case 0xbf: sty_ex();	m6809_ICount -= 7;	break;

	case 0xce: lds_im();	m6809_ICount -= 4;	break;
	case 0xcf: sts_im();	m6809_ICount -= 4;	break;

	case 0xde: lds_di();	m6809_ICount -= 6;	break;
	case 0xdf: sts_di();	m6809_ICount -= 6;	break;

	case 0xee: lds_ix();	m6809_ICount -= 6;	break;
	case 0xef: sts_ix();	m6809_ICount -= 6;	break;

	case 0xfe: lds_ex();	m6809_ICount -= 7;	break;
	case 0xff: sts_ex();	m6809_ICount -= 7;	break;

	default:   illegal();						break;
	}
}

/* $11xx opcodes */
void cpu_6809::pref11()
{
	slapstic_en = 0;
	ireg2 = rd_slow(pcreg++);

	switch (ireg2)
	{
	case 0x3f: swi3();		m6809_ICount -= 20;	break;

	case 0x83: cmpu_im();	m6809_ICount -= 5;	break;
	case 0x8c: cmps_im();	m6809_ICount -= 5;	break;

	case 0x93: cmpu_di();	m6809_ICount -= 7;	break;
	case 0x9c: cmps_di();	m6809_ICount -= 7;	break;

	case 0xa3: cmpu_ix();	m6809_ICount -= 7;	break;
	case 0xac: cmps_ix();	m6809_ICount -= 7;	break;

	case 0xb3: cmpu_ex();	m6809_ICount -= 8;	break;
	case 0xbc: cmps_ex();	m6809_ICount -= 8;	break;

	default:   illegal();						break;
	}
}

// These are just for debugging. 

uint8_t cpu_6809::get_last_ireg()
{
	return ireg;
}

uint8_t cpu_6809::get_last_ireg2()
{
	return ireg2;
}

/* execute instructions on this CPU until icount expires */
int cpu_6809::exec6809(int cycles)
{
	static int count = 0;
	char tempbuffer[80];

	m6809_ICount = cycles;// -m6809.extra_cycles;

	int lastticks = m6809_ICount;

	if (pending_interrupts & (M6809_CWAI | M6809_SYNC))
	{
		m6809_ICount = 0;
	}
	else
	{
		do
		{
			lastticks = m6809_ICount;

			if (pending_interrupts != 0)
				Interrupt();	/* NS 970908 */

			ppc = pcreg;
			slapstic_en = 0;
			ireg = rd_slow(pcreg++);

			if (cpu_num == 0 && logging)
			{
				Dasm6809(tempbuffer, pcreg);
				//LOG_INFO("M6809#%d Slapstic, Bank %d, PC %04X,  X REG %04x", get_active_cpu(), last_starwars_bank, pcreg, xreg);
				//LOG_INFO("%04X: %s  $%04X XREG:%X IREG: %x AREG: %x DREG: %04X", ppc, tempbuffer, M_RDMEM_WORD(pcreg), xreg, ireg, areg, GETDREG);
				//LOG_INFO("Flags: CC:%x F:%X , I:%X ,N:%x, Z:%x, C:%x", cc, (cc >> 6) & 1, (cc >> 4) & 1, (cc >> 3) & 1, (cc >> 2) & 1, cc & 1);
			}

			switch (ireg)
			{
			case 0x00: neg_di();   m6809_ICount -= 6; break;
			case 0x01: illegal();  m6809_ICount -= 2; break;
			case 0x02: illegal();  m6809_ICount -= 2; break;
			case 0x03: com_di();   m6809_ICount -= 6; break;
			case 0x04: lsr_di();   m6809_ICount -= 6; break;
			case 0x05: illegal();  m6809_ICount -= 2; break;
			case 0x06: ror_di();   m6809_ICount -= 6; break;
			case 0x07: asr_di();   m6809_ICount -= 6; break;
			case 0x08: asl_di();   m6809_ICount -= 6; break;
			case 0x09: rol_di();   m6809_ICount -= 6; break;
			case 0x0a: dec_di();   m6809_ICount -= 6; break;
			case 0x0b: illegal();  m6809_ICount -= 2; break;
			case 0x0c: inc_di();   m6809_ICount -= 6; break;
			case 0x0d: tst_di();   m6809_ICount -= 6; break;
			case 0x0e: jmp_di();   m6809_ICount -= 3; break;
			case 0x0f: clr_di();   m6809_ICount -= 6; break;
			case 0x10: pref10();					 break;
			case 0x11: pref11();					 break;
			case 0x12: nop();	   m6809_ICount -= 2; break;
			case 0x13: sync();	   m6809_ICount -= 4; break;
			case 0x14: illegal();  m6809_ICount -= 2; break;
			case 0x15: illegal();  m6809_ICount -= 2; break;
			case 0x16: lbra();	   m6809_ICount -= 5; break;
			case 0x17: lbsr();	   m6809_ICount -= 9; break;
			case 0x18: illegal();  m6809_ICount -= 2; break;
			case 0x19: daa();	   m6809_ICount -= 2; break;
			case 0x1a: orcc();	   m6809_ICount -= 3; break;
			case 0x1b: illegal();  m6809_ICount -= 2; break;
			case 0x1c: andcc();    m6809_ICount -= 3; break;
			case 0x1d: sex();	   m6809_ICount -= 2; break;
			case 0x1e: exg();	   m6809_ICount -= 8; break;
			case 0x1f: tfr();	   m6809_ICount -= 6; break;
			case 0x20: bra();	   m6809_ICount -= 3; break;
			case 0x21: brn();	   m6809_ICount -= 3; break;
			case 0x22: bhi();	   m6809_ICount -= 3; break;
			case 0x23: bls();	   m6809_ICount -= 3; break;
			case 0x24: bcc();	   m6809_ICount -= 3; break;
			case 0x25: bcs();	   m6809_ICount -= 3; break;
			case 0x26: bne();	   m6809_ICount -= 3; break;
			case 0x27: beq();	   m6809_ICount -= 3; break;
			case 0x28: bvc();	   m6809_ICount -= 3; break;
			case 0x29: bvs();	   m6809_ICount -= 3; break;
			case 0x2a: bpl();	   m6809_ICount -= 3; break;
			case 0x2b: bmi();	   m6809_ICount -= 3; break;
			case 0x2c: bge();	   m6809_ICount -= 3; break;
			case 0x2d: blt();	   m6809_ICount -= 3; break;
			case 0x2e: bgt();	   m6809_ICount -= 3; break;
			case 0x2f: ble();	   m6809_ICount -= 3; break;
			case 0x30: leax();	   m6809_ICount -= 4; break;
			case 0x31: leay();	   m6809_ICount -= 4; break;
			case 0x32: leas();	   m6809_ICount -= 4; break;
			case 0x33: leau();	   m6809_ICount -= 4; break;
			case 0x34: pshs();	   m6809_ICount -= 5; break;
			case 0x35: puls();	   m6809_ICount -= 5; break;
			case 0x36: pshu();	   m6809_ICount -= 5; break;
			case 0x37: pulu();	   m6809_ICount -= 5; break;
			case 0x38: illegal();  m6809_ICount -= 2; break;
			case 0x39: rts();	   m6809_ICount -= 5; break;
			case 0x3a: abx();	   m6809_ICount -= 3; break;
			case 0x3b: rti();	   m6809_ICount -= 6; break;
			case 0x3c: cwai();	   m6809_ICount -= 20; break;
			case 0x3d: mul();	   m6809_ICount -= 11; break;
			case 0x3e: illegal();  m6809_ICount -= 2; break;
			case 0x3f: swi();	   m6809_ICount -= 19; break;
			case 0x40: nega();	   m6809_ICount -= 2; break;
			case 0x41: illegal();  m6809_ICount -= 2; break;
			case 0x42: illegal();  m6809_ICount -= 2; break;
			case 0x43: coma();	   m6809_ICount -= 2; break;
			case 0x44: lsra();	   m6809_ICount -= 2; break;
			case 0x45: illegal();  m6809_ICount -= 2; break;
			case 0x46: rora();	   m6809_ICount -= 2; break;
			case 0x47: asra();	   m6809_ICount -= 2; break;
			case 0x48: asla();	   m6809_ICount -= 2; break;
			case 0x49: rola();	   m6809_ICount -= 2; break;
			case 0x4a: deca();	   m6809_ICount -= 2; break;
			case 0x4b: illegal();  m6809_ICount -= 2; break;
			case 0x4c: inca();	   m6809_ICount -= 2; break;
			case 0x4d: tsta();	   m6809_ICount -= 2; break;
			case 0x4e: illegal();  m6809_ICount -= 2; break;
			case 0x4f: clra();	   m6809_ICount -= 2; break;
			case 0x50: negb();	   m6809_ICount -= 2; break;
			case 0x51: illegal();  m6809_ICount -= 2; break;
			case 0x52: illegal();  m6809_ICount -= 2; break;
			case 0x53: comb();	   m6809_ICount -= 2; break;
			case 0x54: lsrb();	   m6809_ICount -= 2; break;
			case 0x55: illegal();  m6809_ICount -= 2; break;
			case 0x56: rorb();	   m6809_ICount -= 2; break;
			case 0x57: asrb();	   m6809_ICount -= 2; break;
			case 0x58: aslb();	   m6809_ICount -= 2; break;
			case 0x59: rolb();	   m6809_ICount -= 2; break;
			case 0x5a: decb();	   m6809_ICount -= 2; break;
			case 0x5b: illegal();  m6809_ICount -= 2; break;
			case 0x5c: incb();	   m6809_ICount -= 2; break;
			case 0x5d: tstb();	   m6809_ICount -= 2; break;
			case 0x5e: illegal();  m6809_ICount -= 2; break;
			case 0x5f: clrb();	   m6809_ICount -= 2; break;
			case 0x60: neg_ix();   m6809_ICount -= 6; break;
			case 0x61: illegal();  m6809_ICount -= 2; break;
			case 0x62: illegal();  m6809_ICount -= 2; break;
			case 0x63: com_ix();   m6809_ICount -= 6; break;
			case 0x64: lsr_ix();   m6809_ICount -= 6; break;
			case 0x65: illegal();  m6809_ICount -= 2; break;
			case 0x66: ror_ix();   m6809_ICount -= 6; break;
			case 0x67: asr_ix();   m6809_ICount -= 6; break;
			case 0x68: asl_ix();   m6809_ICount -= 6; break;
			case 0x69: rol_ix();   m6809_ICount -= 6; break;
			case 0x6a: dec_ix();   m6809_ICount -= 6; break;
			case 0x6b: illegal();  m6809_ICount -= 2; break;
			case 0x6c: inc_ix();   m6809_ICount -= 6; break;
			case 0x6d: tst_ix();   m6809_ICount -= 6; break;
			case 0x6e: jmp_ix();   m6809_ICount -= 3; break;
			case 0x6f: clr_ix();   m6809_ICount -= 6; break;
			case 0x70: neg_ex();   m6809_ICount -= 7; break;
			case 0x71: illegal();  m6809_ICount -= 2; break;
			case 0x72: illegal();  m6809_ICount -= 2; break;
			case 0x73: com_ex();   m6809_ICount -= 7; break;
			case 0x74: lsr_ex();   m6809_ICount -= 7; break;
			case 0x75: illegal();  m6809_ICount -= 2; break;
			case 0x76: ror_ex();   m6809_ICount -= 7; break;
			case 0x77: asr_ex();   m6809_ICount -= 7; break;
			case 0x78: asl_ex();   m6809_ICount -= 7; break;
			case 0x79: rol_ex();   m6809_ICount -= 7; break;
			case 0x7a: dec_ex();   m6809_ICount -= 7; break;
			case 0x7b: illegal();  m6809_ICount -= 2; break;
			case 0x7c: inc_ex();   m6809_ICount -= 7; break;
			case 0x7d: tst_ex();   m6809_ICount -= 7; break;
			case 0x7e: jmp_ex();   m6809_ICount -= 4; break;
			case 0x7f: clr_ex();   m6809_ICount -= 7; break;
			case 0x80: suba_im();  m6809_ICount -= 2; break;
			case 0x81: cmpa_im();  m6809_ICount -= 2; break;
			case 0x82: sbca_im();  m6809_ICount -= 2; break;
			case 0x83: subd_im();  m6809_ICount -= 4; break;
			case 0x84: anda_im();  m6809_ICount -= 2; break;
			case 0x85: bita_im();  m6809_ICount -= 2; break;
			case 0x86: lda_im();   m6809_ICount -= 2; break;
			case 0x87: sta_im();   m6809_ICount -= 2; break;
			case 0x88: eora_im();  m6809_ICount -= 2; break;
			case 0x89: adca_im();  m6809_ICount -= 2; break;
			case 0x8a: ora_im();   m6809_ICount -= 2; break;
			case 0x8b: adda_im();  m6809_ICount -= 2; break;
			case 0x8c: cmpx_im();  m6809_ICount -= 4; break;
			case 0x8d: bsr();	   m6809_ICount -= 7; break;
			case 0x8e: ldx_im();   m6809_ICount -= 3; break;
			case 0x8f: stx_im();   m6809_ICount -= 2; break;
			case 0x90: suba_di();  m6809_ICount -= 4; break;
			case 0x91: cmpa_di();  m6809_ICount -= 4; break;
			case 0x92: sbca_di();  m6809_ICount -= 4; break;
			case 0x93: subd_di();  m6809_ICount -= 6; break;
			case 0x94: anda_di();  m6809_ICount -= 4; break;
			case 0x95: bita_di();  m6809_ICount -= 4; break;
			case 0x96: lda_di();   m6809_ICount -= 4; break;
			case 0x97: sta_di();   m6809_ICount -= 4; break;
			case 0x98: eora_di();  m6809_ICount -= 4; break;
			case 0x99: adca_di();  m6809_ICount -= 4; break;
			case 0x9a: ora_di();   m6809_ICount -= 4; break;
			case 0x9b: adda_di();  m6809_ICount -= 4; break;
			case 0x9c: cmpx_di();  m6809_ICount -= 6; break;
			case 0x9d: jsr_di();   m6809_ICount -= 7; break;
			case 0x9e: ldx_di();   m6809_ICount -= 5; break;
			case 0x9f: stx_di();   m6809_ICount -= 5; break;
			case 0xa0: suba_ix();  m6809_ICount -= 4; break;
			case 0xa1: cmpa_ix();  m6809_ICount -= 4; break;
			case 0xa2: sbca_ix();  m6809_ICount -= 4; break;
			case 0xa3: subd_ix();  m6809_ICount -= 6; break;
			case 0xa4: anda_ix();  m6809_ICount -= 4; break;
			case 0xa5: bita_ix();  m6809_ICount -= 4; break;
			case 0xa6: lda_ix();   m6809_ICount -= 4; break;
			case 0xa7: sta_ix();   m6809_ICount -= 4; break;
			case 0xa8: eora_ix();  m6809_ICount -= 4; break;
			case 0xa9: adca_ix();  m6809_ICount -= 4; break;
			case 0xaa: ora_ix();   m6809_ICount -= 4; break;
			case 0xab: adda_ix();  m6809_ICount -= 4; break;
			case 0xac: cmpx_ix();  m6809_ICount -= 6; break;
			case 0xad: jsr_ix();   m6809_ICount -= 7; break;
			case 0xae: ldx_ix();   m6809_ICount -= 5; break;
			case 0xaf: stx_ix();   m6809_ICount -= 5; break;
			case 0xb0: suba_ex();  m6809_ICount -= 5; break;
			case 0xb1: cmpa_ex();  m6809_ICount -= 5; break;
			case 0xb2: sbca_ex();  m6809_ICount -= 5; break;
			case 0xb3: subd_ex();  m6809_ICount -= 7; break;
			case 0xb4: anda_ex();  m6809_ICount -= 5; break;
			case 0xb5: bita_ex();  m6809_ICount -= 5; break;
			case 0xb6: lda_ex();   m6809_ICount -= 5; break;
			case 0xb7: sta_ex();   m6809_ICount -= 5; break;
			case 0xb8: eora_ex();  m6809_ICount -= 5; break;
			case 0xb9: adca_ex();  m6809_ICount -= 5; break;
			case 0xba: ora_ex();   m6809_ICount -= 5; break;
			case 0xbb: adda_ex();  m6809_ICount -= 5; break;
			case 0xbc: cmpx_ex();  m6809_ICount -= 7; break;
			case 0xbd: jsr_ex();   m6809_ICount -= 8; break;
			case 0xbe: ldx_ex();   m6809_ICount -= 6; break;
			case 0xbf: stx_ex();   m6809_ICount -= 6; break;
			case 0xc0: subb_im();  m6809_ICount -= 2; break;
			case 0xc1: cmpb_im();  m6809_ICount -= 2; break;
			case 0xc2: sbcb_im();  m6809_ICount -= 2; break;
			case 0xc3: addd_im();  m6809_ICount -= 4; break;
			case 0xc4: andb_im();  m6809_ICount -= 2; break;
			case 0xc5: bitb_im();  m6809_ICount -= 2; break;
			case 0xc6: ldb_im();   m6809_ICount -= 2; break;
			case 0xc7: stb_im();   m6809_ICount -= 2; break;
			case 0xc8: eorb_im();  m6809_ICount -= 2; break;
			case 0xc9: adcb_im();  m6809_ICount -= 2; break;
			case 0xca: orb_im();   m6809_ICount -= 2; break;
			case 0xcb: addb_im();  m6809_ICount -= 2; break;
			case 0xcc: ldd_im();   m6809_ICount -= 3; break;
			case 0xcd: std_im();   m6809_ICount -= 2; break;
			case 0xce: ldu_im();   m6809_ICount -= 3; break;
			case 0xcf: stu_im();   m6809_ICount -= 3; break;
			case 0xd0: subb_di();  m6809_ICount -= 4; break;
			case 0xd1: cmpb_di();  m6809_ICount -= 4; break;
			case 0xd2: sbcb_di();  m6809_ICount -= 4; break;
			case 0xd3: addd_di();  m6809_ICount -= 6; break;
			case 0xd4: andb_di();  m6809_ICount -= 4; break;
			case 0xd5: bitb_di();  m6809_ICount -= 4; break;
			case 0xd6: ldb_di();   m6809_ICount -= 4; break;
			case 0xd7: stb_di();   m6809_ICount -= 4; break;
			case 0xd8: eorb_di();  m6809_ICount -= 4; break;
			case 0xd9: adcb_di();  m6809_ICount -= 4; break;
			case 0xda: orb_di();   m6809_ICount -= 4; break;
			case 0xdb: addb_di();  m6809_ICount -= 4; break;
			case 0xdc: ldd_di();   m6809_ICount -= 5; break;
			case 0xdd: std_di();   m6809_ICount -= 5; break;
			case 0xde: ldu_di();   m6809_ICount -= 5; break;
			case 0xdf: stu_di();   m6809_ICount -= 5; break;
			case 0xe0: subb_ix();  m6809_ICount -= 4; break;
			case 0xe1: cmpb_ix();  m6809_ICount -= 4; break;
			case 0xe2: sbcb_ix();  m6809_ICount -= 4; break;
			case 0xe3: addd_ix();  m6809_ICount -= 6; break;
			case 0xe4: andb_ix();  m6809_ICount -= 4; break;
			case 0xe5: bitb_ix();  m6809_ICount -= 4; break;
			case 0xe6: ldb_ix();   m6809_ICount -= 4; break;
			case 0xe7: stb_ix();   m6809_ICount -= 4; break;
			case 0xe8: eorb_ix();  m6809_ICount -= 4; break;
			case 0xe9: adcb_ix();  m6809_ICount -= 4; break;
			case 0xea: orb_ix();   m6809_ICount -= 4; break;
			case 0xeb: addb_ix();  m6809_ICount -= 4; break;
			case 0xec: ldd_ix();   m6809_ICount -= 5; break;
			case 0xed: std_ix();   m6809_ICount -= 5; break;
			case 0xee: ldu_ix();   m6809_ICount -= 5; break;
			case 0xef: stu_ix();   m6809_ICount -= 5; break;
			case 0xf0: subb_ex();  m6809_ICount -= 5; break;
			case 0xf1: cmpb_ex();  m6809_ICount -= 5; break;
			case 0xf2: sbcb_ex();  m6809_ICount -= 5; break;
			case 0xf3: addd_ex();  m6809_ICount -= 7; break;
			case 0xf4: andb_ex();  m6809_ICount -= 5; break;
			case 0xf5: bitb_ex();  m6809_ICount -= 5; break;
			case 0xf6: ldb_ex();   m6809_ICount -= 5; break;
			case 0xf7: stb_ex();   m6809_ICount -= 5; break;
			case 0xf8: eorb_ex();  m6809_ICount -= 5; break;
			case 0xf9: adcb_ex();  m6809_ICount -= 5; break;
			case 0xfa: orb_ex();   m6809_ICount -= 5; break;
			case 0xfb: addb_ex();  m6809_ICount -= 5; break;
			case 0xfc: ldd_ex();   m6809_ICount -= 6; break;
			case 0xfd: std_ex();   m6809_ICount -= 6; break;
			case 0xfe: ldu_ex();   m6809_ICount -= 6; break;
			case 0xff: stu_ex();   m6809_ICount -= 6; break;
			}

			if (catch_nextBranch)
			{
				//change_pc(pcreg);
				catch_nextBranch = 0;
			}

			clocktickstotal += (abs(lastticks - m6809_ICount));
		} while (m6809_ICount > 0);

		m6809_ICount -= m6809.extra_cycles;
		m6809.extra_cycles = 0;
		//clocktickstotal += m6809.extra_cycles;
		timer_update(clocktickstotal, cpu_num);
		if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
	}
	return cycles - m6809_ICount;
}

#ifndef TRUE
#define TRUE         -1
#define FALSE        0
#endif

typedef struct {                                       /* opcode structure */
	int opcode;                                     /* 8-bit opcode value */
	int numoperands;
	char name[6];                                            /* opcode name */
	int mode;                                          /* addressing mode */
	int numcycles;                         /* number of cycles - not used */
} opcodeinfo;

/* 6809 ADDRESSING MODES */
#define INH 0
#define DIR 1
#define IND 2
#define REL 3
#define EXT 4
#define IMM 5
#define LREL 6
#define PG2 7                                    /* PAGE SWITCHES - Page 2 */
#define PG3 8                                                    /* Page 3 */

/* number of opcodes in each page */
#define NUMPG1OPS 223
#define NUMPG2OPS 38
#define NUMPG3OPS 9

int numops[3] = {
   NUMPG1OPS,NUMPG2OPS,NUMPG3OPS,
};

char modenames[9][14] = {
   "inherent",
   "direct",
   "indexed",
   "relative",
   "extended",
   "immediate",
   "long relative",
   "page 2",
   "page 3",
};

opcodeinfo pg1opcodes[NUMPG1OPS] = {                           /* page 1 ops */
	{0,1,"NEG",DIR,6},
	{3,1,"COM",DIR,6},
	{4,1,"LSR",DIR,6},
	{6,1,"ROR",DIR,6},
	{7,1,"ASR",DIR,6},
	{8,1,"ASL",DIR,6},
	{9,1,"ROL",DIR,6},
	{10,1,"DEC",DIR,6},
	{12,1,"INC",DIR,6},
	{13,1,"TST",DIR,6},
	{14,1,"JMP",DIR,3},
	{15,1,"CLR",DIR,6},

	{16,1,"page2",PG2,0},
	{17,1,"page3",PG3,0},
	{18,0,"NOP",INH,2},
	{19,0,"SYNC",INH,4},
	{22,2,"LBRA",LREL,5},
	{23,2,"LBSR",LREL,9},
	{25,0,"DAA",INH,2},
	{26,1,"ORCC",IMM,3},
	{28,1,"ANDCC",IMM,3},
	{29,0,"SEX",INH,2},
	{30,1,"EXG",IMM,8},
	{31,1,"TFR",IMM,6},

	{32,1,"BRA",REL,3},
	{33,1,"BRN",REL,3},
	{34,1,"BHI",REL,3},
	{35,1,"BLS",REL,3},
	{36,1,"BCC",REL,3},
	{37,1,"BCS",REL,3},
	{38,1,"BNE",REL,3},
	{39,1,"BEQ",REL,3},
	{40,1,"BVC",REL,3},
	{41,1,"BVS",REL,3},
	{42,1,"BPL",REL,3},
	{43,1,"BMI",REL,3},
	{44,1,"BGE",REL,3},
	{45,1,"BLT",REL,3},
	{46,1,"BGT",REL,3},
	{47,1,"BLE",REL,3},

	{48,1,"LEAX",IND,2},
	{49,1,"LEAY",IND,2},
	{50,1,"LEAS",IND,2},
	{51,1,"LEAU",IND,2},
	{52,1,"PSHS",INH,5},
	{53,1,"PULS",INH,5},
	{54,1,"PSHU",INH,5},
	{55,1,"PULU",INH,5},
	{57,0,"RTS",INH,5},
	{58,0,"ABX",INH,3},
	{59,0,"RTI",INH,6},
	{60,1,"CWAI",IMM,20},
	{61,0,"MUL",INH,11},
	{63,0,"SWI",INH,19},

	{64,0,"NEGA",INH,2},
	{67,0,"COMA",INH,2},
	{68,0,"LSRA",INH,2},
	{70,0,"RORA",INH,2},
	{71,0,"ASRA",INH,2},
	{72,0,"ASLA",INH,2},
	{73,0,"ROLA",INH,2},
	{74,0,"DECA",INH,2},
	{76,0,"INCA",INH,2},
	{77,0,"TSTA",INH,2},
	{79,0,"CLRA",INH,2},

	{80,0,"NEGB",INH,2},
	{83,0,"COMB",INH,2},
	{84,0,"LSRB",INH,2},
	{86,0,"RORB",INH,2},
	{87,0,"ASRB",INH,2},
	{88,0,"ASLB",INH,2},
	{89,0,"ROLB",INH,2},
	{90,0,"DECB",INH,2},
	{92,0,"INCB",INH,2},
	{93,0,"TSTB",INH,2},
	{95,0,"CLRB",INH,2},

	{96,1,"NEG",IND,6},
	{99,1,"COM",IND,6},
	{100,1,"LSR",IND,6},
	{102,1,"ROR",IND,6},
	{103,1,"ASR",IND,6},
	{104,1,"ASL",IND,6},
	{105,1,"ROL",IND,6},
	{106,1,"DEC",IND,6},
	{108,1,"INC",IND,6},
	{109,1,"TST",IND,6},
	{110,1,"JMP",IND,3},
	{111,1,"CLR",IND,6},

	{112,2,"NEG",EXT,7},
	{115,2,"COM",EXT,7},
	{116,2,"LSR",EXT,7},
	{118,2,"ROR",EXT,7},
	{119,2,"ASR",EXT,7},
	{120,2,"ASL",EXT,7},
	{121,2,"ROL",EXT,7},
	{122,2,"DEC",EXT,7},
	{124,2,"INC",EXT,7},
	{125,2,"TST",EXT,7},
	{126,2,"JMP",EXT,4},
	{127,2,"CLR",EXT,7},

	{128,1,"SUBA",IMM,2},
	{129,1,"CMPA",IMM,2},
	{130,1,"SBCA",IMM,2},
	{131,2,"SUBD",IMM,4},
	{132,1,"ANDA",IMM,2},
	{133,1,"BITA",IMM,2},
	{134,1,"LDA",IMM,2},
	{136,1,"EORA",IMM,2},
	{137,1,"ADCA",IMM,2},
	{138,1,"ORA",IMM,2},
	{139,1,"ADDA",IMM,2},
	{140,2,"CMPX",IMM,4},
	{141,1,"BSR",REL,7},
	{142,2,"LDX",IMM,3},

	{144,1,"SUBA",DIR,4},
	{145,1,"CMPA",DIR,4},
	{146,1,"SBCA",DIR,4},
	{147,1,"SUBD",DIR,6},
	{148,1,"ANDA",DIR,4},
	{149,1,"BITA",DIR,4},
	{150,1,"LDA",DIR,4},
	{151,1,"STA",DIR,4},
	{152,1,"EORA",DIR,4},
	{153,1,"ADCA",DIR,4},
	{154,1,"ORA",DIR,4},
	{155,1,"ADDA",DIR,4},
	{156,1,"CPX",DIR,6},
	{157,1,"JSR",DIR,7},
	{158,1,"LDX",DIR,5},
	{159,1,"STX",DIR,5},

	{160,1,"SUBA",IND,4},
	{161,1,"CMPA",IND,4},
	{162,1,"SBCA",IND,4},
	{163,1,"SUBD",IND,6},
	{164,1,"ANDA",IND,4},
	{165,1,"BITA",IND,4},
	{166,1,"LDA",IND,4},
	{167,1,"STA",IND,4},
	{168,1,"EORA",IND,4},
	{169,1,"ADCA",IND,4},
	{170,1,"ORA",IND,4},
	{171,1,"ADDA",IND,4},
	{172,1,"CPX",IND,6},
	{173,1,"JSR",IND,7},
	{174,1,"LDX",IND,5},
	{175,1,"STX",IND,5},

	{176,2,"SUBA",EXT,5},
	{177,2,"CMPA",EXT,5},
	{178,2,"SBCA",EXT,5},
	{179,2,"SUBD",EXT,7},
	{180,2,"ANDA",EXT,5},
	{181,2,"BITA",EXT,5},
	{182,2,"LDA",EXT,5},
	{183,2,"STA",EXT,5},
	{184,2,"EORA",EXT,5},
	{185,2,"ADCA",EXT,5},
	{186,2,"ORA",EXT,5},
	{187,2,"ADDA",EXT,5},
	{188,2,"CPX",EXT,7},
	{189,2,"JSR",EXT,8},
	{190,2,"LDX",EXT,6},
	{191,2,"STX",EXT,6},

	{192,1,"SUBB",IMM,2},
	{193,1,"CMPB",IMM,2},
	{194,1,"SBCB",IMM,2},
	{195,2,"ADDD",IMM,4},
	{196,1,"ANDB",IMM,2},
	{197,1,"BITB",IMM,2},
	{198,1,"LDB",IMM,2},
	{200,1,"EORB",IMM,2},
	{201,1,"ADCB",IMM,2},
	{202,1,"ORB",IMM,2},
	{203,1,"ADDB",IMM,2},
	{204,2,"LDD",IMM,3},
	{206,2,"LDU",IMM,3},

	{208,1,"SUBB",DIR,4},
	{209,1,"CMPB",DIR,4},
	{210,1,"SBCB",DIR,4},
	{211,1,"ADDD",DIR,6},
	{212,1,"ANDB",DIR,4},
	{213,1,"BITB",DIR,4},
	{214,1,"LDB",DIR,4},
	{215,1,"STB",DIR,4},
	{216,1,"EORB",DIR,4},
	{217,1,"ADCB",DIR,4},
	{218,1,"ORB",DIR,4},
	{219,1,"ADDB",DIR,4},
	{220,1,"LDD",DIR,5},
	{221,1,"STD",DIR,5},
	{222,1,"LDU",DIR,5},
	{223,1,"STU",DIR,5},

	{224,1,"SUBB",IND,4},
	{225,1,"CMPB",IND,4},
	{226,1,"SBCB",IND,4},
	{227,1,"ADDD",IND,6},
	{228,1,"ANDB",IND,4},
	{229,1,"BITB",IND,4},
	{230,1,"LDB",IND,4},
	{231,1,"STB",IND,4},
	{232,1,"EORB",IND,4},
	{233,1,"ADCB",IND,4},
	{234,1,"ORB",IND,4},
	{235,1,"ADDB",IND,4},
	{236,1,"LDD",IND,5},
	{237,1,"STD",IND,5},
	{238,1,"LDU",IND,5},
	{239,1,"STU",IND,5},

	{240,2,"SUBB",EXT,5},
	{241,2,"CMPB",EXT,5},
	{242,2,"SBCB",EXT,5},
	{243,2,"ADDD",EXT,7},
	{244,2,"ANDB",EXT,5},
	{245,2,"BITB",EXT,5},
	{246,2,"LDB",EXT,5},
	{247,2,"STB",EXT,5},
	{248,2,"EORB",EXT,5},
	{249,2,"ADCB",EXT,5},
	{250,2,"ORB",EXT,5},
	{251,2,"ADDB",EXT,5},
	{252,2,"LDD",EXT,6},
	{253,2,"STD",EXT,6},
	{254,2,"LDU",EXT,6},
	{255,2,"STU",EXT,6},
};

opcodeinfo pg2opcodes[NUMPG2OPS] = {                       /* page 2 ops 10xx*/
	{33,3,"LBRN",LREL,5},
	{34,3,"LBHI",LREL,5},
	{35,3,"LBLS",LREL,5},
	{36,3,"LBCC",LREL,5},
	{37,3,"LBCS",LREL,5},
	{38,3,"LBNE",LREL,5},
	{39,3,"LBEQ",LREL,5},
	{40,3,"LBVC",LREL,5},
	{41,3,"LBVS",LREL,5},
	{42,3,"LBPL",LREL,5},
	{43,3,"LBMI",LREL,5},
	{44,3,"LBGE",LREL,5},
	{45,3,"LBLT",LREL,5},
	{46,3,"LBGT",LREL,5},
	{47,3,"LBLE",LREL,5},
	{63,2,"SWI2",INH,20},
	{131,3,"CMPD",IMM,5},
	{140,3,"CMPY",IMM,5},
	{142,3,"LDY",IMM,4},
	{147,2,"CMPD",DIR,7},
	{156,2,"CMPY",DIR,7},
	{158,2,"LDY",DIR,6},
	{159,2,"STY",DIR,6},
	{163,2,"CMPD",IND,7},
	{172,2,"CMPY",IND,7},
	{174,2,"LDY",IND,6},
	{175,2,"STY",IND,6},
	{179,3,"CMPD",EXT,8},
	{188,3,"CMPY",EXT,8},
	{190,3,"LDY",EXT,7},
	{191,3,"STY",EXT,7},
	{206,3,"LDS",IMM,4},
	{222,2,"LDS",DIR,6},
	{223,2,"STS",DIR,6},
	{238,2,"LDS",IND,6},
	{239,2,"STS",IND,6},
	{254,3,"LDS",EXT,7},
	{255,3,"STS",EXT,7},
};

opcodeinfo pg3opcodes[NUMPG3OPS] = {                      /* page 3 ops 11xx */
	{63,1,"SWI3",INH,20},
	{131,3,"CMPU",IMM,5},
	{140,3,"CMPS",IMM,5},
	{147,2,"CMPU",DIR,7},
	{156,2,"CMPS",DIR,7},
	{163,2,"CMPU",IND,7},
	{172,2,"CMPS",IND,7},
	{179,3,"CMPU",EXT,8},
	{188,3,"CMPS",EXT,8},
};

opcodeinfo* pgpointers[3] = {
   pg1opcodes,pg2opcodes,pg3opcodes,
};

const char* regs_6809[5] = { "X","Y","U","S","PC" };
const char* teregs[16] = { "D","X","Y","U","S","PC","inv","inv","A","B","CC",
	  "DP","inv","inv","inv","inv" };

static char* hexstring(int address)
{
	static char labtemp[10];
	sprintf(labtemp, "$%04hX", address);
	return labtemp;
}

int cpu_6809::Dasm6809(char* buffer, int pc)
{
	int i, j, k, page, opcode, numoperands, mode;
	unsigned char operandarray[4];
	char* opname;
	int p = 0;

	buffer[0] = 0;
	opcode = M_RDMEM(pc + (p++));
	for (i = 0; i < numops[0]; i++)
		if (pg1opcodes[i].opcode == opcode)
			break;

	if (i < numops[0])
	{
		if (pg1opcodes[i].mode >= PG2)
		{
			opcode = M_RDMEM(pc + (p++));
			page = pg1opcodes[i].mode - PG2 + 1;          /* get page # */
			for (k = 0; k < numops[page]; k++)
				if (opcode == pgpointers[page][k].opcode)
					break;

			if (k != numops[page])
			{                 /* opcode found */
				numoperands = pgpointers[page][k].numoperands - 1;
				for (j = 0; j < numoperands; j++)
					operandarray[j] = M_RDMEM(pc + (p++));
				mode = pgpointers[page][k].mode;
				opname = pgpointers[page][k].name;
				if (mode != IND)
					sprintf(buffer + strlen(buffer), "%-6s", opname);
				goto printoperands;
			}
			else
			{               /* not found in alternate page */
				strcpy(buffer, "Illegal Opcode");
				return 2;
			}
		}
		else
		{                                /* page 1 opcode */
			numoperands = pg1opcodes[i].numoperands;
			for (j = 0; j < numoperands; j++)
				operandarray[j] = M_RDMEM(pc + (p++));
			mode = pg1opcodes[i].mode;
			opname = pg1opcodes[i].name;
			if (mode != IND)
				sprintf(buffer + strlen(buffer), "%-6s", opname);
			goto printoperands;
		}
	}
	else
	{
		strcpy(buffer, "Illegal Opcode");
		return 1;
	}

printoperands:
	pc += p;
	{
		int rel, pb, offset = 0, reg, pb2;
		int comma;
		int printdollar;                  /* print a leading $? before address */

		printdollar = FALSE;

		if ((opcode != 0x1f) && (opcode != 0x1e))
		{
			switch (mode)
			{                              /* print before operands */
			case IMM:
				strcat(buffer, "#");
			case DIR:
			case EXT:
				printdollar = TRUE;
				break;
			default:
				break;
			}
		}

		switch (mode)
		{
		case REL:                                          /* 8-bit relative */
			rel = operandarray[0];
			strcpy(buffer + strlen(buffer), hexstring((short)(pc + ((rel < 128) ? rel : rel - 256))));
			break;

		case LREL:                                   /* 16-bit long relative */
			rel = (operandarray[0] << 8) + operandarray[1];
			strcpy(buffer + strlen(buffer), hexstring(pc + ((rel < 32768) ? rel : rel - 65536)));
			break;

		case IND:                                  /* indirect- many flavors */
			pb = operandarray[0];
			reg = (pb >> 5) & 0x3;
			pb2 = pb & 0x8f;
			if ((pb2 == 0x88) || (pb2 == 0x8c))
			{                    /* 8-bit offset */

			   /* KW 11/05/98 Fix of indirect opcodes      */

			   /*  offset = M6809_RDOP_ARG(pc+(p++));      */

				offset = M_RDMEM(pc);
				p++;

				/* KW 11/05/98 Fix of indirect opcodes      */

				if (offset > 127)                            /* convert to signed */
					offset = offset - 256;
				if (pb == 0x8c)
					reg = 4;
				sprintf(buffer + strlen(buffer), "%-6s", opname);
				if ((pb & 0x90) == 0x90)
					strcat(buffer, "[");
				if (pb == 0x8c)
					sprintf(buffer + strlen(buffer), "%s,%s", hexstring(offset), regs_6809[reg]);
				else if (offset >= 0)
					sprintf(buffer + strlen(buffer), "$%02X,%s", offset, regs_6809[reg]);
				else
					sprintf(buffer + strlen(buffer), "-$%02X,%s", -offset, regs_6809[reg]);
				if (pb == 0x8c)
					sprintf(buffer + strlen(buffer), " ; ($%04X)", offset + pc);
			}
			else if ((pb2 == 0x89) || (pb2 == 0x8d) || (pb2 == 0x8f))
			{ /* 16-bit */

			   /* KW 11/05/98 Fix of indirect opcodes      */

			   /*  offset = M6809_RDOP_ARG(pc+(p++)) << 8; */
			   /*  offset += M6809_RDOP_ARG(pc+(p++));     */

				offset = M_RDMEM(pc) << 8;
				offset += M_RDMEM(pc + 1);
				p += 2;

				/* KW 11/05/98 Fix of indirect opcodes      */

				if ((pb != 0x8f) && (offset > 32767))
					offset = offset - 65536;
				offset &= 0xffff;
				if (pb == 0x8d)
					reg = 4;
				sprintf(buffer + strlen(buffer), "%-6s", opname);
				if ((pb & 0x90) == 0x90)
					strcat(buffer, "[");
				if (pb == 0x8d)
					sprintf(buffer + strlen(buffer), "%s,%s", hexstring(offset), regs_6809[reg]);
				else if (offset >= 0)
					sprintf(buffer + strlen(buffer), "$%04X,%s", offset, regs_6809[reg]);
				else
					sprintf(buffer + strlen(buffer), "-$%04X,%s", offset, regs_6809[reg]);
				if (pb == 0x8d)
					sprintf(buffer + strlen(buffer), " ; ($%04X)", offset + pc);
			}
			else if (pb & 0x80)
			{
				sprintf(buffer + strlen(buffer), "%-6s", opname);
				if ((pb & 0x90) == 0x90)
					strcat(buffer, "[");
				if ((pb & 0x8f) == 0x80)
					sprintf(buffer + strlen(buffer), ",%s+", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x81)
					sprintf(buffer + strlen(buffer), ",%s++", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x82)
					sprintf(buffer + strlen(buffer), ",-%s", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x83)
					sprintf(buffer + strlen(buffer), ",--%s", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x84)
					sprintf(buffer + strlen(buffer), ",%s", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x85)
					sprintf(buffer + strlen(buffer), "B,%s", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x86)
					sprintf(buffer + strlen(buffer), "A,%s", regs_6809[reg]);
				else if ((pb & 0x8f) == 0x8b)
					sprintf(buffer + strlen(buffer), "D,%s", regs_6809[reg]);
			}
			else
			{                                          /* 5-bit offset */
				offset = pb & 0x1f;
				if (offset > 15)
					offset = offset - 32;
				sprintf(buffer + strlen(buffer), "%-6s", opname);
				sprintf(buffer + strlen(buffer), "%s,%s", hexstring(offset), regs_6809[reg]);
			}
			if ((pb & 0x90) == 0x90)
				strcat(buffer, "]");
			break;

		default:
			if ((opcode == 0x1f) || (opcode == 0x1e))
			{                   /* TFR/EXG */
				sprintf(buffer + strlen(buffer), "%s,%s", teregs[(operandarray[0] >> 4) & 0xf], teregs[operandarray[0] & 0xf]);
			}
			else if ((opcode == 0x34) || (opcode == 0x36))
			{              /* PUSH */
				comma = FALSE;
				if (operandarray[0] & 0x80)
				{
					strcat(buffer, "PC");
					comma = TRUE;
				}
				if (operandarray[0] & 0x40)
				{
					if (comma)
						strcat(buffer, ",");
					if ((opcode == 0x34) || (opcode == 0x35))
						strcat(buffer, "U");
					else
						strcat(buffer, "S");
					comma = TRUE;
				}
				if (operandarray[0] & 0x20)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "Y");
					comma = TRUE;
				}
				if (operandarray[0] & 0x10)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "X");
					comma = TRUE;
				}
				if (operandarray[0] & 0x8)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "DP");
					comma = TRUE;
				}
				if (operandarray[0] & 0x4)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "B");
					comma = TRUE;
				}
				if (operandarray[0] & 0x2)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "A");
					comma = TRUE;
				}
				if (operandarray[0] & 0x1)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "CC");
				}
			}
			else if ((opcode == 0x35) || (opcode == 0x37))
			{              /* PULL */
				comma = FALSE;
				if (operandarray[0] & 0x1)
				{
					strcat(buffer, "CC");
					comma = TRUE;
				}
				if (operandarray[0] & 0x2)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "A");
					comma = TRUE;
				}
				if (operandarray[0] & 0x4)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "B");
					comma = TRUE;
				}
				if (operandarray[0] & 0x8)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "DP");
					comma = TRUE;
				}
				if (operandarray[0] & 0x10)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "X");
					comma = TRUE;
				}
				if (operandarray[0] & 0x20)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "Y");
					comma = TRUE;
				}
				if (operandarray[0] & 0x40)
				{
					if (comma)
						strcat(buffer, ",");
					if ((opcode == 0x34) || (opcode == 0x35))
						strcat(buffer, "U");
					else
						strcat(buffer, "S");
					comma = TRUE;
				}
				if (operandarray[0] & 0x80)
				{
					if (comma)
						strcat(buffer, ",");
					strcat(buffer, "PC");
					strcat(buffer, " ; (PUL? PC=RTS)");
				}
			}
			else
			{
				if (numoperands == 2)
				{
					strcat(buffer + strlen(buffer), hexstring((operandarray[0] << 8) + operandarray[1]));
				}
				else
				{
					if (printdollar)
						strcat(buffer, "$");
					for (i = 0; i < numoperands; i++)
						sprintf(buffer + strlen(buffer), "%02X", operandarray[i]);
				}
			}
			break;
		}
	}

	return p;
}