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
#include "vector.h"
#include "aae_avg.h"
#include "pokyintf.h"
#include "earom.h"
#include "mathbox.h"


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

static int flipscreen = 0;
static int INMENU = 0;
static int tempprot = 1;
static char* tbuffer = nullptr;

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	1512000,
	255,	/* volume */
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
	memcpy(GI[CPU0], tbuffer, 0x10000);
	m_cpu_6502[0]->reset6502();
	INMENU = 1;
}

static void switch_game()
{
	int a = 0;
	int b = 0;
	int oldgamenum = TEMPESTM;

	if (!INMENU) { return; }

	INMENU = 0;
	oldgamenum = gamenum;
	a = (GI[CPU0][0x51]) + 1;
	wrlog("A here is %d", a);
	switch (a)
	{
	case 1: b = 0x10000; gamenum = ALIENST; break;
	case 2: b = 0x20000; gamenum = VBREAK; break;
	case 3: b = 0x30000; gamenum = VORTEX; break;
	case 4: b = 0x40000; gamenum = TEMPTUBE; break;
	case 5: b = 0x50000; gamenum = TEMPEST1; break;
	case 6: b = 0x60000; gamenum = TEMPEST2; break;
	case 7: b = 0x70000; gamenum = TEMPEST; break;
	default: wrlog("Tempest Multigame - unhandled game number?");
	}

	setup_video_config();
	gamenum = oldgamenum; //Reset back
	memset(GI[CPU0], 0x10000, 0);
	memcpy(GI[CPU0], GI[CPU0] + b, 0x10000);
	cache_clear();
	m_cpu_6502[0]->reset6502();
}

READ_HANDLER(pokey_2_tempest_read)
{
	int val = Read_pokey_regs(address, 1);
	if (gamenum == TEMPESTM && (val & 0x10))  //Fire
	{
		switch_game();
	}

	if (gamenum == TEMPESTM && ((val & 0x20) && val & 0x40)) // Start 1 and Start 2 together
	{
		tempm_reset();
	}
	return val;
}

WRITE_HANDLER(TempGo)
{
	avg_go();
}

WRITE_HANDLER(watchdog_reset_w)
{
	WATCHDOG = data;
}

READ_HANDLER(TempestIN0read)
{
	int res;

	res = readinputportbytag("IN0");

	res = res | ((get_eterna_ticks(0) >> 1) & 0x80); //3Khz clock

	if (avg_check()) { bitclr(res, 0x40); }
	else { bitset(res, 0x40); }

	return res;
}

WRITE_HANDLER(tempest_led_w)
{
	set_aae_leds(~data & 0x02, ~data & 0x01, 0);
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

WRITE_HANDLER(prot_w_1)
{
	GI[CPU0][0x011b] = 0;
}

WRITE_HANDLER(prot_w_2)
{
	GI[CPU0][0x011f] = 0;
}

WRITE_HANDLER(prot_w_3)
{
	GI[CPU0][0x0455] = 0;
}

///////////////////////  MAIN LOOP /////////////////////////////////////
void set_tempest_video()
{
	//if (gamenum != VBREAK) avg_clear();
}

//////////////////////////////////////////////////////////////////////////

MEM_READ(TempestRead)
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

MEM_WRITE(TempestWrite)
MEM_ADDR(0x011b, 0x011b, prot_w_1)
MEM_ADDR(0x011f, 0x011f, prot_w_2)
MEM_ADDR(0x0455, 0x0455, prot_w_3)
MEM_ADDR(0x0800, 0x080f, colorram_w)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
MEM_ADDR(0x6080, 0x609f, MathboxGo)
MEM_ADDR(0x4000, 0x4000, coin_write)
MEM_ADDR(0x4800, 0x4800, TempGo)
MEM_ADDR(0x3000, 0x3fff, NoWrite)
MEM_ADDR(0x6000, 0x603f, EaromWrite)
MEM_ADDR(0x6040, 0x6040, EaromCtrl)
MEM_ADDR(0x5000, 0x5000, watchdog_reset_w)
MEM_ADDR(0x5800, 0x5800, avg_reset_w)
MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
MEM_ADDR(0x9000, 0xffff, NoWrite)
MEM_ADDR(0x3000, 0x57ff, NoWrite)
MEM_END

void run_tempest()
{
	pokey_sh_update();
}
/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_tempest(void)
{
	init6502(TempestRead, TempestWrite, 0);

	cache_clear();
	\
		if (gamenum == TEMPESTM)
		{
			tbuffer = (char*)malloc(0x10000);
			memcpy(tbuffer, GI[CPU0], 0x10000);
			INMENU = 1;
		} //Save the menu code so we can overwrite it.

	if (gamenum == TEMPTUBE ||
		gamenum == TEMPEST1 ||
		gamenum == TEMPEST2 ||
		gamenum == TEMPEST3 ||
		gamenum == VBREAK ||
		gamenum == TEMPEST)
	{
		
		//LEVEL SELECTION HACK   (Does NOT Work on Protos)
		if (config.hack) { GI[CPU0][0x90cd] = 0xea; GI[CPU0][0x90ce] = 0xea; }
	}

	pokey_sh_start(&pokey_interface);
	avg_init();
	return 0;
}

void end_tempest()
{
	if (gamenum == TEMPTUBE ||
		gamenum == TEMPEST1 ||
		gamenum == TEMPEST2 ||
		gamenum == TEMPEST3 ||
		gamenum == VBREAK ||
		gamenum == TEMPEST)
	{
		
	}

	pokey_sh_stop();
}