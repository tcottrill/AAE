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

/* Lunar Lander Emu */

#include "llander.h"
#include "aae_mame_driver.h"
#include "samples.h"
#include "vector.h"
#include "old_mame_vecsim_dvg.h"

static int ldvggo = 0;
static int thrust = 0;
static int pic = 0;
static int lamp0 = 0;
static int lastlamp0;
static int a, b, c;

void llander_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if (readinputport(0) & 0x02)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	}
}

READ_HANDLER(llander_zeropage_r)
{
	return GI[CPU0][address];
}

READ_HANDLER(llander_DSW1_r)
{
	int res;
	int res1;

	res1 = readinputportbytag("DSW1");

	res = 0xfc | ((res1 >> (2 * (3 - (address & 0x3)))) & 0x3);
	return res;
}

WRITE_HANDLER(llander_zeropage_w)
{
	GI[CPU0][address] = data;
	GI[CPU0][0x0100 + address] = data;
}

READ_HANDLER(llander_IN0_r)
{
	int val = readinputportbytag("IN0");

	if (dvg_done())
		val |= 0x01;
	if (get_eterna_ticks(0) & 0x100)
		val |= 0x40;

	return val;
}

READ_HANDLER(llander_IN1_r)
{
	int res;
	int bitmask;

	res = readinputportbytag("IN1");

	bitmask = (1 << address);

	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;
	return (res);
}

WRITE_HANDLER(llander_snd_reset_w)
{
	;
}

WRITE_HANDLER(llander_sounds_w)
{
	int tvol;
	//volume    = data & 0x07;
	//tone_3khz = data & 0x10;
	//tone_6khz = data & 0x20; //Only used in Selftest
	//llander_explosion = data & 0x08;

	if (data == 0) { sample_set_volume(1, 2); }
	if (data == 1) { sample_set_volume(1, 10); }
	if (data & 0x07)
	{
		tvol = GI[CPU0][0x01] << 4;
		if (tvol < 10) { tvol = 10; }

		sample_set_volume(1, tvol);
	}

	if (data & 0x10)
	{
		if (sample_playing(3) == 0)
		{
			sample_start(3, 1, 0);
		}
	}

	if (data & 0x20)
	{
		if (sample_playing(4) == 0)
		{
			sample_start(4, 3, 0);
		}
	}
	if (data & 0x08)
	{
		if (sample_playing(2) == 0)
		{
			sample_start(2, 2, 0);
		}
	}
}

///////////////////  /READ KEYS FROM PIA 1 //////////////////////////////////////
/* Lunar lander LED port seems to be mapped thus:

   NNxxxxxx - Apparently unused
   xxNxxxxx - Unknown gives 4 high pulses of variable duration when coin put in ?
   xxxNxxxx - Start    Lamp ON/OFF == 0/1
   xxxxNxxx - Training Lamp ON/OFF == 1/0
   xxxxxNxx - Cadet    Lamp ON/OFF
   xxxxxxNx - Prime    Lamp ON/OFF
   xxxxxxxN - Command  Lamp ON/OFF

   Selection lamps seem to all be driver 50/50 on/off during attract mode ?

*/

///////////////////////////////////////////////////////////////////////////////
WRITE_HANDLER(ll_led_write)
{
	data = data & 0xff;
	lamp0 = ((data >> (4 - 0)) & 1);
	b += ((data >> (4 - 1)) & 1);
	c += ((data >> (4 - 2)) & 1);

	data = data & 0x0f;
	if (data == 0x0f) { pic = 1; }
	if (data == 0x08) { pic = 2; }
	if (data == 0x04) { pic = 3; }
	if (data == 0x02) { pic = 4; }
	if (data == 0x01) { pic = 5; }
}

MEM_WRITE(LlanderWrite)
MEM_ADDR(0x0000, 0x00ff, llander_zeropage_w)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3200, 0x3200, ll_led_write)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3c00, 0x3c00, llander_sounds_w)
MEM_ADDR(0x3e00, 0x3e00, llander_snd_reset_w)
MEM_ADDR(0x4800, 0x5fff, MWA_ROM)
MEM_ADDR(0x6000, 0x7fff, MWA_ROM)
MEM_END

MEM_READ(LlanderRead)
MEM_ADDR(0x0000, 0x00ff, llander_zeropage_r)
MEM_ADDR(0x2000, 0x2000, llander_IN0_r)
MEM_ADDR(0x2400, 0x2407, llander_IN1_r)
MEM_ADDR(0x2800, 0x2803, llander_DSW1_r)
MEM_ADDR(0x2c00, 0x2c00, ip_port_3_r)
MEM_END

void run_llander()
{
	/*
		 if (config.artwork){
		 count++;b=0;c=0;
		 if (count==10){count=0;blink^=1;}

			blit_any_tex(&art_tex[0],6,1,0,0,1024,164); //bright //6
			if (b==6 && c==6) {
			 if (blink ){
				  blit_any_tex(&art_tex[3],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[4],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[5],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[6],1,1,0,0,1023,164);
					 }
							 }
			else {
				switch(pic){
					 case 1:blit_any_tex(&art_tex[2],1,1,0,0,1024,164);break;
					 case 2:blit_any_tex(&art_tex[3],1,1,0,0,1024,164);break;
					 case 3:blit_any_tex(&art_tex[4],1,1,0,0,1024,164);break;
					 case 4:blit_any_tex(&art_tex[5],1,1,0,0,1024,164);break;
					 case 5:blit_any_tex(&art_tex[6],1,1,0,0,1024,164);break;
					  }
				}
		 if (lamp0){blit_any_tex(&art_tex[2],1,1,0,0,1024,164);}
		blit_any_tex(&art_tex[1],15,.70,0,0,1024,164);//dim
		 }

		*/
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_llander()
{
	init6502(LlanderRead, LlanderWrite, 0x7fff, CPU0);

	/*
	if (config.artwork){
	make_single_bitmap( &art_tex[0],"lunaroff.png","llander.zip");
	make_single_bitmap( &art_tex[1],"lunaron.png","llander.zip");
	make_single_bitmap( &art_tex[2],"lunarins.png","llander.zip");
	make_single_bitmap( &art_tex[3],"lunartrn.png","llander.zip");
	make_single_bitmap( &art_tex[4],"lunarcad.png","llander.zip");
	make_single_bitmap( &art_tex[5],"lunarprm.png","llander.zip");
	make_single_bitmap( &art_tex[6],"lunarcom.png","llander.zip");
	}
	*/
	
	sample_start(1, 0, 1);
	dvg_start();
	return 0;
}

void end_llander()
{
}
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////