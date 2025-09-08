/* Cinematronics Emu */

#include "aae_mame_driver.h"
#include "driver_registry.h"
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

//
// Cinematronics Games. Sample definitions from M.A.M.E. (TM)
//

static const char* ripoff_samples[] = {
	"ripoff.zip",
	"efire.wav",
	"eattack.wav",
	"bonuslvl.wav",
	"explosn.wav",
	"shipfire.wav",
	"bg1.wav",
	"bg2.wav",
	"bg3.wav",
	"bg4.wav",
	"bg5.wav",
	"bg6.wav",
	"bg7.wav",
	"bg8.wav",
	 0 };

static const char* armora_samples[] = {
	"armora.zip",
	"loexp.wav",
	"jeepfire.wav",
	"hiexp.wav",
	"tankfire.wav",
	"tankeng.wav",
	"beep.wav",
	"chopper.wav",
	 0 };

static const char* starcas_samples[] = {
	"starcas.zip",
	"lexplode.wav",
	"sexplode.wav",
	"cfire.wav",
	"pfire.wav",
	"drone.wav",
	"shield.wav",
	"star.wav",
	"thrust.wav",
	 0 };

static const char* solarq_samples[] = {
	"solarq.zip",
	"bigexpl.wav",
	"smexpl.wav",
	"lthrust.wav",
	"slaser.wav",
	"pickup.wav",
	"nuke1.wav",
	"nuke2.wav",
	"hypersp.wav",
	"extra.wav",
	"phase.wav",
	"efire.wav",
	0 };

static const char* spacewar_samples[] = {
	"spacewar.zip",
	"explode1.wav",
	"fire1.wav",
	"idle.wav",
	"thrust1.wav",
	"thrust2.wav",
	"pop.wav",
	"explode2.wav",
	"fire2.wav",
	 0 };

static const char* warrior_samples[] = {
	"warrior.zip",
	"appear.wav",
	"bgmhum1.wav",
	"bgmhum2.wav",
	"fall.wav",
	"killed.wav",
	 0 };

static const char* tailg_samples[] = {
	"tailg.zip",
	"hypersp.wav",
	"sexplode.wav",
	"slaser.wav",
	"shield.wav",
	"bounce.wav",
	"thrust1.wav",
	 0 };

static const char* barrier_samples[] = {
	"barrier.zip",
	"playrdie.wav",
	"playmove.wav",
	"enemmove.wav",
	 0 };

/*************************************
 *
 *  War of the Worlds (B&W) + Color
 *
 *************************************/
static const char* wotw_samples[] =
{
	"wotw.zip",
	"cfire.wav",
	"shield.wav",
	"star.wav",
	"thrust.wav",
	"drone.wav",
	"lexplode.wav",
	"sexplode.wav",
	"pfire.wav",
	 0
};

/*************************************
 *
 *  Boxing Bugs
 *
 *************************************/

static const char* boxingb_samples[] =
{
	"boxingb.zip",
	"softexpl.wav",
	"loudexpl.wav",
	"chirp.wav",
	"eggcrack.wav",
	"bugpusha.wav",
	"bugpushb.wav",
	"bugdie.wav",
	"beetle.wav",
	"music.wav",
	"cannon.wav",
	"bounce.wav",
	"bell.wav",
	 0
};

/*************************************
 *
 *  Star Hawk
 *
 *************************************/

static const char* starhawk_samples[] =
{
	"starhawk.zip",
	"explode.wav",
	"rlaser.wav",
	"llaser.wav",
	"k.wav",
	"master.wav",
	"kexit.wav",
	 0
};



static const char* speedfrk_samples[] =
{
	"speedfrk.zip",
	"offroad.wav",
	"engine.wav",
	"horn.wav",
	"crash.wav",
	0
};


static const char* demon_samples[] = {
	"starhawk.zip",
	"explode.wav",
	"rlaser.wav",
	"llaser.wav",
	"k.wav",
	"master.wav",
	"kexit.wav",
	 0 };

/*************************************
 *
 *  Sundance
 *
 *************************************/

static const char* sundance_samples[] =
{
	"sundance.zip",
	"bong.wav",
	"whoosh.wav",
	"explsion.wav",
	"ping1.wav",
	"ping2.wav",
	"hatch.wav",
	 0
};


ART_START(sundance_art)
ART_LOAD("custom.zip", "yellow.png", ART_TEX, 1)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(barrier_art)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("barrier.zip", "barrier_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END


ART_START(ripoff_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("ripoff.zip", "ripoff.png", ART_TEX, 3)
ART_END

ART_START(starhawk_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(speedfrk_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(demon_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("demon.zip", "demon.png", ART_TEX, 1)
ART_END

ART_START(spacewar_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(tailg_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_END


ART_START(armora_art)
ART_LOAD("armora.zip", "armoraoverlay.png", ART_TEX, 1)
ART_LOAD("armora.zip", "armora.png", ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(warrior_art)
ART_LOAD("warrior.zip", "warrior.png", ART_TEX, 0)
//ART_LOAD("warrior.zip","warrior_bezel.png",ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END


ART_START(solarq_art)
ART_LOAD("solarq.zip", "solarq.png", ART_TEX, 0)
ART_LOAD("solarq.zip", "solaroverlay.png", ART_TEX, 1)
ART_LOAD("solarq.zip", "solarquest_bezel.png", ART_TEX, 3)
ART_LOAD("solarq.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(starcas_art)
ART_LOAD("starcas.zip", "starcas2.png", ART_TEX, 1)
ART_LOAD("starcas.zip", "mystarcastle_bezel.png", ART_TEX, 3)
ART_LOAD("starcas.zip", "cineshot.png", GAME_TEX, 0)
ART_END

/***************************************************************************

	 Spacewar

***************************************************************************/

INPUT_PORTS_START(spacewar)
PORT_START("IN0") /* switches */
PORT_DIPNAME(0x03, 0x00, "Time")
PORT_DIPSETTING(0x03, "0:45/coin")
PORT_DIPSETTING(0x00, "1:00/coin")
PORT_DIPSETTING(0x01, "1:30/coin")
PORT_DIPSETTING(0x02, "2:00/coin")
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER1)
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs high */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BITX(0x0800, IP_ACTIVE_LOW, 0, "Option 0", OSD_KEY_0_PAD, IP_JOY_NONE)
PORT_BITX(0x0400, IP_ACTIVE_LOW, 0, "Option 5", OSD_KEY_5_PAD, IP_JOY_NONE)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BITX(0x0080, IP_ACTIVE_LOW, 0, "Option 7", OSD_KEY_7_PAD, IP_JOY_NONE)
PORT_BITX(0x0040, IP_ACTIVE_LOW, 0, "Option 2", OSD_KEY_2_PAD, IP_JOY_NONE)
PORT_BITX(0x0020, IP_ACTIVE_LOW, 0, "Option 6", OSD_KEY_6_PAD, IP_JOY_NONE)
PORT_BITX(0x0010, IP_ACTIVE_LOW, 0, "Option 1", OSD_KEY_1_PAD, IP_JOY_NONE)
PORT_BITX(0x0008, IP_ACTIVE_LOW, 0, "Option 9", OSD_KEY_9_PAD, IP_JOY_NONE)
PORT_BITX(0x0004, IP_ACTIVE_LOW, 0, "Option 4", OSD_KEY_4_PAD, IP_JOY_NONE)
PORT_BITX(0x0002, IP_ACTIVE_LOW, 0, "Option 8", OSD_KEY_8_PAD, IP_JOY_NONE)
PORT_BITX(0x0001, IP_ACTIVE_LOW, 0, "Option 3", OSD_KEY_3_PAD, IP_JOY_NONE)


PORT_START("IN2") /* joystick X */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* joystick Y */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END


/***************************************************************************

	  Barrier

***************************************************************************/

INPUT_PORTS_START(barrier)
PORT_START("IN0") /* switches */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "5")
PORT_DIPNAME(0x02, 0x02, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x02, DEF_STR(On))
PORT_DIPNAME(0x04, 0x04, DEF_STR(Unknown))
PORT_DIPSETTING(0x04, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BITX(0x0001, IP_ACTIVE_LOW, 0, "Skill A", OSD_KEY_A, IP_JOY_NONE)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BITX(0x0004, IP_ACTIVE_LOW, 0, "Skill B", OSD_KEY_B, IP_JOY_NONE)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_PLAYER1)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BITX(0x0040, IP_ACTIVE_LOW, 0, "Skill C", OSD_KEY_C, IP_JOY_NONE)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_PLAYER2)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_PLAYER1)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_PLAYER2)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_PLAYER1)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_PLAYER2)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_PLAYER1)
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_PLAYER2)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Star Hawk

***************************************************************************/

/* TODO: 4way or 8way stick? */
INPUT_PORTS_START(starhawk)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Game_Time))
PORT_DIPSETTING(0x03, "2:00/4:00")
PORT_DIPSETTING(0x01, "1:30/3:00")
PORT_DIPSETTING(0x02, "1:00/2:00")
PORT_DIPSETTING(0x00, "0:45/1:30")
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* input */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 | IPF_PLAYER2)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER2)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER1)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_BUTTON4 | IPF_PLAYER1)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER1)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Star Castle

***************************************************************************/

INPUT_PORTS_START(starcas)

PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Lives))
PORT_DIPSETTING(0x03, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x00, "6")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(4C_3C))
PORT_DIPSETTING(0x0c, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, DEF_STR(2C_3C))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_HIGH)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS")
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0038, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0xe000, IP_ACTIVE_LOW, IPT_UNUSED)


PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Tailgunner

***************************************************************************/

INPUT_PORTS_START(tailg)

PORT_START("SWITCHES")
PORT_DIPNAME(0x23, 0x23, "Shield Points")
PORT_DIPSETTING(0x00, "15")
PORT_DIPSETTING(0x02, "20")
PORT_DIPSETTING(0x01, "30")
PORT_DIPSETTING(0x03, "40")
PORT_DIPSETTING(0x20, "50")
PORT_DIPSETTING(0x22, "60")
PORT_DIPSETTING(0x21, "70")
PORT_DIPSETTING(0x23, "80")
PORT_DIPNAME(0x04, 0x04, DEF_STR(Coinage))
PORT_DIPSETTING(0x00, DEF_STR(2C_1C))
PORT_DIPSETTING(0x04, DEF_STR(1C_1C))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS")
PORT_BIT(0x001f, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0xff00, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("ANALOGX") /* analog stick X */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 100, 5, 0, 0x20, 0xe0)

PORT_START("ANALOGY")/* analog stick Y */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 100, 5, 0, 0x20, 0xe0)

INPUT_PORTS_END

/***************************************************************************

  Ripoff - Input fixed

***************************************************************************/

INPUT_PORTS_START(ripoff)
PORT_START("IN0") /* switches */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Lives))
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x03, "8")
PORT_DIPSETTING(0x00, "12")
PORT_DIPSETTING(0x02, "16")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
PORT_DIPSETTING(0x0c, DEF_STR(4C_3C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, DEF_STR(2C_3C))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x10, DEF_STR(On))
PORT_DIPNAME(0x20, 0x00, "Scores")
PORT_DIPSETTING(0x00, "Individual")
PORT_DIPSETTING(0x20, "Combined")
PORT_SERVICE(0x40, IP_ACTIVE_LOW)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")/* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(speedfrk)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x02, "Extra Time")
PORT_DIPSETTING(0x00, "69")
PORT_DIPSETTING(0x01, "99")
PORT_DIPSETTING(0x02, "129")
PORT_DIPSETTING(0x03, "159")
PORT_DIPNAME(0x04, 0x04, DEF_STR(Unknown))
PORT_DIPSETTING(0x04, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1) /* gas */
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0070, IP_ACTIVE_LOW, IPT_UNUSED) /* gear shift, fake below */
PORT_BIT(0x000f, IP_ACTIVE_LOW, IPT_UNUSED) /* steering wheel, fake below */

PORT_START("WHEEL")/* fake - steering wheel (in4) */
PORT_ANALOG(0xff, 0x00, IPT_DIAL, 100, 1, 0, 0x00, 0xff)

PORT_START("GEAR") /* fake - gear shift (in5) */
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_PLAYER2, "1st gear", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER2, "2nd gear", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_PLAYER2, "3rd gear", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_PLAYER2, "4th gear", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
INPUT_PORTS_END



/***************************************************************************

  Sundance

***************************************************************************/

INPUT_PORTS_START(sundance)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x02, "Time")
PORT_DIPSETTING(0x00, "0:45/coin")
PORT_DIPSETTING(0x02, "1:00/coin")
PORT_DIPSETTING(0x01, "1:30/coin")
PORT_DIPSETTING(0x03, "2:00/coin")
PORT_DIPNAME(0x04, 0x00, DEF_STR(Language))
PORT_DIPSETTING(0x04, DEF_STR(Japanese))
PORT_DIPSETTING(0x00, DEF_STR(English))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Coinage)) // supposedly coinage, doesn't work
PORT_DIPSETTING(0x08, "1 coin/2 players")
PORT_DIPSETTING(0x00, "2 coins/2 players")
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNUSED) /* player 1 motion */
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_UNUSED) /* player 2 motion */
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_UNUSED) /* player 1 motion */
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_UNUSED) /* player 2 motion */
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED) /* 2 suns */
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED) /* player 1 motion */
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED) /* player 2 motion */
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_UNUSED) /* player 1 motion */
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED) /* 4 suns */
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_UNUSED) /* Grid */
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_UNUSED) /* 3 suns */
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_UNUSED) /* player 2 motion */

PORT_START("PAD1")
PORT_BITX(0x0001, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER1, "P1 Pad 1", OSD_KEY_7_PAD, IP_JOY_NONE)
PORT_BITX(0x0002, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER1, "P1 Pad 2", OSD_KEY_8_PAD, IP_JOY_NONE)
PORT_BITX(0x0004, IP_ACTIVE_HIGH, IPT_BUTTON3 | IPF_PLAYER1, "P1 Pad 3", OSD_KEY_9_PAD, IP_JOY_NONE)
PORT_BITX(0x0008, IP_ACTIVE_HIGH, IPT_BUTTON4 | IPF_PLAYER1, "P1 Pad 4", OSD_KEY_4_PAD, IP_JOY_NONE)
PORT_BITX(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON5 | IPF_PLAYER1, "P1 Pad 5", OSD_KEY_5_PAD, IP_JOY_NONE)
PORT_BITX(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON6 | IPF_PLAYER1, "P1 Pad 6", OSD_KEY_6_PAD, IP_JOY_NONE)
PORT_BITX(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON7 | IPF_PLAYER1, "P1 Pad 7", OSD_KEY_1_PAD, IP_JOY_NONE)
PORT_BITX(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON8 | IPF_PLAYER1, "P1 Pad 8", OSD_KEY_2_PAD, IP_JOY_NONE)
PORT_BITX(0x0100, IP_ACTIVE_HIGH, IPT_BUTTON9 | IPF_PLAYER1, "P1 Pad 9", OSD_KEY_3_PAD, IP_JOY_NONE)

PORT_START("PAD2")
PORT_BITX(0x0001, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2, "P2 Pad 1", OSD_KEY_Q, IP_JOY_NONE)
PORT_BITX(0x0002, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER2, "P2 Pad 2", OSD_KEY_W, IP_JOY_NONE)
PORT_BITX(0x0004, IP_ACTIVE_HIGH, IPT_BUTTON3 | IPF_PLAYER2, "P2 Pad 3", OSD_KEY_E, IP_JOY_NONE)
PORT_BITX(0x0008, IP_ACTIVE_HIGH, IPT_BUTTON4 | IPF_PLAYER2, "P2 Pad 4", OSD_KEY_A, IP_JOY_NONE)
PORT_BITX(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON5 | IPF_PLAYER2, "P2 Pad 5", OSD_KEY_S, IP_JOY_NONE)
PORT_BITX(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON6 | IPF_PLAYER2, "P2 Pad 6", OSD_KEY_D, IP_JOY_NONE)
PORT_BITX(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON7 | IPF_PLAYER2, "P2 Pad 7", OSD_KEY_Z, IP_JOY_NONE)
PORT_BITX(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON8 | IPF_PLAYER2, "P2 Pad 8", OSD_KEY_X, IP_JOY_NONE)
PORT_BITX(0x0100, IP_ACTIVE_HIGH, IPT_BUTTON9 | IPF_PLAYER2, "P2 Pad 9", OSD_KEY_C, IP_JOY_NONE)

INPUT_PORTS_END

/***************************************************************************

  Warrior

***************************************************************************/

INPUT_PORTS_START(warrior)

PORT_START("SWITCHES")
PORT_DIPNAME(0x01, 0x01, DEF_STR(Unknown))
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x02, DEF_STR(Unknown))
PORT_DIPSETTING(0x02, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x04, IP_ACTIVE_HIGH)
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_PLAYER1)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_PLAYER1)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER1)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_PLAYER1)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_PLAYER2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_PLAYER2)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER2)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_PLAYER2)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Armor Attack

***************************************************************************/

INPUT_PORTS_START(armora)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x02, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x03, "5")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(4C_3C))
PORT_DIPSETTING(0x0c, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, DEF_STR(2C_3C))
PORT_DIPNAME(0x10, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_HIGH)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Solar Quest

***************************************************************************/

INPUT_PORTS_START(solarq)
PORT_START("SWITCHES")
PORT_DIPNAME(0x05, 0x05, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(4C_3C))
PORT_DIPSETTING(0x05, DEF_STR(1C_1C))
PORT_DIPSETTING(0x04, DEF_STR(2C_3C))
PORT_DIPNAME(0x02, 0x02, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x02, "25 captures")
PORT_DIPSETTING(0x00, "40 captures")
PORT_DIPNAME(0x18, 0x10, DEF_STR(Lives))
PORT_DIPSETTING(0x18, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x20, 0x20, DEF_STR(Free_Play))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_HIGH)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_START1) /* also hyperspace */
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER1)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_START2) /* also nova */
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_BUTTON4 | IPF_PLAYER1)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")/* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Demon

***************************************************************************/

INPUT_PORTS_START(demon)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(4C_3C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(2C_3C))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x08, "5")
PORT_DIPSETTING(0x0c, "6")
PORT_DIPNAME(0x30, 0x30, "Starting Difficulty")
PORT_DIPSETTING(0x30, "1")
PORT_DIPSETTING(0x10, "5")
PORT_DIPSETTING(0x00, "10")
/*	PORT_DIPSETTING(    0x20, "1" )*/
PORT_DIPNAME(0x40, 0x40, DEF_STR(Free_Play))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS")/* inputs */
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_SERVICE(0x0080, IP_ACTIVE_LOW)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER1)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER2)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN) /* also mapped to Button 3, player 2 */


PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  War of the Worlds

***************************************************************************/

INPUT_PORTS_START(wotw)
PORT_START("SWITCHES")
PORT_DIPNAME(0x01, 0x01, DEF_STR(Unknown))
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x02, DEF_STR(Lives))
PORT_DIPSETTING(0x02, "3")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x04, 0x04, DEF_STR(Unknown))
PORT_DIPSETTING(0x04, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Coinage))
PORT_DIPSETTING(0x08, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, DEF_STR(2C_3C))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Free_Play))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_LOW)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x1000, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_START1)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/***************************************************************************

  Boxing Bugs

***************************************************************************/

INPUT_PORTS_START(boxingb)
PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(4C_3C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(2C_3C))
PORT_DIPNAME(0x04, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x08, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "30,000")
PORT_DIPSETTING(0x08, "50,000")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Free_Play))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_LOW)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS") /* inputs */
PORT_BIT(0xf000, IP_ACTIVE_HIGH, IPT_UNUSED)	/* dial */
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER1)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER2)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)

PORT_START("IN2") /* analog stick X - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3") /* analog stick Y - unused */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("DIAL")/* fake (in4) */
PORT_ANALOG(0xff, 0x80, IPT_DIAL, 100, 5, 0, 0x00, 0xff)
INPUT_PORTS_END

/***************************************************************************

   Boxing Bugs

***************************************************************************/
INPUT_PORTS_START(qb3)

PORT_START("SWITCHES")
PORT_DIPNAME(0x03, 0x02, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x02, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x03, "5")
PORT_DIPNAME(0x04, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x04, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Free_Play))	// read at $244, $2c1
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x00, DEF_STR(Unknown))	// read at $27d
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))	 // read at $282
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x40, IP_ACTIVE_LOW)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("INPUTS")
PORT_BIT(0x0001, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_UP)
PORT_BIT(0x0002, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_DOWN)
PORT_BIT(0x0004, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_LEFT)
PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_UP)
PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_RIGHT)
PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_DOWN)
PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_BUTTON4)					// read at $1a5; if 0 add 8 to $25
PORT_DIPNAME(0x0200, 0x0200, "Debug")
PORT_DIPSETTING(0x0200, DEF_STR(Off))
PORT_DIPSETTING(0x0000, DEF_STR(On))
PORT_BIT(0x0400, IP_ACTIVE_LOW, IPT_BUTTON2)					// read at $c7; jmp to $3AF1 if 0
PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_RIGHT)
PORT_DIPNAME(0x1000, 0x1000, "Infinite Lives")
PORT_DIPSETTING(0x1000, DEF_STR(Off))
PORT_DIPSETTING(0x0000, DEF_STR(On))
PORT_BIT(0x2000, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_LEFT)
PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN)

INPUT_PORTS_END


////////////// CINEMATRONICS ROMS //////////////

ROM_START(spacewar)
ROM_REGION(0x1000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("spacewar.1l", 0x0000, 0x0800, CRC(edf0fd53) SHA1(a543d8b95bc77ec061c6b10161a6f3e07401e251))
ROM_LOAD16_BYTE("spacewar.2r", 0x0001, 0x0800, CRC(4f21328b) SHA1(8889f1a9353d6bb1e1078829c1ba77557853739b))
ROM_END

ROM_START(barrier)
ROM_REGION(0x1000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("barrier.t7", 0x0000, 0x0800, CRC(7c3d68c8) SHA1(1138029552b73e94522b3b48096befc057d603c7))
ROM_LOAD16_BYTE("barrier.p7", 0x0001, 0x0800, CRC(aec142b5) SHA1(b268936b82e072f38f1f1dd54e0bc88bcdf19925))
ROM_END

ROM_START(starhawk)
ROM_REGION(0x1000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("u7", 0x0000, 0x0800, CRC(376e6c5c) SHA1(7d9530ed2e75464578b541f61408ba64ee9d2a95))
ROM_LOAD16_BYTE("r7", 0x0001, 0x0800, CRC(bb71144f) SHA1(79591cd3ef8df78ec26e158f7e82ca0dcd72260d))
ROM_END

ROM_START(speedfrk)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("speedfrk.t7", 0x0000, 0x0800, CRC(3552c03f) SHA1(c233dd064195b336556d7405b51065389b228c78))
ROM_LOAD16_BYTE("speedfrk.p7", 0x0001, 0x0800, CRC(4b90cdec) SHA1(69e2312acdc22ef52236b1c4dfee9f51fcdcaa52))
ROM_LOAD16_BYTE("speedfrk.u7", 0x1000, 0x0800, CRC(616c7cf9) SHA1(3c5bf59a09d85261f69e4b9d499cb7a93d79fb57))
ROM_LOAD16_BYTE("speedfrk.r7", 0x1001, 0x0800, CRC(fbe90d63) SHA1(e42b17133464ae48c90263bba01a7d041e938a05))
ROM_END

ROM_START(solarq)
ROM_REGION(0x4000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("solar.6t", 0x0000, 0x1000, CRC(1f3c5333) SHA1(58d847b5f009a0363ae116768b22d0bcfb3d60a4))
ROM_LOAD16_BYTE("solar.6p", 0x0001, 0x1000, CRC(d6c16bcc) SHA1(6953bdc698da060d37f6bc33a810ba44595b1257))
ROM_LOAD16_BYTE("solar.6u", 0x2000, 0x1000, CRC(a5970e5c) SHA1(9ac07924ca86d003964022cffdd6a0436dde5624))
ROM_LOAD16_BYTE("solar.6r", 0x2001, 0x1000, CRC(b763fff2) SHA1(af1fd978e46a4aee3048e6e36c409821d986f7ee))
ROM_END

ROM_START(sundance)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("sundance.t7", 0x0000, 0x0800, CRC(d5b9cb19) SHA1(72dca386b48a582186898c32123d61b4fd58632e))
ROM_LOAD16_BYTE("sundance.p7", 0x0001, 0x0800, CRC(445c4f20) SHA1(972d0b0613f154ee3347206cae05ee8c36796f84))
ROM_LOAD16_BYTE("sundance.u7", 0x1000, 0x0800, CRC(67887d48) SHA1(be225dbd3508fad2711286834880065a4fc0a2fc))
ROM_LOAD16_BYTE("sundance.r7", 0x1001, 0x0800, CRC(10b77ebd) SHA1(3d43bd47c498d5ea74a7322f8d25dbc0c0187534))
ROM_END

ROM_START(wotw)
ROM_REGION(0x4000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("wow_le.t7", 0x0000, 0x1000, CRC(b16440f9) SHA1(9656a26814736f8ff73575063b5ebbb2e8aa7dd0))
ROM_LOAD16_BYTE("wow_lo.p7", 0x0001, 0x1000, CRC(bfdf4a5a) SHA1(db4eceb68e17020d0a597ba105ec3b91ce48b7c1))
ROM_LOAD16_BYTE("wow_ue.u7", 0x2000, 0x1000, CRC(9b5cea48) SHA1(c2bc002e550a0d36e713d07f6aefa79c70b8e284))
ROM_LOAD16_BYTE("wow_uo.r7", 0x2001, 0x1000, CRC(c9d3c866) SHA1(57a47bf06838fe562981321249fe5ae585316f22))
ROM_END

ROM_START(starcas)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("starcas3.t7", 0x0000, 0x0800, CRC(b5838b5d) SHA1(6ac30be55514cba55180c85af69072b5056d1d4c))
ROM_LOAD16_BYTE("starcas3.p7", 0x0001, 0x0800, CRC(f6bc2f4d) SHA1(ef6f01556b154cfb3e37b2a99d6ea6292e5ec844))
ROM_LOAD16_BYTE("starcas3.u7", 0x1000, 0x0800, CRC(188cd97c) SHA1(c021e93a01e9c65013073de551a8c24fd1a68bde))
ROM_LOAD16_BYTE("starcas3.r7", 0x1001, 0x0800, CRC(c367b69d) SHA1(98354d34ceb03e080b1846611d533be7bdff01cc))
ROM_END

ROM_START(tailg)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("tgunner.t70", 0x0000, 0x0800, CRC(21ec9a04) SHA1(b442f34360d1d4769e7bca73a2d79ce97d335460))
ROM_LOAD16_BYTE("tgunner.p70", 0x0001, 0x0800, CRC(8d7410b3) SHA1(59ead49bd229a873f15334d0999c872d3d6581d4))
ROM_LOAD16_BYTE("tgunner.t71", 0x1000, 0x0800, CRC(2c954ab6) SHA1(9edf189a19b50a9abf458d4ef8ba25b53934385e))
ROM_LOAD16_BYTE("tgunner.p71", 0x1001, 0x0800, CRC(8e2c8494) SHA1(65e461ec4938f9895e5ac31442193e06c8731dc1))
ROM_END


ROM_START(ripoff)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("ripoff.t7", 0x0000, 0x0800, CRC(40c2c5b8) SHA1(bc1f3b540475c9868443a72790a959b1f36b93c6))
ROM_LOAD16_BYTE("ripoff.p7", 0x0001, 0x0800, CRC(a9208afb) SHA1(ea362494855be27a07014832b01e65c1645385d0))
ROM_LOAD16_BYTE("ripoff.u7", 0x1000, 0x0800, CRC(29c13701) SHA1(5e7672deffac1fa8f289686a5527adf7e51eb0bb))
ROM_LOAD16_BYTE("ripoff.r7", 0x1001, 0x0800, CRC(150bd4c8) SHA1(e1e2f0dfec4f53d8ff67b0e990514c304f496b3a))
ROM_END


ROM_START(armora)
ROM_REGION(0x4000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("ar414le.t6", 0x0000, 0x1000, CRC(d7e71f84) SHA1(0b29278a6a698f07eae597bc0a8650e91eaabffa))
ROM_LOAD16_BYTE("ar414lo.p6", 0x0001, 0x1000, CRC(df1c2370) SHA1(b74834d1a591a741892ec41269a831d3590ff766))
ROM_LOAD16_BYTE("ar414ue.u6", 0x2000, 0x1000, CRC(b0276118) SHA1(88f33cb2f46a89819c85f810c7cff812e918391e))
ROM_LOAD16_BYTE("ar414uo.r6", 0x2001, 0x1000, CRC(229d779f) SHA1(0cbdd83eb224146944049346f30d9c72d3ad5f52))
ROM_END

ROM_START(warrior)
ROM_REGION(0x2000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("warrior.t7", 0x0000, 0x0800, CRC(ac3646f9) SHA1(515c3acb638fad27fa57f6b438c8ec0b5b76f319))
ROM_LOAD16_BYTE("warrior.p7", 0x0001, 0x0800, CRC(517d3021) SHA1(0483dcaf92c336a07d2c535823348ee886567e85))
ROM_LOAD16_BYTE("warrior.u7", 0x1000, 0x0800, CRC(2e39340f) SHA1(4b3cfb3674dd2a668d4d65e28cb37d7ad20f118d))
ROM_LOAD16_BYTE("warrior.r7", 0x1001, 0x0800, CRC(8e91b502) SHA1(27614c3a8613f49187039cfb05ee96303caf72ba))
ROM_END

ROM_START(demon)
ROM_REGION(0x4000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("demon.7t", 0x0000, 0x1000, CRC(866596c1) SHA1(65202dcd5c6bf6c11fe76a89682a1505b1870cc9))
ROM_LOAD16_BYTE("demon.7p", 0x0001, 0x1000, CRC(1109e2f1) SHA1(c779b6af1ca09e2e295fc9a0e221ddf283b683ed))
ROM_LOAD16_BYTE("demon.7u", 0x2000, 0x1000, CRC(d447a3c3) SHA1(32f6fb01231aa4f3d93e32d639a89f0cf9624a71))
ROM_LOAD16_BYTE("demon.7r", 0x2001, 0x1000, CRC(64b515f0) SHA1(2dd9a6d784ec1baf31e8c6797ddfdc1423c69470))
ROM_END

ROM_START(boxingb)
ROM_REGION(0x8000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("u1a", 0x0000, 0x1000, CRC(d3115b0f) SHA1(9448e7ac1cdb5c7e0739623151be230ab630c4ea))
ROM_LOAD16_BYTE("u1b", 0x0001, 0x1000, CRC(3a44268d) SHA1(876ebe942ded787cfe357563a33d3e26a1483c5a))
ROM_LOAD16_BYTE("u2a", 0x2000, 0x1000, CRC(c97a9cbb) SHA1(8bdeb9ee6b24c0a4554bbf4532a43481a0360019))
ROM_LOAD16_BYTE("u2b", 0x2001, 0x1000, CRC(98d34ff5) SHA1(6767a02a99a01712383300f9acb96cdeffbc9c69))
ROM_LOAD16_BYTE("u3a", 0x4000, 0x1000, CRC(5bb3269b) SHA1(a9dbc91b1455760f10bad0d2ccf540e040a00d4e))
ROM_LOAD16_BYTE("u3b", 0x4001, 0x1000, CRC(85bf83ad) SHA1(9229042e39c53fae56dc93f8996bf3a3fcd35cb8))
ROM_LOAD16_BYTE("u4a", 0x6000, 0x1000, CRC(25b51799) SHA1(46465fe62907ae66a0ce730581e4e9ba330d4369))
ROM_LOAD16_BYTE("u4b", 0x6001, 0x1000, CRC(7f41de6a) SHA1(d01dffad3cb6e76c535a034ea0277dce5801c5f1))
ROM_END

ROM_START(qb3)
ROM_REGION(0x8000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("qb3_le_t7.bin", 0x0000, 0x2000, CRC(adaaee4c) SHA1(35c6bbb50646a3ddec12f115fcf3f2283e15b0a0))
ROM_LOAD16_BYTE("qb3_lo_p7.bin", 0x0001, 0x2000, CRC(72f6199f) SHA1(ae8f81f218940cfc3aef8f82dfe8cc14220770ce))
ROM_LOAD16_BYTE("qb3_ue_u7.bin", 0x4000, 0x2000, CRC(050a996d) SHA1(bf29236112746b5925b29fb231f152a4bde3f4f9))
ROM_LOAD16_BYTE("qb3_uo_r7.bin", 0x4001, 0x2000, CRC(33fa77a2) SHA1(27a6853f8c2614a2abd7bfb9a62c357797312068))

ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("qb3_snd_u12.bin", 0x0000, 0x1000, CRC(f86663de) SHA1(29c7e75ba22be00d59fc8de5de6d94fcee287a09))
ROM_LOAD("qb3_snd_u11.bin", 0x1000, 0x1000, CRC(32ed58fc) SHA1(483a19f0d540d7d348fce4274fba254ee95bc8d6))
ROM_END


// Solar Quest
AAE_DRIVER_BEGIN(drv_solarq, "solarq", "Solar Quest")
AAE_DRIVER_ROM(rom_solarq)
AAE_DRIVER_FUNCS(&init_solarq, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_solarq)
AAE_DRIVER_SAMPLES(solarq_samples)
AAE_DRIVER_ART(solarq_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Star Castle
AAE_DRIVER_BEGIN(drv_starcas, "starcas", "Star Castle")
AAE_DRIVER_ROM(rom_starcas)
AAE_DRIVER_FUNCS(&init_starcas, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_starcas)
AAE_DRIVER_SAMPLES(starcas_samples)
AAE_DRIVER_ART(starcas_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// RipOff
AAE_DRIVER_BEGIN(drv_ripoff, "ripoff", "RipOff")
AAE_DRIVER_ROM(rom_ripoff)
AAE_DRIVER_FUNCS(&init_ripoff, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_ripoff)
AAE_DRIVER_SAMPLES(ripoff_samples)
AAE_DRIVER_ART(ripoff_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU,4980750,1,1,0,0,nullptr,nullptr,nullptr, nullptr,nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Armor Attack
AAE_DRIVER_BEGIN(drv_armora, "armora", "Armor Attack")
AAE_DRIVER_ROM(rom_armora)
AAE_DRIVER_FUNCS(&init_armora, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_armora)
AAE_DRIVER_SAMPLES(armora_samples)
AAE_DRIVER_ART(armora_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY2, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 772)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Barrier
AAE_DRIVER_BEGIN(drv_barrier, "barrier", "Barrier")
AAE_DRIVER_ROM(rom_barrier)
AAE_DRIVER_FUNCS(&init_barrier, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_barrier)
AAE_DRIVER_SAMPLES(barrier_samples)
AAE_DRIVER_ART(barrier_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Sundance
AAE_DRIVER_BEGIN(drv_sundance, "sundance", "Sundance")
AAE_DRIVER_ROM(rom_sundance)
AAE_DRIVER_FUNCS(&init_sundance, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_sundance)
AAE_DRIVER_SAMPLES(sundance_samples)
AAE_DRIVER_ART(sundance_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Warrior
AAE_DRIVER_BEGIN(drv_warrior, "warrior", "Warrior")
AAE_DRIVER_ROM(rom_warrior)
AAE_DRIVER_FUNCS(&init_warrior, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_warrior)
AAE_DRIVER_SAMPLES(warrior_samples)
AAE_DRIVER_ART(warrior_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 780)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// TailGunner
AAE_DRIVER_BEGIN(drv_tailg, "tailg", "TailGunner")
AAE_DRIVER_ROM(rom_tailg)
AAE_DRIVER_FUNCS(&init_tailg, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_tailg)
AAE_DRIVER_SAMPLES(tailg_samples)
AAE_DRIVER_ART(tailg_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// StarHawk
AAE_DRIVER_BEGIN(drv_starhawk, "starhawk", "StarHawk")
AAE_DRIVER_ROM(rom_starhawk)
AAE_DRIVER_FUNCS(&init_starhawk, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_starhawk)
AAE_DRIVER_SAMPLES(starhawk_samples)
AAE_DRIVER_ART(starhawk_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// SpaceWar
AAE_DRIVER_BEGIN(drv_spacewar, "spacewar", "SpaceWar")
AAE_DRIVER_ROM(rom_spacewar)
AAE_DRIVER_FUNCS(&init_spacewar, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_spacewar)
AAE_DRIVER_SAMPLES(spacewar_samples)
AAE_DRIVER_ART(spacewar_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Speed Freak
AAE_DRIVER_BEGIN(drv_speedfrk, "speedfrk", "Speed Freak")
AAE_DRIVER_ROM(rom_speedfrk)
AAE_DRIVER_FUNCS(&init_speedfrk, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_speedfrk)
AAE_DRIVER_SAMPLES(speedfrk_samples)
AAE_DRIVER_ART(speedfrk_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Demon
AAE_DRIVER_BEGIN(drv_demon, "demon", "Demon")
AAE_DRIVER_ROM(rom_demon)
AAE_DRIVER_FUNCS(&init_demon, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_demon)
AAE_DRIVER_SAMPLES(demon_samples)
AAE_DRIVER_ART(demon_art)
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY2, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 800)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Boxing Bugs
AAE_DRIVER_BEGIN(drv_boxingb, "boxingb", "Boxing Bugs")
AAE_DRIVER_ROM(rom_boxingb)
AAE_DRIVER_FUNCS(&init_boxingb, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_boxingb)
AAE_DRIVER_SAMPLES(boxingb_samples)
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// War of the Worlds
AAE_DRIVER_BEGIN(drv_wotw, "wotw", "War of the Worlds")
AAE_DRIVER_ROM(rom_wotw)
AAE_DRIVER_FUNCS(&init_wotw, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_wotw)
AAE_DRIVER_SAMPLES(wotw_samples)
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// QB-3 (prototype)
AAE_DRIVER_BEGIN(drv_qb3, "qb3", "QB-3 (prototype)")
AAE_DRIVER_ROM(rom_qb3)
AAE_DRIVER_FUNCS(&init_qb3, &run_cinemat, &end_cinemat)
AAE_DRIVER_INPUT(input_ports_qb3)
AAE_DRIVER_SAMPLES(wotw_samples)
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(AAE_CPU_ENTRY(CPU_CCPU, 4980750, 1, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY())
AAE_DRIVER_VIDEO_CORE(38, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_solarq)
AAE_REGISTER_DRIVER(drv_starcas)
AAE_REGISTER_DRIVER(drv_ripoff)
AAE_REGISTER_DRIVER(drv_armora)
AAE_REGISTER_DRIVER(drv_barrier)
AAE_REGISTER_DRIVER(drv_sundance)
AAE_REGISTER_DRIVER(drv_warrior)
AAE_REGISTER_DRIVER(drv_tailg)
AAE_REGISTER_DRIVER(drv_starhawk)
AAE_REGISTER_DRIVER(drv_spacewar)
AAE_REGISTER_DRIVER(drv_speedfrk)
AAE_REGISTER_DRIVER(drv_demon)
AAE_REGISTER_DRIVER(drv_boxingb)
AAE_REGISTER_DRIVER(drv_wotw)
AAE_REGISTER_DRIVER(drv_qb3)
