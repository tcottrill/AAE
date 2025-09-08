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

#include "tempest.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"    // AAE_REGISTER_DRIVER
#include "aae_avg.h"
#include "aae_pokey.h"
#include "earom.h"
#include "mathbox.h"
#include "timer.h"
#include "glcode.h"

#include "vector_fonts.h"

//
// Tempest Multigame Notes:
// Tempest Multigame is Copyright 1999 Clay Cowgill, and provides a very nice menu system to run
// multiple games on a real tempest arcade machine.
// Tempest Multigame emulation is setup with Aliens Enabled, and the optional “Reset Adapter” is installed.
// Pressing Start 1 and Start 2 together resets the game and reenters the menu
// Note: Aliens requires the Watchdog to be disabled to function.
//
// Regarding the Tempest random number generator protection:
// No work has been done in this code to accurately emulate the Tempest PRNG, so the protection code is patched out
// on all production varations of tempest that are emulated. This results in this emulator not being accurate as to real hardware.
//
// Note for me:
// Add the watchdog reset to this (ALL ATARI) code, fix the config menu colors on here,
// fix the menu not coming up correctly when reset and a default game is selected with the spiker
//

// From M.A.M.E. (TM)
// license:BSD-3-Clause
// copyright-holders:Brad Oliver, Bernd Wiebelt, Allard van der Bas
/***************************************************************************

	Atari Tempest hardware

	Games supported:
		* Tempest [5 sets]
		* Tempest Tubes

	Known bugs:
		* none at this time

****************************************************************************

	TEMPEST
	-------
	HEX        R/W   D7 D6 D5 D4 D3 D2 D2 D0  function
	0000-07FF  R/W   D  D  D  D  D  D  D  D   program ram (2K)
	0800-080F   W                D  D  D  D   Colour ram

	0C00        R                         D   Right coin sw
	0C00        R                      D      Center coin sw
	0C00        R                   D         Left coin sw
	0C00        R                D            Slam sw
	0C00        R             D               Self test sw
	0C00        R          D                  Diagnostic step sw
	0C00        R       D                     Halt
	0C00        R    D                        3kHz ??
	0D00        R    D  D  D  D  D  D  D  D   option switches
	0E00        R    D  D  D  D  D  D  D  D   option switches

	2000-2FFF  R/W   D  D  D  D  D  D  D  D   Vector Ram (4K)
	3000-3FFF   R    D  D  D  D  D  D  D  D   Vector Rom (4K)

	4000        W                         D   Right coin counter
	4000        W                      D      left  coin counter
	4000        W                D            Video invert - x
	4000        W             D               Video invert - y
	4800        W                             Vector generator GO

	5000        W                             WD clear
	5800        W                             Vect gen reset

	6000-603F   W    D  D  D  D  D  D  D  D   EAROM write
	6040        W    D  D  D  D  D  D  D  D   EAROM control
	6040        R    D                        Mathbox status
	6050        R    D  D  D  D  D  D  D  D   EAROM read

	6060        R    D  D  D  D  D  D  D  D   Mathbox read
	6070        R    D  D  D  D  D  D  D  D   Mathbox read
	6080-609F   W    D  D  D  D  D  D  D  D   Mathbox start

	60C0-60CF  R/W   D  D  D  D  D  D  D  D   Custom audio chip 1
	60D0-60DF  R/W   D  D  D  D  D  D  D  D   Custom audio chip 2

	60E0        R                         D   one player start LED
	60E0        R                      D      two player start LED
	60E0        R                   D         FLIP

	9000-DFFF  R     D  D  D  D  D  D  D  D   Program ROM (20K)

	notes: program ram decode may be incorrect, but it appears like
	this on the schematics, and the troubleshooting guide.

	ZAP1,FIRE1,FIRE2,ZAP2 go to pokey2 , bits 3,and 4
	(depending on state of FLIP)
	player1 start, player2 start are pokey2 , bits 5 and 6

	encoder wheel goes to pokey1 bits 0-3
	pokey1, bit4 is cocktail detect

	TEMPEST SWITCH SETTINGS (Atari, 1980)
	-------------------------------------

	GAME OPTIONS:
	(8-position switch at L12 on Analog Vector-Generator PCB)

	1   2   3   4   5   6   7   8   Meaning
	-------------------------------------------------------------------------
	Off Off                         2 lives per game
	On  On                          3 lives per game
	On  Off                         4 lives per game
	Off On                          5 lives per game
			On  On  Off             Bonus life every 10000 pts
			On  On  On              Bonus life every 20000 pts
			On  Off On              Bonus life every 30000 pts
			On  Off Off             Bonus life every 40000 pts
			Off On  On              Bonus life every 50000 pts
			Off On  Off             Bonus life every 60000 pts
			Off Off On              Bonus life every 70000 pts
			Off Off Off             No bonus lives
						On  On      English
						On  Off     French
						Off On      German
						Off Off     Spanish
								On  1-credit minimum
								Off 2-credit minimum

	GAME OPTIONS:
	(4-position switch at D/E2 on Math Box PCB)

	1   2   3   4                   Meaning
	-------------------------------------------------------------------------
		Off                         Minimum rating range: 1, 3, 5, 7, 9
		On                          Minimum rating range tied to high score
			Off Off                 Medium difficulty (see notes)
			Off On                  Easy difficulty (see notes)
			On  Off                 Hard difficulty (see notes)
			On  On                  Medium difficulty (see notes)

	PRICING OPTIONS:
	(8-position switch at N13 on Analog Vector-Generator PCB)

	1   2   3   4   5   6   7   8   Meaning
	-------------------------------------------------------------------------
	On  On  On                      No bonus coins
	On  On  Off                     For every 2 coins, game adds 1 more coin
	On  Off On                      For every 4 coins, game adds 1 more coin
	On  Off Off                     For every 4 coins, game adds 2 more coins
	Off On  On                      For every 5 coins, game adds 1 more coin
	Off On  Off                     For every 3 coins, game adds 1 more coin
	On  Off                 Off On  Demonstration Mode (see notes)
	Off Off                 Off On  Demonstration-Freeze Mode (see notes)
				On                  Left coin mech * 1
				Off                 Left coin mech * 2
					On  On          Right coin mech * 1
					On  Off         Right coin mech * 4
					Off On          Right coin mech * 5
					Off Off         Right coin mech * 6
							Off On  Free Play
							Off Off 1 coin 2 plays
							On  On  1 coin 1 play
							On  Off 2 coins 1 play

	GAME SETTING NOTES:
	-------------------

	Demonstration Mode:
	- Plays a normal game of Tempest, but pressing SUPERZAP sends you
	  directly to the next level.

	Demonstration-Freeze Mode:
	- Just like Demonstration Mode, but with frozen screen action.

	Both Demonstration Modes:
	- Pressing RESET in either mode will cause the game to lock up.
	  To recover, set switch 1 to On.
	- You can start at any level from 1..81, so it's an easy way of
	  seeing what the game can throw at you
	- The score is zeroed at the end of the game, so you also don't
	  have to worry about artificially high scores disrupting your
	  scoring records as stored in the game's EAROM.

	Easy Difficulty:
	- Enemies move more slowly
	- One less enemy shot on the screen at any given time

	Hard Difficulty:
	- Enemies move more quickly
	- 1-4 more enemy shots on the screen at any given time
	- One more enemy may be on the screen at any given time

	High Scores:
	- Changing toggles 1-5 at L12 (more/fewer lives, bonus ship levels)
	  will erase the high score table.
	- You should also wait 8-10 seconds after a game has been played
	  before entering self-test mode or powering down; otherwise, you
	  might erase or corrupt the high score table.

-----------------------------------------

Atari Bulletin, December 4, 1981

Tempest Program Bug

Tempest Uprights Prior to Serial #17426

  If the score on your Tempest(tm) is greater then 170,000, there
is a 12% chance that a program bug may award 40 credits for one
quarter.

  The ROM (#136002-217) in this package, replaces the ROM in
location J-1 on the main PCB and will correct the problem.

  All cabaret and cocktail cabinets will have the correct ROM
Installed.

Thank you,

Fred McCord
Field Service Manager

Tech Tip, December 11, 1981

Tempest(tm) ROM #136002-117

We have found that the above part number in location J1 should be replaced with part
number 136002-217 in the main board in order to eliminate the possibility of receiving
extra bonus plays after 170,000 points.

RMA# T1700

Exchange offer expires on March 15, 1982

-----------------------------------------

Skill-Step(tm) feature of your new Tempest(tm) game:

I. Player rating mode

  1. Occurs at beginning of every game
  2. Player is given 10 seconds to choose his starting level
  3. Player may choose from those levels displayed at bottom of screen
  4. Player chooses level by:
   a. spinning knob until white box surrounds desired level and then
   b. pressing fire or superzapper (or start 1 or start 2 if remaining
	  time is less then 8 seconds), or by waiting until timer expires
  5. Player is given a 3 second warning
  6. Level choices are determined by several factors
   a. If the game has been idle for 1 or more attract mode cycles
	  since the last game, then the choices are levels 1,3,5,7,9
   b. If a player has just finished a game and pressed start before
	  the attract mode has finished its play mode, then the choices
	  depend on the highest level reached in that previous game as
	  follows

Highest level
reached in
last game            Level choices this game
-------------        -----------------------
1 through 11         1,3,5,7,9
12 or 13             1,3,5,7,9,11
14 or 15             1,3,5,7,9,11,13
16 or 17             1,3,5,7,9,11,13,15
18, 19 or 20         1,3,5,7,9,11,13,15,17
21 or 22             1,3,5,7,9,11,13,15,17,20
23 or 24             1,3,5,7,9,11,13,15,17,20,22
25 or 26             1,3,5,7,9,11,13,15,17,20,22,24
27 or 28             1,3,5,7,9,11,13,15,17,20,22,24,26
29, 30 or 31         1,3,5,7,9,11,13,15,17,20,22,24,26,28
32 or 33             1,3,5,7,9,11,13,15,17,20,22,24,26,28,31
34, 35 or 36         1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33
37,38,39,40          1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36
41,42,43,44          1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40
45, 46 or 47         1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44
48 or 49             1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47
50, 51 or 52         1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49
53,54,55,56          1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52
57,58,59,60          1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56
61, 62 or 63         1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56,60
64 or 65             1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56,60,63
66 through 73        1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56,60,63,65
74 through 81        1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56,60,63,65,73
82 through 99        1,3,5,7,9,11,13,15,17,20,22,24,26,28,31,33,36,40,44,47,49,52,56,60,63,65,73,81

Rom revisions and bug fixes / effects:

Revision 2
136002-217.j1  Fixes the score cheat (see tech notes above)
136002-222.r1  In test mode, changes spinner letters to a line, described in Tempest manual C0-190-01 New Roms

Revision 3
136002-316.h1  Fixes screen collapse between players when using newer deflection board

136002-134.f1  Contains the 136002-316.h1 code fix even though it's listed as a rev 1 rom

Note: Roms for Tempest Analog Vector-Generator PCB Assembly A037383-03 or A037383-04 are twice
	  the size as those for Tempest Analog Vector-Generator PCB Assembly A037383-01 or A037383-02

***************************************************************************/

ART_START(tempestart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

static int flipscreen = 0;
static int INMENU = 0;
static int tempprot = 1;
static char* tbuffer = nullptr;

void tempest_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	1512000,
	100,	/* volume */
	6, //POKEY_DEFAULT_GAIN/2
	USE_CLIP,
	/* The 8 pot handlers */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	/* The allpot handler */
	{ input_port_1_r, input_port_2_r },
};

void tempm_reset()
{
	unsigned char* RAM = Machine->memory_region[CPU0];
	memcpy(RAM, tbuffer, 0x10000);
	cpu_reset(CPU0);
	INMENU = 1;
}

static void switch_game()
{
	int a = 0;
	int b = 0;

	if (INMENU == 0) { return; }

	a = (Machine->memory_region[CPU0][0x51]) + 1;
	//	LOG_INFO("A here is %d", a);
	switch (a)
	{
	case 1: b = 0x10000;  break;
	case 2: b = 0x20000;  break;
	case 3: b = 0x30000;  break;
	case 4: b = 0x40000;  break;
	case 5: b = 0x50000;  break;
	case 6: b = 0x60000;  break;
	case 7: b = 0x70000;  break;
	default: LOG_INFO("Tempest Multigame - unhandled game number?");
	}

	setup_video_config();
	INMENU = 0;

	memset(&Machine->memory_region[REGION_CPU1], 0x10000, 0);
	unsigned char* RAM = Machine->memory_region[CPU0];
	memcpy(RAM, Machine->memory_region[REGION_CPU1] + b, 0x10000);

	cache_clear();
	cpu_reset(CPU0);
}

READ_HANDLER(pokey_2_tempest_read)
{
	int val = Read_pokey_regs(address, 1);

	if ((val & 0x10))  //Fire
	{
		switch_game();
	}

	if (((val & 0x20) && val & 0x40)) // Start 1 and Start 2 together
	{
		if (val != 0xe7) tempm_reset();
	}
	return val;
}

READ_HANDLER(TempestIN0read)
{
	int res;

	res = readinputportbytag("IN0");

	if (get_eterna_ticks(0) & 0x100) //3Khz clock
		res |= 0x80;

	if (avg_check()) res |= 0x40;

	return res;
}

WRITE_HANDLER(tempest_led_w)
{
	set_led_status(0, ~data & 0x02);
	set_led_status(1, ~data & 0x01);
	/* FLIP is bit 0x04 */
}

WRITE_HANDLER(colorram_w)
{
	int bit3 = (~data >> 3) & 1;
	int bit2 = (~data >> 2) & 1;
	int bit1 = (~data >> 1) & 1;
	int bit0 = (~data >> 0) & 1;
	int r = bit1 * 0xee + bit0 * 0x11;
	int g = bit3 * 0xee;
	int b = bit2 * 0xee;

	//Update the color ram.
	vec_colors[address].r = r;
	vec_colors[address].g = g;
	vec_colors[address].b = b;
	Machine->memory_region[CPU0][address + 0x800] = data;
}

WRITE_HANDLER(avg_reset_w)
{
	avg_clear();
}//AVGRESET

WRITE_HANDLER(coin_write)
{
	if ((data & 0x08)) { flipscreen = 1; }
	else { flipscreen = 0; }
}

//////////////////////////////////////////////////////////////////////////

MEM_READ(TempestMenuRead)
MEM_ADDR(0x0c00, 0x0c00, TempestIN0read)
MEM_ADDR(0x0d00, 0x0d00, ip_port_3_r)
MEM_ADDR(0x0e00, 0x0e00, ip_port_4_r)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_r)
MEM_ADDR(0x60d0, 0x60df, pokey_2_tempest_read)
MEM_ADDR(0x6040, 0x6040, MathboxStatusRead)
MEM_ADDR(0x6050, 0x6050, EaromRead)
MEM_ADDR(0x6060, 0x6060, MathboxLowbitRead)
MEM_ADDR(0x6070, 0x6070, MathboxHighbitRead)
MEM_END

MEM_WRITE(TempestMenuWrite)
MEM_ADDR(0x0800, 0x080f, colorram_w)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
MEM_ADDR(0x6080, 0x609f, MathboxGo)
MEM_ADDR(0x4000, 0x4000, coin_write)
MEM_ADDR(0x4800, 0x4800, advdvg_go_w)
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)
MEM_ADDR(0x6000, 0x603f, EaromWrite)
MEM_ADDR(0x6040, 0x6040, EaromCtrl)
MEM_ADDR(0x5000, 0x5000, watchdog_reset_w)
MEM_ADDR(0x5800, 0x5800, avg_reset_w)
MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
MEM_ADDR(0x9000, 0xffff, MWA_ROM)
MEM_ADDR(0x3000, 0x57ff, MWA_ROM)
MEM_END

MEM_READ(TempestRead)
MEM_ADDR(0x0000, 0x07ff, MRA_RAM)
MEM_ADDR(0x0c00, 0x0c00, TempestIN0read)
MEM_ADDR(0x0d00, 0x0d00, ip_port_3_r)
MEM_ADDR(0x0e00, 0x0e00, ip_port_4_r)
MEM_ADDR(0x2000, 0x2fff, MRA_RAM)
MEM_ADDR(0x3000, 0x3fff, MRA_ROM)
MEM_ADDR(0x6040, 0x6040, MathboxStatusRead)
MEM_ADDR(0x6050, 0x6050, EaromRead)
MEM_ADDR(0x6060, 0x6060, MathboxLowbitRead)
MEM_ADDR(0x6070, 0x6070, MathboxHighbitRead)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_r)
MEM_ADDR(0x60d0, 0x60df, pokey_2_r)
MEM_ADDR(0x9000, 0xdfff, MRA_ROM)
MEM_ADDR(0xf000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(TempestWrite)
MEM_ADDR(0x0000, 0x07ff, MWA_RAM)
MEM_ADDR(0x0800, 0x080f, colorram_w)
MEM_ADDR(0x2000, 0x2fff, MWA_RAM)
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x4000, coin_write)
MEM_ADDR(0x4800, 0x4800, advdvg_go_w)
MEM_ADDR(0x5000, 0x5000, watchdog_reset_w)
MEM_ADDR(0x5800, 0x5800, avg_reset_w)
MEM_ADDR(0x6000, 0x603f, EaromWrite)
MEM_ADDR(0x6040, 0x6040, EaromCtrl)
MEM_ADDR(0x6080, 0x609f, MathboxGo)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
MEM_ADDR(0x9000, 0xffff, MWA_ROM)
MEM_END

void run_tempest()
{
	watchdog_reset_w(0, 0, 0); // Required for protos.I should set this up so it is just here for those. 
	pokey_sh_update();
}

int init_tempestm()
{
	//init6502(TempestMenuRead, TempestMenuWrite, 0xffff, CPU0);

	cache_clear();

	LOG_INFO("TEMPMG INIT CALLED");
	tbuffer = (char*)malloc(0x10000);
	memcpy(tbuffer, Machine->memory_region[REGION_CPU1], 0x10000);
	INMENU = 1;

	pokey_sh_start(&pokey_interface);
	avg_start_tempest();

	return 0;
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_tempest(void)
{
	pokey_sh_start(&pokey_interface);
	//init6502(TempestRead, TempestWrite, 0xffff, CPU0);
	if (config.hack) 
	{ 
		//LEVEL SELECTION HACK   (Does NOT Work on Protos)
		Machine->memory_region[CPU0][0x9001] = 0xd1;
		Machine->memory_region[CPU0][0x90cd] = 0xea; 
		Machine->memory_region[CPU0][0x90ce] = 0xea; 
	}
	
	avg_start_tempest();
	//timer_set(TIME_IN_HZ(240), CPU0, tempest_interrupt);
	return 0;
}

int init_vbrakout(void)
{
	pokey_sh_start(&pokey_interface);
	//init6502(TempestRead, TempestWrite, 0xffff, CPU0);
	avg_start_tempest();
  //timer_set(TIME_IN_HZ(240), CPU0, tempest_interrupt);
	return 0;

}


void end_tempest()
{
	pokey_sh_stop();
}


INPUT_PORTS_START(tempest)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_TILT)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
/* bit 6 is the VG HALT bit. We set it to "low" */
/* per default (busy vector processor). */
/* handled by tempest_IN0_r() */
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* bit 7 is tied to a 3khz (?) clock */
/* handled by tempest_IN0_r() */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN1")	/* IN1/DSW0 */
/* This is the Tempest spinner input. It only uses 4 bits. */
PORT_ANALOG(0x0f, 0x00, IPT_DIAL | IPF_REVERSE, 25, 20, 0, 0, 0)
/* The next one is reponsible for cocktail mode.
 * According to the documentation, this is not a switch, although
 * it may have been planned to put it on the Math Box PCB, D/E2 )
 */
PORT_DIPNAME(0x10, 0x10, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN2")	/* IN2 */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x01, "Easy")
PORT_DIPSETTING(0x00, "Medium1")
PORT_DIPSETTING(0x03, "Medium2")
PORT_DIPSETTING(0x02, "Hard")
PORT_DIPNAME(0x04, 0x00, "Rating")
PORT_DIPSETTING(0x00, "1, 3, 5, 7, 9")
PORT_DIPSETTING(0x04, "tied to high score")
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("DS1")	/* DSW1 - (N13 on analog vector generator PCB */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_2C))
PORT_DIPSETTING(0x02, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, "Left Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0xe0, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x80, "1 each 5")
PORT_DIPSETTING(0x40, "1 each 4 (+Demo)")
PORT_DIPSETTING(0xa0, "1 each 3")
PORT_DIPSETTING(0x60, "2 each 4 (+Demo)")
PORT_DIPSETTING(0x20, "1 each 2")
PORT_DIPSETTING(0xc0, "Freeze Mode")
PORT_DIPSETTING(0xe0, "Freeze Mode")

PORT_START("DS2")	/* DSW2 - (L12 on analog vector generator PCB */
PORT_DIPNAME(0x01, 0x00, "Minimum")
PORT_DIPSETTING(0x00, "1 Credit")
PORT_DIPSETTING(0x01, "2 Credit")
PORT_DIPNAME(0x06, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x04, "German")
PORT_DIPSETTING(0x06, "Spanish")
PORT_DIPNAME(0x38, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x08, "10000")
PORT_DIPSETTING(0x00, "20000")
PORT_DIPSETTING(0x10, "30000")
PORT_DIPSETTING(0x18, "40000")
PORT_DIPSETTING(0x20, "50000")
PORT_DIPSETTING(0x28, "60000")
PORT_DIPSETTING(0x30, "70000")
PORT_DIPSETTING(0x38, "None")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0xc0, "2")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x40, "4")
PORT_DIPSETTING(0x80, "5")
INPUT_PORTS_END



ROM_START(tempestm)
ROM_REGION(0x90000, REGION_CPU1, 0)
ROM_LOAD("menu_D1.bin", 0x9000, 0x0800, CRC(8a6633fb) SHA1(b143a5d2019f24666b350b40b0dab2924bb9c7c0))
ROM_LOAD("menu_E1.bin", 0x9800, 0x0800, CRC(2eedfdf6) SHA1(2ed494bd8610bebd07284289ca8b7059fd805300))
ROM_LOAD("menu_F1.bin", 0xa000, 0x0800, CRC(12f62746) SHA1(37356b5738c27ffe4c38f1b6cf99ae21441d8e8e))
ROM_LOAD("136002.113", 0xa800, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("136002.114", 0xb000, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("136002.115", 0xb800, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("136002.116", 0xc000, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("136002.117", 0xc800, 0x0800, CRC(55952119) SHA1(470d914fa52fce3786cb6330889876d3547dca65))
ROM_LOAD("136002.118", 0xd000, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("menu_R1.bin", 0xd800, 0x0800, CRC(1d8f194a) SHA1(c77f6b83f5c498c0f2d5372089a4604913a4aad5))
ROM_RELOAD(0xf800, 0x0800)
ROM_LOAD("menu_N3.bin", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("menu_R3.bin", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_LOAD("alienst/aliens_d1.bin", 0x19000, 0x0800, CRC(337e21f6) SHA1(7adadeaa975e22f0b20e8f1fb6ad68b5c3934133))
ROM_LOAD("alienst/aliens_e1.bin", 0x19800, 0x0800, CRC(337e21f6) SHA1(7adadeaa975e22f0b20e8f1fb6ad68b5c3934133))
ROM_LOAD("alienst/aliens_f1.bin", 0x1a000, 0x0800, CRC(4d2aabb0) SHA1(31106a1fc22d2a19866f07b8d6c6f4bf76007909))
ROM_LOAD("alienst/aliens_h1.bin", 0x1a800, 0x0800, CRC(a503f54a) SHA1(91ebf9f69a183a04a5bf55fcdd9e191523bb66bb))
ROM_LOAD("alienst/aliens_j1.bin", 0x1b000, 0x0800, CRC(5487d531) SHA1(c95f037151b824345af03f27a6c3c7eb8a899b2c))
ROM_LOAD("alienst/aliens_k1.bin", 0x1b800, 0x0800, CRC(ac96e87) SHA1(37461e84e6f46516c25dbf4ddb2ffd65877445c0))
ROM_LOAD("alienst/aliens_l1.bin", 0x1c000, 0x0800, CRC(cd246ac2) SHA1(de2e6fe2e72c092c3874e797fc302a71dbf57710))
ROM_LOAD("alienst/aliens_n1.bin", 0x1c800, 0x0800, CRC(bd98c5f3) SHA1(268487d9cf46b4b7b49eab7420d078bf676e636c))
ROM_LOAD("alienst/aliens_p1.bin", 0x1d000, 0x0800, CRC(7c10adbd) SHA1(38579128a90bff4a7a4ae46d6aaa42118b8bc218))
ROM_LOAD("alienst/aliens_r1.bin", 0x1d800, 0x0800, CRC(555c3070) SHA1(032f03af23c7ccac8a2bf50c3c646e141921ffee))
ROM_RELOAD(0x1f800, 0x0800)
ROM_LOAD("alienst/aliens_n3.bin", 0x13000, 0x0800, CRC(5c8fd38b) SHA1(bb0d6bd062eba53b5d64b3f444d5ce0a34728bf5))
ROM_LOAD("alienst/aliens_r3.bin", 0x13800, 0x0800, CRC(6cabcd08) SHA1(e3950de50f3dfbc4d4d2f4fe26625d8ef94c0819))
ROM_LOAD("vbreak/vb_d1.bin", 0x29000, 0x0800, CRC(6fd3efe5) SHA1(d195d08984ad8797607bc1989e8a606d51547c68))
ROM_LOAD("vbreak/vb_e1.bin", 0x29800, 0x0800, CRC(9974b9a5) SHA1(6ecc6f72070895bb15992977348f58835233911f))
ROM_LOAD("vbreak/vb_f1.bin", 0x2a000, 0x0800, CRC(44d611d8) SHA1(82cd63fc9067ea1f00feeffbee66e7d750cab7e5))
ROM_LOAD("vbreak/vb_h1.bin", 0x2a800, 0x0800, CRC(cd58fc11) SHA1(060e31e55183ccef67a1adc91fb48c22424a4ba5))
ROM_LOAD("vbreak/136002.114", 0x2b000, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("vbreak/136002.115", 0x2b800, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("vbreak/136002.116", 0x2c000, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("vbreak/136002.117", 0x2c800, 0x0800, CRC(55952119) SHA1(470d914fa52fce3786cb6330889876d3547dca65))
ROM_LOAD("vbreak/136002.118", 0x2d000, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("vbreak/vb_r1.bin", 0x2d800, 0x0800, CRC(1ae2dd53) SHA1(b908ba6b59195aea853380a56a243aa8fa2fba71))
ROM_RELOAD(0x2f800, 0x0800)
ROM_LOAD("vbreak/vb_n3.bin", 0x23000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("vbreak/vb_r3.bin", 0x23800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_LOAD("vortex/d1.bin", 0x39000, 0x0800, CRC(3aff3417) SHA1(3b7c31f01b7467757ec85e98a17038e5df5720bb))
ROM_LOAD("vortex/e1.bin", 0x39800, 0x0800, CRC(11861be3) SHA1(a35797c649e8286c844cee6dac86ac50f4fbd669))
ROM_LOAD("vortex/f1.bin", 0x3a000, 0x0800, CRC(1d251111) SHA1(2912a21dc708231e28d6164e54e593a8300b9c4a))
ROM_LOAD("vortex/h1.bin", 0x3a800, 0x0800, CRC(937a9859) SHA1(336b25291533d19294f1ced730bbf20971849adf))
ROM_LOAD("vortex/j1.bin", 0x3b000, 0x0800, CRC(79481246) SHA1(c5362670fd29ef1432f8e626323da395d6e8a675))
ROM_LOAD("vortex/k1.bin", 0x3b800, 0x0800, CRC(390f872a) SHA1(c5463ea2d2307e21c941b5b459e3652c12154609))
ROM_LOAD("vortex/lm1.bin", 0x3c000, 0x0800, CRC(515760dd) SHA1(773f06c9a64e72f9d3d8a5c622bf3ec2b4ba678d))
ROM_LOAD("vortex/mn1.bin", 0x3c800, 0x0800, CRC(c6c41c68) SHA1(9323c07fc80a947142dde008c53f5e8c0b0c572d))
ROM_LOAD("vortex/p1.bin", 0x3d000, 0x0800, CRC(3c2ff130) SHA1(32ebabcb2cbd7aab5e29de2b873f02ed78776ae6))
ROM_LOAD("vortex/r1.bin", 0x3d800, 0x0800, CRC(67cafbb1) SHA1(467515733d843398e6fe29661002536a1e6c8fc9))
ROM_RELOAD(0x3f800, 0x0800)
ROM_LOAD("vortex/n3.bin", 0x33000, 0x0800, CRC(29c6a1cb) SHA1(290702a1c0942a68e288b37963e51eba02177a3f))
ROM_LOAD("vortex/r3.bin", 0x33800, 0x0800, CRC(7fbe5e21) SHA1(e5de6c3af82e64444b0ddcda559e9cb4fbf6c1da))
ROM_LOAD("temptube/136002-113.d1", 0x49000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("temptube/136002-114.e1", 0x49800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("temptube/136002-115.f1", 0x4a000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("temptube/136002-316.h1", 0x4a800, 0x0800, CRC(aeb0f7e9) SHA1(a5cc25015b98692673cfc1c7c2e9634efd750870))
ROM_LOAD("temptube/136002-217.j1", 0x4b000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
ROM_LOAD("temptube/tube-118.k1", 0x4b800, 0x0800, CRC(cefb03f0) SHA1(41ddfa4991fa49a31d4740a04551556acca66196))
ROM_LOAD("temptube/136002-119.lm1", 0x4c000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("temptube/136002-120.mn1", 0x4c800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("temptube/136002-121.p1", 0x4d000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("temptube/136002-222.r1", 0x4d800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
ROM_RELOAD(0x4f800, 0x0800)
ROM_LOAD("temptube/136002-123.np3", 0x43000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("temptube/136002-124.r3", 0x43800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_LOAD("tempest1/136002-113.d1", 0x59000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("tempest1/136002-114.e1", 0x59800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("tempest1/136002-115.f1", 0x5a000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("tempest1/136002-116.h1", 0x5a800, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("tempest1/136002-117.j1", 0x5b000, 0x0800, CRC(55952119) SHA1(470d914fa52fce3786cb6330889876d3547dca65))
ROM_LOAD("tempest1/136002-118.k1", 0x5b800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("tempest1/136002-119.lm1", 0x5c000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("tempest1/136002-120.mn1", 0x5c800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("tempest1/136002-121.p1", 0x5d000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("tempest1/136002-122.r1", 0x5d800, 0x0800, CRC(796a9918) SHA1(c862a0d4ea330161e4c3cc8e5e9ad38893fffbd4))
ROM_RELOAD(0x5f800, 0x0800)
ROM_LOAD("tempest1/136002-123.np3", 0x53000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("tempest1/136002-124.r3", 0x53800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_LOAD("tempest2/136002-113.d1", 0x69000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("tempest2/136002-114.e1", 0x69800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("tempest2/136002-115.f1", 0x6a000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("tempest2/136002-116.h1", 0x6a800, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("tempest2/136002-217.j1", 0x6b000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
ROM_LOAD("tempest2/136002-118.k1", 0x6b800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("tempest2/136002-119.lm1", 0x6c000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("tempest2/136002-120.mn1", 0x6c800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("tempest2/136002-121.p1", 0x6d000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("tempest2/136002-222.r1", 0x6d800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
ROM_RELOAD(0x6f800, 0x0800)
ROM_LOAD("tempest2/136002-123.np3", 0x63000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("tempest2/136002-124.r3", 0x63800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_LOAD("tempest/136002-113.d1", 0x79000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("tempest/136002-114.e1", 0x79800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("tempest/136002-115.f1", 0x7a000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("tempest/136002-316.h1", 0x7a800, 0x0800, CRC(aeb0f7e9) SHA1(a5cc25015b98692673cfc1c7c2e9634efd750870))
ROM_LOAD("tempest/136002-217.j1", 0x7b000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
ROM_LOAD("tempest/136002-118.k1", 0x7b800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("tempest/136002-119.lm1", 0x7c000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("tempest/136002-120.mn1", 0x7c800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("tempest/136002-121.p1", 0x7d000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("tempest/136002-222.r1", 0x7d800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
ROM_RELOAD(0x7f800, 0x0800)
ROM_LOAD("tempest/136002-123.np3", 0x73000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("tempest/136002-124.r3", 0x73800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
ROM_END


ROM_START(aliensv) //TEMPEST PROTO
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("aliens_d1.bin", 0x9000, 0x0800, CRC(337e21f6) SHA1(7adadeaa975e22f0b20e8f1fb6ad68b5c3934133))
ROM_LOAD("aliens_e1.bin", 0x9800, 0x0800, CRC(337e21f6) SHA1(7adadeaa975e22f0b20e8f1fb6ad68b5c3934133))
ROM_LOAD("aliens_f1.bin", 0xa000, 0x0800, CRC(4d2aabb0) SHA1(31106a1fc22d2a19866f07b8d6c6f4bf76007909))
ROM_LOAD("aliens_h1.bin", 0xa800, 0x0800, CRC(a503f54a) SHA1(91ebf9f69a183a04a5bf55fcdd9e191523bb66bb))
ROM_LOAD("aliens_j1.bin", 0xb000, 0x0800, CRC(5487d531) SHA1(c95f037151b824345af03f27a6c3c7eb8a899b2c))
ROM_LOAD("aliens_k1.bin", 0xb800, 0x0800, CRC(ac96e87) SHA1(37461e84e6f46516c25dbf4ddb2ffd65877445c0))
ROM_LOAD("aliens_l1.bin", 0xc000, 0x0800, CRC(cd246ac2) SHA1(de2e6fe2e72c092c3874e797fc302a71dbf57710))
ROM_LOAD("aliens_n1.bin", 0xc800, 0x0800, CRC(bd98c5f3) SHA1(268487d9cf46b4b7b49eab7420d078bf676e636c))
ROM_LOAD("aliens_p1.bin", 0xd000, 0x0800, CRC(7c10adbd) SHA1(38579128a90bff4a7a4ae46d6aaa42118b8bc218))
ROM_LOAD("aliens_r1.bin", 0xd800, 0x0800, CRC(555c3070) SHA1(032f03af23c7ccac8a2bf50c3c646e141921ffee))
ROM_RELOAD(0xf800, 0x0800)
ROM_LOAD("aliens_n3.bin", 0x3000, 0x0800, CRC(5c8fd38b) SHA1(bb0d6bd062eba53b5d64b3f444d5ce0a34728bf5))
ROM_LOAD("aliens_r3.bin", 0x3800, 0x0800, CRC(6cabcd08) SHA1(e3950de50f3dfbc4d4d2f4fe26625d8ef94c0819))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(vortex)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("d1.bin", 0x9000, 0x0800, CRC(3aff3417) SHA1(3b7c31f01b7467757ec85e98a17038e5df5720bb))
ROM_LOAD("e1.bin", 0x9800, 0x0800, CRC(11861be3) SHA1(a35797c649e8286c844cee6dac86ac50f4fbd669))
ROM_LOAD("f1.bin", 0xa000, 0x0800, CRC(1d251111) SHA1(2912a21dc708231e28d6164e54e593a8300b9c4a))
ROM_LOAD("h1.bin", 0xa800, 0x0800, CRC(937a9859) SHA1(336b25291533d19294f1ced730bbf20971849adf))
ROM_LOAD("j1.bin", 0xb000, 0x0800, CRC(79481246) SHA1(c5362670fd29ef1432f8e626323da395d6e8a675))
ROM_LOAD("k1.bin", 0xb800, 0x0800, CRC(390f872a) SHA1(c5463ea2d2307e21c941b5b459e3652c12154609))
ROM_LOAD("lm1.bin", 0xc000, 0x0800, CRC(515760dd) SHA1(773f06c9a64e72f9d3d8a5c622bf3ec2b4ba678d))
ROM_LOAD("mn1.bin", 0xc800, 0x0800, CRC(c6c41c68) SHA1(9323c07fc80a947142dde008c53f5e8c0b0c572d))
ROM_LOAD("p1.bin", 0xd000, 0x0800, CRC(3c2ff130) SHA1(32ebabcb2cbd7aab5e29de2b873f02ed78776ae6))
ROM_LOAD("r1.bin", 0xd800, 0x0800, CRC(67cafbb1) SHA1(467515733d843398e6fe29661002536a1e6c8fc9))
ROM_RELOAD(0xf800, 0x0800)
ROM_LOAD("n3.bin", 0x3000, 0x0800, CRC(29c6a1cb) SHA1(290702a1c0942a68e288b37963e51eba02177a3f))
ROM_LOAD("r3.bin", 0x3800, 0x0800, CRC(7fbe5e21) SHA1(e5de6c3af82e64444b0ddcda559e9cb4fbf6c1da))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END


ROM_START(vbrakout)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("vbrakout.113", 0x9000, 0x0800, CRC(6fd3efe5) SHA1(d195d08984ad8797607bc1989e8a606d51547c68))
ROM_LOAD("vbrakout.114", 0x9800, 0x0800, CRC(9974b9a5) SHA1(6ecc6f72070895bb15992977348f58835233911f))
ROM_LOAD("vbrakout.115", 0xa000, 0x0800, CRC(44d611d8) SHA1(82cd63fc9067ea1f00feeffbee66e7d750cab7e5))
ROM_LOAD("vbrakout.116", 0xa800, 0x0800, CRC(cd58fc11) SHA1(060e31e55183ccef67a1adc91fb48c22424a4ba5))
ROM_LOAD("vbrakout.122", 0xd800, 0x0800, CRC(1ae2dd53) SHA1(b908ba6b59195aea853380a56a243aa8fa2fba71))
ROM_RELOAD(0xf800, 0x0800)
ROM_LOAD("136002-123.np3", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
ROM_LOAD("136002-124.r3", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(temptube)//TEMPEST TUBES
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136002-113.d1", 0x9000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("136002-114.e1", 0x9800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("136002-115.f1", 0xa000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("136002-316.h1", 0xa800, 0x0800, CRC(aeb0f7e9) SHA1(a5cc25015b98692673cfc1c7c2e9634efd750870))
ROM_LOAD("136002-217.j1", 0xb000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
ROM_LOAD("tube-118.k1", 0xb800, 0x0800, CRC(cefb03f0) SHA1(41ddfa4991fa49a31d4740a04551556acca66196))
ROM_LOAD("136002-119.lm1", 0xc000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("136002-120.mn1", 0xc800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("136002-121.p1", 0xd000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("136002-222.r1", 0xd800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
ROM_RELOAD(0xf800, 0x0800) /* for reset/interrupt vectors */// Vector ROM
ROM_LOAD("136002-123.np3", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e)) /* May be labeled "136002-111", same data */
ROM_LOAD("136002-124.r3", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06)) /* May be labeled "136002-112", same data */
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

///////////////////////////////////////////////////////////////////////////////////////////////////////////
ROM_START(tempest) // rev 3
ROM_REGION(0x10000, REGION_CPU1, 0)

ROM_LOAD("136002-133.d1", 0x9000, 0x1000, CRC(1d0cc503) SHA1(7bef95db9b1102d6b1166bda0ccb276ef4cc3764)) /* 136002-113 + 136002-114 */
ROM_LOAD("136002-134.f1", 0xa000, 0x1000, CRC(c88e3524) SHA1(89144baf1efc703b2336774793ce345b37829ee7)) /* 136002-115 + 136002-316 */
ROM_LOAD("136002-235.j1", 0xb000, 0x1000, CRC(a4b2ce3f) SHA1(a5f5fb630a48c5d25346f90d4c13aaa98f60b228)) /* 136002-217 + 136002-118 */
ROM_LOAD("136002-136.lm1", 0xc000, 0x1000, CRC(65a9a9f9) SHA1(73aa7d6f4e7093ccb2d97f6344f354872bcfd72a)) /* 136002-119 + 136002-120 */
ROM_LOAD("136002-237.p1", 0xd000, 0x1000, CRC(de4e9e34) SHA1(04be074e45bf5cd95a852af97cd04e35b7f27fc4)) /* 136002-121 + 136002-222 */
ROM_RELOAD(0xf000, 0x1000) /* for reset/interrupt vectors */
/* Vector ROM */
ROM_LOAD("136002-138.np3", 0x3000, 0x1000, CRC(9995256d) SHA1(2b725ee1a57d423c7d7377a1744f48412e0f2f69))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(tempest1) /* rev 1 */
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136002-123.np3", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e)) /* May be labeled "136002-111", same data */
ROM_LOAD("136002-124.r3", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06)) /* May be labeled "136002-112", same data */
ROM_LOAD("136002-113.d1", 0x9000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("136002-114.e1", 0x9800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("136002-115.f1", 0xa000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("136002-116.h1", 0xa800, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("136002-117.j1", 0xb000, 0x0800, CRC(55952119) SHA1(470d914fa52fce3786cb6330889876d3547dca65))
ROM_LOAD("136002-118.k1", 0xb800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("136002-119.lm1", 0xc000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("136002-120.mn1", 0xc800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("136002-121.p1", 0xd000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("136002-122.r1", 0xd800, 0x0800, CRC(796a9918) SHA1(c862a0d4ea330161e4c3cc8e5e9ad38893fffbd4))
ROM_RELOAD(0xf800, 0x0800) /* for reset/interrupt vectors */
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(tempest2) // rev 2
ROM_REGION(0x10000, REGION_CPU1, 0)
// Roms are for Tempest Analog Vector-Generator PCB Assembly A037383-01 or A037383-02
ROM_LOAD("136002-113.d1", 0x9000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
ROM_LOAD("136002-114.e1", 0x9800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
ROM_LOAD("136002-115.f1", 0xa000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
ROM_LOAD("136002-116.h1", 0xa800, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
ROM_LOAD("136002-217.j1", 0xb000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
ROM_LOAD("136002-118.k1", 0xb800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
ROM_LOAD("136002-119.lm1", 0xc000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
ROM_LOAD("136002-120.mn1", 0xc800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
ROM_LOAD("136002-121.p1", 0xd000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
ROM_LOAD("136002-222.r1", 0xd800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
ROM_RELOAD(0xf800, 0x0800) /* for reset/interrupt vectors */
// Vector ROM
ROM_LOAD("136002-123.np3", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e)) /* May be labeled "136002-111", same data */
ROM_LOAD("136002-124.r3", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06)) /* May be labeled "136002-112", same data */
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(tempest3) // rev 2
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136002-113.d1", 0x9000, 0x0800)
ROM_LOAD("136002-114.e1", 0x9800, 0x0800)
ROM_LOAD("136002-115.f1", 0xa000, 0x0800)
ROM_LOAD("136002-316.h1", 0xa800, 0x0800)
ROM_LOAD("136002-217.j1", 0xb000, 0x0800)
ROM_LOAD("136002-118.k1", 0xb800, 0x0800)
ROM_LOAD("136002-119.lm1", 0xc000, 0x0800)
ROM_LOAD("136002-120.mn1", 0xc800, 0x0800)
ROM_LOAD("136002-121.p1", 0xd000, 0x0800)
ROM_LOAD("136002-222.r1", 0xd800, 0x0800)
ROM_RELOAD(0xf800, 0x0800) /* for reset/interrupt vectors */
/* Vector ROM */
ROM_LOAD("136002-123.np3", 0x3000, 0x0800)
ROM_LOAD("136002-124.r3", 0x3800, 0x0800)
// Roms are for Tempest Analog Vector-Generator PCB Assembly A037383-03 or A037383-04
//ROM_LOAD("136002-237.p1", 0x9000, 0x1000)
//ROM_LOAD("136002-136.lm1", 0xa000, 0x1000)
//ROM_LOAD("136002-235.j1", 0xb000, 0x1000)
//ROM_LOAD("136002-134.f1", 0xc000, 0x1000)
//ROM_LOAD("136002-133.d1", 0xd000, 0x1000)
//ROM_RELOAD(0xf000, 0x1000)//Reload
// Vector ROM
//ROM_LOAD("136002-138.np3", 0x3000, 0x1000)
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END


// Tempest Multigame (1999 Clay Cowgill)
AAE_DRIVER_BEGIN(drv_tempestm, "tempestm", "Tempest Multigame (1999 Clay Cowgill)")
AAE_DRIVER_ROM(rom_tempestm)
AAE_DRIVER_FUNCS(&init_tempestm, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestMenuRead,   // init_tempestm()
		/*w8*/       TempestMenuWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Tempest (Revision 3)
AAE_DRIVER_BEGIN(drv_tempest, "tempest", "Tempest (Revision 3)")
AAE_DRIVER_ROM(rom_tempest)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1515000,           // rev3
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,       // init_tempest()
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Tempest (Revision 2B)
AAE_DRIVER_BEGIN(drv_tempest3, "tempest3", "Tempest (Revision 2B)")
AAE_DRIVER_ROM(rom_tempest3)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()


// Tempest (Revision 2A)
AAE_DRIVER_BEGIN(drv_tempest2, "tempest2", "Tempest (Revision 2A)")
AAE_DRIVER_ROM(rom_tempest2)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()


// Tempest (Revision 1)
AAE_DRIVER_BEGIN(drv_tempest1, "tempest1", "Tempest (Revision 1)")
AAE_DRIVER_ROM(rom_tempest1)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Tempest Tubes
AAE_DRIVER_BEGIN(drv_temptube, "temptube", "Tempest Tubes")
AAE_DRIVER_ROM(rom_temptube)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Aliens (Tempest Alpha)
AAE_DRIVER_BEGIN(drv_aliensv, "aliensv", "Aliens (Tempest Alpha)")
AAE_DRIVER_ROM(rom_aliensv)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Vector Breakout (1999 Clay Cowgill)
AAE_DRIVER_BEGIN(drv_vbrakout, "vbrakout", "Vector Breakout (1999 Clay Cowgill)")
AAE_DRIVER_ROM(rom_vbrakout)
AAE_DRIVER_FUNCS(&init_vbrakout, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &tempest_interrupt,
		/*r8*/       TempestRead,       // init_vbrakout()
		/*w8*/       TempestWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Vortex (Tempest Beta)
AAE_DRIVER_BEGIN(drv_vortex, "vortex", "Vortex (Tempest Beta)")
AAE_DRIVER_ROM(rom_vortex)
AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
AAE_DRIVER_INPUT(input_ports_tempest)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(tempestart)
AAE_DRIVER_CPUS(
AAE_CPU_ENTRY(
/*type*/     CPU_M6502,
/*freq*/     1512000,
/*div*/      100,
/*ipf*/      4,
/*int type*/ INT_TYPE_INT,
/*int cb*/   &tempest_interrupt,
/*r8*/       TempestRead,
/*w8*/       TempestWrite,
/*pr*/       nullptr,
/*pw*/       nullptr,
/*r16*/      nullptr,
/*w16*/      nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_tempestm)
AAE_REGISTER_DRIVER(drv_tempest)
AAE_REGISTER_DRIVER(drv_tempest3)
AAE_REGISTER_DRIVER(drv_tempest2)
AAE_REGISTER_DRIVER(drv_tempest1)
AAE_REGISTER_DRIVER(drv_temptube)
AAE_REGISTER_DRIVER(drv_aliensv)
AAE_REGISTER_DRIVER(drv_vbrakout)
AAE_REGISTER_DRIVER(drv_vortex)
