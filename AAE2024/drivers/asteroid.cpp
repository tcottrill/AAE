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
#include "vector.h"
#include "earom.h"
#include "aae_avg.h"
#include "aae_mame_pokey_2.4.h"
#include "acommon.h"
#include "loaders.h"
#include "timer.h"

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

int SCRFLIP;
int dvggo = 0;
UINT8 buffer[0x100];

int psound = 0;
int vec_done = 0;

void asteroid_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if (!(readinputport(0) & 0x80)) { set_pending_interrupt(INT_TYPE_NMI, CPU0); }
}

int asteroid_load_hi()
{
	int num = 0;
	if (gamenum == ASTEROID) { num = 0x1d; }
	else num = 0x1c;

	if (memcmp(&GI[CPU0][num], "\x00\x00", 2) == 0 && memcmp(&GI[CPU0][0x0050], "\x00\x00", 2) == 0 &&
		memcmp(&GI[CPU0][0x0000], "\x10", 1) == 0) {
		load_hi_aae(num, 0x35, 0);

		return 1;
	}
	else return 0;	// we can't load the hi scores yet
}

void asteroid_save_hi()
{
	int num;
	if (gamenum == ASTEROID) { num = 0x1d; }
	else num = 0x1c;
	save_hi_aae(num, 0x35, 0);
}

void set_ast_colors()
{
	int i = 0;

	vec_colors[0].r = 0;
	vec_colors[0].g = 0;
	vec_colors[0].b = 0;

	for (i = 1; i < 17; i++)
	{
		vec_colors[i].r = i * 16;
		vec_colors[i].g = i * 16;
		vec_colors[i].b = i * 16;
	}
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
WRITE_HANDLER(DeluxeLedWrite)
{  //From M.A.M.E. (TM)
	static int led0 = 0;
	static int led1 = 0;

	if (address & 0xff) { led1 = (data & 0x80) ? 0 : 1; }
	else { led0 = (data & 0x80) ? 0 : 1; }

	set_aae_leds(led0, led1, 0);
}

WRITE_HANDLER(DeluxeSwapRam)
{
	static int astdelux_bank = 0;
	int astdelux_newbank;
	unsigned char* RAM = GI[CPU0];

	astdelux_newbank = (data >> 7) & 1;
	SCRFLIP = GI[CPU0][0x1e];
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
WRITE_HANDLER(AsteroidsSwapRam)
{
	static int asteroid_bank = 0;
	int asteroid_newbank;
	asteroid_newbank = (data >> 2) & 1;
	SCRFLIP = GI[CPU0][0x18];
	if (asteroid_bank != asteroid_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		asteroid_bank = asteroid_newbank;
		memcpy(buffer, GI[CPU0] + 0x200, 0x100);
		memcpy(GI[CPU0] + 0x200, GI[CPU0] + 0x300, 0x100);
		memcpy(GI[CPU0] + 0x300, buffer, 0x100);
	}
	set_aae_leds((~data & 0x02), (~data & 0x01), 0);
}

static void dvg_vector_timer(int scale)
{
	total_length += scale;
}
/////////////////////////////VECTOR GENERATOR//////////////////////////////////

//UGH, get this out of here
void dvg_generate_vector_list(void)
{
	int  pc = 0x4000;
	int sp = 0;
	int stack[4];
	int scale = 0;
	int done = 0;
	UINT16 firstwd, secondwd = 0;
	UINT16 opcode;
	int  x, y;
	int temp;
	int z;
	int a;
	int  deltax, deltay; //float
	int  currentx, currenty = 0; //float
	total_length = 0;

	while (!done)
	{
		firstwd = memrdwd(pc);
		opcode = firstwd & 0xf000;
		pc++;
		pc++;

		switch (opcode)
		{
		case 0xf000:

			// compute raw X and Y values //
			z = (firstwd & 0xf0) >> 4;
			y = firstwd & 0x0300;
			x = (firstwd & 0x03) << 8;

			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400) { y = -y; }
			if (firstwd & 0x04) { x = -x; }
			//Invert Drawing if in Cocktail mode and Player 2 selected
			if (!testsw) {
				if (SCRFLIP && config.cocktail)
				{
					x = -x;
					y = -y;
				}
			}

			temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >> 11) & 0x01);
			temp = ((scale + temp) & 0x0f);
			if (temp > 9) temp = -1;
			dvg_vector_timer(temp);

			deltax = (x << VEC_SHIFT) >> (9 - temp);
			deltay = (y << VEC_SHIFT) >> (9 - temp);

			if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
			{
				if (z == 7) { cache_txt(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, config.explode_point_size, 255); }
				else { cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 1.0); }
			}

			else
			{
				cache_line(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, (currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0);
				cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 0);
				cache_point((currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0, 0);
			}

			currentx += deltax;
			currenty -= deltay;
			break;

		case 0://done = 1; break;
		case 0x1000:
		case 0x2000:
		case 0x3000:
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
		case 0x8000:
		case 0x9000:

			// Get Second Word
			secondwd = memrdwd(pc); pc++; pc++;
			// compute raw X and Y values and intensity //
			z = secondwd >> 12;
			y = firstwd & 0x03ff;
			x = secondwd & 0x03ff;

			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400)
			{
				y = -y;
			}
			if (secondwd & 0x400)
			{
				x = -x;
			}
			//Invert Drawing if in Cocktail mode and Player 2 selected
			if (!testsw) {
				if (SCRFLIP && config.cocktail)
				{
					x = -x; y = -y;
				}
			}
			// Do overall scaling
			temp = scale + (opcode >> 12); temp = temp & 0x0f;
			if (temp > 9) { temp = -1; }
			dvg_vector_timer(temp);

			deltax = (x << VEC_SHIFT) >> (9 - temp);
			deltay = (y << VEC_SHIFT) >> (9 - temp);

			if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
			{
				if (z > 14) cache_txt(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, config.fire_point_size, 255);
				else cache_point(currentx, currenty, z, 0, 0, 1.0);
			}

			cache_line(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, (currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0);
			cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 0);
			cache_point((currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0, 0);

			currentx += deltax;
			currenty -= deltay;
			break;

		case 0xa000:

			secondwd = memrdwd(pc);
			pc++;
			pc++;
			x = twos_comp_val(secondwd, 12);
			y = twos_comp_val(firstwd, 12);

			//Invert the screen drawing if cocktail and Player 2 selected and we are not in test mode
			if (!testsw) {
				if (SCRFLIP && config.cocktail)
				{
					x = 1024 - x; y = 1024 - y;
				}
			}
			//Do overall draw scaling
			scale = (secondwd >> 12) & 0x0f;
			currenty = (1130 - y) << VEC_SHIFT;
			currentx = x << VEC_SHIFT;
			break;

		case 0xb000: done = 1; break;

		case 0xc000: a = 0x4000 + ((firstwd & 0x0fff) << 1);
			stack[sp] = pc;
			if (sp == 4) { done = 1; sp = 0; }
			else { sp = sp + 1; pc = a; }
			break;

		case 0xd000:
			sp = sp - 1;
			pc = stack[sp];
			break;

		case 0xe000:
			a = 0x4000 + ((firstwd & 0x0fff) << 1);
			pc = a;
			break;
		}
	}
}

//////////////// VECTOR GENERATOR TRIGGER ///////////////////////////////////////
WRITE_HANDLER(Watchdog_reset_w)
{
	WATCHDOG = data;
	//wrlog("Watchdog write %d", data);
}

WRITE_HANDLER(BWVectorGeneratorInternal)
{
	if (vec_done) return;

	dvggo = 1;
	cache_clear();
	total_length = 0;
	dvg_generate_vector_list();
	vec_done = 1;//wrlog("DVG GO CALLED THIS FRAME");
	if (total_length == 0)
	{
		dvggo = 0;
		vec_done = 0;
	}
}

READ_HANDLER(AstPIA1ROCKRead) //FIX
{
	int res;
	//int bitmask;
	int val = 0;
	float me;

	res = 0;

	switch (address)
	{
	case 0x2001: if (get_video_ticks(0) & 0x100)  val = 0x80; break;//if (get_hertz_counter()) val=0x80;break;
	case 0x2002: me = 6.7 * total_length; if (get_video_ticks(0) > me && vec_done == 1) { vec_done = 0; }break;//me = (((4500 * total_length)/ 1000000) * 1512);
		//	case 0x2003: if (key[config.kp1but3] || my_j[config.j1but3])return 0;else return 0x80; break; /*Shield */
		//   case 0x2004: if (key[config.kp1but1] || my_j[config.j1but1])return 0;else return 0x80; break;/* Fire */
	case 0x2005: return 0x80; break;
	case 0x2006: return 0; break;
	case 0x2007: return 0; break;//if (testsw)return 0;else return 0x80;break;/* Self Test */
	}
	return 0;
}

READ_HANDLER(AstPIA1Read)
{
	float me;
	int res;
	int bitmask;

	res = readinputportbytag("IN0");

	bitmask = (1 << (address));

	if (get_video_ticks(0) & 0x100)
		res |= 0x02;
	if (address == 0x02)
	{
		me = (((4500 * total_length) / 1000000) * 1512);
		if (get_video_ticks(0) > me && vec_done == 1) { vec_done = 0; res |= 0x04; }
	}
	//if (!avgdvg_done()) {res |= 0x04;}

	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;

	return res;
}

READ_HANDLER(AstPIA2Read)
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

void run_asteroids()
{
	static int k = 0;
	vec_done = 0;

	if (psound && !paused) { pokey_sh_update(); }

	wrlog("Watchdog this frame %x", WATCHDOG);
	if (WATCHDOG == 0) k++;
	if (k > 60)
	{
		k = 0;
		cpu_reset(0);
	}
	WATCHDOG = 0;
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

MEM_WRITE(AsteroidDeluxeWrite)
MEM_ADDR(0x2c00, 0x2c0f, pokey_1_w)
MEM_ADDR(0x3000, 0x3000, BWVectorGeneratorInternal)
MEM_ADDR(0x3c03, 0x3c03, astdelux_sounds_w)
MEM_ADDR(0x3c04, 0x3c04, DeluxeSwapRam)
MEM_ADDR(0x3600, 0x3600, asteroid_explode_w)
MEM_ADDR(0x3400, 0x3400, Watchdog_reset_w)
MEM_ADDR(0x3200, 0x323f, EaromWrite)
MEM_ADDR(0x3a00, 0x3a00, EaromCtrl)
MEM_ADDR(0x3c00, 0x3c01, DeluxeLedWrite)
MEM_ADDR(0x4800, 0x7fff, NoWrite)
MEM_END

MEM_READ(AsteroidDeluxeRead)
MEM_ADDR(0x2000, 0x2007, AstPIA1Read)
MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r)
MEM_ADDR(0x2c00, 0x2c0f, pokey_1_r)
MEM_ADDR(0x2c40, 0x2c7f, EaromRead)
MEM_END

MEM_READ(AsteroidRead)
MEM_ADDR(0x2000, 0x2007, AstPIA1Read)
MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
MEM_ADDR(0x2800, 0x2803, asteroid_DSW1_r) /* DSW1 */
MEM_END

MEM_READ(AsterRockRead)
MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
MEM_ADDR(0x2000, 0x2007, AstPIA1ROCKRead)
MEM_END

MEM_WRITE(AsteroidWrite)
MEM_ADDR(0x3000, 0x3000, BWVectorGeneratorInternal)
MEM_ADDR(0x3200, 0x3200, AsteroidsSwapRam)
MEM_ADDR(0x3400, 0x3400, Watchdog_reset_w)
MEM_ADDR(0x3600, 0x3600, asteroid_explode_w)
MEM_ADDR(0x3a00, 0x3a00, asteroid_thump_w)
MEM_ADDR(0x3c00, 0x3c05, asteroid_sounds_w)
MEM_ADDR(0x6800, 0x7fff, NoWrite) //Program Rom
MEM_ADDR(0x5000, 0x57ff, NoWrite) //Vector Rom
MEM_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
void end_asteroids()
{
	sample_stop(4);
	asteroid_save_hi();
}

void end_astdelux()
{
	sample_stop(4);
	cache_clear();
	//SaveEarom();
	pokey_sh_stop();
}
int init_asteroid(void)
{
	if (gamenum == ASTEROCK)
	{
		init6502(AsterRockRead, AsteroidWrite, 0);
	}
	else
	{
		init6502(AsteroidRead, AsteroidWrite, 0);
	}

	cache_clear();
	set_ast_colors();

	if (config.bezel) { config.cocktail = 0; }//Just to check for stupidity

	psound = 0;
	dvggo = 0;
	total_length = 0;
	AsteroidsSwapRam(0, 0, 0);

	SCRFLIP = 0;

	//timer_set(TIME_IN_HZ(23), 0, watchdog_callback);
	sample_set_volume(4, 0);
	sample_start(4, kThrust, 1);
	sample_set_volume(4, 0);

	wrlog("End init");
	return 0;
}

int init_astdelux(void)
{
	int k;
	if (config.bezel) { config.cocktail = 0; }//Just to check
	init6502(AsteroidDeluxeRead, AsteroidDeluxeWrite, 0);

	cache_clear();
	set_ast_colors();
	
	psound = 1; //Enable Pokey Sound Processing
	dvggo = 0;
	vec_done = 0;
	total_length = 0;

	sample_set_volume(4, 0);
	sample_start(4, kThrust, 1);
	sample_set_volume(4, 0);

	//timer_set(TIME_IN_HZ(23), 0, watchdog_callback);

	k = pokey_sh_start(&pokey_interface);

	return 0;
}