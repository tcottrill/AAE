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
#include "../log.h"
//#include "timer.h"


static unsigned char haspostbyte[] = {
	/*0*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*1*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*2*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*3*/      1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
	/*4*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*5*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*6*/      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	/*7*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*8*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*9*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*A*/      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	/*B*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*C*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*D*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	/*E*/      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	/*F*/      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* timings for 1-uint8_t opcodes */
static unsigned char cycles[] =
{
	/*	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
	/*0*/		6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,
	/*1*/		0, 0, 2, 2, 0, 0, 5, 9, 0, 2, 3, 0, 3, 2, 8, 7,
	/*2*/		3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	/*3*/		4, 4, 4, 4, 5, 5, 5, 5, 0, 5, 3, 6, 0,11, 0,19,
	/*4*/		2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
	/*5*/		2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
	/*6*/		6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,
	/*7*/		7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,
	/*8*/		2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 0,
	/*9*/		4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
	/*A*/		4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
	/*B*/		5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6,
	/*C*/		2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 0, 3, 3,
	/*D*/		4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*E*/		4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*F*/		5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6
};

/* timings for 2-uint8_t opcodes */
static unsigned char cycles2[] =
{
	/*	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
	/*0*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*1*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*2*/		0, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	/*3*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,20,
	/*4*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*5*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*6*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*7*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/*8*/		0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 4, 0,
	/*9*/		0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 6, 6,
	/*A*/		0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 6, 6,
	/*B*/		0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 7, 7,
	/*C*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,
	/*D*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6,
	/*E*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6,
	/*F*/		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7
};


cpu_6809::cpu_6809(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	cpu_num = num;
	init6809(addr);
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
			//if  (cpu_num) wrlog("Reading from address: %x", addr);
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
		//wrlog("Reading from address: %x", addr);
		temp = MEM[addr];
	}
	if (MemRead && mmem)
	{
		if (log_debug_rw) wrlog("Warning! Unhandled Read at %x", addr);
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
			//if (cpu_num) wrlog("Writing to address: %x data: %x", addr, byte);
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
		if (log_debug_rw) wrlog("Warning! Unhandled Write at %x data: %x", addr, byte);
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
	pcreg = newpc;
}


uint8_t cpu_6809::M_RDMEM(uint16_t A)
{
	return  get6809memory(A);
}

void cpu_6809::M_WRMEM(uint16_t A, uint8_t V)
{
	put6809memory(A, V);

}
//////////////////////////////////////////////////////////////////////////
unsigned int cpu_6809::M_RDMEM_WORD(uint32_t A)
{
	int i;

	i = get6809memory(A) << 8;
	i |= get6809memory(((A)+1) & 0xFFFF);
	return i;
}

void cpu_6809::M_WRMEM_WORD(uint32_t A, uint16_t V)
{
	put6809memory(A, V >> 8);
	put6809memory(((A)+1) & 0xFFFF, V & 255);
}
//THESE GO THROUGH THE HANDLERS


int32_t  cpu_6809::rd_slow(int32_t addr)
{
	return get6809memory(addr);
}

int32_t  cpu_6809::rd_slow_wd(int32_t addr)
{
	return((get6809memory(addr) << 8) | (get6809memory((addr + 1) & 0xffff)));
}

void cpu_6809::wr_slow(int32_t addr, int32_t v)
{
	put6809memory(addr, v);
}

void cpu_6809::wr_slow_wd(int32_t addr, int32_t v)
{
	put6809memory(addr, v >> 8);
	put6809memory(((addr)+1) & 0xFFFF, v & 255);
}
//////////////////////

//These do NOT go through the handlers
int32_t  cpu_6809::rd_fast(int32_t addr)
{
	//wrlog("Reading from address Fast: %x", addr);
	return MEM[addr];
}

int32_t  cpu_6809::rd_fast_wd(int32_t addr)
{
	//wrlog("Reading from address Fast: %x", addr);
	return((MEM[addr] << 8) | (MEM[(addr + 1) & 0xffff]));
}

void cpu_6809::wr_fast(int32_t addr, int32_t v)
{
	//wrlog("Writing to address Fast: %x", addr);
	MEM[addr] = v;
}

void cpu_6809::wr_fast_wd(int32_t addr, int32_t v)
{
	//wrlog("Writing to address Fast: %x", addr);


	MEM[addr] = v >> 8;
	MEM[(addr + 1) & 0xffff] = v & 255;
}

////////////////////////////////////

/* macros to access memory */

//#define immword(w)	{w=(rd_slow_wd)(pcreg);pcreg+=2;}

#define PUSHBYTE(b) {--sreg;(wr_fast)(sreg,b);}
#define PUSHWORD(w) {sreg-=2;(wr_fast_wd)(sreg,w);}

#define PULLBYTE(b) {b=(rd_fast)(sreg);sreg++;}
#define PULLWORD(w) {w=(rd_fast_wd)(sreg);sreg+=2;}

#define PSHUBYTE(b) {--ureg;(wr_fast)(ureg,b);}
#define PSHUWORD(w) {ureg-=2;(wr_fast_wd)(ureg,w);}

#define PULUBYTE(b) {b=(rd_fast)(ureg);ureg++;}
#define PULUWORD(w) {w=(rd_fast_wd)(ureg);ureg+=2;}

/* CC masks						  H  NZVC
								7654 3210	*/
#define CLR_HNZVC	cc&=0xd0
#define CLR_NZV		cc&=0xf1
#define CLR_HNZC	cc&=0xd2
#define CLR_NZVC	cc&=0xf0
#define CLR_Z		cc&=0xfb
#define CLR_NZC		cc&=0xf2
#define CLR_ZC		cc&=0xfa

								/* macros for CC -- CC bits affected should be reset before calling */
#define SET_Z(a)		if(!a)SEZ
#define SET_Z8(a)		SET_Z((uint8_t)a)
#define SET_Z16(a)		SET_Z((uint16_t)a)
#define SET_N8(a)		cc|=((a&0x80)>>4)
#define SET_N16(a)		cc|=((a&0x8000)>>12)
#define SET_H(a,b,r)	cc|=(((a^b^r)&0x10)<<1)
#define SET_C8(a)		cc|=((a&0x100)>>8)
#define SET_C16(a)		cc|=((a&0x10000)>>16)
#define SET_V8(a,b,r)	cc|=(((a^b^r^(r>>1))&0x80)>>6)
#define SET_V16(a,b,r)	cc|=(((a^b^r^(r>>1))&0x8000)>>14)
/* combos */
#define SET_NZ8(a)			{SET_N8(a);SET_Z(a);}
#define SET_NZ16(a)			{SET_N16(a);SET_Z(a);}
#define SET_FLAGS8(a,b,r)	{SET_N8(r);SET_Z8(r);SET_V8(a,b,r);SET_C8(r);}
#define SET_FLAGS16(a,b,r)	{SET_N16(r);SET_Z16(r);SET_V16(a,b,r);SET_C16(r);}

/* for treating an unsigned uint8_t as a signed uint16_t */
#define SIGNED(b) ((uint16_t)(b&0x80?b|0xff00:b))

/* macros to access dreg */
#define GETDREG ((areg<<8)|breg)
#define SETDREG(n) {areg=(n)>>8;breg=(n);}

/* macros for addressing modes (postbytes have their own code) */

//#define IMM8 eaddr=pcreg++
//#define IMM16 {eaddr=pcreg;pcreg+=2;}

/* macros to set status flags */
#define SEC cc|=0x01
#define CLC cc&=0xfe
#define SEZ cc|=0x04
#define CLZ cc&=0xfb
#define SEN cc|=0x08
#define CLN cc&=0xf7
#define SEV cc|=0x02
#define CLV cc&=0xfd
#define SEH cc|=0x20
#define CLH cc&=0xdf

/* macros for convenience */

/* macros for branch instructions */
//#define BRANCH(f) {    t=(rd_slow)(pcreg);pcreg++; if(f)pcreg+=SIGNED(t);}
//#define LBRANCH(f) {t=(rd_slow_wd)(pcreg);pcreg+=2; if(f)pcreg+=t;}
//#define NXORV  ((cc&0x08)^((cc&0x02)<<2))

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
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
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
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	t = ~t;
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $04 LSR direct -0*-* */
void cpu_6809::lsr_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $05 ILLEGAL */

/* $06 ROR direct -**-* */
void cpu_6809::ror_di()
{
	uint8_t t = 0; uint8_t r = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $07 ASR direct ?**-* */
void cpu_6809::asr_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	CLR_NZC; cc |= (t & 0x01);
	t >>= 1; t |= ((t & 0x40) << 1);
	SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $08 ASL direct ?**** */
void cpu_6809::asl_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $09 ROL direct -**** */
void cpu_6809::rol_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $0A DEC direct -***- */
void cpu_6809::dec_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	--t;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $0B ILLEGAL */

/* $OC INC direct -***- */
void cpu_6809::inc_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	++t;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $OD TST direct -**0- */
void cpu_6809::tst_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(t);
}

/* $0E JMP direct ----- */
void cpu_6809::jmp_di()
{
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	pcreg = eaddr;
}

/* $0F CLR direct -0100 */
void cpu_6809::clr_di()
{
	//DIRECT;

	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);

	M_WRMEM(eaddr, 0);
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
	clockticks6809 = 0;
}

/* $14 ILLEGAL */

/* $15 ILLEGAL */

/* $16 LBRA relative ----- */
void cpu_6809::lbra()
{
	eaddr = rd_slow_wd(pcreg); pcreg += 2;
	pcreg += eaddr;
}

/* $17 LBSR relative ----- */
void cpu_6809::lbsr()
{
	eaddr = rd_slow_wd(pcreg); pcreg += 2;
	PUSHWORD(pcreg); pcreg += eaddr;
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
	t = rd_slow(pcreg); pcreg++;//immuint8_t(t);
	cc |= t;
}

/* $1B ILLEGAL */

/* $1C ANDCC immediate ##### */
void cpu_6809::andcc()
{
	uint8_t t = 0;
	t = rd_slow(pcreg); pcreg++;//immuint8_t(t);
	cc &= t;
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
	uint16_t t1 = 0, t2 = 0;
	uint8_t tb;

	//immuint8_t(tb);
	tb = (rd_slow)(pcreg); pcreg++;
	GETREG(t1, tb >> 4);
	GETREG(t2, tb & 15);
	SETREG(t2, tb >> 4);
	SETREG(t1, tb & 15);
}

/* $1F TFR inherent ----- */
void cpu_6809::tfr()
{
	uint8_t tb;
	uint16_t t = 0;
	tb = (rd_slow)(pcreg); pcreg++;
	//immuint8_t(tb);
	GETREG(t, tb >> 4);
	SETREG(t, tb & 15);
}

/* $20 BRA relative ----- */
void cpu_6809::bra()
{
	uint8_t t = 0;
	//BRANCH(1);
	t = (rd_slow)(pcreg); pcreg++; if (1)pcreg += SIGNED(t);
	/* JB 970823 - speed up busy loops */
	if (t == 0xfe) clockticks6809 = 0;
}

/* $21 BRN relative ----- */
void cpu_6809::brn()
{
	uint8_t t = 0;
	//BRANCH(0);
	t = (rd_slow)(pcreg); pcreg++; if (0)pcreg += SIGNED(t);
}

/* $1021 LBRN relative ----- */
void cpu_6809::lbrn()
{
	uint16_t t;
	//LBRANCH(0);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (0)pcreg += t;
}

/* $22 BHI relative ----- */
void cpu_6809::bhi()
{
	uint8_t t = 0;
	//BRANCH(!(cc & 0x05));
	t = (rd_slow)(pcreg); pcreg++; if (!(cc & 0x05))pcreg += SIGNED(t);
}

/* $1022 LBHI relative ----- */
void cpu_6809::lbhi()
{
	uint16_t t;
	//LBRANCH(!(cc & 0x05));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(cc & 0x05))pcreg += t;
}

/* $23 BLS relative ----- */
void cpu_6809::bls()
{
	uint8_t t = 0;
	//BRANCH(cc & 0x05);
	t = (rd_slow)(pcreg); pcreg++; if (cc & 0x05) pcreg += SIGNED(t);
}

/* $1023 LBLS relative ----- */
void cpu_6809::lbls()
{
	uint16_t t;
	//LBRANCH(cc & 0x05);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (cc & 0x05) pcreg += t;
}

/* $24 BCC relative ----- */
void cpu_6809::bcc()
{
	uint8_t t = 0;
	//BRANCH(!(cc & 0x01));
	t = (rd_slow)(pcreg); pcreg++; if (!(cc & 0x01))pcreg += SIGNED(t);
}

/* $1024 LBCC relative ----- */
void cpu_6809::lbcc()
{
	uint16_t t;
	//LBRANCH(!(cc & 0x01));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(cc & 0x01)) pcreg += t;
}

/* $25 BCS relative ----- */
void cpu_6809::bcs()
{
	uint8_t t = 0;
	//BRANCH(cc & 0x01);
	t = (rd_slow)(pcreg); pcreg++;
	if (cc & 0x01)pcreg += SIGNED(t);
}

/* $1025 LBCS relative ----- */
void cpu_6809::lbcs()
{
	uint16_t t;
	//LBRANCH(cc & 0x01);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (cc & 0x01) pcreg += t;
}

/* $26 BNE relative ----- */
void cpu_6809::bne()
{
	uint8_t t = 0;
	//BRANCH(!(cc & 0x04));
	t = (rd_slow)(pcreg); pcreg++; if (!(cc & 0x04))pcreg += SIGNED(t);
}

/* $1026 LBNE relative ----- */
void cpu_6809::lbne()
{
	uint16_t t;
	//LBRANCH(!(cc & 0x04));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(cc & 0x04)) pcreg += t;
}

/* $27 BEQ relative ----- */
void cpu_6809::beq()
{
	uint8_t t = 0;
	//BRANCH(cc & 0x04);
	t = (rd_slow)(pcreg); pcreg++; if (cc & 0x04)pcreg += SIGNED(t);
}

/* $1027 LBEQ relative ----- */
void cpu_6809::lbeq()
{
	uint16_t t;
	//LBRANCH(cc & 0x04);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (cc & 0x04) pcreg += t;
}

/* $28 BVC relative ----- */
void cpu_6809::bvc()
{
	uint8_t t = 0;
	//BRANCH(!(cc & 0x02));
	t = (rd_slow)(pcreg); pcreg++; if (!(cc & 0x02))pcreg += SIGNED(t);
}

/* $1028 LBVC relative ----- */
void cpu_6809::lbvc()
{
	uint16_t t;
	//LBRANCH(!(cc & 0x02));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(cc & 0x02)) pcreg += t;
}

/* $29 BVS relative ----- */
void cpu_6809::bvs()
{
	uint8_t t = 0;
	//BRANCH(cc & 0x02);
	t = (rd_slow)(pcreg); pcreg++; if (cc & 0x02) pcreg += SIGNED(t);
}

/* $1029 LBVS relative ----- */
void cpu_6809::lbvs()
{
	uint16_t t;
	//LBRANCH(cc & 0x02);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (cc & 0x02) pcreg += t;
}

/* $2A BPL relative ----- */
void cpu_6809::bpl()
{
	uint8_t t = 0;
	//BRANCH(!(cc & 0x08));
	t = (rd_slow)(pcreg); pcreg++; if (!(cc & 0x08))pcreg += SIGNED(t);
}

/* $102A LBPL relative ----- */
void cpu_6809::lbpl()
{
	uint16_t t;
	//LBRANCH(!(cc & 0x08));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(cc & 0x08)) pcreg += t;
}

/* $2B BMI relative ----- */
void cpu_6809::bmi()
{
	uint8_t t = 0;
	//BRANCH(cc & 0x08);
	t = (rd_slow)(pcreg); pcreg++; if (cc & 0x08)pcreg += SIGNED(t);
}

/* $102B LBMI relative ----- */
void cpu_6809::lbmi()
{
	uint16_t t;
	//LBRANCH(cc & 0x08);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (cc & 0x08) pcreg += t;
}

/* $2C BGE relative ----- */
void cpu_6809::bge()
{
	uint8_t t = 0;
	//BRANCH(!((cc & 0x08) ^ ((cc & 0x02) << 2)));
	t = (rd_slow)(pcreg); pcreg++; if (!((cc & 0x08) ^ ((cc & 0x02) << 2))) pcreg += SIGNED(t);
}

/* $102C LBGE relative ----- */
void cpu_6809::lbge()
{
	uint16_t t;
	//LBRANCH(!((cc & 0x08) ^ ((cc & 0x02) << 2)));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!((cc & 0x08) ^ ((cc & 0x02) << 2))) pcreg += t;
}

/* $2D BLT relative ----- */
void cpu_6809::blt()
{
	uint8_t t = 0;
	//BRANCH(((cc & 0x08) ^ ((cc & 0x02) << 2)));
	t = (rd_slow)(pcreg); pcreg++; if ((cc & 0x08) ^ ((cc & 0x02) << 2)) pcreg += SIGNED(t);
}

/* $102D LBLT relative ----- */
void cpu_6809::lblt()
{
	uint16_t t;
	//LBRANCH(((cc & 0x08) ^ ((cc & 0x02) << 2)));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (((cc & 0x08) ^ ((cc & 0x02) << 2))) pcreg += t;
}

/* $2E BGT relative ----- */
void cpu_6809::bgt()
{
	uint8_t t = 0;
	//BRANCH(!(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04));
	t = (rd_slow)(pcreg); pcreg++; if (!(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04)) pcreg += SIGNED(t);
}

/* $102E LBGT relative ----- */
void cpu_6809::lbgt()
{
	uint16_t t;
	//LBRANCH(!(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04));
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (!(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04)) pcreg += t;
}

/* $2F BLE relative ----- */
void cpu_6809::ble()
{
	uint8_t t = 0;
	//BRANCH(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04);
	t = (rd_slow)(pcreg); pcreg++; if (((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04) pcreg += SIGNED(t);
}

/* $102F LBLE relative ----- */
void cpu_6809::lble()
{
	uint16_t t;
	//LBRANCH(((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04);
	t = (rd_slow_wd)(pcreg); pcreg += 2; if (((cc & 0x08) ^ ((cc & 0x02) << 2)) || cc & 0x04) pcreg += t;
}

/* $30 LEAX indexed --*-- */
void cpu_6809::leax()
{
	xreg = eaddr; CLR_Z; SET_Z(xreg);
}

/* $31 LEAY indexed --*-- */
void cpu_6809::leay()
{
	yreg = eaddr; CLR_Z; SET_Z(yreg);
}

/* $32 LEAS indexed ----- */
void cpu_6809::leas()
{
	sreg = eaddr;
}

/* $33 LEAU indexed ----- */
void cpu_6809::leau()
{
	ureg = eaddr;
}

/* $34 PSHS inherent ----- */
void cpu_6809::pshs()
{
	uint8_t t = 0;
	t = (rd_slow)(pcreg); pcreg++;
	//immuint8_t(t);
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
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	if (t & 0x01) PULLBYTE(cc);
	if (t & 0x02) PULLBYTE(areg);
	if (t & 0x04) PULLBYTE(breg);
	if (t & 0x08) PULLBYTE(dpreg);
	if (t & 0x10) PULLWORD(xreg);
	if (t & 0x20) PULLWORD(yreg);
	if (t & 0x40) PULLWORD(ureg);
	if (t & 0x80) PULLWORD(pcreg);
}

/* $36 PSHU inherent ----- */
void cpu_6809::pshu()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
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
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	if (t & 0x01) PULUBYTE(cc);
	if (t & 0x02) PULUBYTE(areg);
	if (t & 0x04) PULUBYTE(breg);
	if (t & 0x08) PULUBYTE(dpreg);
	if (t & 0x10) PULUWORD(xreg);
	if (t & 0x20) PULUWORD(yreg);
	if (t & 0x40) PULUWORD(sreg);
	if (t & 0x80) PULUWORD(pcreg);
}

/* $38 ILLEGAL */

/* $39 RTS inherent ----- */
void cpu_6809::rts()
{
	PULLWORD(pcreg);
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
}

/* $3C CWAI inherent ----1 */
void cpu_6809::cwai()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	cc &= t;
	/* CWAI should stack the entire machine state on the hardware stack,
		then wait for an interrupt. A poor fake is to force an IRQ. */
	clockticks6809 = 0;
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
	cc |= 0x80;
	PUSHWORD(pcreg);
	PUSHWORD(ureg);
	PUSHWORD(yreg);
	PUSHWORD(xreg);
	PUSHBYTE(dpreg);
	PUSHBYTE(breg);
	PUSHBYTE(areg);
	PUSHBYTE(cc);
	cc |= 0x50;
	pcreg = M_RDMEM_WORD(0xfffa);
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
	t = ~M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $64 LSR indexed -0*-* */
void cpu_6809::lsr_ix()
{
	uint8_t t = 0;
	t = M_RDMEM(eaddr); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $65 ILLEGAL */

/* $66 ROR indexed -**-* */
void cpu_6809::ror_ix()
{
	uint8_t t = 0; uint8_t r = 0;
	t = M_RDMEM(eaddr); r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $67 ASR indexed ?**-* */
void cpu_6809::asr_ix()
{
	uint8_t t = 0;
	t = M_RDMEM(eaddr); CLR_NZC; cc |= (t & 0x01);
	t >>= 1; t |= ((t & 0x40) << 1);
	SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $68 ASL indexed ?**** */
void cpu_6809::asl_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $69 ROL indexed -**** */
void cpu_6809::rol_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $6A DEC indexed -***- */
void cpu_6809::dec_ix()
{
	uint8_t t = 0;
	t = M_RDMEM(eaddr) - 1;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $6B ILLEGAL */

/* $6C INC indexed -***- */
void cpu_6809::inc_ix()
{
	uint8_t t = 0;
	t = M_RDMEM(eaddr) + 1;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $6D TST indexed -**0- */
void cpu_6809::tst_ix()
{
	uint8_t t = 0;
	t = M_RDMEM(eaddr); CLR_NZV; SET_NZ8(t);
}

/* $6E JMP indexed ----- */
void cpu_6809::jmp_ix()
{
	pcreg = eaddr;
}

/* $6F CLR indexed -0100 */
void cpu_6809::clr_ix()
{
	M_WRMEM(eaddr, 0);
	CLR_NZVC; SEZ;
}

/* $70 NEG extended ?**** */
void cpu_6809::neg_ex()
{
	uint16_t r, t;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
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
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	t = ~t;
	CLR_NZV; SET_NZ8(t); SEC;
	M_WRMEM(eaddr, t);
}

/* $74 LSR extended -0*-* */
void cpu_6809::lsr_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	CLR_NZC; cc |= (t & 0x01);
	t >>= 1; SET_Z8(t);
	M_WRMEM(eaddr, t);
}

/* $75 ILLEGAL */

/* $76 ROR extended -**-* */
void cpu_6809::ror_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = (cc & 0x01) << 7;
	CLR_NZC; cc |= (t & 0x01);
	r |= t >> 1; SET_NZ8(r);
	M_WRMEM(eaddr, r);
}

/* $77 ASR extended ?**-* */
void cpu_6809::asr_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
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
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $79 ROL extended -**** */
void cpu_6809::rol_ex()
{
	uint16_t t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = cc & 0x01; r |= t << 1;
	CLR_NZVC; SET_FLAGS8(t, t, r);
	M_WRMEM(eaddr, r);
}

/* $7A DEC extended -***- */
void cpu_6809::dec_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	--t;
	CLR_NZV; if (t == 0x7F) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $7B ILLEGAL */

/* $7C INC extended -***- */
void cpu_6809::inc_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	++t;
	CLR_NZV; if (t == 0x80) SEV; SET_NZ8(t);
	M_WRMEM(eaddr, t);
}

/* $7D TST extended -**0- */
void cpu_6809::tst_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(t);
}

/* $7E JMP extended ----- */
void cpu_6809::jmp_ex()
{
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	pcreg = eaddr;
}

/* $7F CLR extended -0100 */
void cpu_6809::clr_ex()
{
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM(eaddr, 0);
	CLR_NZVC; SEZ;
}

/* $80 SUBA immediate ?**** */
void cpu_6809::suba_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $81 CMPA immediate ?**** */
void cpu_6809::cmpa_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $82 SBCA immediate ?**** */
void cpu_6809::sbca_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $83 SUBD (CMPD CMPU) immediate -**** */
void cpu_6809::subd_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $1083 CMPD immediate -**** */
void cpu_6809::cmpd_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $1183 CMPU immediate -**** */
void cpu_6809::cmpu_im()
{
	uint32_t r, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $84 ANDA immediate -**0- */
void cpu_6809::anda_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $85 BITA immediate -**0- */
void cpu_6809::bita_im()
{
	uint8_t t = 0; uint8_t r = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $86 LDA immediate -**0- */
void cpu_6809::lda_im()
{
	//immuint8_t(areg);
	areg = (rd_slow)(pcreg); pcreg++;

	CLR_NZV; SET_NZ8(areg);
}

/* is this a legal instruction? */
/* $87 STA immediate -**0- */
void cpu_6809::sta_im()
{
	CLR_NZV; SET_NZ8(areg);
	//IMM8;
	eaddr = pcreg++;
	M_WRMEM(eaddr, areg);
}

/* $88 EORA immediate -**0- */
void cpu_6809::eora_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $89 ADCA immediate ***** */
void cpu_6809::adca_im()
{
	uint16_t t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $8A ORA immediate -**0- */
void cpu_6809::ora_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $8B ADDA immediate ***** */
void cpu_6809::adda_im()
{
	uint16_t t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $8C CMPX (CMPY CMPS) immediate -**** */
void cpu_6809::cmpx_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $108C CMPY immediate -**** */
void cpu_6809::cmpy_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $118C CMPS immediate -**** */
void cpu_6809::cmps_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $8D BSR ----- */
void cpu_6809::bsr()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	PUSHWORD(pcreg); pcreg += SIGNED(t);
}

/* $8E LDX (LDY) immediate -**0- */
void cpu_6809::ldx_im()
{
	xreg = (rd_slow_wd)(pcreg); pcreg += 2;
	CLR_NZV; SET_NZ16(xreg);
}

/* $108E LDY immediate -**0- */
void cpu_6809::ldy_im()
{
	yreg = (rd_slow_wd)(pcreg); pcreg += 2;
	CLR_NZV; SET_NZ16(yreg);
}

/* is this a legal instruction? */
/* $8F STX (STY) immediate -**0- */
void cpu_6809::stx_im()
{
	CLR_NZV; SET_NZ16(xreg);
	//IMM16;
	eaddr = pcreg; pcreg += 2;
	M_WRMEM_WORD(eaddr, xreg);
}

/* is this a legal instruction? */
/* $108F STY immediate -**0- */
void cpu_6809::sty_im()
{
	CLR_NZV; SET_NZ16(yreg);
	//IMM16;
	eaddr = pcreg; pcreg += 2;
	M_WRMEM_WORD(eaddr, yreg);
}

/* $90 SUBA direct ?**** */
void cpu_6809::suba_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $91 CMPA direct ?**** */
void cpu_6809::cmpa_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $92 SBCA direct ?**** */
void cpu_6809::sbca_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $93 SUBD (CMPD CMPU) direct -**** */
void cpu_6809::subd_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $1093 CMPD direct -**** */
void cpu_6809::cmpd_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $1193 CMPU direct -**** */
void cpu_6809::cmpu_di()
{
	uint32_t r, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $94 ANDA direct -**0- */
void cpu_6809::anda_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $95 BITA direct -**0- */
void cpu_6809::bita_di()
{
	uint8_t t = 0; uint8_t r = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $96 LDA direct -**0- */
void cpu_6809::lda_di()
{
	//DIRuint8_t(areg);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	areg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $97 STA direct -**0- */
void cpu_6809::sta_di()
{
	CLR_NZV; SET_NZ8(areg);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM(eaddr, areg);
}

/* $98 EORA direct -**0- */
void cpu_6809::eora_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $99 ADCA direct ***** */
void cpu_6809::adca_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);
	r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $9A ORA direct -**0- */
void cpu_6809::ora_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $9B ADDA direct ***** */
void cpu_6809::adda_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $9C CMPX (CMPY CMPS) direct -**** */
void cpu_6809::cmpx_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $109C CMPY direct -**** */
void cpu_6809::cmpy_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $119C CMPS direct -**** */
void cpu_6809::cmps_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $9D JSR direct ----- */
void cpu_6809::jsr_di()
{
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	PUSHWORD(pcreg);
	pcreg = eaddr;
}

/* $9E LDX (LDY) direct -**0- */
void cpu_6809::ldx_di()
{
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	xreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(xreg);
}

/* $109E LDY direct -**0- */
void cpu_6809::ldy_di()
{
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	yreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(yreg);
}

/* $9F STX (STY) direct -**0- */
void cpu_6809::stx_di()
{
	CLR_NZV; SET_NZ16(xreg);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM_WORD(eaddr, xreg);
}

/* $109F STY direct -**0- */
void cpu_6809::sty_di()
{
	CLR_NZV; SET_NZ16(yreg);
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM_WORD(eaddr, yreg);
}

/* $a0 SUBA indexed ?**** */
void cpu_6809::suba_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $a1 CMPA indexed ?**** */
void cpu_6809::cmpa_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $a2 SBCA indexed ?**** */
void cpu_6809::sbca_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $a3 SUBD (CMPD CMPU) indexed -**** */
void cpu_6809::subd_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $10a3 CMPD indexed -**** */
void cpu_6809::cmpd_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11a3 CMPU indexed -**** */
void cpu_6809::cmpu_ix()
{
	uint32_t r, b;
	b = M_RDMEM_WORD(eaddr); r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $a4 ANDA indexed -**0- */
void cpu_6809::anda_ix()
{
	areg &= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a5 BITA indexed -**0- */
void cpu_6809::bita_ix()
{
	uint8_t r;
	r = areg & M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(r);
}

/* $a6 LDA indexed -**0- */
void cpu_6809::lda_ix()
{
	areg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a7 STA indexed -**0- */
void cpu_6809::sta_ix()
{
	CLR_NZV; SET_NZ8(areg);
	M_WRMEM(eaddr, areg);
}

/* $a8 EORA indexed -**0- */
void cpu_6809::eora_ix()
{
	areg ^= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $a9 ADCA indexed ***** */
void cpu_6809::adca_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $aA ORA indexed -**0- */
void cpu_6809::ora_ix()
{
	areg |= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $aB ADDA indexed ***** */
void cpu_6809::adda_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $aC CMPX (CMPY CMPS) indexed -**** */
void cpu_6809::cmpx_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $10aC CMPY indexed -**** */
void cpu_6809::cmpy_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11aC CMPS indexed -**** */
void cpu_6809::cmps_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $aD JSR indexed ----- */
void cpu_6809::jsr_ix()
{
	PUSHWORD(pcreg);
	pcreg = eaddr;
}

/* $aE LDX (LDY) indexed -**0- */
void cpu_6809::ldx_ix()
{
	xreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(xreg);
}

/* $10aE LDY indexed -**0- */
void cpu_6809::ldy_ix()
{
	yreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(yreg);
}

/* $aF STX (STY) indexed -**0- */
void cpu_6809::stx_ix()
{
	CLR_NZV; SET_NZ16(xreg);
	M_WRMEM_WORD(eaddr, xreg);
}

/* $10aF STY indexed -**0- */
void cpu_6809::sty_ix()
{
	CLR_NZV; SET_NZ16(yreg);
	M_WRMEM_WORD(eaddr, yreg);
}

/* $b0 SUBA extended ?**** */
void cpu_6809::suba_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $b1 CMPA extended ?**** */
void cpu_6809::cmpa_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg - t;
	CLR_NZVC; SET_FLAGS8(areg, t, r);
}

/* $b2 SBCA extended ?**** */
void cpu_6809::sbca_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(areg, t, r);
	areg = r;
}

/* $b3 SUBD (CMPD CMPU) extended -**** */
void cpu_6809::subd_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $10b3 CMPD extended -**** */
void cpu_6809::cmpd_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11b3 CMPU extended -**** */
void cpu_6809::cmpu_ex()
{
	uint32_t r, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	r = ureg - b;
	CLR_NZVC; SET_FLAGS16(ureg, b, r);
}

/* $b4 ANDA extended -**0- */
void cpu_6809::anda_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	areg &= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $b5 BITA extended -**0- */
void cpu_6809::bita_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $b6 LDA extended -**0- */
void cpu_6809::lda_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	areg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(areg);
}

/* $b7 STA extended -**0- */
void cpu_6809::sta_ex()
{
	CLR_NZV; SET_NZ8(areg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM(eaddr, areg);
}

/* $b8 EORA extended -**0- */
void cpu_6809::eora_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	areg ^= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $b9 ADCA extended ***** */
void cpu_6809::adca_ex()
{
	uint16_t t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $bA ORA extended -**0- */
void cpu_6809::ora_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	areg |= t;
	CLR_NZV; SET_NZ8(areg);
}

/* $bB ADDA extended ***** */
void cpu_6809::adda_ex()
{
	uint16_t t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = areg + t;
	CLR_HNZVC; SET_FLAGS8(areg, t, r); SET_H(areg, t, r);
	areg = r;
}

/* $bC CMPX (CMPY CMPS) extended -**** */
void cpu_6809::cmpx_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = xreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $10bC CMPY extended -**** */
void cpu_6809::cmpy_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = yreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $11bC CMPS extended -**** */
void cpu_6809::cmps_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = sreg; r = d - b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
}

/* $bD JSR extended ----- */
void cpu_6809::jsr_ex()
{
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	PUSHWORD(pcreg);
	pcreg = eaddr;
}

/* $bE LDX (LDY) extended -**0- */
void cpu_6809::ldx_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	xreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(xreg);
}

/* $10bE LDY extended -**0- */
void cpu_6809::ldy_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	yreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(yreg);
}

/* $bF STX (STY) extended -**0- */
void cpu_6809::stx_ex()
{
	CLR_NZV; SET_NZ16(xreg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM_WORD(eaddr, xreg);
}

/* $10bF STY extended -**0- */
void cpu_6809::sty_ex()
{
	CLR_NZV; SET_NZ16(yreg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM_WORD(eaddr, yreg);
}

/* $c0 SUBB immediate ?**** */
void cpu_6809::subb_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $c1 CMPB immediate ?**** */
void cpu_6809::cmpb_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $c2 SBCB immediate ?**** */
void cpu_6809::sbcb_im()
{
	uint16_t	t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $c3 ADDD immediate -**** */
void cpu_6809::addd_im()
{
	uint32_t r, d, b;
	b = (rd_slow_wd)(pcreg); pcreg += 2;
	d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $c4 ANDB immediate -**0- */
void cpu_6809::andb_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $c5 BITB immediate -**0- */
void cpu_6809::bitb_im()
{
	uint8_t t = 0; uint8_t r = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $c6 LDB immediate -**0- */
void cpu_6809::ldb_im()
{
	//immuint8_t(breg);
	breg = (rd_slow)(pcreg); pcreg++;
	CLR_NZV; SET_NZ8(breg);
}

/* is this a legal instruction? */
/* $c7 STB immediate -**0- */
void cpu_6809::stb_im()
{
	CLR_NZV; SET_NZ8(breg);
	//IMM8;
	eaddr = pcreg++;
	M_WRMEM(eaddr, breg);
}

/* $c8 EORB immediate -**0- */
void cpu_6809::eorb_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $c9 ADCB immediate ***** */
void cpu_6809::adcb_im()
{
	uint16_t t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $cA ORB immediate -**0- */
void cpu_6809::orb_im()
{
	uint8_t t = 0;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $cB ADDB immediate ***** */
void cpu_6809::addb_im()
{
	uint16_t t, r;
	//immuint8_t(t);
	t = (rd_slow)(pcreg); pcreg++;
	r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $cC LDD immediate -**0- */
void cpu_6809::ldd_im()
{
	uint16_t t;
	t = (rd_slow_wd)(pcreg); pcreg += 2;
	SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* is this a legal instruction? */
/* $cD STD immediate -**0- */
void cpu_6809::std_im()
{
	uint16_t t;
	//IMM16;
	eaddr = pcreg; pcreg += 2;
	t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $cE LDU (LDS) immediate -**0- */
void cpu_6809::ldu_im()
{
	ureg = (rd_slow_wd)(pcreg); pcreg += 2;
	CLR_NZV; SET_NZ16(ureg);
}

/* $10cE LDS immediate -**0- */
void cpu_6809::lds_im()
{
	sreg = (rd_slow_wd)(pcreg); pcreg += 2;
	CLR_NZV; SET_NZ16(sreg);
}

/* is this a legal instruction? */
/* $cF STU (STS) immediate -**0- */
void cpu_6809::stu_im()
{
	CLR_NZV; SET_NZ16(ureg);
	//IMM16;
	eaddr = pcreg; pcreg += 2;
	M_WRMEM_WORD(eaddr, ureg);
}

/* is this a legal instruction? */
/* $10cF STS immediate -**0- */
void cpu_6809::sts_im()
{
	CLR_NZV; SET_NZ16(sreg);
	//IMM16;
	eaddr = pcreg; pcreg += 2;
	M_WRMEM_WORD(eaddr, sreg);
}

/* $d0 SUBB direct ?**** */
void cpu_6809::subb_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $d1 CMPB direct ?**** */
void cpu_6809::cmpb_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $d2 SBCB direct ?**** */
void cpu_6809::sbcb_di()
{
	uint16_t	t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $d3 ADDD direct -**** */
void cpu_6809::addd_di()
{
	uint32_t r, d, b;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $d4 ANDB direct -**0- */
void cpu_6809::andb_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $d5 BITB direct -**0- */
void cpu_6809::bitb_di()
{
	uint8_t t = 0; uint8_t r = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $d6 LDB direct -**0- */
void cpu_6809::ldb_di()
{
	//DIRuint8_t(breg);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	breg = M_RDMEM(eaddr);

	CLR_NZV; SET_NZ8(breg);
}

/* $d7 STB direct -**0- */
void cpu_6809::stb_di()
{
	CLR_NZV; SET_NZ8(breg);
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM(eaddr, breg);
}

/* $d8 EORB direct -**0- */
void cpu_6809::eorb_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $d9 ADCB direct ***** */
void cpu_6809::adcb_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $dA ORB direct -**0- */
void cpu_6809::orb_di()
{
	uint8_t t = 0;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $dB ADDB direct ***** */
void cpu_6809::addb_di()
{
	uint16_t t, r;
	//DIRuint8_t(t);
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM(eaddr);

	r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $dC LDD direct -**0- */
void cpu_6809::ldd_di()
{
	uint16_t t;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = M_RDMEM_WORD(eaddr);
	SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $dD STD direct -**0- */
void cpu_6809::std_di()
{
	uint16_t t;
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	t = GETDREG;
	CLR_NZV;
	SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $dE LDU (LDS) direct -**0- */
void cpu_6809::ldu_di()
{
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	ureg = M_RDMEM_WORD(eaddr);
	CLR_NZV;
	SET_NZ16(ureg);
}

/* $10dE LDS direct -**0- */
void cpu_6809::lds_di()
{
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	sreg = M_RDMEM_WORD(eaddr);
	CLR_NZV;
	SET_NZ16(sreg);
}

/* $dF STU (STS) direct -**0- */
void cpu_6809::stu_di()
{
	CLR_NZV;
	SET_NZ16(ureg);
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM_WORD(eaddr, ureg);
}

/* $10dF STS direct -**0- */
void cpu_6809::sts_di()
{
	CLR_NZV;
	SET_NZ16(sreg);
	//DIRECT;
	eaddr = (rd_slow)(pcreg); pcreg++; eaddr |= (dpreg << 8);
	M_WRMEM_WORD(eaddr, sreg);
}

/* $e0 SUBB indexed ?**** */
void cpu_6809::subb_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $e1 CMPB indexed ?**** */
void cpu_6809::cmpb_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $e2 SBCB indexed ?**** */
void cpu_6809::sbcb_ix()
{
	uint16_t	t, r;
	t = M_RDMEM(eaddr); r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $e3 ADDD indexed -**** */
void cpu_6809::addd_ix()
{
	uint32_t r, d, b;
	b = M_RDMEM_WORD(eaddr); d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $e4 ANDB indexed -**0- */
void cpu_6809::andb_ix()
{
	breg &= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e5 BITB indexed -**0- */
void cpu_6809::bitb_ix()
{
	uint8_t r;
	r = breg & M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(r);
}

/* $e6 LDB indexed -**0- */
void cpu_6809::ldb_ix()
{
	breg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e7 STB indexed -**0- */
void cpu_6809::stb_ix()
{
	CLR_NZV; SET_NZ8(breg);
	M_WRMEM(eaddr, breg);
}

/* $e8 EORB indexed -**0- */
void cpu_6809::eorb_ix()
{
	breg ^= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $e9 ADCB indexed ***** */
void cpu_6809::adcb_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $eA ORB indexed -**0- */
void cpu_6809::orb_ix()
{
	breg |= M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $eB ADDB indexed ***** */
void cpu_6809::addb_ix()
{
	uint16_t t, r;
	t = M_RDMEM(eaddr); r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $eC LDD indexed -**0- */
void cpu_6809::ldd_ix()
{
	uint16_t t;
	t = M_RDMEM_WORD(eaddr); SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $eD STD indexed -**0- */
void cpu_6809::std_ix()
{
	uint16_t t;
	t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $eE LDU (LDS) indexed -**0- */
void cpu_6809::ldu_ix()
{
	ureg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10eE LDS indexed -**0- */
void cpu_6809::lds_ix()
{
	sreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(sreg);
}

/* $eF STU (STS) indexed -**0- */
void cpu_6809::stu_ix()
{
	CLR_NZV; SET_NZ16(ureg);
	M_WRMEM_WORD(eaddr, ureg);
}

/* $10eF STS indexed -**0- */
void cpu_6809::sts_ix()
{
	CLR_NZV; SET_NZ16(sreg);
	M_WRMEM_WORD(eaddr, sreg);
}

/* $f0 SUBB extended ?**** */
void cpu_6809::subb_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $f1 CMPB extended ?**** */
void cpu_6809::cmpb_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg - t;
	CLR_NZVC; SET_FLAGS8(breg, t, r);
}

/* $f2 SBCB extended ?**** */
void cpu_6809::sbcb_ex()
{
	uint16_t	t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg - t - (cc & 0x01);
	CLR_NZVC; SET_FLAGS8(breg, t, r);
	breg = r;
}

/* $f3 ADDD extended -**** */
void cpu_6809::addd_ex()
{
	uint32_t r, d, b;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	b = M_RDMEM_WORD(eaddr);
	d = GETDREG; r = d + b;
	CLR_NZVC; SET_FLAGS16(d, b, r);
	SETDREG(r);
}

/* $f4 ANDB extended -**0- */
void cpu_6809::andb_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	breg &= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $f5 BITB extended -**0- */
void cpu_6809::bitb_ex()
{
	uint8_t t = 0; uint8_t r = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg & t;
	CLR_NZV; SET_NZ8(r);
}

/* $f6 LDB extended -**0- */
void cpu_6809::ldb_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	breg = M_RDMEM(eaddr);
	CLR_NZV; SET_NZ8(breg);
}

/* $f7 STB extended -**0- */
void cpu_6809::stb_ex()
{
	CLR_NZV; SET_NZ8(breg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM(eaddr, breg);
}

/* $f8 EORB extended -**0- */
void cpu_6809::eorb_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	breg ^= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $f9 ADCB extended ***** */
void cpu_6809::adcb_ex()
{
	uint16_t t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg + t + (cc & 0x01);
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $fA ORB extended -**0- */
void cpu_6809::orb_ex()
{
	uint8_t t = 0;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	breg |= t;
	CLR_NZV; SET_NZ8(breg);
}

/* $fB ADDB extended ***** */
void cpu_6809::addb_ex()
{
	uint16_t t, r;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM(eaddr);
	r = breg + t;
	CLR_HNZVC; SET_FLAGS8(breg, t, r); SET_H(breg, t, r);
	breg = r;
}

/* $fC LDD extended -**0- */
void cpu_6809::ldd_ex()
{
	uint16_t t;
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	t = M_RDMEM_WORD(eaddr);
	SETDREG(t);
	CLR_NZV; SET_NZ16(t);
}

/* $fD STD extended -**0- */
void cpu_6809::std_ex()
{
	uint16_t t;
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	t = GETDREG;
	CLR_NZV; SET_NZ16(t);
	M_WRMEM_WORD(eaddr, t);
}

/* $fE LDU (LDS) extended -**0- */
void cpu_6809::ldu_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	ureg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(ureg);
}

/* $10fE LDS extended -**0- */
void cpu_6809::lds_ex()
{
	eaddr = (rd_slow_wd)(pcreg);
	pcreg += 2;
	sreg = M_RDMEM_WORD(eaddr);
	CLR_NZV; SET_NZ16(sreg);
}

/* $fF STU (STS) extended -**0- */
void cpu_6809::stu_ex()
{
	CLR_NZV; SET_NZ16(ureg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM_WORD(eaddr, ureg);
}

/* $10fF STS extended -**0- */
void cpu_6809::sts_ex()
{
	CLR_NZV; SET_NZ16(sreg);
	eaddr = (rd_slow_wd)(pcreg); pcreg += 2;
	M_WRMEM_WORD(eaddr, sreg);
}

/****************************************************************************/
/* Set all registers to given values                                        */
/****************************************************************************/
void cpu_6809::m6809_SetRegs(m6809_Regs* Regs)
{
	pcreg = Regs->pc;
	ureg = Regs->u;
	sreg = Regs->s;
	xreg = Regs->x;
	yreg = Regs->y;
	dpreg = Regs->dp;
	areg = Regs->a;
	breg = Regs->b;
	cc = Regs->cc;
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
}

// Reset MyCpu 
void cpu_6809::reset6809()
{
	pcreg = M_RDMEM_WORD(0xfffe);

	dpreg = 0x00;		/* Direct page register = 0x00 */
	cc = 0x00;			/* Clear all flags */
	cc |= 0x10;			/* IRQ disabled */
	cc |= 0x40;			/* FIRQ disabled */
	areg = 0x00;		/* clear accumulator a */
	breg = 0x00;		/* clear accumulator b */
	clockticks6809 = 0;// m6809_IPeriod;
	m6809_IRequest = INT_NONE;
}

// FIRQ
void cpu_6809::firq6809()
{
	/* FIRQ */
	if (!(cc & 0x40))
	{
		/* fast IRQ */
		PUSHWORD(pcreg);
		PUSHBYTE(cc);
		cc &= 0x7f;
		cc |= 0x50;
		pcreg = M_RDMEM_WORD(0xfff6);
		clockticks6809 += 10;
	}
}

// Maskerable Interrupt 
void cpu_6809::irq6809()
{
	if (!(cc & 0x10))
	{
		/* standard IRQ */
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
		clockticks6809 += 19;
	}
	else
	{
		m6809_IRequest = INT_IRQ;
	}

}

// NonMaskerable Interrupt 
void cpu_6809::nmi6809()
{
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
	pcreg = M_RDMEM_WORD(0xfffc); //change_pc(pcreg);	/* TS 971002 */
	clockticks6809 += 19;
}

void cpu_6809::fetch_effective_address()
{
	uint8_t postbyte = M_RDMEM(pcreg++);

	switch (postbyte)
	{
	case 0x00: eaddr = xreg; break;
	case 0x01: eaddr = xreg + 1; break;
	case 0x02: eaddr = xreg + 2; break;
	case 0x03: eaddr = xreg + 3; break;
	case 0x04: eaddr = xreg + 4; break;
	case 0x05: eaddr = xreg + 5; break;
	case 0x06: eaddr = xreg + 6; break;
	case 0x07: eaddr = xreg + 7; break;
	case 0x08: eaddr = xreg + 8; break;
	case 0x09: eaddr = xreg + 9; break;
	case 0x0A: eaddr = xreg + 10; break;
	case 0x0B: eaddr = xreg + 11; break;
	case 0x0C: eaddr = xreg + 12; break;
	case 0x0D: eaddr = xreg + 13; break;
	case 0x0E: eaddr = xreg + 14; break;
	case 0x0F: eaddr = xreg + 15; break;
	case 0x10: eaddr = xreg - 16; break;
	case 0x11: eaddr = xreg - 15; break;
	case 0x12: eaddr = xreg - 14; break;
	case 0x13: eaddr = xreg - 13; break;
	case 0x14: eaddr = xreg - 12; break;
	case 0x15: eaddr = xreg - 11; break;
	case 0x16: eaddr = xreg - 10; break;
	case 0x17: eaddr = xreg - 9; break;
	case 0x18: eaddr = xreg - 8; break;
	case 0x19: eaddr = xreg - 7; break;
	case 0x1A: eaddr = xreg - 6; break;
	case 0x1B: eaddr = xreg - 5; break;
	case 0x1C: eaddr = xreg - 4; break;
	case 0x1D: eaddr = xreg - 3; break;
	case 0x1E: eaddr = xreg - 2; break;
	case 0x1F: eaddr = xreg - 1; break;
	case 0x20: eaddr = yreg; break;
	case 0x21: eaddr = yreg + 1; break;
	case 0x22: eaddr = yreg + 2; break;
	case 0x23: eaddr = yreg + 3; break;
	case 0x24: eaddr = yreg + 4; break;
	case 0x25: eaddr = yreg + 5; break;
	case 0x26: eaddr = yreg + 6; break;
	case 0x27: eaddr = yreg + 7; break;
	case 0x28: eaddr = yreg + 8; break;
	case 0x29: eaddr = yreg + 9; break;
	case 0x2A: eaddr = yreg + 10; break;
	case 0x2B: eaddr = yreg + 11; break;
	case 0x2C: eaddr = yreg + 12; break;
	case 0x2D: eaddr = yreg + 13; break;
	case 0x2E: eaddr = yreg + 14; break;
	case 0x2F: eaddr = yreg + 15; break;
	case 0x30: eaddr = yreg - 16; break;
	case 0x31: eaddr = yreg - 15; break;
	case 0x32: eaddr = yreg - 14; break;
	case 0x33: eaddr = yreg - 13; break;
	case 0x34: eaddr = yreg - 12; break;
	case 0x35: eaddr = yreg - 11; break;
	case 0x36: eaddr = yreg - 10; break;
	case 0x37: eaddr = yreg - 9; break;
	case 0x38: eaddr = yreg - 8; break;
	case 0x39: eaddr = yreg - 7; break;
	case 0x3A: eaddr = yreg - 6; break;
	case 0x3B: eaddr = yreg - 5; break;
	case 0x3C: eaddr = yreg - 4; break;
	case 0x3D: eaddr = yreg - 3; break;
	case 0x3E: eaddr = yreg - 2; break;
	case 0x3F: eaddr = yreg - 1; break;
	case 0x40: eaddr = ureg; break;
	case 0x41: eaddr = ureg + 1; break;
	case 0x42: eaddr = ureg + 2; break;
	case 0x43: eaddr = ureg + 3; break;
	case 0x44: eaddr = ureg + 4; break;
	case 0x45: eaddr = ureg + 5; break;
	case 0x46: eaddr = ureg + 6; break;
	case 0x47: eaddr = ureg + 7; break;
	case 0x48: eaddr = ureg + 8; break;
	case 0x49: eaddr = ureg + 9; break;
	case 0x4A: eaddr = ureg + 10; break;
	case 0x4B: eaddr = ureg + 11; break;
	case 0x4C: eaddr = ureg + 12; break;
	case 0x4D: eaddr = ureg + 13; break;
	case 0x4E: eaddr = ureg + 14; break;
	case 0x4F: eaddr = ureg + 15; break;
	case 0x50: eaddr = ureg - 16; break;
	case 0x51: eaddr = ureg - 15; break;
	case 0x52: eaddr = ureg - 14; break;
	case 0x53: eaddr = ureg - 13; break;
	case 0x54: eaddr = ureg - 12; break;
	case 0x55: eaddr = ureg - 11; break;
	case 0x56: eaddr = ureg - 10; break;
	case 0x57: eaddr = ureg - 9; break;
	case 0x58: eaddr = ureg - 8; break;
	case 0x59: eaddr = ureg - 7; break;
	case 0x5A: eaddr = ureg - 6; break;
	case 0x5B: eaddr = ureg - 5; break;
	case 0x5C: eaddr = ureg - 4; break;
	case 0x5D: eaddr = ureg - 3; break;
	case 0x5E: eaddr = ureg - 2; break;
	case 0x5F: eaddr = ureg - 1; break;
	case 0x60: eaddr = sreg; break;
	case 0x61: eaddr = sreg + 1; break;
	case 0x62: eaddr = sreg + 2; break;
	case 0x63: eaddr = sreg + 3; break;
	case 0x64: eaddr = sreg + 4; break;
	case 0x65: eaddr = sreg + 5; break;
	case 0x66: eaddr = sreg + 6; break;
	case 0x67: eaddr = sreg + 7; break;
	case 0x68: eaddr = sreg + 8; break;
	case 0x69: eaddr = sreg + 9; break;
	case 0x6A: eaddr = sreg + 10; break;
	case 0x6B: eaddr = sreg + 11; break;
	case 0x6C: eaddr = sreg + 12; break;
	case 0x6D: eaddr = sreg + 13; break;
	case 0x6E: eaddr = sreg + 14; break;
	case 0x6F: eaddr = sreg + 15; break;
	case 0x70: eaddr = sreg - 16; break;
	case 0x71: eaddr = sreg - 15; break;
	case 0x72: eaddr = sreg - 14; break;
	case 0x73: eaddr = sreg - 13; break;
	case 0x74: eaddr = sreg - 12; break;
	case 0x75: eaddr = sreg - 11; break;
	case 0x76: eaddr = sreg - 10; break;
	case 0x77: eaddr = sreg - 9; break;
	case 0x78: eaddr = sreg - 8; break;
	case 0x79: eaddr = sreg - 7; break;
	case 0x7A: eaddr = sreg - 6; break;
	case 0x7B: eaddr = sreg - 5; break;
	case 0x7C: eaddr = sreg - 4; break;
	case 0x7D: eaddr = sreg - 3; break;
	case 0x7E: eaddr = sreg - 2; break;
	case 0x7F: eaddr = sreg - 1; break;
	case 0x80: eaddr = xreg; xreg++; break;
	case 0x81: eaddr = xreg; xreg += 2; break;
	case 0x82: xreg--; eaddr = xreg; break;
	case 0x83: xreg -= 2; eaddr = xreg; break;
	case 0x84: eaddr = xreg; break;
	case 0x85: eaddr = xreg + SIGNED(breg); break;
	case 0x86: eaddr = xreg + SIGNED(areg); break;
	case 0x87: eaddr = 0; break; /*ILLEGAL*/
	case 0x88: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = xreg + SIGNED(eaddr); break;
	case 0x89: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += xreg; break;
	case 0x8A: eaddr = 0; break; /*ILLEGAL*/
	case 0x8B: eaddr = xreg + GETDREG; break;
	case 0x8C: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr); break;
	case 0x8D: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; break;
	case 0x8E: eaddr = 0; break; /*ILLEGAL*/
	case 0x8F: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; break;
	case 0x90: eaddr = xreg; xreg++; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x91: eaddr = xreg; xreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x92: xreg--; eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x93: xreg -= 2; eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x94: eaddr = xreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x95: eaddr = xreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x96: eaddr = xreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x97: eaddr = 0; break; /*ILLEGAL*/
	case 0x98: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = xreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x99: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += xreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x9A: eaddr = 0; break; /*ILLEGAL*/
	case 0x9B: eaddr = xreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x9C: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x9D: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0x9E: eaddr = 0; break; /*ILLEGAL*/
	case 0x9F: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xA0: eaddr = yreg; yreg++; break;
	case 0xA1: eaddr = yreg; yreg += 2; break;
	case 0xA2: yreg--; eaddr = yreg; break;
	case 0xA3: yreg -= 2; eaddr = yreg; break;
	case 0xA4: eaddr = yreg; break;
	case 0xA5: eaddr = yreg + SIGNED(breg); break;
	case 0xA6: eaddr = yreg + SIGNED(areg); break;
	case 0xA7: eaddr = 0; break; /*ILLEGAL*/
	case 0xA8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = yreg + SIGNED(eaddr); break;
	case 0xA9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += yreg; break;
	case 0xAA: eaddr = 0; break; /*ILLEGAL*/
	case 0xAB: eaddr = yreg + GETDREG; break;
	case 0xAC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr); break;
	case 0xAD: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; break;
	case 0xAE: eaddr = 0; break; /*ILLEGAL*/
	case 0xAF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; break;
	case 0xB0: eaddr = yreg; yreg++; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB1: eaddr = yreg; yreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB2: yreg--; eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB3: yreg -= 2; eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB4: eaddr = yreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB5: eaddr = yreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB6: eaddr = yreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB7: eaddr = 0; break; /*ILLEGAL*/
	case 0xB8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = yreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xB9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += yreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xBA: eaddr = 0; break; /*ILLEGAL*/
	case 0xBB: eaddr = yreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xBC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xBD: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xBE: eaddr = 0; break; /*ILLEGAL*/
	case 0xBF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xC0: eaddr = ureg; ureg++; break;
	case 0xC1: eaddr = ureg; ureg += 2; break;
	case 0xC2: ureg--; eaddr = ureg; break;
	case 0xC3: ureg -= 2; eaddr = ureg; break;
	case 0xC4: eaddr = ureg; break;
	case 0xC5: eaddr = ureg + SIGNED(breg); break;
	case 0xC6: eaddr = ureg + SIGNED(areg); break;
	case 0xC7: eaddr = 0; break; /*ILLEGAL*/
	case 0xC8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = ureg + SIGNED(eaddr); break;
	case 0xC9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += ureg; break;
	case 0xCA: eaddr = 0; break; /*ILLEGAL*/
	case 0xCB: eaddr = ureg + GETDREG; break;
	case 0xCC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr); break;
	case 0xCD: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; break;
	case 0xCE: eaddr = 0; break; /*ILLEGAL*/
	case 0xCF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; break;
	case 0xD0: eaddr = ureg; ureg++; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD1: eaddr = ureg; ureg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD2: ureg--; eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD3: ureg -= 2; eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD4: eaddr = ureg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD5: eaddr = ureg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD6: eaddr = ureg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD7: eaddr = 0; break; /*ILLEGAL*/
	case 0xD8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = ureg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xD9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += ureg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xDA: eaddr = 0; break; /*ILLEGAL*/
	case 0xDB: eaddr = ureg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xDC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xDD: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xDE: eaddr = 0; break; /*ILLEGAL*/
	case 0xDF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xE0: eaddr = sreg; sreg++; break;
	case 0xE1: eaddr = sreg; sreg += 2; break;
	case 0xE2: sreg--; eaddr = sreg; break;
	case 0xE3: sreg -= 2; eaddr = sreg; break;
	case 0xE4: eaddr = sreg; break;
	case 0xE5: eaddr = sreg + SIGNED(breg); break;
	case 0xE6: eaddr = sreg + SIGNED(areg); break;
	case 0xE7: eaddr = 0; break; /*ILLEGAL*/
	case 0xE8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = sreg + SIGNED(eaddr); break;
	case 0xE9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += sreg; break;
	case 0xEA: eaddr = 0; break; /*ILLEGAL*/
	case 0xEB: eaddr = sreg + GETDREG; break;
	case 0xEC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr); break;
	case 0xED: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; break;
	case 0xEE: eaddr = 0; break; /*ILLEGAL*/
	case 0xEF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; break;
	case 0xF0: eaddr = sreg; sreg++; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF1: eaddr = sreg; sreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF2: sreg--; eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF3: sreg -= 2; eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF4: eaddr = sreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF5: eaddr = sreg + SIGNED(breg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF6: eaddr = sreg + SIGNED(areg); eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF7: eaddr = 0; break; /*ILLEGAL*/
	case 0xF8: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = sreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xF9: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += sreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xFA: eaddr = 0; break; /*ILLEGAL*/
	case 0xFB: eaddr = sreg + GETDREG; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xFC: eaddr = (rd_slow)(pcreg); pcreg++; eaddr = pcreg + SIGNED(eaddr);
		eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xFD: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr += pcreg; eaddr = M_RDMEM_WORD(eaddr); break;
	case 0xFE: eaddr = 0; break; /*ILLEGAL*/
	case 0xFF: eaddr = (rd_slow_wd)(pcreg); pcreg += 2; eaddr = M_RDMEM_WORD(eaddr); break;
	}

}
/* execute instructions on this CPU until icount expires */
int cpu_6809::exec6809(int timerTicks)
{
	uint8_t ireg;

	static int count = 0;

	clockticks6809 = 0;
	while (clockticks6809 < timerTicks)
	{
		int lastticks = clockticks6809;

		ppc = pcreg;
		ireg = M_RDMEM(pcreg++);
		//wrlog("CPU: %d Count:%d  INS: %x, PC: %x CC: %x", cpu_num, count, ireg, pcreg, cc);
		count++;
		if (ireg != 0x10 && ireg != 0x11)
		{
			if (haspostbyte[ireg]) fetch_effective_address();

			switch (ireg)
			{
			case 0x00: neg_di(); break;
			case 0x01: illegal(); break;
			case 0x02: illegal(); break;
			case 0x03: com_di(); break;
			case 0x04: lsr_di(); break;

			case 0x05: illegal(); break;
			case 0x06: ror_di(); break;
			case 0x07: asr_di(); break;
			case 0x08: asl_di(); break;
			case 0x09: rol_di(); break;
			case 0x0a: dec_di(); break;
			case 0x0b: illegal(); break;
			case 0x0c: inc_di(); break;
			case 0x0d: tst_di(); break;
			case 0x0e: jmp_di(); break;
			case 0x0f: clr_di(); break;
			case 0x10: illegal(); break;
			case 0x11: illegal(); break;
			case 0x12: nop(); break;
			case 0x13: sync(); break;
			case 0x14: illegal(); break;
			case 0x15: illegal(); break;
			case 0x16: lbra(); break;
			case 0x17: lbsr(); break;
			case 0x18: illegal(); break;
			case 0x19: daa(); break;
			case 0x1a: orcc(); break;
			case 0x1b: illegal(); break;
			case 0x1c: andcc(); break;
			case 0x1d: sex(); break;
			case 0x1e: exg(); break;
			case 0x1f: tfr(); break;
			case 0x20: bra(); break;
			case 0x21: brn(); break;
			case 0x22: bhi(); break;
			case 0x23: bls(); break;
			case 0x24: bcc(); break;
			case 0x25: bcs(); break;
			case 0x26: bne(); break;
			case 0x27: beq(); break;
			case 0x28: bvc(); break;
			case 0x29: bvs(); break;
			case 0x2a: bpl(); break;
			case 0x2b: bmi(); break;
			case 0x2c: bge(); break;
			case 0x2d: blt(); break;
			case 0x2e: bgt(); break;
			case 0x2f: ble(); break;
			case 0x30: leax(); break;
			case 0x31: leay(); break;
			case 0x32: leas(); break;
			case 0x33: leau(); break;
			case 0x34: pshs(); break;
			case 0x35: puls(); break;
			case 0x36: pshu(); break;
			case 0x37: pulu(); break;
			case 0x38: illegal(); break;
			case 0x39: rts(); break;
			case 0x3a: abx(); break;
			case 0x3b: rti(); break;
			case 0x3c: cwai(); break;
			case 0x3d: mul(); break;
			case 0x3e: illegal(); break;
			case 0x3f: swi(); break;
			case 0x40: nega(); break;
			case 0x41: illegal(); break;
			case 0x42: illegal(); break;
			case 0x43: coma(); break;
			case 0x44: lsra(); break;
			case 0x45: illegal(); break;
			case 0x46: rora(); break;
			case 0x47: asra(); break;
			case 0x48: asla(); break;
			case 0x49: rola(); break;
			case 0x4a: deca(); break;
			case 0x4b: illegal(); break;
			case 0x4c: inca(); break;
			case 0x4d: tsta(); break;
			case 0x4e: illegal(); break;
			case 0x4f: clra(); break;
			case 0x50: negb(); break;
			case 0x51: illegal(); break;
			case 0x52: illegal(); break;
			case 0x53: comb(); break;
			case 0x54: lsrb(); break;
			case 0x55: illegal(); break;
			case 0x56: rorb(); break;
			case 0x57: asrb(); break;
			case 0x58: aslb(); break;
			case 0x59: rolb(); break;
			case 0x5a: decb(); break;
			case 0x5b: illegal(); break;
			case 0x5c: incb(); break;
			case 0x5d: tstb(); break;
			case 0x5e: illegal(); break;
			case 0x5f: clrb(); break;
			case 0x60: neg_ix(); break;
			case 0x61: illegal(); break;
			case 0x62: illegal(); break;
			case 0x63: com_ix(); break;
			case 0x64: lsr_ix(); break;
			case 0x65: illegal(); break;
			case 0x66: ror_ix(); break;
			case 0x67: asr_ix(); break;
			case 0x68: asl_ix(); break;
			case 0x69: rol_ix(); break;
			case 0x6a: dec_ix(); break;
			case 0x6b: illegal(); break;
			case 0x6c: inc_ix(); break;
			case 0x6d: tst_ix(); break;
			case 0x6e: jmp_ix(); break;
			case 0x6f: clr_ix(); break;
			case 0x70: neg_ex(); break;
			case 0x71: illegal(); break;
			case 0x72: illegal(); break;
			case 0x73: com_ex(); break;
			case 0x74: lsr_ex(); break;
			case 0x75: illegal(); break;
			case 0x76: ror_ex(); break;
			case 0x77: asr_ex(); break;
			case 0x78: asl_ex(); break;
			case 0x79: rol_ex(); break;
			case 0x7a: dec_ex(); break;
			case 0x7b: illegal(); break;
			case 0x7c: inc_ex(); break;
			case 0x7d: tst_ex(); break;
			case 0x7e: jmp_ex(); break;
			case 0x7f: clr_ex(); break;
			case 0x80: suba_im(); break;
			case 0x81: cmpa_im(); break;
			case 0x82: sbca_im(); break;
			case 0x83: subd_im(); break;
			case 0x84: anda_im(); break;
			case 0x85: bita_im(); break;
			case 0x86: lda_im(); break;
			case 0x87: sta_im(); break;
			case 0x88: eora_im(); break;
			case 0x89: adca_im(); break;
			case 0x8a: ora_im(); break;
			case 0x8b: adda_im(); break;
			case 0x8c: cmpx_im(); break;
			case 0x8d: bsr(); break;
			case 0x8e: ldx_im(); break;
			case 0x8f: stx_im(); break;
			case 0x90: suba_di(); break;
			case 0x91: cmpa_di(); break;
			case 0x92: sbca_di(); break;
			case 0x93: subd_di(); break;
			case 0x94: anda_di(); break;
			case 0x95: bita_di(); break;
			case 0x96: lda_di(); break;
			case 0x97: sta_di(); break;
			case 0x98: eora_di(); break;
			case 0x99: adca_di(); break;
			case 0x9a: ora_di(); break;
			case 0x9b: adda_di(); break;
			case 0x9c: cmpx_di(); break;
			case 0x9d: jsr_di(); break;
			case 0x9e: ldx_di(); break;
			case 0x9f: stx_di(); break;
			case 0xa0: suba_ix(); break;
			case 0xa1: cmpa_ix(); break;
			case 0xa2: sbca_ix(); break;
			case 0xa3: subd_ix(); break;
			case 0xa4: anda_ix(); break;
			case 0xa5: bita_ix(); break;
			case 0xa6: lda_ix(); break;
			case 0xa7: sta_ix(); break;
			case 0xa8: eora_ix(); break;
			case 0xa9: adca_ix(); break;
			case 0xaa: ora_ix(); break;
			case 0xab: adda_ix(); break;
			case 0xac: cmpx_ix(); break;
			case 0xad: jsr_ix(); break;
			case 0xae: ldx_ix(); break;
			case 0xaf: stx_ix(); break;
			case 0xb0: suba_ex(); break;
			case 0xb1: cmpa_ex(); break;
			case 0xb2: sbca_ex(); break;
			case 0xb3: subd_ex(); break;
			case 0xb4: anda_ex(); break;
			case 0xb5: bita_ex(); break;
			case 0xb6: lda_ex(); break;
			case 0xb7: sta_ex(); break;
			case 0xb8: eora_ex(); break;
			case 0xb9: adca_ex(); break;
			case 0xba: ora_ex(); break;
			case 0xbb: adda_ex(); break;
			case 0xbc: cmpx_ex(); break;
			case 0xbd: jsr_ex(); break;
			case 0xbe: ldx_ex(); break;
			case 0xbf: stx_ex(); break;
			case 0xc0: subb_im(); break;
			case 0xc1: cmpb_im(); break;
			case 0xc2: sbcb_im(); break;
			case 0xc3: addd_im(); break;
			case 0xc4: andb_im(); break;
			case 0xc5: bitb_im(); break;
			case 0xc6: ldb_im(); break;
			case 0xc7: stb_im(); break;
			case 0xc8: eorb_im(); break;
			case 0xc9: adcb_im(); break;
			case 0xca: orb_im(); break;
			case 0xcb: addb_im(); break;
			case 0xcc: ldd_im(); break;
			case 0xcd: std_im(); break;
			case 0xce: ldu_im(); break;
			case 0xcf: stu_im(); break;
			case 0xd0: subb_di(); break;
			case 0xd1: cmpb_di(); break;
			case 0xd2: sbcb_di(); break;
			case 0xd3: addd_di(); break;
			case 0xd4: andb_di(); break;
			case 0xd5: bitb_di(); break;
			case 0xd6: ldb_di(); break;
			case 0xd7: stb_di(); break;
			case 0xd8: eorb_di(); break;
			case 0xd9: adcb_di(); break;
			case 0xda: orb_di(); break;
			case 0xdb: addb_di(); break;
			case 0xdc: ldd_di(); break;
			case 0xdd: std_di(); break;
			case 0xde: ldu_di(); break;
			case 0xdf: stu_di(); break;
			case 0xe0: subb_ix(); break;
			case 0xe1: cmpb_ix(); break;
			case 0xe2: sbcb_ix(); break;
			case 0xe3: addd_ix(); break;
			case 0xe4: andb_ix(); break;
			case 0xe5: bitb_ix(); break;
			case 0xe6: ldb_ix(); break;
			case 0xe7: stb_ix(); break;
			case 0xe8: eorb_ix(); break;
			case 0xe9: adcb_ix(); break;
			case 0xea: orb_ix(); break;
			case 0xeb: addb_ix(); break;
			case 0xec: ldd_ix(); break;
			case 0xed: std_ix(); break;
			case 0xee: ldu_ix(); break;
			case 0xef: stu_ix(); break;
			case 0xf0: subb_ex(); break;
			case 0xf1: cmpb_ex(); break;
			case 0xf2: sbcb_ex(); break;
			case 0xf3: addd_ex(); break;
			case 0xf4: andb_ex(); break;
			case 0xf5: bitb_ex(); break;
			case 0xf6: ldb_ex(); break;
			case 0xf7: stb_ex(); break;
			case 0xf8: eorb_ex(); break;
			case 0xf9: adcb_ex(); break;
			case 0xfa: orb_ex(); break;
			case 0xfb: addb_ex(); break;
			case 0xfc: ldd_ex(); break;
			case 0xfd: std_ex(); break;
			case 0xfe: ldu_ex(); break;
			case 0xff: stu_ex(); break;
			}

			clockticks6809 += cycles[ireg];
		}
		else
		{ // if ireg is 0x10 or 0x11
			uint16_t iregw;
			iregw = ireg;
			iregw <<= 8;
			iregw |= M_RDMEM(pcreg++);

			if (haspostbyte[iregw & 0xff]) fetch_effective_address();
			//if (cpu_num) wrlog(":%d  INS_W: %x, PC: %x CC: %x", count, ireg, pcreg, cc);
			switch (iregw)
			{
			case 0x1021: lbrn(); break;
			case 0x1022: lbhi(); break;
			case 0x1023: lbls(); break;
			case 0x1024: lbcc(); break;
			case 0x1025: lbcs(); break;
			case 0x1026: lbne(); break;
			case 0x1027: lbeq(); break;
			case 0x1028: lbvc(); break;
			case 0x1029: lbvs(); break;
			case 0x102a: lbpl(); break;
			case 0x102b: lbmi(); break;
			case 0x102c: lbge(); break;
			case 0x102d: lblt(); break;
			case 0x102e: lbgt(); break;
			case 0x102f: lble(); break;
			case 0x103f: swi2(); break;
			case 0x1083: cmpd_im(); break;
			case 0x108c: cmpy_im(); break;
			case 0x108e: ldy_im(); break;
			case 0x108f: sty_im(); break; /* ILLEGAL? */
			case 0x1093: cmpd_di(); break;
			case 0x109c: cmpy_di(); break;
			case 0x109e: ldy_di(); break;
			case 0x109f: sty_di(); break;
			case 0x10a3: cmpd_ix(); break;
			case 0x10ac: cmpy_ix(); break;
			case 0x10ae: ldy_ix(); break;
			case 0x10af: sty_ix(); break;
			case 0x10b3: cmpd_ex(); break;
			case 0x10bc: cmpy_ex(); break;
			case 0x10be: ldy_ex(); break;
			case 0x10bf: sty_ex(); break;
			case 0x10ce: lds_im(); break;
			case 0x10cf: sts_im(); break; /* ILLEGAL? */
			case 0x10de: lds_di(); break;
			case 0x10df: sts_di(); break;
			case 0x10ee: lds_ix(); break;
			case 0x10ef: sts_ix(); break;
			case 0x10fe: lds_ex(); break;
			case 0x10ff: sts_ex(); break;
			case 0x113f: swi3(); break;
			case 0x1183: cmpu_im(); break;
			case 0x118c: cmps_im(); break;
			case 0x1193: cmpu_di(); break;
			case 0x119c: cmps_di(); break;
			case 0x11a3: cmpu_ix(); break;
			case 0x11ac: cmps_ix(); break;
			case 0x11b3: cmpu_ex(); break;
			case 0x11bc: cmps_ex(); break;
			default: illegal(); break;
			}
			clockticks6809 += cycles2[iregw & 0xff];
		}

		clocktickstotal += (clockticks6809 - lastticks);
		//timer_update(clockticks6809 - lastticks, cpu_num);
		//wrlog("Cycles diff %d", clockticks6809-lastticks);

		if (clocktickstotal > 0xfffffff) clocktickstotal = 0;

	}

	/* Interrupt if needed  */
	//if (cpu_interrupt() == INT_IRQ) m6809_Interrupt();
	return 0x80000000;
	//return clockticks6809;
}
