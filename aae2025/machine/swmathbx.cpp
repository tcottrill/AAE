//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================

/*****************************************************************
machine\swmathbx.c

This file is Copyright 1997, Steve Baines.

Release 2.0 (5 August 1997)

See drivers\starwars.c for notes

******************************************************************/


#include "swmathbx.h"
#include <stdlib.h>     /* srand, rand */

#define READ_ACC      0x02
#define LAC           0x01
#define M_HALT        0x04
#define INC_BIC       0x08
#define CLEAR_ACC     0x10
#define LDC           0x20
#define LDB           0x40
#define LDA           0x80

#define LDA_CACC      0x90
#define LDB_CACC      0x50
#define NOP           0x00
#define R_ACC_M_HLT   0x06
#define INC_BIC_R_ACC 0x0a
#define INC_BIC_M_HLT 0x0c
#define INC_BIC_M_HLT_R_ACC 0x0e
#define C_ACC_M_HLT   0x14
#define LDC_M_HLT     0x24
#define LDB_M_HLT     0x44
#define LDA_M_HLT     0x84

#define MATHDEBUG 0

static int MPA = 0; /* PROM address counter */
static int BIC = 0; /* Block index counter  */


/*******************************************************/
/*   Divider handlers                                  */
/*******************************************************/

static int RESULT=0;	/* ASG 971002 */
static int DIVISOR = 0;
static int DIVIDEND = 0;

/* Store decoded PROM elements */
static int PROM_STR[1024]; /* Storage for instruction strobe only */
static int PROM_MAS[1024]; /* Storage for direct address only */
static int PROM_AM[1024]; /* Storage for address mode select only */

//Note to me: Make sure to transition code or change to GI[0] for translate_proms below!!!! 

void translate_proms(void)
{
	int cnt, val;
	unsigned char* RAM = Machine->memory_region[0];
	for (cnt = 0; cnt < 1024; cnt++)
	{
		/* Translate PROMS into 16 bit code */
		val = 0;
		val = (val | ((RAM[0x0c00 + cnt]) & 0x000f)); /* Set LS nibble */
		val = (val | ((RAM[0x0800 + cnt] << 4) & 0x00f0));
		val = (val | ((RAM[0x0400 + cnt] << 8) & 0x0f00));
		val = (val | ((RAM[0x0000 + cnt] << 12) & 0xf000)); /* Set MS nibble */

		/* Perform pre-decoding */
		PROM_STR[cnt] = (val >> 8) & 0x00ff;
		PROM_MAS[cnt] = (val & 0x007f);
		PROM_AM[cnt] = ((val >> 7) & 0x0001);
		//wrlog("PROM_STR %x, PROM_MAS %x PROM_AM %x", PROM_STR[cnt], PROM_MAS[cnt], PROM_AM[cnt]);

	}
	wrlog("MathBox init completed.");
}

void run_mbox(void)
{
	static short ACC, A, B, C;

	int RAMWORD = 0;
	int MA_byte;
	int tmp;
	int M_STOP = 100000; /* Limit on number of instructions allowed before halt */
	int MA;
	int IP15_8, IP7, IP6_0; /* Instruction PROM values */

	unsigned char* RAM = Machine->memory_region[0];

	while (M_STOP > 0)
	{
		IP15_8 = PROM_STR[MPA];
		IP7 = PROM_AM[MPA];
		IP6_0 = PROM_MAS[MPA];

		/* Construct the current RAM address */
		if (IP7 == 0)
		{
			MA = ((IP6_0 & 3) | ((BIC & 0x01ff) << 2)); /* MA10-2 set to BIC8-0 */
		}
		else
		{
			MA = IP6_0;
		}

		/* Convert RAM offset to eight bit addressing (2kx8 rather than 1k*16)
		and apply base address offset */

		MA_byte = 0x5000 + (MA << 1);

		RAMWORD = ((RAM[MA_byte + 1] & 0x00ff) | ((RAM[MA_byte] & 0x00ff) << 8));

		/*
		 * RAMWORD is the sixteen bit Math RAM value for the selected address
		 * MA_byte is the base address of this location as seen by the main CPU
		 * IP is the 16 bit instruction word from the PROM. IP7_0 have already
		 * been used in the address selection stage
		 * IP15_8 provide the instruction strobes
		 */

		switch (IP15_8)
		{
		case READ_ACC:
			RAM[MA_byte + 1] = (ACC & 0x00ff);
			RAM[MA_byte] = (((ACC & 0xff00) >> 8) & 0x00ff);
			break;

		case R_ACC_M_HLT:
			M_STOP = 0;
			RAM[MA_byte + 1] = (ACC & 0x00ff);
			RAM[MA_byte] = (((ACC & 0xff00) >> 8) & 0x00ff);
			break;

		case LAC:
			ACC = RAMWORD;
			break;

		case M_HALT:
			M_STOP = 0;
			break;

		case INC_BIC:
			BIC++;
			BIC = BIC & 0x1ff; /* Restrict to 9 bits */
			break;

		case INC_BIC_M_HLT:
			M_STOP = 0;
			BIC++;
			BIC = BIC & 0x1ff; /* Restrict to 9 bits */
			break;

		case INC_BIC_M_HLT_R_ACC:
			M_STOP = 0;
			BIC++;
			BIC = BIC & 0x1ff; /* Restrict to 9 bits */
			RAM[MA_byte + 1] = (ACC & 0x00ff);
			RAM[MA_byte] = (((ACC & 0xff00) >> 8) & 0x00ff);
			break;

		case INC_BIC_R_ACC:
			BIC++;
			BIC = BIC & 0x1ff; /* Restrict to 9 bits */
			RAM[MA_byte + 1] = (ACC & 0x00ff);
			RAM[MA_byte] = (((ACC & 0xff00) >> 8) & 0x00ff);
			break;

		case CLEAR_ACC:
			ACC = 0;
			break;

		case NOP:
			break;

		case C_ACC_M_HLT:
			ACC = 0;
			M_STOP = 1;
			break;

		case LDC:
			/* Writing to C triggers the calculation */
			C = RAMWORD;
			/*   ACC=ACC+(  ( (long)((A-B)*C) )>>14  ); */
				/* round the result - this fixes bad tranch vectors in Star Wars */
			ACC = ACC + (((((long)((A - B) * C)) >> 13) + 1) >> 1);
			break;

		case LDB:
			B = RAMWORD;
			break;

		case LDC_M_HLT:
			/* Writing to C triggers the calculation */
			M_STOP = 0;
			C = RAMWORD;
			ACC = ACC + (((long)((A - B) * C)) >> 14);
			/* round the result - this fixes bad tranch vectors in Star Wars */
			ACC = ACC + (((((long)((A - B) * C)) >> 13) + 1) >> 1);
			break;
		case LDB_M_HLT:
			M_STOP = 0;
			B = RAMWORD;
			break;

		case LDA_M_HLT:
			M_STOP = 0;
			A = RAMWORD;
			break;

		case LDB_CACC:
			ACC = 0;
			B = RAMWORD;
			break;

		case LDA:
			A = RAMWORD;
			break;

		case LDA_CACC:
			ACC = 0;
			A = RAMWORD;
			break;

		default:
			wrlog("MBerror: %x\n", IP15_8);
		}
		/*
		 * Now update the PROM address counter
		 * Done like this because the top two bits are not part of the counter
		 * This means that each of the four pages should wrap around rather than
		 * leaking from one to another.  It may not matter, but I've put it in anyway
		 */
		tmp = MPA;
		tmp++;
		MPA = (MPA & 0x0300) | (tmp & 0x00ff); /* New MPA value */

		M_STOP--; /* Decrease count */
	}
}

/******************************************************/
/****** Other Starwars Math related handlers **********/

static int PRN = 0;

UINT8 prng(UINT32 address, struct MemoryReadByte* psMemRead)
{
	PRN = (int)((PRN+0x2364)^2); //rand(); //
	return (PRN & 0xff);
}
/********************************************************/
void prngclr()
{
	PRN = 0;
}

/*********************************************************/


/********************************************************/
UINT8 reh(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return((RESULT & 0xff00) >> 8);
}
/********************************************************/
UINT8 rel(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return(RESULT & 0x00ff);
}
/********************************************************/

void swmathbx(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	data &= 0xff;	/* ASG 971002 -- make sure we only get bytes here */

	//wrlog("Mathbox Write: %x data: %x", address, data);

	switch (address)
	{
	case 0:
		MPA = (data << 2); run_mbox();
		break;

	case 1:
		BIC = ((BIC & 0x00ff) | ((data & 0x01) << 8));
		break;

	case 2:
		BIC = ((BIC & 0x0100) | data);
		break;

	case 4: /* dvsrh */
		DIVISOR = ((DIVISOR & 0x00ff) | (data << 8));

		break;

	case 5: /* dvsrl */

	  /* Note: Divide is triggered by write to low byte.  This is */
	  /*       dependant on the proper 16 bit write order in the  */
	  /*       6809 emulation (high bytes, then low byte).        */
	  /*       If the Tie fighters look corrupt, he byte order of */
	  /*       the 16 bit writes in the 6809 are backwards        */

		DIVISOR = ((DIVISOR & 0xff00) | (data));

		if (DIVIDEND >= 2 * DIVISOR)
			RESULT = 0x7fff;
		else
			RESULT = (int)((((long)DIVIDEND << 14) / (long)DIVISOR));
		break;

	case 6: /* dvddh */
		DIVIDEND = ((DIVIDEND & 0x00ff) | (data << 8));
		break;

	case 7: /* dvddl */
		DIVIDEND = ((DIVIDEND & 0xff00) | (data));
		break;

	default:
		break;
	}
}