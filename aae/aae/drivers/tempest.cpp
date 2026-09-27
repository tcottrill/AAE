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
#include "mame_late_avgdvg.h"
#include "c012294_interface.h"
#include "er2055.h"
#include "mathbox.h"
#include "timer.h"
#include "cpu_6502.h"

// Regression guard: this file must never see OpenGL headers. If this fires,
// a render header re-leaked glew.h — fix the header, not this guard.
#ifdef __glew_h__
#error "OpenGL headers leaked into a non-render translation unit"
#endif
//
// Tempest Multigame Notes:
// Tempest Multigame is Copyright 1999 Clay Cowgill, and provides a very nice menu system to run
// multiple games on a real tempest arcade machine.
// Tempest Multigame emulation is setup with Aliens Enabled, and the optional "Reset Adapter" is installed.
// Pressing Start 1 and Start 2 together resets the game and reenters the menu
// Supposedly Aliens needs the watchdog disabled, but I am not seeing any issues so I am leaving it alone.
// Note for me:
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
	4000        W                   D         center coin counter
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

// IRQ rate - DELIBERATELY 240 Hz. The board's IRQ is the 3 kHz clock divided
// by 12: 12.096 MHz / 4096 / 12 = 246.09375 Hz (MAME 0.286: CLOCK_3KHZ / 12),
// four per picture, a 61.5 Hz picture. Every Tempest-hardware entry here runs
// at 60 fps with ipf 4 and this firing on every pass = 240 Hz, four IRQs per
// presented frame at fixed cycle positions: frame-locked to the 60 Hz display,
// 2.5% slow by choice - the same trade as Asteroids, Black Widow and Quantum,
// not an oversight. Hardware-exact alternative: ipf 0 and
// timer_set(TIME_IN_HZ(12096000.0 / 4096 / 12), CPU0, ...) in the inits.
void tempest_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	1512000,
	{ 150, 150 },
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

// Tempest Multigame bank latch, modelled on the real Cowgill hardware (the
// HBMAME tempmg driver maps it at 0xe000). The MENU ROM selects a game by
// writing the bank number to 0xe000 and jumping through the new bank's
// vectors - no input snooping, no CPU reset. The EAROM "default game"
// auto-launch works the same way, since the menu performs it with the same
// register write.
//
// ROM layout (HBMAME's): bank n's 0x9000-0xdfff program image lives at
// 0x11000 + n * 0x8000 and its 0xf800-0xffff vectors at 0x17800 + n * 0x8000
// (n: 0 menu, 1 aliens, 2 vbrakout, 3 vortex, 4 temptube, 5 tempest rev 1,
// 6 tempest rev 2A, 7 tempest rev 2B). The four 4K vector ROM images sit in
// REGION_USER1; banks 4-7 share image 0. Like the real latch, only the ROM
// windows are copied; RAM is untouched.
// video.ini section per bank: each game keeps the rect it has as a standalone
// set (Vector Breakout's differs from Tempest's); a bank whose set has no
// section falls back to [tempmg], then to the defaults.
static const char* const tempmg_bank_set[8] = {
	"tempmg", "aliensv", "vbrakout", "vortex", "temptube", "tempest1", "tempest2", "tempest3"
};
static int tempmg_bank = 0;

static void tempmg_setbank(int bank)
{
	unsigned char* RAM = Machine->memory_region[REGION_CPU1];
	const unsigned char* vec = Machine->memory_region[REGION_USER1];

	if (bank != tempmg_bank)
	{
		// The outgoing game's picture must not linger under the incoming
		// game's geometry: drop the vector buffer and display list, then
		// load the new bank's video.ini rect.
		avgdvg_discard();
		setup_video_config(tempmg_bank_set[bank]);
	}
	tempmg_bank = bank;

	memcpy(&RAM[0x9000], &RAM[0x11000 + bank * 0x8000], 0x5000);	/* program ROM */
	memcpy(&RAM[0xf800], &RAM[0x17800 + bank * 0x8000], 0x0800);	/* reset/interrupt vectors */
	memcpy(&RAM[0x3000], &vec[(bank < 4 ? bank : 0) * 0x1000], 0x1000);	/* vector ROM */
}

WRITE_HANDLER(tempmg_rombank_w)
{
	tempmg_setbank(data & 7);
}

static void tempmg_reset()
{
	// The optional "Reset Adapter" pulses the 6502 reset line; the bank
	// latch returns to the menu bank.
	tempmg_setbank(0);
	cpu_reset(CPU0);
}

READ_HANDLER(pokey_2_tempest_read)
{
	int val = Read_pokey_regs(address, 1);

	if ((val & (0x20 | 0x40)) == (0x20 | 0x40)) // Start1 + Start2 pressed together
	{
		//LOG_INFO("VAL HERE is %x", val);
		if (val == 0x60) tempmg_reset();
	}
	return val;
}

READ_HANDLER(TempestIN0read)
{
	int res;

	res = readinputportbytag("IN0");

	// 3KHz clock on bit 7: include the in-slice cycle count (get6502ticks(0),
	// reset each slice by cpu_exec_now) since eternaticks only advances at slice
	// boundaries - edge-waiting loops (self-test) need a real ~3KHz square wave.
	// Same fix as llander_IN0_r.
	if ((get_eterna_ticks(0) + m_cpu_6502[CPU0]->get6502ticks(0)) & 0x100)
		res |= 0x80;

	if (avgdvg_done()) res |= 0x40;

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
	avgdvg_reset(0, 0);
}//AVGRESET

// OUT0 ($4000): D0-D2 coin counters (not modelled), D3 video invert X,
// D4 video invert Y (MAME 0.286 tempest_coin_w; the ROM's K_MVINVX /
// K_MVINVY). The game sets the inverts for player 2 in cocktail mode.
WRITE_HANDLER(coin_write)
{
	(void)address;
	avg_set_flip_x(data & 0x08);
	avg_set_flip_y(data & 0x10);
}

//////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// EAROM (ER2055) - MAME tempest.cpp earom_read/earom_write/earom_control_w.
// CK = EDB0, C1 = /EDB2, C2 = EDB1, CS1 = EDB3, /CS2 = GND.
// `address` is the offset from the range start (every core passes
// addr - lowAddr), so & 0x3f selects the word directly.
// ---------------------------------------------------------------------------
static er2055 earom;

READ_HANDLER(earom_read)
{
	return er2055_data(&earom);
}

WRITE_HANDLER(earom_write)
{
	er2055_set_address(&earom, address & 0x3f);
	er2055_set_data(&earom, data);
}

WRITE_HANDLER(earom_control_w)
{
	er2055_set_control(&earom, (data >> 3) & 1, true, !((data >> 2) & 1), (data >> 1) & 1);
	er2055_set_clk(&earom, data & 1);
}

MEM_READ(TempmgRead)
MEM_ADDR(0x0c00, 0x0c00, TempestIN0read)
MEM_ADDR(0x0d00, 0x0d00, ip_port_3_r)
MEM_ADDR(0x0e00, 0x0e00, ip_port_4_r)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_r)
MEM_ADDR(0x60d0, 0x60df, pokey_2_tempest_read)
MEM_ADDR(0x6040, 0x6040, MathboxStatusRead)
MEM_ADDR(0x6050, 0x6050, earom_read)
MEM_ADDR(0x6060, 0x6060, MathboxLowbitRead)
MEM_ADDR(0x6070, 0x6070, MathboxHighbitRead)
MEM_END

MEM_WRITE(TempmgWrite)
MEM_ADDR(0x0800, 0x080f, colorram_w)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
MEM_ADDR(0x6080, 0x609f, MathboxGo)
MEM_ADDR(0x4000, 0x4000, coin_write)
MEM_ADDR(0x4800, 0x4800, avgdvg_go_w)
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)
MEM_ADDR(0x6000, 0x603f, earom_write)
MEM_ADDR(0x6040, 0x6040, earom_control_w)
MEM_ADDR(0x5000, 0x5000, watchdog_reset_w)
MEM_ADDR(0x5800, 0x5800, avg_reset_w)
MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
MEM_ADDR(0xe000, 0xe000, tempmg_rombank_w)	/* multigame bank latch */
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
MEM_ADDR(0x6050, 0x6050, earom_read)
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
MEM_ADDR(0x4800, 0x4800, avgdvg_go_w)
MEM_ADDR(0x5000, 0x5000, watchdog_reset_w)
MEM_ADDR(0x5800, 0x5800, avg_reset_w)
MEM_ADDR(0x6000, 0x603f, earom_write)
MEM_ADDR(0x6040, 0x6040, earom_control_w)
MEM_ADDR(0x6080, 0x609f, MathboxGo)
MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
MEM_ADDR(0x9000, 0xffff, MWA_ROM)
MEM_END

void run_tempest()
{
	pokey_sh_update();
}

// Hands the mathbox its PROM regions. Returns non-zero and flags have_error
// when the regions are missing, which aborts the game start.
static int tempest_mathbox_start()
{
	if (!mathbox_init(Machine->memory_region[REGION_USER2], Machine->memory_region[REGION_USER3])) {
		have_error = 1;
		return 1;
	}
	return 0;
}

int init_tempmg()
{
	cache_clear();
	// Bank 0 (the menu) lives at 0x11000 like every other bank; copy it into
	// the live 64K before the CPU starts. tempmg_bank is already 0, so this
	// does not reconfigure the video.
	tempmg_bank = 0;
	tempmg_setbank(0);
	pokey_sh_start(&pokey_interface);
	avg_start_tempest();
	er2055_init(&earom);
	nvram_set_region(earom.rom, sizeof(earom.rom), 0x00);
	earom_control_w(0, 0, nullptr);   // MAME machine_reset(): earom_control_w(0)
	return tempest_mathbox_start();
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_tempest(void)
{
	pokey_sh_start(&pokey_interface);
	if (config.hack)
	{
		//LEVEL SELECTION HACK   (Does NOT Work on Protos)
		Machine->memory_region[CPU0][0x9001] = 0xd1;
		Machine->memory_region[CPU0][0x90cd] = 0xea;
		Machine->memory_region[CPU0][0x90ce] = 0xea;
	}

	avg_start_tempest();
	er2055_init(&earom);
	nvram_set_region(earom.rom, sizeof(earom.rom), 0x00);
	earom_control_w(0, 0, nullptr);   // MAME machine_reset(): earom_control_w(0)
	return tempest_mathbox_start();
}

// Vector Breakout shares init_tempest's setup but must never get the
// level-select hack: it patches Tempest's program ROM at 0x9001/0x90cd/0x90ce,
// and in Vector Breakout 0x9001 is the reset routine (blank screen if patched).
int init_vbrakout(void)
{
	pokey_sh_start(&pokey_interface);
	avg_start_tempest();
	er2055_init(&earom);
	nvram_set_region(earom.rom, sizeof(earom.rom), 0x00);
	earom_control_w(0, 0, nullptr);   // MAME machine_reset(): earom_control_w(0)
	return tempest_mathbox_start();
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
PORT_ANALOG(0x0f, 0x00, IPT_DIAL | IPF_REVERSE, 25, 20, 0, 0)
/* The next one is reponsible for cocktail mode.
 * According to the documentation, this is not a switch, although
 * it may have been planned to put it on the Math Box PCB, D/E2 )
 */
	// The port value is fed straight in as POKEY 1's ALLPOT mask, and the ROM
	// reads bit 4 set as cocktail (K_COCKTA), so 0x00 is Upright. (MAME's
	// table shows the opposite values because its per-pot callback inverts.)
	PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
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

// Mathbox PROMs, shared by every set on this hardware. The mapping PROM is
// 32 x 8; the six microcode PROMs are 256 x 4 and are merged into three byte
// planes: 127 (bits 3-0) low / 128 (bits 7-4) high at 0x000, 129 / 130 at
// 0x100, 131 / 132 at 0x200. This is the order verified against the MBUCOD
// source (see machine/mathbox.cpp); MAME's tables pair them the other way
// round and never read them.
#define TEMPEST_MATHBOX_PROMS() \
	ROM_REGION(0x20, REGION_USER2, 0) \
	ROM_LOAD("136002-126.a1", 0x0000, 0x0020, CRC(8b04f921) SHA1(317b3397482f13b2d1bc21f296d3b3f9a118787b)) \
	ROM_REGION(0x300, REGION_USER3, 0) \
	ROM_LOAD_NIB_LOW ("136002-127.e1", 0x0000, 0x0100, CRC(276eadd5) SHA1(55718cd8ec4bcf75076d5ef0ee1ed2551e19d9ba)) \
	ROM_LOAD_NIB_HIGH("136002-128.f1", 0x0000, 0x0100, CRC(823b61ae) SHA1(d99a839874b45f64e14dae92a036e47a53705d16)) \
	ROM_LOAD_NIB_LOW ("136002-129.h1", 0x0100, 0x0100, CRC(09f5a4d5) SHA1(d6f2ac07ca9ee385c08831098b0dcaf56808993b)) \
	ROM_LOAD_NIB_HIGH("136002-130.j1", 0x0100, 0x0100, CRC(8119b847) SHA1(c4fbaedd4ce1ad6a4128cbe902b297743edb606a)) \
	ROM_LOAD_NIB_LOW ("136002-131.k1", 0x0200, 0x0100, CRC(b31f6e24) SHA1(ce5f8ca34d06a5cfa0076b47400e61e0130ffe74)) \
	ROM_LOAD_NIB_HIGH("136002-132.l1", 0x0200, 0x0100, CRC(2af82e87) SHA1(3816835a9ccf99a76d246adf204989d9261bb065))

	ROM_START(tempmg)
	ROM_REGION(0x50000, REGION_CPU1, 0)
	ROM_LOAD("tempmg-113.d1", 0x11000, 0x0800, CRC(8a6633fb) SHA1(b143a5d2019f24666b350b40b0dab2924bb9c7c0))
	ROM_LOAD("tempmg-114.e1", 0x11800, 0x0800, CRC(2eedfdf6) SHA1(2ed494bd8610bebd07284289ca8b7059fd805300))
	ROM_LOAD("tempmg-115.f1", 0x12000, 0x0800, CRC(12f62746) SHA1(37356b5738c27ffe4c38f1b6cf99ae21441d8e8e))
	ROM_LOAD("tempmg-222.r1", 0x15800, 0x0800, CRC(1d8f194a) SHA1(c77f6b83f5c498c0f2d5372089a4604913a4aad5))
	ROM_RELOAD(0x17800, 0x0800)
	ROM_LOAD("aliens.d1", 0x19000, 0x0800, CRC(337e21f6) SHA1(7adadeaa975e22f0b20e8f1fb6ad68b5c3934133))
	ROM_RELOAD(0x19800, 0x0800)
	ROM_LOAD("aliens.f1", 0x1a000, 0x0800, CRC(4d2aabb0) SHA1(31106a1fc22d2a19866f07b8d6c6f4bf76007909))
	ROM_LOAD("aliens.h1", 0x1a800, 0x0800, CRC(a503f54a) SHA1(91ebf9f69a183a04a5bf55fcdd9e191523bb66bb))
	ROM_LOAD("aliens.j1", 0x1b000, 0x0800, CRC(5487d531) SHA1(c95f037151b824345af03f27a6c3c7eb8a899b2c))
	ROM_LOAD("aliens.k1", 0x1b800, 0x0800, CRC(0ac96e87) SHA1(37461e84e6f46516c25dbf4ddb2ffd65877445c0))
	ROM_LOAD("aliens.l1", 0x1c000, 0x0800, CRC(cd246ac2) SHA1(de2e6fe2e72c092c3874e797fc302a71dbf57710))
	ROM_LOAD("aliens.n1", 0x1c800, 0x0800, CRC(bd98c5f3) SHA1(268487d9cf46b4b7b49eab7420d078bf676e636c))
	ROM_LOAD("aliens.p1", 0x1d000, 0x0800, CRC(7c10adbd) SHA1(38579128a90bff4a7a4ae46d6aaa42118b8bc218))
	ROM_LOAD("aliens.r1", 0x1d800, 0x0800, CRC(555c3070) SHA1(032f03af23c7ccac8a2bf50c3c646e141921ffee))
	ROM_RELOAD(0x1f800, 0x0800) /* for reset/interrupt vectors */
	ROM_LOAD("vbrakout.113", 0x21000, 0x0800, CRC(6fd3efe5) SHA1(d195d08984ad8797607bc1989e8a606d51547c68))
	ROM_LOAD("vbrakout.114", 0x21800, 0x0800, CRC(9974b9a5) SHA1(6ecc6f72070895bb15992977348f58835233911f))
	ROM_LOAD("vbrakout.115", 0x22000, 0x0800, CRC(44d611d8) SHA1(82cd63fc9067ea1f00feeffbee66e7d750cab7e5))
	ROM_LOAD("vbrakout.116", 0x22800, 0x0800, CRC(cd58fc11) SHA1(060e31e55183ccef67a1adc91fb48c22424a4ba5))
	ROM_LOAD("vbrakout.122", 0x25800, 0x0800, CRC(1ae2dd53) SHA1(b908ba6b59195aea853380a56a243aa8fa2fba71))
	ROM_RELOAD(0x27800, 0x0800) /* for reset/interrupt vectors */
	ROM_LOAD("d1.bin", 0x29000, 0x0800, CRC(3aff3417) SHA1(3b7c31f01b7467757ec85e98a17038e5df5720bb))
	ROM_LOAD("e1.bin", 0x29800, 0x0800, CRC(11861be3) SHA1(a35797c649e8286c844cee6dac86ac50f4fbd669))
	ROM_LOAD("f1.bin", 0x2a000, 0x0800, CRC(1d251111) SHA1(2912a21dc708231e28d6164e54e593a8300b9c4a))
	ROM_LOAD("h1.bin", 0x2a800, 0x0800, CRC(937a9859) SHA1(336b25291533d19294f1ced730bbf20971849adf))
	ROM_LOAD("j1.bin", 0x2b000, 0x0800, CRC(79481246) SHA1(c5362670fd29ef1432f8e626323da395d6e8a675))
	ROM_LOAD("k1.bin", 0x2b800, 0x0800, CRC(390f872a) SHA1(c5463ea2d2307e21c941b5b459e3652c12154609))
	ROM_LOAD("lm1.bin", 0x2c000, 0x0800, CRC(515760dd) SHA1(773f06c9a64e72f9d3d8a5c622bf3ec2b4ba678d))
	ROM_LOAD("mn1.bin", 0x2c800, 0x0800, CRC(c6c41c68) SHA1(9323c07fc80a947142dde008c53f5e8c0b0c572d))
	ROM_LOAD("p1.bin", 0x2d000, 0x0800, CRC(3c2ff130) SHA1(32ebabcb2cbd7aab5e29de2b873f02ed78776ae6))
	ROM_LOAD("r1.bin", 0x2d800, 0x0800, CRC(67cafbb1) SHA1(467515733d843398e6fe29661002536a1e6c8fc9))
	ROM_RELOAD(0x2f800, 0x0800) /* for reset/interrupt vectors */
	ROM_LOAD("136002.113", 0x31000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
	ROM_RELOAD(0x39000, 0x0800)
	ROM_RELOAD(0x41000, 0x0800)
	ROM_RELOAD(0x49000, 0x0800)
	ROM_LOAD("136002-114.e1", 0x31800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
	ROM_RELOAD(0x39800, 0x0800)
	ROM_RELOAD(0x41800, 0x0800)
	ROM_RELOAD(0x49800, 0x0800)
	ROM_LOAD("136002.115", 0x32000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
	ROM_RELOAD(0x3a000, 0x0800)
	ROM_RELOAD(0x42000, 0x0800)
	ROM_RELOAD(0x4a000, 0x0800)
	ROM_LOAD("136002.316", 0x32800, 0x0800, CRC(aeb0f7e9) SHA1(a5cc25015b98692673cfc1c7c2e9634efd750870))
	ROM_RELOAD(0x4a800, 0x0800)
	ROM_RELOAD(0x12800, 0x0800)
	ROM_LOAD("136002.217", 0x33000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
	ROM_RELOAD(0x43000, 0x0800)
	ROM_RELOAD(0x4b000, 0x0800)
	ROM_RELOAD(0x13000, 0x0800)
	ROM_LOAD("tube-118.k1", 0x33800, 0x0800, CRC(cefb03f0) SHA1(41ddfa4991fa49a31d4740a04551556acca66196))
	ROM_LOAD("136002.119", 0x34000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
	ROM_RELOAD(0x3c000, 0x0800)
	ROM_RELOAD(0x44000, 0x0800)
	ROM_RELOAD(0x4c000, 0x0800)
	ROM_RELOAD(0x14000, 0x0800)
	ROM_LOAD("136002.120", 0x34800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
	ROM_RELOAD(0x3c800, 0x0800)
	ROM_RELOAD(0x44800, 0x0800)
	ROM_RELOAD(0x4c800, 0x0800)
	ROM_RELOAD(0x14800, 0x0800)
	ROM_LOAD("136002.121", 0x35000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
	ROM_RELOAD(0x3d000, 0x0800)
	ROM_RELOAD(0x45000, 0x0800)
	ROM_RELOAD(0x4d000, 0x0800)
	ROM_RELOAD(0x15000, 0x0800)
	ROM_LOAD("136002.222", 0x35800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
	ROM_RELOAD(0x37800, 0x0800)
	ROM_RELOAD(0x45800, 0x0800)
	ROM_RELOAD(0x47800, 0x0800)
	ROM_RELOAD(0x4d800, 0x0800)
	ROM_RELOAD(0x4f800, 0x0800)
	ROM_LOAD("136002-116.h1", 0x3a800, 0x0800, CRC(7356896c) SHA1(a013ede292189a8f5a907de882ee1a573d784b3c))
	ROM_RELOAD(0x42800, 0x0800)
	ROM_LOAD("136002-117.j1", 0x3b000, 0x0800, CRC(55952119) SHA1(470d914fa52fce3786cb6330889876d3547dca65))
	ROM_LOAD("136002.118", 0x3b800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
	ROM_RELOAD(0x43800, 0x0800)
	ROM_RELOAD(0x4b800, 0x0800)
	ROM_RELOAD(0x13800, 0x0800)
	ROM_LOAD("136002-122.r1", 0x3d800, 0x0800, CRC(796a9918) SHA1(c862a0d4ea330161e4c3cc8e5e9ad38893fffbd4))
	ROM_RELOAD(0x3f800, 0x0800)
	/* Vector ROMs: image 0 menu, 1 aliens, 2 vbrakout, 3 vortex */
	ROM_REGION(0x4000, REGION_USER1, 0)
	ROM_LOAD("136002-123.np3", 0x0000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
	ROM_RELOAD(0x2000, 0x0800)
	ROM_LOAD("136002-124.r3", 0x0800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
	ROM_RELOAD(0x2800, 0x0800)
	ROM_LOAD("aliens.n3", 0x1000, 0x0800, CRC(5c8fd38b) SHA1(bb0d6bd062eba53b5d64b3f444d5ce0a34728bf5))
	ROM_LOAD("aliens.r3", 0x1800, 0x0800, CRC(6cabcd08) SHA1(e3950de50f3dfbc4d4d2f4fe26625d8ef94c0819))
	ROM_LOAD("n3.bin", 0x3000, 0x0800, CRC(29c6a1cb) SHA1(290702a1c0942a68e288b37963e51eba02177a3f))
	ROM_LOAD("r3.bin", 0x3800, 0x0800, CRC(7fbe5e21) SHA1(e5de6c3af82e64444b0ddcda559e9cb4fbf6c1da))
	/* AVG PROM */
	ROM_REGION(0x100, REGION_PROMS, 0)
	ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
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
	TEMPEST_MATHBOX_PROMS()
	ROM_END

	ROM_START(tempest3) // rev 2
	ROM_REGION(0x10000, REGION_CPU1, 0)
	// Roms are for Tempest Analog Vector-Generator PCB Assembly A037383-01 or A037383-02
	ROM_LOAD("136002-113.d1", 0x9000, 0x0800, CRC(65d61fe7) SHA1(38a1e8a8f65b7887cf3e190269fe4ce2c6f818aa))
	ROM_LOAD("136002-114.e1", 0x9800, 0x0800, CRC(11077375) SHA1(ed8ff0ca969da6672a7683b93d4fcf2935a0d903))
	ROM_LOAD("136002-115.f1", 0xa000, 0x0800, CRC(f3e2827a) SHA1(bd04fcfbbba995e08c3144c1474fcddaaeb1c700))
	ROM_LOAD("136002-316.h1", 0xa800, 0x0800, CRC(aeb0f7e9) SHA1(a5cc25015b98692673cfc1c7c2e9634efd750870))
	ROM_LOAD("136002-217.j1", 0xb000, 0x0800, CRC(ef2eb645) SHA1(b1a2c969e8897e335d5354de6ae04a65d4b2a1e4))
	ROM_LOAD("136002-118.k1", 0xb800, 0x0800, CRC(beb352ab) SHA1(f213166d3970e0bd0f29d8dea8d6afa6990cce38))
	ROM_LOAD("136002-119.lm1", 0xc000, 0x0800, CRC(a4de050f) SHA1(ea302e43a313a5a18115e74ddbaaedde0fbecda7))
	ROM_LOAD("136002-120.mn1", 0xc800, 0x0800, CRC(35619648) SHA1(48f1e8bed7ec6afa0b4c549a30e5ec331c071e40))
	ROM_LOAD("136002-121.p1", 0xd000, 0x0800, CRC(73d38e47) SHA1(9980606376a79ba94f8e2a325871a6c8d10d83fc))
	ROM_LOAD("136002-222.r1", 0xd800, 0x0800, CRC(707bd5c3) SHA1(2f0af6fb7154c244c794f7247e5c16a1e06ddf7d))
	ROM_RELOAD(0xf800, 0x0800) /* for reset/interrupt vectors */
	/* Vector ROM */
	ROM_LOAD("136002-123.np3", 0x3000, 0x0800, CRC(29f7e937) SHA1(686c8b9b8901262e743497cee7f2f7dd5cb3af7e))
	ROM_LOAD("136002-124.r3", 0x3800, 0x0800, CRC(c16ec351) SHA1(a30a3662c740810c0f20e3712679606921b8ca06))
	/* AVG PROM */
	ROM_REGION(0x100, REGION_PROMS, 0)
	ROM_LOAD("136002-125.d7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
	TEMPEST_MATHBOX_PROMS()
	ROM_END

	// Tempest Multigame (1999 Clay Cowgill)
	AAE_DRIVER_BEGIN(drv_tempmg, "tempmg", "Tempest Multigame (1999 Clay Cowgill)")
	AAE_DRIVER_ROM(rom_tempmg)
	AAE_DRIVER_FUNCS(&init_tempmg, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
	AAE_DRIVER_CPUS(
		AAE_CPU_ENTRY(
			/*type*/     CPU_M6502,
			/*freq*/     1512000,
			/*div*/      100,
			/*ipf*/      4,
			/*int type*/ INT_TYPE_INT,
			/*int cb*/   &tempest_interrupt,
			/*r8*/       TempmgRead,   // init_tempmg()
			/*w8*/       TempmgWrite,
			/*pr*/       nullptr,
			/*pw*/       nullptr,
			/*r16*/      nullptr,
			/*w16*/      nullptr
		),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY()
	)
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_END()

	// Tempest (Revision 3)
	AAE_DRIVER_BEGIN(drv_tempest, "tempest", "Tempest (Revision 3)")
	AAE_DRIVER_ROM(rom_tempest)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
	AAE_DRIVER_CPUS(
		AAE_CPU_ENTRY(
			/*type*/     CPU_M6502,
			/*freq*/     1512000,           // rev3
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_END()

	// Tempest (Revision 2B)
	AAE_DRIVER_BEGIN(drv_tempest3, "tempest3", "Tempest (Revision 2B)")
	AAE_DRIVER_ROM(rom_tempest3)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Tempest (Revision 2A)
	AAE_DRIVER_BEGIN(drv_tempest2, "tempest2", "Tempest (Revision 2A)")
	AAE_DRIVER_ROM(rom_tempest2)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Tempest (Revision 1)
	AAE_DRIVER_BEGIN(drv_tempest1, "tempest1", "Tempest (Revision 1)")
	AAE_DRIVER_ROM(rom_tempest1)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Tempest Tubes
	AAE_DRIVER_BEGIN(drv_temptube, "temptube", "Tempest Tubes")
	AAE_DRIVER_ROM(rom_temptube)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Aliens (Tempest Alpha)
	AAE_DRIVER_BEGIN(drv_aliensv, "aliensv", "Aliens (Tempest Alpha)")
	AAE_DRIVER_ROM(rom_aliensv)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Vector Breakout (1999 Clay Cowgill)
	AAE_DRIVER_BEGIN(drv_vbrakout, "vbrakout", "Vector Breakout (1999 Clay Cowgill)")
	AAE_DRIVER_ROM(rom_vbrakout)
	AAE_DRIVER_FUNCS(&init_vbrakout, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_CLONE_OF("tempest")
	AAE_DRIVER_END()

	// Vortex (Tempest Beta)
	AAE_DRIVER_BEGIN(drv_vortex, "vortex", "Vortex (Tempest Beta)")
	AAE_DRIVER_ROM(rom_vortex)
	AAE_DRIVER_FUNCS(&init_tempest, &run_tempest, &end_tempest)
	AAE_DRIVER_INPUT(input_ports_tempest)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()
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
	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
	AAE_DRIVER_SCREEN(1024, 768, 0, 580, 0, 570)
	AAE_DRIVER_RASTER_NONE()
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
	AAE_DRIVER_NVRAM(generic_nvram_handler)
	AAE_DRIVER_END()

	AAE_REGISTER_DRIVER(drv_tempmg)
	AAE_REGISTER_DRIVER(drv_tempest)
	AAE_REGISTER_DRIVER(drv_tempest3)
	AAE_REGISTER_DRIVER(drv_tempest2)
	AAE_REGISTER_DRIVER(drv_tempest1)
	AAE_REGISTER_DRIVER(drv_temptube)
	AAE_REGISTER_DRIVER(drv_aliensv)
	AAE_REGISTER_DRIVER(drv_vbrakout)
	AAE_REGISTER_DRIVER(drv_vortex)