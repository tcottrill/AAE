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
#include "driver_registry.h"    // AAE_REGISTER_DRIVER
#include "earom.h"
#include "aae_pokey.h"
#include "acommon.h"
#include "old_mame_vecsim_dvg.h"

#define MASTER_CLOCK (12096000)
#define CLOCK_3KHZ   (MASTER_CLOCK / 4096)

int get_bit(int num, int bit_position) {
	return (num >> bit_position) & 1; // Shift right and AND with 1 to isolate the bit
}


static const char* asteroidsamples[] =
{
	"asteroid.zip",
	"explode1.wav",
	"explode2.wav",
	"explode3.wav",
	"explode4.wav",
	"thrust.wav",
	"thumphi.wav",
	"thumplo.wav",
	"fire.wav",
	"lsaucer.wav",
	"ssaucer.wav",
	"sfire.wav",
	"life.wav",
	0	/* end of array */
};

static const char* deluxesamples[] =
{
	"astdelux.zip",
	"explode1.wav",
	"explode2.wav",
	"explode3.wav",
	"explode4.wav",
	"thrust.wav",
	0
};

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

void asterock_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if ((readinputport(0) & 0x80))
	{
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	}
}

int asteroid1_hiload()
{
	if (memcmp(&Machine->memory_region[CPU0][0x001c], "\x00\x00", 2) == 0 && memcmp(&Machine->memory_region[CPU0][0x0050], "\x00\x00", 2) == 0 &&
		memcmp(&Machine->memory_region[CPU0][0x0000], "\x10", 1) == 0) {
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
	if (memcmp(&Machine->memory_region[CPU0][0x001d], "\x00\x00", 2) == 0 && memcmp(&Machine->memory_region[CPU0][0x0050], "\x00\x00", 2) == 0 &&
		memcmp(&Machine->memory_region[CPU0][0x0000], "\x10", 1) == 0) {
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
		sample_stop(4);
	}
	if ((data & 0x80) && !(lastthrust & 0x80))
	{
		sample_start(4, kThrust, 1);
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
			sample_start(4, kThrust, 1);
		}
		if (!(data & 0x80) && (lastthrust & 0x80))
		{
			sample_stop(4);
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
	/*
	static int explosion = -1;
	int explosion2;
	int sound = -1;

	if (data & 0x3c)
	{
		explosion2 = data >> 6;
		if (explosion2 != explosion)
		{
			//Not Needed, done for us with reallocate in allegro code
			//LOG_INFO("Calling sample stop explode");
			//	sample_stop(7);
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
		
			//LOG_INFO("Calling sample start explode");
			sample_start(8, sound, 0);
		}
		explosion = explosion2;
	}
	else explosion = -1;
	*/
	if (data == 0x3d) { sample_start(8, 0, 0); }
	if (data == 0xfd) { sample_start(8, 1, 0); }
	if (data == 0xbd) { sample_start(8, 2, 0); }


}
///////////////////////////////////////////////////////////////////////////////
WRITE_HANDLER(astdelux_led_w)
{  //From M.A.M.E. (TM)
	static int led0 = 0;
	static int led1 = 0;

	if (address & 0xff) 
	{
		led1 = (data & 0x80) ? 0 : 1; 
	}
	else 
	{
		led0 = (data & 0x80) ? 0 : 1; 
	}
		
	set_led_status(0,led0);
	set_led_status(1, led1);
}

WRITE_HANDLER(astdelux_bank_switch_w)
{
	static int astdelux_bank = 0;
	int astdelux_newbank;
	unsigned char* RAM = Machine->memory_region[CPU0];

	astdelux_newbank = (data >> 7) & 1;
	
	if (readinputportbytag("COCKTAIL") & 0x01) set_screen_flipping(astdelux_newbank);

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
	
	if (readinputportbytag("COCKTAIL") & 0x01) set_screen_flipping(asteroid_newbank);
	 
	if (asteroid_bank != asteroid_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		asteroid_bank = asteroid_newbank;
		memcpy(buffer, Machine->memory_region[CPU0] + 0x200, 0x100);
		memcpy(Machine->memory_region[CPU0] + 0x200, Machine->memory_region[CPU0] + 0x300, 0x100);
		memcpy(Machine->memory_region[CPU0] + 0x300, buffer, 0x100);
	}
	
	set_led_status(0, ~data & 0x02);
	set_led_status(1, ~data & 0x01);
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
		//LOG_INFO("DVG returning IN0 BUSY? Cycles %d", cpu_getcycles(0));
		res |= 0x04;
	}
	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;

	return res;
}

READ_HANDLER(asterock_IN0_r)
{
	int res;
	int bitmask;

	res = readinputport(0);
	bitmask = (1 << address);
	
	if (get_eterna_ticks(0) & 0x100)
		res |= 0x04;
	if (!dvg_done())
		res |= 0x1;

	if (res & bitmask)
		res = ~0x80;
	else
		res = 0x80;
	if (address == 7) return 0;
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
	6,
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
MEM_ADDR(0x0000, 0x03ff, MRA_RAM)
MEM_ADDR(0x2000, 0x2007, asteroid_IN0_r)
MEM_ADDR(0x2400, 0x2407, asteroid_IN1_r)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r) /* DSW1 */
MEM_ADDR(0x4000, 0x47ff, MRA_RAM)
MEM_ADDR(0x5000, 0x57ff, MRA_ROM)
MEM_ADDR(0x6800, 0x7fff, MRA_ROM)
MEM_END

MEM_READ(AsterockRead)
MEM_ADDR(0x2000, 0x2007, asterock_IN0_r)
MEM_ADDR(0x2400, 0x2407, asteroid_IN1_r)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r) /* DSW1 */
MEM_END

MEM_WRITE(AsteroidWrite)
MEM_ADDR(0x0000, 0x03ff, MWA_RAM)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3200, 0x3200, asteroid_bank_switch_w)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3600, 0x3600, asteroid_explode_w)
MEM_ADDR(0x3a00, 0x3a00, asteroid_thump_w)
MEM_ADDR(0x3c00, 0x3c05, asteroid_sounds_w)
MEM_ADDR(0x4000, 0x47ff, MWA_RAM)
MEM_ADDR(0x5000, 0x57ff, MWA_ROM) //Vector Rom
MEM_ADDR(0x6800, 0x7fff, MWA_ROM) //Program Rom
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
	//init6502(AsteroidRead, AsteroidWrite, 0x7fff, CPU0);
	
	dvg_start_asteroid();
	//timer_set(TIME_IN_HZ(MASTER_CLOCK / 4096), 0, clock3k_update);

	LOG_INFO("End init");
	return 0;
}

int init_asterock(void)
{
	//init6502(AsterockRead, AsteroidWrite, 0x7fff, CPU0);

	dvg_start_asteroid();
	//timer_set(TIME_IN_HZ(MASTER_CLOCK / 4096), 0, clock3k_update);

	LOG_INFO("End Asterock init");
	return 0;
}

int init_astdelux(void)
{
	int k;

	//init6502(AsteroidDeluxeRead, AsteroidDeluxeWrite, 0x7fff, CPU0);
	dvg_start_asteroid();
	k = pokey_sh_start(&pokey_interface);

	return 0;
}

//ART

ART_START(asteroidsart)
ART_LOAD("asteroid.zip", "asteroids_internal.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

ART_START(astdeluxart)
ART_LOAD("astdelux.zip", "astdelux.png", ART_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("astdelux.zip", "asteroids_deluxe_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

ART_START(astdelu1art)
ART_LOAD("astdelux.zip", "astdelu1.png", ART_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("astdelux.zip", "asteroids_deluxe_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

// InputPorts
// Asteroid

INPUT_PORTS_START(asteroid)
PORT_START("IN0") /* IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* Bit 2 and 3 are handled in the machine dependent part. */
/* Bit 2 is the 3 KHz source and Bit 3 the VG_HALT bit    */
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON3)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_SERVICE(0x80, IP_ACTIVE_HIGH)

PORT_START("IN1") /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

PORT_START("DSW1") /* DSW 1 */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "Spanish")
PORT_DIPNAME(0x04, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x00, "4")
PORT_DIPNAME(0x08, 0x00, "Center Mech")
PORT_DIPSETTING(0x00, "X 1")
PORT_DIPSETTING(0x08, "X 2")
PORT_DIPNAME(0x30, 0x00, "Right Mech")
PORT_DIPSETTING(0x00, "X 1")
PORT_DIPSETTING(0x10, "X 4")
PORT_DIPSETTING(0x20, "X 5")
PORT_DIPSETTING(0x30, "X 6")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Coinage))
PORT_DIPSETTING(0xc0, DEF_STR(2C_1C))
PORT_DIPSETTING(0x80, DEF_STR(1C_1C))
PORT_DIPSETTING(0x40, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))

PORT_START("COCKTAIL")// Fake Cocktail Switch 
PORT_DIPNAME(0x01, 0x00, "Cabinet")
PORT_DIPSETTING(0x00, "Upright")
PORT_DIPSETTING(0x01, "Cocktail")
PORT_BIT(0xfe, IP_ACTIVE_HIGH, IPT_UNKNOWN)
INPUT_PORTS_END

INPUT_PORTS_START(asterock)
PORT_START("IN0")
// Bit 0 is VG_HALT, handled in the machine dependent part 
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* Bit 2 and 3 are handled in the machine dependent part. */
/* Bit 2 is the 3 KHz source and Bit 3 the VG_HALT bit    */
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON3)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_TILT)
PORT_SERVICE(0x80, IP_ACTIVE_HIGH)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

PORT_START("DSW")
PORT_DIPNAME(0x03, 0x03, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "French")
PORT_DIPSETTING(0x02, "German")
PORT_DIPSETTING(0x03, "Italian")
PORT_DIPNAME(0x0C, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x08, "4")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x10, 0x00, "Records Table")
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x10, DEF_STR(On))
PORT_DIPNAME(0x20, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x20, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Coinage))
PORT_DIPSETTING(0xc0, DEF_STR(2C_1C))
PORT_DIPSETTING(0x80, DEF_STR(1C_1C))
PORT_DIPSETTING(0x40, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))


PORT_START("COCKTAIL")// Fake Cocktail Switch 
PORT_DIPNAME(0x01, 0x00, "Cabinet")
PORT_DIPSETTING(0x00, "Upright")
PORT_DIPSETTING(0x01, "Cocktail")
PORT_BIT(0xfe, IP_ACTIVE_HIGH, IPT_UNKNOWN)

INPUT_PORTS_END

INPUT_PORTS_START(astdelux)
// PORT_START_TAG("IN0")
PORT_START("IN0") /* IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* Bit 2 and 3 are handled in the machine dependent part. */
/* Bit 2 is the 3 KHz source and Bit 3 the VG_HALT bit    */
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON3)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_SERVICE(0x80, IP_ACTIVE_HIGH)

PORT_START("IN1") /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

PORT_START("DSW1") /* DSW 1 */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "Spanish")
PORT_DIPNAME(0x0c, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2-4")
PORT_DIPSETTING(0x04, "3-5")
PORT_DIPSETTING(0x08, "4-6")
PORT_DIPSETTING(0x0c, "5-7")
PORT_DIPNAME(0x10, 0x00, "Minimum plays")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x10, "2")
PORT_DIPNAME(0x20, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPSETTING(0x20, "Easy")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x40, "12000")
PORT_DIPSETTING(0x80, "15000")
PORT_DIPSETTING(0xc0, "None")

PORT_START("DSW2") /* DSW 2 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x00, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x03, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x0c, "Right Coin")
PORT_DIPSETTING(0x00, "*6")
PORT_DIPSETTING(0x04, "*5")
PORT_DIPSETTING(0x08, "*4")
PORT_DIPSETTING(0x0c, "*1")
PORT_DIPNAME(0x10, 0x10, "Center Coin")
PORT_DIPSETTING(0x00, "*2")
PORT_DIPSETTING(0x10, "*1")
PORT_DIPNAME(0xe0, 0x80, "Bonus Coins")
PORT_DIPSETTING(0x60, "1 each 5")
PORT_DIPSETTING(0x80, "2 each 4")
PORT_DIPSETTING(0xa0, "1 each 4")
PORT_DIPSETTING(0xc0, "1 each 2")
PORT_DIPSETTING(0xe0, "None")

PORT_START("COCKTAIL")// Fake Cocktail Switch 
PORT_DIPNAME(0x01, 0x00, "Cabinet")
PORT_DIPSETTING(0x00, "Upright")
PORT_DIPSETTING(0x01, "Cocktail")
PORT_BIT(0xfe, IP_ACTIVE_HIGH, IPT_UNKNOWN)
INPUT_PORTS_END

// Romsets
ROM_START(meteorts)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("mv_np3.bin", 0x5000, 0x0800, CRC(11d1c4ae) SHA1(433c2c05b92094bbe102c356d7f1a907db13da67))
ROM_LOAD("m0_c1.bin", 0x6800, 0x0800, CRC(dff88688) SHA1(7f4148a580fb6f605499c99e7dde7068eca1651a))
ROM_LOAD("m1_f1.bin", 0x7000, 0x0800, CRC(e53c28a9) SHA1(d9f081e73511ec43377f0c6457747f15a470d4dc))
ROM_LOAD("m2_j1.bin", 0x7800, 0x0800, CRC(64bd0408) SHA1(141d053cb4cce3fece98293136928b527d3ade0f))
ROM_RELOAD(0xf800, 0x800)
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END


ROM_START(asteroidb)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("035127-02.np3", 0x5000, 0x0800, CRC(8b71fd9e) SHA1(8cd5005e531eafa361d6b7e9eed159d164776c70))
ROM_LOAD("035145ll.de1", 0x6800, 0x0800, CRC(605fc0f2) SHA1(8d897a3b75bd1f2537470f0a34a97a8c0853ee08))
ROM_LOAD("035144ll.c1", 0x7000, 0x0800, CRC(e106de77) SHA1(003e99d095bd4df6fae243ea1dd5b12f3eb974f1))
ROM_LOAD("035143ll.b1", 0x7800, 0x0800, CRC(6b1d8594) SHA1(ff3cd93f1bc5734bface285e442125b395602d7d))
ROM_RELOAD(0xf800, 0x800)
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END


ROM_START(asterock)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("10505.2", 0x6800, 0x0400, CRC(cdf720c6) SHA1(85fe748096478e28a06bd98ff3aad73ab21b22a4))
ROM_LOAD("10505.3", 0x6c00, 0x0400, CRC(ee58bdf0) SHA1(80094cb5dafd327aff6658ede33106f0493a809d))
ROM_LOAD("10505.4", 0x7000, 0x0400, CRC(8d3e421e) SHA1(5f5719ab84d4755e69bef205d313b455bc59c413))
ROM_LOAD("10505.5", 0x7400, 0x0400, CRC(d2ce7672) SHA1(b6012e09b2439a614a55bcf23be0692c42830e21))
ROM_LOAD("10505.6", 0x7800, 0x0400, CRC(74103c87) SHA1(e568b5ac573a6d0474cf672b3c62abfbd3320799))
ROM_LOAD("10505.7", 0x7c00, 0x0400, CRC(75a39768) SHA1(bf22998fd692fb01964d8894e421435c55d746a0))
ROM_RELOAD(0xf800, 0x800)
// Vector ROM
ROM_LOAD("10505.0", 0x5000, 0x0400, CRC(6bd2053f) SHA1(790f2858f44bbb1854e2d9d549e29f4815c4665b))
ROM_LOAD("10505.1", 0x5400, 0x0400, CRC(231ce201) SHA1(710f4c19864d725ba1c9ea447a97e84001a679f7))
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(asteroid1) //VERSION 1
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("035127-01.np3", 0x5000, 0x0800, CRC(99699366) SHA1(9b2828fc1cef7727f65fa65e1e11e309b7c98792))
ROM_LOAD("035145-01.ef2", 0x6800, 0x0800, CRC(e9bfda64) SHA1(291dc567ebb31b35df83d9fb87f4080f251ff9c8))
ROM_LOAD("035144-01.h2", 0x7000, 0x0800, CRC(e53c28a9) SHA1(d9f081e73511ec43377f0c6457747f15a470d4dc))
ROM_LOAD("035143-01.j2", 0x7800, 0x0800, CRC(7d4e3d05) SHA1(d88000e904e158efde50e453e2889ecd2cb95f24))
ROM_RELOAD(0xf800, 0x800)
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(asteroid) // Version 4
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("035145-04e.ef2", 0x6800, 0x0800, CRC(b503eaf7) SHA1(5369dcfe01c0b9e48b15a96a0de8d23ee8ef9145))
ROM_LOAD("035144-04e.h2", 0x7000, 0x0800, CRC(25233192) SHA1(51b2865fa897cdaa84ac6500c4b4833a80827019))
ROM_LOAD("035143-02.j2", 0x7800, 0x0800, CRC(312caa02) SHA1(1ce2eac1ab90b972e3f1fc3d250908f26328d6cb))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
// Vector ROM
ROM_LOAD("035127-02.np3", 0x5000, 0x0800, CRC(8b71fd9e) SHA1(8cd5005e531eafa361d6b7e9eed159d164776c70))
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(astdelux)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036430-02.d1", 0x6000, 0x0800, CRC(a4d7a525) SHA1(abe262193ec8e1981be36928e9a89a8ac95cd0ad))
ROM_LOAD("036431-02.ef1", 0x6800, 0x0800, CRC(d4004aae) SHA1(aa2099b8fc62a79879efeea70ea1e9ed77e3e6f0))
ROM_LOAD("036432-02.fh1", 0x7000, 0x0800, CRC(6d720c41) SHA1(198218cd2f43f8b83e4463b1f3a8aa49da5015e4))
ROM_LOAD("036433-03.j1", 0x7800, 0x0800, CRC(0dcc0be6) SHA1(bf10ffb0c4870e777d6b509cbede35db8bb6b0b8))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
// Vector ROM
ROM_LOAD("036800-02.r2", 0x4800, 0x0800, CRC(bb8cabe1) SHA1(cebaa1b91b96e8b80f2b2c17c6fd31fa9f156386))
ROM_LOAD("036799-01.np2", 0x5000, 0x0800, CRC(7d511572) SHA1(1956a12bccb5d3a84ce0c1cc10c6ad7f64e30b40))
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(astdelux2)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036800-01.r2", 0x4800, 0x0800, CRC(3b597407) SHA1(344fea2e5d84acce365d76daed61e96b9b6b37cc))
ROM_LOAD("036799-01.np2", 0x5000, 0x0800, CRC(7d511572) SHA1(1956a12bccb5d3a84ce0c1cc10c6ad7f64e30b40))
ROM_LOAD("036430-01.d1", 0x6000, 0x0800, CRC(8f5dabc6) SHA1(5d7543e19acab99ddb63c0ffd60f54d7a0f267f5))
ROM_LOAD("036431-01.ef1", 0x6800, 0x0800, CRC(157a8516) SHA1(9041d8c2369d004f198681e02b59a923fa8f70c9))
ROM_LOAD("036432-01.fh1", 0x7000, 0x0800, CRC(fdea913c) SHA1(ded0138a20d80317d67add5bb2a64e6274e0e409))
ROM_LOAD("036433-02.j1", 0x7800, 0x0800, CRC(d8db74e3) SHA1(52b64e867df98d14742eb1817b59931bb7f941d9))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
ROM_REGION(0x0100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(astdelux1)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036800-01.r2", 0x4800, 0x0800, CRC(3b597407) SHA1(344fea2e5d84acce365d76daed61e96b9b6b37cc))
ROM_LOAD("036799-01.np2", 0x5000, 0x0800, CRC(7d511572) SHA1(1956a12bccb5d3a84ce0c1cc10c6ad7f64e30b40))
ROM_LOAD("036430-01.d1", 0x6000, 0x0800, CRC(8f5dabc6) SHA1(5d7543e19acab99ddb63c0ffd60f54d7a0f267f5))
ROM_LOAD("036431-01.ef1", 0x6800, 0x0800, CRC(157a8516) SHA1(9041d8c2369d004f198681e02b59a923fa8f70c9))
ROM_LOAD("036432-01.fh1", 0x7000, 0x0800, CRC(fdea913c) SHA1(ded0138a20d80317d67add5bb2a64e6274e0e409))
ROM_LOAD("036433-01.j1", 0x7800, 0x0800, CRC(ef09bac7) SHA1(6a4b37dbfe4e6badc4e81036b1430da2e9cb8ca4))
ROM_RELOAD(0xf800, 0x800)
ROM_REGION(0x0100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END
//////////////////////////////////////////////////////////////////////////////////////////////
// DRIVERS
// 
// 

// Meteorites (Asteroids bootleg)
AAE_DRIVER_BEGIN(drv_meteorts, "meteorts", "Meteorites (Asteroids Bootleg)")
AAE_DRIVER_ROM(rom_meteorts)
AAE_DRIVER_FUNCS(&init_asteroid, &run_asteroid, &end_asteroid)
AAE_DRIVER_INPUT(input_ports_asteroid)
AAE_DRIVER_SAMPLES(asteroidsamples)
AAE_DRIVER_ART(asteroidsart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,                  // 4 NMIs per frame
		/*int type*/ INT_TYPE_NMI,
		/*int cb*/   asteroid_interrupt,
		/*r8*/       AsterockRead,       // bootleg read map
		/*w8*/       AsteroidWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////
// Asterock (Asteroids bootleg)
AAE_DRIVER_BEGIN(drv_asterock, "asterock", "Asterock (Asteroids Bootleg)")
AAE_DRIVER_ROM(rom_asterock)
AAE_DRIVER_FUNCS(&init_asterock, &run_asteroid, &end_asteroid)
AAE_DRIVER_INPUT(input_ports_asterock)
AAE_DRIVER_SAMPLES(asteroidsamples)
AAE_DRIVER_ART(asteroidsart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502,
		1512000,
		100,
		4,                  // 4 NMIs per frame
		INT_TYPE_NMI,
		asteroid_interrupt, // interrupt callback
		AsteroidRead,
		AsteroidWrite,
		nullptr, nullptr,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE(asteroid_hiload, asteroid_hisave)
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////
// Asteroids (Bootleg on Lunar Lander Hardware)
AAE_DRIVER_BEGIN(drv_asteroidb, "asteroidb", "Asteroids (Bootleg on Lunar Lander Hardware)")
AAE_DRIVER_ROM(rom_asteroidb)
AAE_DRIVER_FUNCS(&init_asteroid, &run_asteroid, &end_asteroid)
AAE_DRIVER_INPUT(input_ports_asteroid)
AAE_DRIVER_SAMPLES(asteroidsamples)
AAE_DRIVER_ART(asteroidsart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502,
		1512000,
		100,
		4,                  // 4 NMIs per frame
		INT_TYPE_NMI,
		asteroid_interrupt, // interrupt callback
		AsteroidRead,
		AsteroidWrite,
		nullptr, nullptr,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////
// Asteroids (Revision 1)
AAE_DRIVER_BEGIN(drv_asteroid1, "asteroid1", "Asteroids (Revision 1)")
AAE_DRIVER_ROM(rom_asteroid1)
AAE_DRIVER_FUNCS(&init_asteroid, &run_asteroid, &end_asteroid)
AAE_DRIVER_INPUT(input_ports_asteroid)
AAE_DRIVER_SAMPLES(asteroidsamples)
AAE_DRIVER_ART(asteroidsart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502,
		1512000,
		100,
		4,                  // 4 NMIs per frame
		INT_TYPE_NMI,
		asteroid_interrupt, // interrupt callback
		AsteroidRead,
		AsteroidWrite,
		nullptr, nullptr,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE(asteroid1_hiload, asteroid1_hisave)
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////
// Asteroids (Revision 2)
AAE_DRIVER_BEGIN(drv_asteroid, "asteroid", "Asteroids (Revision 2)")
AAE_DRIVER_ROM(rom_asteroid)
AAE_DRIVER_FUNCS(&init_asteroid, &run_asteroid, &end_asteroid)
AAE_DRIVER_INPUT(input_ports_asteroid)
AAE_DRIVER_SAMPLES(asteroidsamples)
AAE_DRIVER_ART(asteroidsart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502,
		1512000,
		100,
		4,                  // 4 NMIs per frame
		INT_TYPE_NMI,
		asteroid_interrupt, // interrupt callback
		AsteroidRead,
		AsteroidWrite,
		nullptr, nullptr,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE(asteroid_hiload, asteroid_hisave)
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////
// Asteroids Deluxe (Revision 1)
// // Asteroids Deluxe (Revision 1)
AAE_DRIVER_BEGIN(drv_astdelux1, "astdelux1", "Asteroids Deluxe (Revision 1)")
AAE_DRIVER_ROM(rom_astdelux1)
AAE_DRIVER_FUNCS(&init_astdelux, &run_astdelux, &end_astdelux)
AAE_DRIVER_INPUT(input_ports_astdelux)
AAE_DRIVER_SAMPLES(deluxesamples)
AAE_DRIVER_ART(astdelu1art)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(CPU_M6502,1512000, 100,4, INT_TYPE_NMI,asteroid_interrupt,AsteroidDeluxeRead,AsteroidDeluxeWrite,
		nullptr,
		nullptr,
		nullptr,
		nullptr	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()
// Asteroids Deluxe (Revision 2)
AAE_DRIVER_BEGIN(drv_astdelux2, "astdelux2", "Asteroids Deluxe (Revision 2)")
AAE_DRIVER_ROM(rom_astdelux2)
AAE_DRIVER_FUNCS(&init_astdelux, &run_astdelux, &end_astdelux)
AAE_DRIVER_INPUT(input_ports_astdelux)
AAE_DRIVER_SAMPLES(deluxesamples)
AAE_DRIVER_ART(astdeluxart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 4, INT_TYPE_NMI, asteroid_interrupt, AsteroidDeluxeRead, AsteroidDeluxeWrite,
		nullptr,
		nullptr,
		nullptr,
		nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Asteroids Deluxe (Revision 3)
AAE_DRIVER_BEGIN(drv_astdelux, "astdelux", "Asteroids Deluxe (Revision 3)")
AAE_DRIVER_ROM(rom_astdelux)
AAE_DRIVER_FUNCS(&init_astdelux, &run_astdelux, &end_astdelux)
AAE_DRIVER_INPUT(input_ports_astdelux)
AAE_DRIVER_SAMPLES(deluxesamples)
AAE_DRIVER_ART(astdeluxart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 4, INT_TYPE_NMI, asteroid_interrupt, AsteroidDeluxeRead, AsteroidDeluxeWrite,
		nullptr,
		nullptr,
		nullptr,
		nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1040, 0, 820)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_meteorts)
AAE_REGISTER_DRIVER(drv_asterock)
AAE_REGISTER_DRIVER(drv_asteroidb)
AAE_REGISTER_DRIVER(drv_asteroid1)
AAE_REGISTER_DRIVER(drv_asteroid)
AAE_REGISTER_DRIVER(drv_astdelux1)
AAE_REGISTER_DRIVER(drv_astdelux2)
AAE_REGISTER_DRIVER(drv_astdelux)