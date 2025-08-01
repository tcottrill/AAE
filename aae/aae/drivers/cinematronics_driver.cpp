/* Cinematronics Emu */

#include "aae_mame_driver.h"
#include "ccpu.h"
#include "cinematronics_driver.h"
#include "cinematronics_video.h"
#include "cinematronics_sound.h"



//= ========================================================================== =
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

//From M.A.M.E. (TM)
/***************************************************************************

	Cinematronics vector hardware

	driver by Aaron Giles
		license:BSD-3-Clause
		copyright-holders:Aaron Giles

	Special thanks to Neil Bradley, Zonn Moore, and Jeff Mitchell of the
	Retrocade Alliance

	Games supported:
		* Space Wars
		* Barrier
		* Star Hawk
		* Star Castle
		* Tailgunner
		* Rip Off
		* Speed Freak
		* Sundance
		* Warrior
		* Armor Attack
		* Solar Quest
		* Demon
		* War of the Worlds
		* Boxing Bugs

***************************************************************************/
#define CCPU_MEMSIZE_4K        	4
#define CCPU_MEMSIZE_8K        	8
#define CCPU_MEMSIZE_16K       	16
#define CCPU_MEMSIZE_32K       	32

#define CCPU_MONITOR_BILEV  	0
#define CCPU_MONITOR_16LEV  	1
#define CCPU_MONITOR_64LEV  	2
#define CCPU_MONITOR_WOWCOL 	3

// Coin Handling.
static UINT8 coin_detected;
static UINT8 coin_last_reset;
static UINT8 mux_select;

namespace CCPU_INPUTS {
	enum input_handler { CCPUIN, BOXINGB, SUNDANCE, SPEEDFRK };
}
static int ccpuinput_type = 0;

/*************************************
 *
 *  Coin handlers
 *
 *************************************/
static int input_changed(int coin_inserted)
{
	/* on the falling edge of a new coin, set the coin_detected flag */
	if (coin_inserted == 0)
		coin_detected = 1;
}

static int coin_read(int coin_input_r)
{
	return !coin_detected;
}

/*************************************
 *
 *  Speed Freak inputs
 *
 *************************************/

static int speedfrk_wheel_r(int offset)
{
	static const UINT8 speedfrk_steer[] = { 0xe, 0x6, 0x2, 0x0, 0x3, 0x7, 0xf };

	static int last_wheel = 0, last_frame = 0;
	int delta_wheel = 0;

	
	// the shift register is cleared once per 'frame'
	if (cpu_getcurrentframe() > last_frame)
	{
		delta_wheel = (INT8)readinputportbytag("WHEEL") / 8;
		//LOG_INFO("DELTA WHEEL is %d", delta_wheel);

		if (delta_wheel > 3)
			delta_wheel = 3;
		else if (delta_wheel < -3)
			delta_wheel = -3;
	}
	last_frame = cpu_getcurrentframe();
	//LOG_INFO("Return %x for speedfreak", (speedfrk_steer[delta_wheel + 3] >> offset) & 1);
	return (speedfrk_steer[delta_wheel + 3] >> offset) & 1;
}

static int speedfrk_gear_r(int offset)
{
	static int gear = 0x0e;
	int gearval = readinputportbytag("GEAR");

	// check the fake gear input port and determine the bit settings for the gear
	if ((gearval & 0x0f) != 0x0f)
		gear = gearval & 0x0f;

	// add the start key into the mix -- note that it overlaps 4th gear
	if (!(readinputportbytag("INPUTS") & 0x80))
		gear &= ~0x08;

	return (gear >> offset) & 1;
}

/*************************************
 *
 *  Sundance inputs
 *
 *************************************/
 // I've never seen anyone create a struct like this before, neat but confusing.

static const struct
{
	const char* portname;
	UINT16 bitmask;
} sundance_port_map[16] =
{
	{"PAD1", 0x155},	/* bit  0 is set if P1 1,3,5,7,9 is pressed */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },

	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },

	{ "PAD2", 0x1a1 },	/* bit  8 is set if P2 1,6,8,9 is pressed */
	{ "PAD1", 0x1a1 },	/* bit  9 is set if P1 1,6,8,9 is pressed */
	{ "PAD2", 0x155 },	/* bit 10 is set if P2 1,3,5,7,9 is pressed */
	{ 0, 0 },

	{ "PAD1", 0x093 },	/* bit 12 is set if P1 1,2,5,8 is pressed */
	{ "PAD2", 0x093 },	/* bit 13 is set if P2 1,2,5,8 is pressed */
	{ "PAD1", 0x048 },	/* bit 14 is set if P1 4,8 is pressed */
	{ "PAD2", 0x048 },	/* bit 15 is set if P2 4,8 is pressed */
};

static int sundance_read(int offset)
{
	// handle special keys first
	if (sundance_port_map[offset].portname)
		return (readinputportbytag(sundance_port_map[offset].portname) & sundance_port_map[offset].bitmask) ? 0 : 1;
	else
		return (readinputport(1) >> offset) & 1;
}

/**************************************
 *
 *  Boxing Bugs inputs
 *
 *************************************/

static int boxingb_dial_r(int offset)
{
	UINT16 value = readinputportbytag("DIAL");
	offset -= 0x0b;

	if (!MUX_VAL) offset += 4;
	return (value >> offset) & 1;
}

/*************************************
 *
 *  General input handlers
 *
 *************************************/

UINT16 get_ccpu_inputs(int offset)
{
	switch (ccpuinput_type)
	{
	case CCPU_INPUTS::CCPUIN:
	{
		return (readinputport(1) >> offset) & 1;
		break;
	}

	case CCPU_INPUTS::BOXINGB: {
		if (offset > 0xb)  return  boxingb_dial_r(offset);
		else
			return (readinputport(1) >> offset) & 1;
		break;
	}

	case CCPU_INPUTS::SUNDANCE:
		return sundance_read(offset);
		break;

	case CCPU_INPUTS::SPEEDFRK: {
		if (offset < 0x04)  return  speedfrk_wheel_r(offset);
		else if (offset > 0x03 && offset < 0x07)  return  speedfrk_gear_r(offset);
		else if (offset > 0x06) return (readinputport(1) >> offset) & 1;
		break;
	}

	default: LOG_INFO("Somehting is wrong with the CCPU Input config.!");
	}

	return 0;
}

UINT16 get_ccpu_switches(int offset)
{
	static const UINT8 switch_shuffle[8] = { 2,5,4,3,0,1,6,7 };
	static int coindown = 0;
	UINT16 test = readinputport(0);

	if (offset == 7)
	{
		if ((test & 0x80) && coindown) { coindown--; } //Clear Countdown
		else if ((test & 0x80) == 0 && coindown) { bitset(test, 0x80); } //Ignore Coin down.
		else if ((test & 0x80) == 0 && coindown == 0) { coindown = 2; } //Set coin countdown
	}

	return (test >> switch_shuffle[offset]) & 1;
}

/*************************************
 *
 *  General output handlers
 *
 *************************************/

void coin_handler(int data)//coin_reset_w
{
	/* on the rising edge of a coin reset, clear the coin_detected flag */
	if (coin_last_reset != data && data != 0)
	{
		coin_detected = 0;
	}
	coin_last_reset = data;
}

void qb3_ram_bank_w(int data)
{


}

/*
static int mux_set(int data) // mux_select_w
{
	mux_select = data;
	LOG_INFO("MUX SELECT");
}
*/

UINT8 joystick_read(void)
{
	int xval = (INT16)(cpunum_get_reg(0, CCPU_X) << 4) >> 4;

	//LOG_INFO("joystick read XVAL %x: MUXVAL %x  ", xval, MUX_VAL);
	//LOG_INFO("Returned value %x",( (readinputportbytag(MUX_VAL ? "IN2" : "IN3") << 4) - xval) < 0x800);

	return ((readinputportbytag(MUX_VAL ? "ANALOGX" : "ANALOGY") << 4) - xval) < 0x800;
}

void run_cinemat(void)
{
	cinevid_update();

}

int init_starhawk()
{
	init_cinemat();
	init_cinemat_snd(starhawk_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_4K);
	return 1;
}

int init_ripoff()
{
	init_cinemat();
	init_cinemat_snd(ripoff_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_8K);
	return 1;
}

int init_solarq()
{
	init_cinemat();
	init_cinemat_snd(solarq_sound);
	video_type_set(COLOR_64LEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_16K);
	return 1;
}

int init_starcas()
{
	init_cinemat();
	init_cinemat_snd(starcas_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_8K);
	return 1;
}

int init_armora()
{
	init_cinemat();
	init_cinemat_snd(armora_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_16K);
	return 1;
}

int init_barrier()
{
	init_cinemat();
	init_cinemat_snd(barrier_sound);
	video_type_set(COLOR_BILEVEL, 1);
	init_ccpu(0, CCPU_MEMSIZE_8K);
	return 1;
}

int init_sundance()
{
	init_cinemat();
	init_cinemat_snd(sundance_sound);
	video_type_set(COLOR_16LEVEL, 1);
	init_ccpu(0, CCPU_MEMSIZE_8K);
	ccpuinput_type = CCPU_INPUTS::SUNDANCE;
	return 1;
}

int init_warrior()
{
	init_cinemat();
	init_cinemat_snd(warrior_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_8K);
	return 1;
}

int init_tailg()
{
	init_cinemat();
	init_cinemat_snd(tailg_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(1, CCPU_MEMSIZE_8K);
	return 1;
}

int init_spacewar()
{
	init_cinemat();
	init_cinemat_snd(spacewar_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(1, CCPU_MEMSIZE_4K);
	return 1;
}

// TODO: Add Sound to Speed Freak
int init_speedfrk()
{
	init_cinemat();
	init_cinemat_snd(speedfrk_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(1, CCPU_MEMSIZE_8K);
	ccpuinput_type = CCPU_INPUTS::SPEEDFRK;
	return 1;
}

int init_demon()
{
	init_cinemat();
	init_cinemat_snd(demon_sound);
	video_type_set(COLOR_BILEVEL, 0);
	init_ccpu(0, CCPU_MEMSIZE_16K);
	return 1;
}

int init_boxingb()
{
	init_cinemat();
	init_cinemat_snd(boxingb_sound);
	video_type_set(COLOR_RGB, 0);
	init_ccpu(0, CCPU_MEMSIZE_32K);
	ccpuinput_type = CCPU_INPUTS::BOXINGB;
	return 1;
}

int init_wotw()
{
	init_cinemat();
	init_cinemat_snd(wotwc_sound);
	video_type_set(COLOR_RGB, 0);
	init_ccpu(0, CCPU_MEMSIZE_16K);
	return 1;
}

int init_qb3()
{
	init_cinemat();
	init_cinemat_snd(wotwc_sound);
	video_type_set(COLOR_QB3, 0);
	init_ccpu(0, CCPU_MEMSIZE_32K);
	return 1;
}


int init_cinemat()
{
	// reset the coin states
	coin_detected = 0;
	coin_last_reset = 0;
	// reset mux select
	mux_select = 0;
	MUX_VAL = 0;
	video_type_set(COLOR_BILEVEL, 0);
	ccpuinput_type = CCPU_INPUTS::CCPUIN;
	return 1;
}

void end_cinemat()
{
	ccpu_reset();
}