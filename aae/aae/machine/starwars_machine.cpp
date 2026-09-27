// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.(TM) Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E.(TM) Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

/*****************************************************************
machine\swmathbx.c

This file is Copyright 1997, Steve Baines.

Release 2.0 (5 August 1997)

Ported to MAME 0.159 matrix-processor/divider/busy-flag/X2212 behaviour
(machine/starwars.c) - see drivers\starwars.c for notes

******************************************************************/

#include <random>
#include "aae_mame_driver.h"
#include "starwars_machine.h"
#include "mame_late_avgdvg.h"
#include "timer.h"

extern int bank1;
extern int bank2;

/* Control select values for ADC_R */
#define kPitch		0
#define kYaw		1
#define kThrust		2

/* Constants for matrix processor operations */
#define NOP			0x00
#define LAC			0x01
#define READ_ACC	0x02
#define M_HALT		0x04
#define INC_BIC		0x08
#define CLEAR_ACC	0x10
#define LDC			0x20
#define LDB			0x40
#define LDA			0x80

/* Debugging flag */
#define MATHDEBUG	0

#define MASTER_CLOCK (12096000)

/* Local variables */
static UINT8 control_num = kPitch;

static int MPA; /* PROM address counter */
static int BIC; /* Block index counter  */

/* Matrix processor busy flag (starwars_input_1_r bit 7 / IN1) and the
 * one-shot timer that clears it once the (emulated) processor time for the
 * last run_mproc() has elapsed - see starwars_state::math_run_clear /
 * m_math_timer in MAME 0.159 machine/starwars.c. */
static int math_run;
static int math_busy_timer = -1;

/* Matrix processor accumulator/registers. These are file-static (one
 * instance, like the single starwars_state MAME allocates) rather than
 * function-local statics so their lifetime/reset behaviour is explicit.
 * MAME's starwars_mproc_reset() does NOT reset m_A/m_B/m_C/m_ACC, only
 * m_MPA/m_BIC/m_math_run, so swmathbox_reset() below mirrors that and
 * leaves these alone. */
static INT16 A, B, C;
static INT32 ACC;

/* Starwars divider state - UINT16 so the restoring-division wraparound
 * matches MAME's m_dvd_shift/m_quotient_shift/m_divisor/m_dividend. */
static UINT16 dvd_shift;
static UINT16 quotient_shift;
static UINT16 divisor;
static UINT16 dividend;

/* Store decoded PROM elements */
static UINT8 PROM_STR[1024]; /* Storage for instruction strobe only */
static UINT8 PROM_MAS[1024]; /* Storage for direct address only */
static UINT8 PROM_AM[1024]; /* Storage for address mode select only */

/* Local function prototypes */
static void run_mproc(void);

/*************************************
 *
 *	Input port 1
 *
 *************************************/

READ_HANDLER_NS(starwars_input_1_r)
{
	int x = input_port_1_r(1);

	/* matrix processor busy flag - starwars_state::matrix_flag_r in MAME
	 * 0.159. Replaces the old "we happen to be at this PC" kludge. */
	if (math_run)
		x |= 0x80;
	else
		x &= ~0x80;

	/* set the AVG done flag */
	if (avgdvg_done())
		x |= 0x40;
	else
		x &= ~0x40;

	return x;
}

/*************************************
 *
 *	X2212 nvram store
 *
 *************************************/

// A write anywhere in $46a0-$46bf pulses /STORE: one rising edge copies the
// live SRAM into the EEPROM. Mirrors starwars_state::starwars_nstore_w,
// which pulses the x2212 device's store line 0 -> 1 -> 0.
WRITE_HANDLER_NS(starwars_nstore_w)
{
	x2212_store_line_w(0);
	x2212_store_line_w(1);
	x2212_store_line_w(0);
}

/*************************************
 *
 *	Output latch
 *
 *************************************/

WRITE_HANDLER_NS(starwars_out_w)
{
	unsigned char* RAM = memory_region(REGION_CPU1);

	// MAME maps this latch at 0x4680-0x469f with the register selected by
	// offset & 7 (the top bits are just address-decode don't-cares).
	switch (address & 7)
	{
	case 0:		/* Coin counter 1 */
		//coin_counter_w(0, data);
		break;

	case 1:		/* Coin counter 2 */
		//coin_counter_w(1, data);
		break;

	case 2:		/* LED 3 */
		set_led_status(2, ~data & 0x80);
		break;

	case 3:		/* LED 2 */
		set_led_status(1, ~data & 0x80);
		break;

	case 4:		/* bank switch */
		if (data & 0x80)
		{
			bank1 = 0x10000;
			bank2 = 0x1c000;
		}
		else
		{
			bank1 = 0x06000;
			bank2 = 0x0a000;
		}
		break;

	case 5:		/* reset PRNG - MAME 0.159 does nothing here (the PRNG is
				   now machine().rand(), which is free-running) */
		break;

	case 6:		/* LED 1 */
		set_led_status(0, ~data & 0x80);
		break;

	case 7:		/* NVRAM array recall - active-low line, so the recall
				   input to the x2212 is ~data & 0x80 (matches
				   starwars_state::starwars_out_w case 7) */
		x2212_recall_line_w(~data & 0x80);
		break;
	}
}

/*************************************
 *
 *	ADC input and control
 *
 *************************************/

READ_HANDLER_NS(starwars_adc_r)
{
	/* pitch */
	if (control_num == kPitch)
		return readinputport(4);

	/* yaw */
	else if (control_num == kYaw)
		return readinputport(5);

	/* default to unused thrust */
	else
		return 0;
}

WRITE_HANDLER_NS(starwars_adc_select_w)
{
	control_num = address;
}

/*************************************
 *
 *	Matrix Processor initialization
 *
 *************************************/

void swmathbox_init(void)
{
	UINT8* src = memory_region(REGION_GFX1);
	int cnt, val;

	for (cnt = 0; cnt < 1024; cnt++)
	{
		/* translate PROMS into 16 bit code */
		val = (src[0x0c00 + cnt]) & 0x000f; /* Set LS nibble */
		val |= (src[0x0800 + cnt] << 4) & 0x00f0;
		val |= (src[0x0400 + cnt] << 8) & 0x0f00;
		val |= (src[0x0000 + cnt] << 12) & 0xf000; /* Set MS nibble */

		/* perform pre-decoding */
		PROM_STR[cnt] = (val >> 8) & 0x00ff;
		PROM_MAS[cnt] = val & 0x007f;
		PROM_AM[cnt] = (val >> 7) & 0x0001;
	}
}

/*************************************
 *
 *	Matrix Processor busy-flag timer init
 *
 *	Allocates the one-shot timer that clears math_run once the emulated
 *	matrix processor time for the last run_mproc() has elapsed - MAME's
 *	m_math_timer (allocated once in starwars_mproc_init, re-armed each
 *	run_mproc() via m_math_timer->adjust()). Called once from both
 *	init_starwars() and init_esb() (TomCat shares init_starwars); like
 *	avgdvg_init()'s vg_halt_timer/vg_run_timer, timer_init() wipes all
 *	timers before every driver init runs, so allocating here unconditionally
 *	can never leak or double-arm a timer across game (re)loads.
 *
 *************************************/

void swmathbox_timer_init(void)
{
	math_busy_timer = timer_alloc([](int param) { math_run = 0; });
}

/*************************************
 *
 *	Matrix Processor reset
 *
 *************************************/

void swmathbox_reset(void)
{
	MPA = BIC = 0;
	math_run = 0;
}

/*************************************
 *
 *	Matrix Processor execution
 *
 *************************************/

static void run_mproc(void)
{
	UINT8* RAM = memory_region(REGION_CPU1);
	int RAMWORD = 0;
	int MA_byte;
	int tmp;
	int M_STOP = 100000; /* Limit on number of instructions allowed before halt */
	int MA;
	int IP15_8, IP7, IP6_0; /* Instruction PROM values */
	int mptime;

	//LOG_INFO("Running Matrix Processor...\n");

	mptime = 0;
	math_run = 1;

	/* loop until finished */
	while (M_STOP > 0)
	{
		/* each step of the matrix processor takes five clock cycles */
		mptime += 5;

		/* fetch the current instruction data */
		IP15_8 = PROM_STR[MPA];
		IP7 = PROM_AM[MPA];
		IP6_0 = PROM_MAS[MPA];

#if (MATHDEBUG)
		printf("\n(MPA:%x), Strobe: %x, IP7: %d, IP6_0:%x\n", MPA, IP15_8, IP7, IP6_0);
		printf("(BIC: %x), A: %x, B: %x, C: %x, ACC: %x\n", BIC, A, B, C, ACC);
#endif

		/* construct the current RAM address */
		if (IP7 == 0)
			MA = (IP6_0 & 3) | ((BIC & 0x01ff) << 2);	/* MA10-2 set to BIC8-0 */
		else
			MA = IP6_0;

		/* convert RAM offset to eight bit addressing (2kx8 rather than 1k*16)
			and apply base address offset. AAE's Math RAM lives directly in
			the main CPU region at 0x5000-0x5fff (unlike MAME's dedicated
			"mathram" share), so MA_byte is offset by that base here. */

		MA_byte = 0x5000 + (MA << 1);
		RAMWORD = (RAM[MA_byte + 1] & 0x00ff) | ((RAM[MA_byte] & 0x00ff) << 8);

		//LOG_INFO("MATH ADDR: %x, CPU ADDR: %x, RAMWORD: %x\n", MA, MA_byte, RAMWORD);

		/*
		 * RAMWORD is the sixteen bit Math RAM value for the selected address
		 * MA_byte is the base address of this location as seen by the main CPU
		 * IP is the 16 bit instruction word from the PROM. IP7_0 have already
		 * been used in the address selection stage
		 * IP15_8 provide the instruction strobes
		 */

		/* The accumulator is built from two ls299 (msb) and two ls164
		 * (lsb). You can only read/write the 16 msb. The lsb are
		 * used while adding up multiplication results giving better
		 * accuracy.
		 */

		/* 0x10 - CLEAR_ACC */
		if (IP15_8 & CLEAR_ACC)
		{
			ACC = 0;
		}

		/* 0x01 - LAC (also clears lsb)*/
		if (IP15_8 & LAC)
			ACC = (RAMWORD << 16);

		/* 0x02 - READ_ACC */
		if (IP15_8 & READ_ACC)
		{
			RAM[MA_byte + 1] = ((ACC >> 16) & 0xff);
			RAM[MA_byte] = ((ACC >> 24) & 0xff);
		}

		/* 0x04 - M_HALT */
		if (IP15_8 & M_HALT)
			M_STOP = 0;

		/* 0x08 - INC_BIC */
		if (IP15_8 & INC_BIC)
			BIC = (BIC + 1) & 0x1ff; /* Restrict to 9 bits */

		/* 0x20 - LDC*/
		if (IP15_8 & LDC)
		{
			C = (INT16)RAMWORD;

			/* This is a serial subtractor - multiplier (74ls384) -
			 * accumulator. For the full calculation 33 GMCLK pulses
			 * are generated. The calculation performed is:
			 *
			 * ACC = ACC + (A - B) * C
			 *
			 * 1. pulse: Bit 0 of A and B are subtracted. Bit 0 of the
			 * multiplication between multiplicand C and 0 is
			 * calculated (bit 0 of A-B is not yet at the multiplier
			 * input). Bit 0 of ACC is added to 0 (again, 'real' results
			 * from the previous operations are no yet there).
			 *
			 * 2. pulse: Bit 1 of A-B is calculated. Bit 1 of
			 * mutliplication is calculated based on bit 0 of A-B and
			 * bit 1 of C. Bit 1 of ACC is added to the multiplication
			 * result from first pulse.
			 *
			 * 3. pulse: Bit 2 of A-B is calculated. Bit 2 of
			 * mutliplication is calculated based on bit 1 of A-B and
			 * bit 2 of C. Bit 2 of ACC is added to the multiplication
			 * between bit 1 of C and bit 0 of A-B.
			 *
			 * etc.
			 *
			 * This pipeline causes the shifts between A-B, C and ACC.
			 * The 32 bit ACC and one bit adder form a ring so it
			 * takes 33 clock pulses to do a full rotation.
			 */

			ACC += (((INT32)(A - B) << 1) * C) << 1;

			/* A and B are sign extended (requred by the ls384). After
			 * multiplication they just contain the sign.
			 */
			A = (A & 0x8000) ? 0xffff : 0;
			B = (B & 0x8000) ? 0xffff : 0;

			/* The multiply-add holds the main matrix processor counter
			 * for 33 cycles
			 */
			mptime += 33;
		}

		/* 0x40 - LDB */
		if (IP15_8 & LDB)
			B = (INT16)RAMWORD;

		/* 0x80 - LDA */
		if (IP15_8 & LDA)
			A = (INT16)RAMWORD;

		/*
		 * Now update the PROM address counter
		 * Done like this because the top two bits are not part of the counter
		 * This means that each of the four pages should wrap around rather than
		 * leaking from one to another.  It may not matter, but I've put it in anyway
		 */
		tmp = MPA + 1;
		MPA = (MPA & 0x0300) | (tmp & 0x00ff); /* New MPA value */

		M_STOP--; /* Decrease count */
	}

	/* Arm the busy-flag clear timer for mptime master-clock periods - see
	 * starwars_state::run_mproc()'s m_math_timer->adjust() in MAME 0.159.
	 * math_busy_timer is allocated once by swmathbox_timer_init(); this just
	 * re-arms the existing slot (one-shot: period 0). */
	timer_adjust(math_busy_timer, TIME_IN_HZ(MASTER_CLOCK) * mptime, 1, 0);
}

/*************************************
 *
 *	Pseudo-RNG read
 *
 *************************************/

READ_HANDLER_NS(swmathbx_prng_r)
{
	/*
	 * The PRNG is a modified 23 bit LFSR. Taps are at 4 and 22 so the
	 * resulting LFSR polynomial is,
	 *
	 * x^5 + x^{23} + 1
	 *
	 * which is prime. It has a loop length of 8388607. The feedback
	 * bit is inverted so the PRNG can start with 0. Only 8 bits from
	 * bit 8 to 15 can be read by the CPU. The PRNG runs constantly at
	 * a clock speed of 3 MHz.
	 *
	 * MAME 0.159 just returns machine().rand() rather than modelling the
	 * LFSR. AAE has no equivalent machine-wide PRNG helper, so this uses a
	 * dedicated Mersenne Twister seeded from a real entropy source (the old
	 * "(PRN + 0x2364) ^ 2" bodge produced a short, entirely predictable
	 * cycle and is removed).
	 */
	static std::mt19937 rng(std::random_device{}());
	static std::uniform_int_distribution<int> dist(0, 255);
	return (UINT8)dist(rng);
}

/*************************************
 *
 *	Starwars divider
 *
 *************************************/

READ_HANDLER_NS(swmathbx_reh_r)
{
	return (quotient_shift & 0xff00) >> 8;
}

READ_HANDLER_NS(swmathbx_rel_r)
{
	return quotient_shift & 0x00ff;
}

WRITE_HANDLER_NS(swmathbx_w)
{
	int i;

	data &= 0xff;	/* ASG 971002 -- make sure we only get bytes here */
	switch (address)
	{
	case 0:	/* mw0 */
		MPA = data << 2;	/* Set starting PROM address */
		run_mproc();			/* and run the Matrix Processor */
		break;

	case 1:	/* mw1 */
		BIC = (BIC & 0x00ff) | ((data & 0x01) << 8);
		break;

	case 2:	/* mw2 */
		BIC = (BIC & 0x0100) | data;
		break;

	case 4: /* dvsrh */
		divisor = (divisor & 0x00ff) | (data << 8);
		dvd_shift = dividend;
		quotient_shift = 0;
		break;

	case 5: /* dvsrl */
		/* Note: Divide is triggered by write to low byte.  This is */
		/*       dependant on the proper 16 bit write order in the  */
		/*       6809 emulation (high bytes, then low byte).        */
		/*       If the Tie fighters look corrupt, he byte order of */
		/*       the 16 bit writes in the 6809 are backwards        */

		divisor = (divisor & 0xff00) | data;

		/*
		 * Simple restoring division as shown in the
		 * schematics. The algorithm produces the same "wrong"
		 * results as the hardware if divisor < 2*dividend or
		 * divisor > 0x8000.
		 */
		for (i = 1; i < 16; i++)
		{
			quotient_shift <<= 1;
			if (((INT32)dvd_shift + (divisor ^ 0xffff) + 1) & 0x10000)
			{
				quotient_shift |= 1;
				dvd_shift = (dvd_shift + (divisor ^ 0xffff) + 1) << 1;
			}
			else
			{
				dvd_shift <<= 1;
			}
		}
		break;

	case 6: /* dvddh */
		dividend = (dividend & 0x00ff) | (data << 8);
		break;

	case 7: /* dvddl */
		dividend = (dividend & 0xff00) | (data);
		break;

	default:
		break;
	}
}
