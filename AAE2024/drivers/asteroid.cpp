//============================================================================
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
/* Asteroid Emu */
#include "asteroid.h"
#include "aae_mame_driver.h"
#include "samples.h"
#include "earom.h"
#include "aae_mame_pokey_2.4.h"
#include "acommon.h"
#include "loaders.h"
#include "old_mame_vecsim_dvg.h"

#define MASTER_CLOCK (12096000)
#define CLOCK_3KHZ   (MASTER_CLOCK / 4096)

static int THREEKHZ_CLOCK = 0;
/*
	Asteroids Voice breakdown:
	0 - thump
	1 - saucer
	2 - player fire
	3 - saucer fire
	4 - player thrust
	5 - extra life
	6 - explosions
*/

/* Constants for the sound names in the asteroid sample array */
/* Move the sounds Astdelux and Asteroid have in common to the */
/* beginning. BW */
/* Swapped High and Low thump. Corrected saucer sound stop */

#define kExplode1    0
#define kExplode2    1
#define kExplode3    2
#define kExplode4    3
#define kThrust      4
#define kHighThump   5
#define kLowThump    6
#define kFire        7
#define kLargeSaucer 8
#define kSmallSaucer 9
#define kSaucerFire	 10
#define kLife        11

void clock3k_update()
{

	THREEKHZ_CLOCK ^= 1;
}
void asteroid_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if (!(readinputport(0) & 0x80))
	{
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	}
}

int asteroid1_hiload()
{
	if (memcmp(&GI[CPU0][0x001c], "\x00\x00", 2) == 0 && memcmp(&GI[CPU0][0x0050], "\x00\x00", 2) == 0 &&
		memcmp(&GI[CPU0][0x0000], "\x10", 1) == 0) {
		load_hi_aae(0x1c, 0x35, 0);

		return 1;
	}
	else return 0;	// we can't load the hi scores yet
}

void asteroid1_hisave()
{
	save_hi_aae(0x1c, 0x35, 0);
}

int asteroid_hiload()
{
	if (memcmp(&GI[CPU0][0x001d], "\x00\x00", 2) == 0 && memcmp(&GI[CPU0][0x0050], "\x00\x00", 2) == 0 &&
		memcmp(&GI[CPU0][0x0000], "\x10", 1) == 0) {
		load_hi_aae(0x1d, 0x35, 0);

		return 1;
	}
	else return 0;	// we can't load the hi scores yet
}

void asteroid_hisave()
{
	save_hi_aae(0x1d, 0x35, 0);
}

/*************************************
*
*	Sound Handlers
*
*************************************/

WRITE_HANDLER(asteroid_noise_reset_w)
{
	//sample_stop (2);
}

WRITE_HANDLER(astdelux_sounds_w)
{
	static int lastthrust = 0;

	if (!(data & 0x80) && (lastthrust & 0x80))
	{
		voice_ramp_volume(kThrust, 30, 0);
	}
	if ((data & 0x80) && !(lastthrust & 0x80))
	{
		voice_ramp_volume(kThrust, 30, config.mainvol);
	}

	lastthrust = data;
}

WRITE_HANDLER(asteroid_thump_w)
{
	/* is the volume bit on? */
	if (data & 0x10)
	{
		int sound;

		if (data & 0x0f)
			sound = kLowThump;
		else
			sound = kHighThump;
		sample_start(7, sound, 0);
	}
}

WRITE_HANDLER(asteroid_sounds_w)
{
	static int fire = 0;
	static int sfire = 0;
	static int saucer = 0;
	static int lastsaucer = 0;
	static int lastthrust = 0;
	int sound;
	int fire2;
	int sfire2;

	switch (address & 0xf)
	{
	case 0: // Saucer sounds
		if ((data & 0x80) && !(lastsaucer & 0x80))
		{
			if (saucer)
				sound = kLargeSaucer;
			else
				sound = kSmallSaucer;
			sample_start(2, sound, 1);
		}
		if (!(data & 0x80) && (lastsaucer & 0x80))
			sample_stop(2);
		lastsaucer = data;
		break;
	case 1: // Saucer fire
		sfire2 = data & 0x80;
		if (sfire2 != sfire)
		{
			sample_stop(3);
			if (sfire2) sample_start(3, kSaucerFire, 0);
		}
		sfire = sfire2;
		break;
	case 2: // Saucer sound select
		saucer = data & 0x80;
		break;
	case 3:
		if ((data & 0x80) && !(lastthrust & 0x80))
		{
			//sample_start(4, kThrust, 1);
			//sample_set_volume(kThrust, config.mainvol);
			voice_ramp_volume(kThrust, 30, config.mainvol);
		}
		if (!(data & 0x80) && (lastthrust & 0x80))
		{
			//sample_stop(4);
			//sample_set_volume(kThrust, 0);
			voice_ramp_volume(kThrust, 30, 0);
		}
		lastthrust = data;
		break;
	case 4: // Player fire
		fire2 = data & 0x80;
		if (fire2 != fire)
		{
			sample_stop(5);
			if (fire2) sample_start(5, kFire, 0);
		}
		fire = fire2;
		break;
	case 5: // life sound
		if (data & 0x80) sample_start(6, kLife, 0);
		break;
	}
}

WRITE_HANDLER(asteroid_explode_w)
{
	static int explosion = -1;
	int explosion2;
	int sound = -1;

	if (data & 0x3c)
	{
		explosion2 = data >> 6;
		if (explosion2 != explosion)
		{
			//sample_stop(7); //Not Needed, done for us with reallocate in allegro code
			switch (explosion2)
			{
			case 0:
			case 1:
				sound = kExplode1;
				break;
			case 2:
				sound = kExplode2;
				break;
			case 3:
				sound = kExplode3;
				break;
			}

			sample_start(1, sound, 0);
		}
		explosion = explosion2;
	}
	else explosion = -1;
}
///////////////////////////////////////////////////////////////////////////////
WRITE_HANDLER(astdelux_led_w)
{  //From M.A.M.E. (TM)
	static int led0 = 0;
	static int led1 = 0;

	if (address & 0xff) { led1 = (data & 0x80) ? 0 : 1; }
	else { led0 = (data & 0x80) ? 0 : 1; }

	set_aae_leds(led0, led1, 0);
}

WRITE_HANDLER(astdelux_bank_switch_w)
{
	static int astdelux_bank = 0;
	int astdelux_newbank;
	unsigned char* RAM = GI[CPU0];

	astdelux_newbank = (data >> 7) & 1;
	//SCRFLIP = GI[CPU0][0x1e];
	if (astdelux_bank != astdelux_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		int temp;
		int i;

		astdelux_bank = astdelux_newbank;
		for (i = 0; i < 0x100; i++) {
			temp = RAM[0x200 + i];
			RAM[0x200 + i] = RAM[0x300 + i];
			RAM[0x300 + i] = temp;
		}
	}
}
////////////  CALL ASTEROIDS SWAPRAM  ////////////////////////////////////////////
WRITE_HANDLER(asteroid_bank_switch_w)
{
	UINT8 buffer[0x100];
	static int asteroid_bank = 0;
	int asteroid_newbank;
	asteroid_newbank = (data >> 2) & 1;
	
	//wrlog("Asteroid Bankswitch write %x", data);

	//SCRFLIP = GI[CPU0][0x18];
	if (asteroid_bank != asteroid_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		asteroid_bank = asteroid_newbank;
		memcpy(buffer, GI[CPU0] + 0x200, 0x100);
		memcpy(GI[CPU0] + 0x200, GI[CPU0] + 0x300, 0x100);
		memcpy(GI[CPU0] + 0x300, buffer, 0x100);
	}
	set_aae_leds((~data & 0x02), (~data & 0x01), 0);
}

READ_HANDLER(asteroid_IN0_r)
{
	int res;
	int bitmask;

	res = readinputportbytag("IN0");

	bitmask = (1 << (address));

	if (get_eterna_ticks(0) & 0x100)
		res |= 0x02;

	if (!dvg_done())
	{
		//wrlog("DVG returning IN0 BUSY? Cycles %d", cpu_getcycles(0));
		res |= 0x04;
	}
	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;

	return res;
}

READ_HANDLER(asteroid_IN1_r)
{
	int res;
	int bitmask;

	res = readinputportbytag("IN1");

	address &= 0xf;

	bitmask = (1 << address);

	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;
	return (res);
}

READ_HANDLER(asteroid_DSW1_r)
{
	int res;
	int res1;

	res1 = readinputportbytag("DSW1");

	res = 0xfc | ((res1 >> (2 * (3 - (address & 0x3)))) & 0x3);
	return res;
}

void run_asteroid()
{
	
}

void run_astdelux()
{
	pokey_sh_update(); 
}
/////////////////END MAIN LOOP/////////////////////////////////////////////

static struct POKEYinterface pokey_interface =
{
	1,			/* 4 chips */
	1512000,
	255,	/* volume */
	POKEY_DEFAULT_GAIN,
	NO_CLIP,
	/* The 8 pot handlers */
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0},
	/* The allpot handler */
	{ input_port_3_r }, //Dip here
};

MEM_READ(AsteroidDeluxeRead)
MEM_ADDR(0x2000, 0x2007, asteroid_IN0_r)
MEM_ADDR(0x2400, 0x2407, asteroid_IN1_r)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r)
MEM_ADDR(0x2c00, 0x2c0f, pokey_1_r)
MEM_ADDR(0x2c40, 0x2c7f, EaromRead)
MEM_END

MEM_WRITE(AsteroidDeluxeWrite)
MEM_ADDR(0x2c00, 0x2c0f, pokey_1_w)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3c03, 0x3c03, astdelux_sounds_w)
MEM_ADDR(0x3c04, 0x3c04, astdelux_bank_switch_w)
MEM_ADDR(0x3600, 0x3600, asteroid_explode_w)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3200, 0x323f, EaromWrite)
MEM_ADDR(0x3a00, 0x3a00, EaromCtrl)
MEM_ADDR(0x3c00, 0x3c01, astdelux_led_w)
MEM_ADDR(0x4800, 0x7fff, MWA_ROM)
MEM_END

MEM_READ(AsteroidRead)
MEM_ADDR(0x2000, 0x2007, asteroid_IN0_r)
MEM_ADDR(0x2400, 0x2407, asteroid_IN1_r)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r) /* DSW1 */
MEM_END

MEM_WRITE(AsteroidWrite)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3200, 0x3200, asteroid_bank_switch_w)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3600, 0x3600, asteroid_explode_w)
MEM_ADDR(0x3a00, 0x3a00, asteroid_thump_w)
MEM_ADDR(0x3c00, 0x3c05, asteroid_sounds_w)
MEM_ADDR(0x6800, 0x7fff, MWA_ROM) //Program Rom
MEM_ADDR(0x5000, 0x57ff, MWA_ROM) //Vector Rom
MEM_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
void end_asteroid()
{
	sample_stop(4);
	dvg_end();
}

void end_astdelux()
{
	sample_stop(4);
	pokey_sh_stop();
}
int init_asteroid(void)
{
	init6502(AsteroidRead, AsteroidWrite, 0x7fff, CPU0);
	
	dvg_start_asteroid();

	sample_set_volume(4, 0);
	sample_start(4, kThrust, 1);
	sample_set_volume(4, 0);

	//timer_set(TIME_IN_HZ(MASTER_CLOCK / 4096), 0, clock3k_update);

	wrlog("End init");
	return 0;
}

int init_astdelux(void)
{
	int k;

	init6502(AsteroidDeluxeRead, AsteroidDeluxeWrite, 0x7fff, CPU0);

	dvg_start_asteroid();

	sample_set_volume(4, 0);
	sample_start(4, kThrust, 1);
	sample_set_volume(4, 0);

	k = pokey_sh_start(&pokey_interface);

	return 0;
}