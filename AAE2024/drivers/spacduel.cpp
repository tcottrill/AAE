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


#include "spacduel.h"
#include "samples.h"
#include "vector.h"
#include "aae_avg.h"
#include "earom.h"
#include "pokyintf.h"
#include "aae_mame_driver.h"




#define IN_LEFT	(1 << 0)
#define IN_RIGHT (1 << 1)
#define IN_FIRE (1 << 2)
#define IN_SHIELD (1 << 3)
#define IN_THRUST (1 << 4)
#define IN_P1 (1 << 5)
#define IN_P2 (1 << 6)


static struct POKEYinterface pokey_interface =
{
	2,			/* 2 chips */
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

WRITE_HANDLER(AVGgo)
{
	avg_go();
}

READ_HANDLER(IN0read)
{
	int val = readinputport(0);

	//Fix this mess below later
	if (get_eterna_ticks(0) & 0x100) { bitset(val, 0x80); }
	else { bitclr(val, 0x80); }
	if (avg_check()) { bitclr(val, 0x40); }
	else { bitset(val, 0x40); }
	if (key[config.ktestadv]) { bitclr(val, 0x20); }
	return val;
}

READ_HANDLER(SDControls)
{
	int res;
	int res1;
	int res2;

	res1 = readinputport(3);
	res2 = readinputport(4);
	res = 0x00;

	switch (address & 0x07)
	{
	case 0:
		if (res1 & IN_SHIELD) res |= 0x80;
		if (res1 & IN_FIRE) res |= 0x40;
		break;
	case 1: /* Player 2 */
		if (res2 & IN_SHIELD) res |= 0x80;
		if (res2 & IN_FIRE) res |= 0x40;
		break;
	case 2:
		if (res1 & IN_LEFT) res |= 0x80;
		if (res1 & IN_RIGHT) res |= 0x40;
		break;
	case 3: /* Player 2 */
		if (res2 & IN_LEFT) res |= 0x80;
		if (res2 & IN_RIGHT) res |= 0x40;
		break;
	case 4:
		if (res1 & IN_THRUST) res |= 0x80;
		if (res1 & IN_P1) res |= 0x40;
		break;
	case 5:  /* Player 2 */
		if (res2 & IN_THRUST) res |= 0x80;
		break;
	case 6:
		if (res1 & IN_P2) res |= 0x80;
		break;
	case 7:
		res = (0x00 /* upright */ | (0 & 0x40));
		break;
	}

	return res;
}

WRITE_HANDLER(avgdvg_reset_w)
{
	wrlog("AVG RESET");
}

WRITE_HANDLER(intack)
{
	GI[CPU0][0x88c0] = data;
}

WRITE_HANDLER(led_write)
{
	set_aae_leds(~data & 0x10, ~data & 0x20, 0);
}

void run_spacduel()
{
	pokey_sh_update();
}

MEM_READ(BwidowRead)

MEM_ADDR(0x6000, 0x67ff, pokey_1_r)
MEM_ADDR(0x6800, 0x6fff, pokey_2_r)
MEM_ADDR(0x7000, 0x7000, EaromRead)
MEM_ADDR(0x7800, 0x7800, IN0read)
MEM_ADDR(0x8000, 0x8000, ip_port_3_r)
MEM_ADDR(0x8800, 0x8800, ip_port_4_r)
MEM_END

MEM_WRITE(BwidowWrite)
MEM_ADDR(0x6000, 0x67ff, pokey_1_w)
MEM_ADDR(0x6800, 0x6fff, pokey_2_w)
MEM_ADDR(0x8800, 0x8800, led_write)
MEM_ADDR(0x8840, 0x8840, AVGgo)
MEM_ADDR(0x88c0, 0x88c0, intack)
MEM_ADDR(0x8900, 0x8900, EaromCtrl)
MEM_ADDR(0x8940, 0x897f, EaromWrite)
MEM_ADDR(0x8980, 0x89ed, NoWrite) //Watchdog
MEM_ADDR(0x9000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(GravRead)
MEM_ADDR(0x7800, 0x7800, IN0read)
MEM_ADDR(0x6000, 0x600f, pokey_1_r)
MEM_ADDR(0x6800, 0x680f, pokey_2_r)
MEM_ADDR(0x7000, 0x7000, EaromRead)
MEM_ADDR(0x8000, 0x8000, ip_port_3_r)
MEM_ADDR(0x8800, 0x8800, ip_port_4_r)
MEM_END

MEM_WRITE(GravWrite)
MEM_ADDR(0x2800, 0x5fff, NoWrite)
MEM_ADDR(0x6000, 0x67ff, pokey_1_w)
MEM_ADDR(0x6800, 0x6fff, pokey_2_w)
MEM_ADDR(0x8800, 0x8800, led_write)
MEM_ADDR(0x8840, 0x8840, AVGgo)
MEM_ADDR(0x8900, 0x8900, EaromCtrl)
MEM_ADDR(0x8940, 0x897f, EaromWrite)
MEM_ADDR(0x8980, 0x89ed, NoWrite)
MEM_ADDR(0x9000, 0xffff, NoWrite)
MEM_END

MEM_READ(SpaceDuelRead)
MEM_ADDR(0x800, 0x800, IN0read)
MEM_ADDR(0x1000, 0x100f, pokey_1_r)
MEM_ADDR(0x1400, 0x140f, pokey_2_r)
MEM_ADDR(0x0a00, 0x0a00, EaromRead)
MEM_ADDR(0x0900, 0x0907, SDControls)
MEM_END

MEM_WRITE(SpaceDuelWrite)
MEM_ADDR(0x1000, 0x100f, pokey_1_w)
MEM_ADDR(0x1400, 0x140f, pokey_2_w)
MEM_ADDR(0x0c80, 0x0c80, AVGgo)
MEM_ADDR(0x0c00, 0x0c00, led_write)
MEM_ADDR(0x0d00, 0x0d00, NoWrite)
MEM_ADDR(0x0d80, 0x0d80, avgdvg_reset_w)
MEM_ADDR(0x0e00, 0x0e00, NoWrite)
MEM_ADDR(0x0f00, 0x0f3f, EaromWrite)
MEM_ADDR(0x0e80, 0x0e80, EaromCtrl)
MEM_ADDR(0x2800, 0x8fff, NoWrite)
MEM_ADDR(0xf000, 0xffff, NoWrite)
MEM_ADDR(0x0905, 0x0906, NoWrite)
MEM_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_spacduel()
{
	switch (gamenum)
	{
	case SPACDUEL: init6502(SpaceDuelRead, SpaceDuelWrite, 0); break;
	case GRAVITAR: init6502(GravRead, GravWrite, 0); break;
	case GRAVITR2: init6502(GravRead, GravWrite, 0); break;
	case GRAVP:    init6502(GravRead, GravWrite, 0); break;
	case BWIDOW:   init6502(BwidowRead, BwidowWrite, 0); break;
	case LUNARBAT: init6502(GravRead, GravWrite, 0); break;
	case LUNARBA1: init6502(SpaceDuelRead, SpaceDuelWrite, 0); break;
	}

	cache_clear();
	pokey_sh_start(&pokey_interface);
	LoadEarom();
	avg_init();
	wrlog("SPACDUEL INIT COMPLETED");
	return 0;
}
void end_spacduel()
{
	pokey_sh_stop();
	SaveEarom();
}
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////