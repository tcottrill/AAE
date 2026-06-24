/*************************************************************************

 Exidy Vertigo hardware  -  vector processor (AAE port)

 The Vertigo vector CPU consists of four AMD 2901 bit slice processors,
 logic to control microcode program flow and a digital vector generator.
 The microcode for the bit slice CPUs is stored in 13 bipolar proms for a
 total of 512 52-bit wide micro instructions.

 Adapted from M.A.M.E.(TM) vidhrdw/vertigo.c (driver by Mathis Rosenhauer).
 The bit-slice / vector-generator logic is preserved verbatim. AAE changes:
   - V_ADDPOINT() uses AAE's vector_add_point() / VECTOR_COLOR444().
   - profiler_mark() and state-save calls dropped.
   - AAE has no ROMX_LOAD nibble-interleave, so the 512 x 64-bit microcode
     words are assembled here by hand from the 13 raw PROMs (loaded into
     REGION_PROMS by the driver), replicating MAME's ROM_REGION64_BE layout.

*************************************************************************/

#include "vertigo.h"
#include "memory.h"        // memory_region / REGION_*
#include "mame_vector.h"   // vector_add_point / vector_clear_list
#include "colordefs.h"     // VECTOR_COLOR444
#include "sys_log.h"       // LOG_INFO (debug)
#include <cstdint>
#include <cstring>


/*************************************
 *
 *  Macros and enums
 *
 *************************************/

#define MC_LENGTH 512

// AAE's vector_update() scales each point by 1024/(65536 * visible_extent), so
// coordinates are pixel<<16 (not MAME's <<14). With the driver's 0..2047 /
// 0..1791 visible area this maps the vector CPU's h / (0x6ff - v) counters
// across the full screen.
#define V_ADDPOINT(h,v,c,i) \
	vector_add_point (((h) & 0x7ff) << 14, (0x6ff - ((v) & 0x7ff)) << 14, VECTOR_COLOR444(c), (i))

#define ADD(r,s,c)	(((r)  + (s) + (c)) & 0xffff)
#define SUBR(r,s,c) ((~(r) + (s) + (c)) & 0xffff)
#define SUBS(r,s,c) (((r) + ~(s) + (c)) & 0xffff)
#define OR(r,s)		((r) | (s))
#define AND(r,s)	((r) & (s))
#define NOTRS(r,s)	(~(r) & (s))
#define EXOR(r,s)	((r) ^ (s))
#define EXNOR(r,s)	(~((r) ^ (s)))

/* values for MC_DST */
enum {
	QREG = 0,
	NOP,
	RAMA,
	RAMF,
	RAMQD,
	RAMD,
	RAMQU,
	RAMU
};

/* values for MC_IF */
enum {
	S_ROMDE = 0,
	S_RAMDE
};

/* values for MC_OA */
enum {
	S_SREG = 0,
	S_ROMA,
	S_RAMD
};

/* values for MC_JMP */
enum {
	S_JBK = 0,
	S_CALL,
	S_OPT,
	S_RETURN
};

/* values for MC_JCON */
enum {
	S_ALWAYS = 0,
	S_MSB,
	S_FEQ0,
	S_Y10,
	S_VFIN,
	S_FPOS,
	S_INTL4
};


/*************************************
 *
 *  Global variables
 *
 *************************************/

UINT16 *vertigo_vectorram;


/*************************************
 *
 *  Typedefs
 *
 *************************************/

typedef struct _am2901
{
	uint32_t ram[16];	/* internal ram */
	uint32_t d;			/* direct data D input */
	uint32_t q;			/* Q register */
	uint32_t f;			/* F ALU result */
	uint32_t y;			/* Y output */
} am2901;

typedef struct _vector_generator
{
	uint32_t sreg;		/* shift register */
	uint32_t l1;		/* latch 1 adder operand only */
	uint32_t l2;		/* latch 2 adder operand only */
	uint32_t c_v;		/* vertical position counter */
	uint32_t c_h;		/* horizontal position counter */
	uint32_t c_l;		/* length counter */
	uint32_t adder_s;	/* slope generator result and B input */
	uint32_t adder_a;	/* slope generator A input */
	uint32_t color;		/* color */
	uint32_t intensity;	/* intensity */
	uint32_t brez;		/* h/v-counters enable */
	uint32_t vfin;		/* drawing yes/no */
	uint32_t hud1;		/* h-counter up or down (stored in L1) */
	uint32_t hud2;		/* h-counter up or down (stored in L2) */
	uint32_t vud1;		/* v-counter up or down (stored in L1) */
	uint32_t vud2;		/* v-counter up or down (stored in L2) */
	uint32_t hc1;		/* use h- or v-counter in L1 mode */
	uint32_t ven;		/* vector intensity enable */
} vector_generator;

typedef struct _microcode
{
	uint32_t x;
	uint32_t a;
	uint32_t b;
	uint32_t inst;
	uint32_t dest;
	uint32_t cn;
	uint32_t mreq;
	uint32_t rsel;
	uint32_t rwrite;
	uint32_t of;
	uint32_t iif;
	uint32_t oa;
	uint32_t jpos;
	uint32_t jmp;
	uint32_t jcon;
	uint32_t ma;
} microcode;

typedef struct _vproc
{
	UINT16 sram[64];	/* external sram */
	UINT16 ramlatch;	/* latch between 2901 and sram */
	UINT16 rom_adr;		/* vector ROM/RAM address latch */
	uint32_t pc;		/* program counter */
	uint32_t ret;		/* return address */
} vproc;


/*************************************
 *
 *  Statics
 *
 *************************************/

static vproc vs;
static am2901 bsp;
static vector_generator vgen;
static UINT16 *vertigo_vectorrom;
static microcode mc[MC_LENGTH];

static unsigned g_vproc_points = 0;   // DEBUG: points emitted by the current vproc run


/*************************************
 *
 *  Vector processor initialization
 *
 *************************************/

void vertigo_vproc_init(void)
{
	int i;

	vertigo_vectorrom = (UINT16 *)memory_region(REGION_USER1);

	/* Assemble the 512 x 64-bit microcode words from the 13 raw PROMs.
	   MAME assembles these declaratively with ROMX_LOAD into a big-endian
	   64-bit region; AAE has no such macro, so the driver loads the PROMs raw
	   into REGION_PROMS (0x200 bytes each, in the order below) and we pack the
	   nibbles here. Each PROM contributes one nibble of one byte of the
	   8-byte (big-endian) word:

	     byte1 LO = vuc.10                         (bits 48-51)
	     byte2 LO = vuc.09   byte2 HI = vuc.13     (bits 40-43 / 44-47)
	     byte3 LO = vuc.07   byte3 HI = vuc.08     (bits 32-35 / 36-39)
	     byte4 LO = vuc.05   byte4 HI = vuc.06     (bits 24-27 / 28-31)
	     byte5 LO = vuc.11   byte5 HI = vuc.12     (bits 16-19 / 20-23)
	     byte6 LO = vuc.01   byte6 HI = vuc.02     (bits  8-11 / 12-15)
	     byte7 LO = vuc.03   byte7 HI = vuc.04     (bits  0- 3 /  4- 7)

	   PROM load order in REGION_PROMS (must match the driver's ROM_LOADs):
	     0:vuc.10 1:vuc.09 2:vuc.13 3:vuc.07 4:vuc.08 5:vuc.05 6:vuc.06
	     7:vuc.11 8:vuc.12 9:vuc.01 10:vuc.02 11:vuc.03 12:vuc.04            */
	const UINT8 *prom = memory_region(REGION_PROMS);

	for (i = 0; i < MC_LENGTH; i++)
	{
		#define P(n) ((uint64_t)(prom[(n) * 0x200 + i] & 0x0f))
		uint64_t mcode =
			  (P(0)  << 48)
			| (P(1)  << 40) | (P(2)  << 44)
			| (P(3)  << 32) | (P(4)  << 36)
			| (P(5)  << 24) | (P(6)  << 28)
			| (P(7)  << 16) | (P(8)  << 20)
			| (P(9)  <<  8) | (P(10) << 12)
			| (P(11) <<  0) | (P(12) <<  4);
		#undef P

		mc[i].x = (mcode >> 44) & 0x3f;
		mc[i].a = (mcode >> 40) & 0xf;
		mc[i].b = (mcode >> 36) & 0xf;
		mc[i].inst = (mcode >> 27) & 077;
		mc[i].dest = (mcode >> 33) & 07;
		mc[i].cn = (mcode >> 26) & 0x1;
		mc[i].mreq = (mcode >> 25) & 0x1;
		mc[i].rwrite = (mcode >> 23) & 0x1;
		mc[i].rsel = mc[i].rwrite & ((mcode >> 24) & 0x1);
		mc[i].of =  (mcode >> 20) & 0x7;
		mc[i].iif = (mcode >> 18) & 0x3;
		mc[i].oa = (mcode >> 16) & 0x3;
		mc[i].jpos = (mcode >> 14) & 0x1;
		mc[i].jmp = (mcode >> 12) & 0x3;
		mc[i].jcon = (mcode >> 9) & 0x7;
		mc[i].ma = mcode & 0x1ff;
	}

	memset(&vs, 0, sizeof(vs));
	memset(&bsp, 0, sizeof(bsp));
	memset(&vgen, 0, sizeof(vgen));
}


/********************************************
 *
 *  4 x AM2901 bit slice processors
 *  Q3 and IN3 are hardwired
 *
 ********************************************/

static void am2901x4 (am2901 *bsp, microcode *mc)
{
	switch (mc->inst)
	{
	case 000: bsp->f = ADD(bsp->ram[mc->a], bsp->q, mc->cn); break;
	case 001: bsp->f = ADD(bsp->ram[mc->a], bsp->ram[mc->b], mc->cn); break;
	case 002: bsp->f = ADD(0, bsp->q, mc->cn); break;
	case 003: bsp->f = ADD(0, bsp->ram[mc->b], mc->cn); break;
	case 004: bsp->f = ADD(0, bsp->ram[mc->a], mc->cn); break;
	case 005: bsp->f = ADD(bsp->d, bsp->ram[mc->a], mc->cn); break;
	case 006: bsp->f = ADD(bsp->d, bsp->q, mc->cn); break;
	case 007: bsp->f = ADD(bsp->d, 0, mc->cn); break;

	case 010: bsp->f = SUBR(bsp->ram[mc->a], bsp->q, mc->cn); break;
	case 011: bsp->f = SUBR(bsp->ram[mc->a], bsp->ram[mc->b], mc->cn); break;
	case 012: bsp->f = SUBR(0, bsp->q, mc->cn); break;
	case 013: bsp->f = SUBR(0, bsp->ram[mc->b], mc->cn); break;
	case 014: bsp->f = SUBR(0, bsp->ram[mc->a], mc->cn); break;
	case 015: bsp->f = SUBR(bsp->d, bsp->ram[mc->a], mc->cn); break;
	case 016: bsp->f = SUBR(bsp->d, bsp->q, mc->cn); break;
	case 017: bsp->f = SUBR(bsp->d, 0, mc->cn); break;

	case 020: bsp->f = SUBS(bsp->ram[mc->a], bsp->q, mc->cn); break;
	case 021: bsp->f = SUBS(bsp->ram[mc->a], bsp->ram[mc->b], mc->cn); break;
	case 022: bsp->f = SUBS(0, bsp->q, mc->cn); break;
	case 023: bsp->f = SUBS(0, bsp->ram[mc->b], mc->cn); break;
	case 024: bsp->f = SUBS(0, bsp->ram[mc->a], mc->cn); break;
	case 025: bsp->f = SUBS(bsp->d, bsp->ram[mc->a], mc->cn); break;
	case 026: bsp->f = SUBS(bsp->d, bsp->q, mc->cn); break;
	case 027: bsp->f = SUBS(bsp->d, 0, mc->cn); break;

	case 030: bsp->f = OR(bsp->ram[mc->a], bsp->q); break;
	case 031: bsp->f = OR(bsp->ram[mc->a], bsp->ram[mc->b]); break;
	case 032: bsp->f = OR(0, bsp->q); break;
	case 033: bsp->f = OR(0, bsp->ram[mc->b]); break;
	case 034: bsp->f = OR(0, bsp->ram[mc->a]); break;
	case 035: bsp->f = OR(bsp->d, bsp->ram[mc->a]); break;
	case 036: bsp->f = OR(bsp->d, bsp->q); break;
	case 037: bsp->f = OR(bsp->d, 0); break;

	case 040: bsp->f = AND(bsp->ram[mc->a], bsp->q); break;
	case 041: bsp->f = AND(bsp->ram[mc->a], bsp->ram[mc->b]); break;
	case 042: bsp->f = AND(0, bsp->q); break;
	case 043: bsp->f = AND(0, bsp->ram[mc->b]); break;
	case 044: bsp->f = AND(0, bsp->ram[mc->a]); break;
	case 045: bsp->f = AND(bsp->d, bsp->ram[mc->a]); break;
	case 046: bsp->f = AND(bsp->d, bsp->q); break;
	case 047: bsp->f = AND(bsp->d, 0); break;

	case 050: bsp->f = NOTRS(bsp->ram[mc->a], bsp->q); break;
	case 051: bsp->f = NOTRS(bsp->ram[mc->a], bsp->ram[mc->b]); break;
	case 052: bsp->f = NOTRS(0, bsp->q); break;
	case 053: bsp->f = NOTRS(0, bsp->ram[mc->b]); break;
	case 054: bsp->f = NOTRS(0, bsp->ram[mc->a]); break;
	case 055: bsp->f = NOTRS(bsp->d, bsp->ram[mc->a]); break;
	case 056: bsp->f = NOTRS(bsp->d, bsp->q); break;
	case 057: bsp->f = NOTRS(bsp->d, 0); break;

	case 060: bsp->f = EXOR(bsp->ram[mc->a], bsp->q); break;
	case 061: bsp->f = EXOR(bsp->ram[mc->a], bsp->ram[mc->b]); break;
	case 062: bsp->f = EXOR(0, bsp->q); break;
	case 063: bsp->f = EXOR(0, bsp->ram[mc->b]); break;
	case 064: bsp->f = EXOR(0, bsp->ram[mc->a]); break;
	case 065: bsp->f = EXOR(bsp->d, bsp->ram[mc->a]); break;
	case 066: bsp->f = EXOR(bsp->d, bsp->q); break;
	case 067: bsp->f = EXOR(bsp->d, 0); break;

	case 070: bsp->f = EXNOR(bsp->ram[mc->a], bsp->q); break;
	case 071: bsp->f = EXNOR(bsp->ram[mc->a], bsp->ram[mc->b]); break;
	case 072: bsp->f = EXNOR(0, bsp->q); break;
	case 073: bsp->f = EXNOR(0, bsp->ram[mc->b]); break;
	case 074: bsp->f = EXNOR(0, bsp->ram[mc->a]); break;
	case 075: bsp->f = EXNOR(bsp->d, bsp->ram[mc->a]); break;
	case 076: bsp->f = EXNOR(bsp->d, bsp->q); break;
	case 077: bsp->f = EXNOR(bsp->d, 0); break;
	}

	switch (mc->dest)
	{
	case QREG:
		bsp->q = bsp->f;
		bsp->y = bsp->f;
		break;
	case NOP:
		bsp->y = bsp->f;
		break;
	case RAMA:
		bsp->y = bsp->ram[mc->a];
		bsp->ram[mc->b] = bsp->f;
		break;
	case RAMF:
		bsp->y = bsp->f;
		bsp->ram[mc->b] = bsp->f;
		break;
	case RAMQD:
		bsp->y = bsp->f;
		bsp->q = (bsp->q >> 1) & 0x7fff;			/* Q3 is low */
		bsp->ram[mc->b] = (bsp->f >> 1) | 0x8000;	/* IN3 is high! */
		break;
	case RAMD:
		bsp->y = bsp->f;
		bsp->ram[mc->b] = (bsp->f >> 1) | 0x8000;	/* IN3 is high! */
		break;
	case RAMQU:
		bsp->y = bsp->f;
		bsp->ram[mc->b] = (bsp->f << 1) & 0xffff;
		bsp->q = (bsp->q << 1) & 0xffff;
		break;
	case RAMU:
		bsp->y = bsp->f;
		bsp->ram[mc->b] = (bsp->f << 1) & 0xffff;
		break;
	}
}


/********************************************
 *
 *  Vector Generator
 *
 *  This part of the hardware draws vectors
 *  under control of the bit slice processors.
 *  It is just a bunch of counters, latches
 *  and DACs.
 *
 ********************************************/

static void vertigo_vgen (vector_generator *vg)
{
	if (vg->c_l & 0x800)
	{
		vg->vfin = 1;
		vg->c_l = (vg->c_l+1) & 0xfff;

		if ((vg->c_l & 0x800) == 0)
		{
			vg->brez = 0;
			vg->vfin = 0;
		}

		if (vg->brez) /* H/V counter enabled */
		{
			/* Depending on MSB of adder only one or both
               counters are de-/incremented. This is all
               defined by the shift register which is
               latched in bits 12-15 of L1/L2.
            */
			if (vg->adder_s & 0x800)
			{
				if (vg->hc1)
					vg->c_h += vg->hud1? -1: 1;
				else
					vg->c_v += vg->vud1? -1: 1;
				vg->adder_a = vg->l1;
			}
			else
			{
				vg->c_h += vg->hud2? -1: 1;
				vg->c_v += vg->vud2? -1: 1;
				vg->adder_a = vg->l2;
			}

			/* H/V counters are 12 bit */
			vg->c_v &= 0xfff;
			vg->c_h &= 0xfff;
		}

		vg->adder_s = (vg->adder_s + vg->adder_a) & 0xfff;
	}

	if (vg->brez ^ vg->ven)
	{
		if (vg->brez)
		V_ADDPOINT (vg->c_h, vg->c_v, 0, 0);
		else
			V_ADDPOINT (vg->c_h, vg->c_v, vg->color, vg->intensity);
		vg->ven = vg->brez;
		g_vproc_points++;   // DEBUG: count emitted points
	}
}

/*************************************
 *
 *  Vector processor
 *
 *************************************/

void vertigo_vproc(int cycles, int irq4)
{
	int jcond;
	microcode *cmc;

	if (irq4) { vector_clear_list();	cache_clear(); }

	g_vproc_points = 0;   // DEBUG: count points emitted by this run

	while (cycles--)
	{
		/* Microcode at current PC */
		cmc = &mc[vs.pc];

		/* Load data */
		if (cmc->iif == S_RAMDE)
		{
			bsp.d = vs.ramlatch;
		}
		else if (cmc->iif == S_ROMDE)
		{
			if (vs.rom_adr < 0x2000)
			{
				bsp.d = vertigo_vectorram[vs.rom_adr & 0xfff];
			}
			else
			{
				bsp.d = vertigo_vectorrom[vs.rom_adr & 0x7fff];
			}
		}

		/* SRAM selected ? */
		if (cmc->rsel == 0)
		{
			if (cmc->rwrite)
			{
				bsp.d = vs.sram[cmc->x];
			}
			else
			{
				/* Data can be transferred between vector ROM/RAM
                   and SRAM without going through the 2901 */
				vs.sram[cmc->x] = bsp.d;
			}
		}

		am2901x4 (&bsp, cmc);

		/* Store data */
		switch (cmc->oa)
		{
		case S_RAMD:
			vs.ramlatch = bsp.y;
			if (cmc->iif==S_RAMDE && (cmc->rsel == 0) && (cmc->rwrite == 0))
				vs.sram[cmc->x] = vs.ramlatch;
			break;
		case S_ROMA:
			vs.rom_adr = bsp.y;
			break;
		case S_SREG:
			/* FPOS is shifted into sreg */
			vgen.sreg = (vgen.sreg >> 1) | ((bsp.f >> 9) & 4);
			break;
		default:
			break;
		}

		/* Vector generator setup */
		switch (cmc->of)
		{
		case 0:
			vgen.color = bsp.y & 0xfff;
			break;
		case 1:
			vgen.intensity = bsp.y & 0xff;
			break;
		case 2:
			vgen.l1 = bsp.y & 0xfff;
			vgen.adder_s = 0;
			vgen.adder_a = vgen.l2;
			vgen.hud1 = vgen.sreg & 1;
			vgen.vud1 = vgen.sreg & 2;
			vgen.hc1  = vgen.sreg & 4;
			vgen.brez = 1;
			break;
		case 3:
			vgen.l2 = bsp.y & 0xfff;
			vgen.adder_s = (vgen.adder_s + vgen.adder_a) & 0xfff;
			if (vgen.adder_s & 0x800)
				vgen.adder_a = vgen.l1;
			else
				vgen.adder_a = vgen.l2;
			vgen.hud2 = vgen.sreg & 1;
			vgen.vud2 = vgen.sreg & 2;
			break;
		case 4:
			vgen.c_v = bsp.y & 0xfff;
			break;
		case 5:
			vgen.c_h = bsp.y & 0xfff;
			break;
		case 6:
			/* Loading the c_l counter starts
             * the vector counters if MSB is set
             */
			vgen.c_l = bsp.y & 0xfff;
			break;
		}

		vertigo_vgen (&vgen);

		/* Microcode program flow */
		switch (cmc->jcon)
		{
		case S_MSB:
			/* ALU most significant bit */
			jcond = (bsp.f >> 15) & 1;
			break;
		case S_FEQ0:
			/* ALU is 0 */
			jcond = (bsp.f == 0)? 1 : 0;
			break;
		case S_Y10:
			jcond = (bsp.y >> 10) & 1;
			break;
		case S_VFIN:
			jcond = vgen.vfin;
			break;
		case S_FPOS:
			/* FPOS is bit 11 */
			jcond = (bsp.f >> 11) & 1;
			break;
		case S_INTL4:
			jcond = irq4;
			/* Detect idle loop. If the code takes a jump
             on irq4 or !irq4 the destination is a idle loop
             waiting for irq4 state change. We then take a short
             cut and run for just 100 cycles to make sure the
             loop is actually entered.
            */
			if ((cmc->jpos != irq4) && cycles > 100)
			{
				cycles=100;
			}
			break;
		default:
			jcond = 1;
			break;
		}

		if (jcond ^ cmc->jpos)
		{
			/* Except for JBK, address bit 8 isn't changed
               in program flow. */
			switch (cmc->jmp)
			{
			case S_JBK:
				/* JBK is the only jump where MA8 is used */
				vs.pc = cmc->ma;
				break;
			case S_CALL:
				/* call and store return address */
				vs.ret = (vs.pc + 1) & 0xff;
				vs.pc = (vs.pc & 0x100) | (cmc->ma & 0xff);
				break;
			case S_OPT:
				/* OPT is used for microcode jump tables. The first
                   four address bits are defined by bits 12-15
                   of 2901 input (D) */
				vs.pc = (vs.pc & 0x100) | (cmc->ma & 0xf0) | ((bsp.d >> 12) & 0xf);
				break;
			case S_RETURN:
				/* return from call */
				vs.pc = (vs.pc & 0x100) | vs.ret;
				break;
			}
		}
		else
		{
			vs.pc = (vs.pc & 0x100) | ((vs.pc + 1) & 0xff);
		}
	}

	// DEBUG: points this run + a checksum of the vector RAM (the 68000's display
	// list). If both stay constant after the first frames, the CPU is not updating
	// the picture -- the vproc is just redrawing the same list.
	uint32_t vsum = 0;
	for (int i = 0; i < 0x1000; i++) vsum = vsum * 31u + vertigo_vectorram[i];
	LOG_INFO("VTG vproc irq4=%d points=%u vramsum=%08X", irq4, g_vproc_points, vsum);
}
