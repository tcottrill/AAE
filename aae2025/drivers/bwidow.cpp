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

#include "bwidow.h"
#include "aae_avg.h"
#include "earom.h"
#include "pokyintf.h"
#include "aae_mame_driver.h"
#include "timer.h"

#define IN_LEFT	(1 << 0)
#define IN_RIGHT (1 << 1)
#define IN_FIRE (1 << 2)
#define IN_SHIELD (1 << 3)
#define IN_THRUST (1 << 4)
#define IN_P1 (1 << 5)
#define IN_P2 (1 << 6)

void bwidow_interrupt(int dummy)
{
	//WRLOG("BWidow Interrupt");
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

static struct POKEYinterface pokey_interface =
{
	2,			/* 2 chips */
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

READ_HANDLER(IN0read)
{
	int val = readinputport(0);

	//Fix this mess below later
	if (get_eterna_ticks(0) & 0x100) { bitset(val, 0x80); }
	//	else { bitclr(val, 0x80); }
	if (!avg_check())
	{
		bitclr(val, 0x40);
	}
	else
	{
		bitset(val, 0x40);
	}

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

WRITE_HANDLER(irq_ack_w)
{
	;//write_to_log("irq_ack_w this frame");
	Machine->memory_region[CPU0][0x88c0] = data;
}

WRITE_HANDLER(avgdvg_reset_w)
{
//	wrlog("AVG RESET");
}

WRITE_HANDLER(bwidow_misc_w)
{
	/*
		0x10 = p1 led
		0x20 = p2 led
		0x01 = coin counter 1
		0x02 = coin counter 2
	*/
	static int lastdata;

	if (data == lastdata) return;
	set_led_status(0, ~data & 0x10);
	set_led_status(1, ~data & 0x20);
	//coin_counter_w(0, data & 0x01);
	//coin_counter_w(1, data & 0x02);
	lastdata = data;
}

WRITE_HANDLER(spacduel_misc_w)
{
	static int lastdata;

	if (data == lastdata) return;
	set_led_status(1, ~data & 0x10);
	set_led_status(0, ~data & 0x20);
	lastdata = data;
}

void run_bwidow()
{
	pokey_sh_update();
}

MEM_READ(BwidowRead)
MEM_ADDR(0x6000, 0x600f, pokey_1_r)
MEM_ADDR(0x6800, 0x680f, pokey_2_r)
MEM_ADDR(0x7000, 0x7000, EaromRead)
MEM_ADDR(0x7800, 0x7800, IN0read)
MEM_ADDR(0x8000, 0x8000, ip_port_3_r)
MEM_ADDR(0x8800, 0x8800, ip_port_4_r)
MEM_END

MEM_WRITE(BwidowWrite)
MEM_ADDR(0x6000, 0x67ff, pokey_1_w)
MEM_ADDR(0x6800, 0x6fff, pokey_2_w)
MEM_ADDR(0x8800, 0x8800, bwidow_misc_w)
MEM_ADDR(0x8840, 0x8840, advdvg_go_w)
MEM_ADDR(0x88c0, 0x88c0, irq_ack_w)
MEM_ADDR(0x8900, 0x8900, EaromCtrl)
MEM_ADDR(0x8940, 0x897f, EaromWrite)
MEM_ADDR(0x8980, 0x89ed, watchdog_reset_w)
MEM_ADDR(0x9000, 0xffff, MWA_ROM)
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
MEM_ADDR(0x0c80, 0x0c80, advdvg_go_w)
MEM_ADDR(0x0c00, 0x0c00, spacduel_misc_w)
MEM_ADDR(0x0d00, 0x0d00, watchdog_reset_w)
MEM_ADDR(0x0d80, 0x0d80, avgdvg_reset_w)
MEM_ADDR(0x0e00, 0x0e00, irq_ack_w)
MEM_ADDR(0x0f00, 0x0f3f, EaromWrite)
MEM_ADDR(0x0e80, 0x0e80, EaromCtrl)
MEM_ADDR(0x2800, 0x8fff, MWA_ROM)
MEM_ADDR(0xf000, 0xffff, MWA_ROM)
MEM_ADDR(0x0905, 0x0906, MWA_ROM)
MEM_END

int init_bwidow()
{
	pokey_sh_start(&pokey_interface);
	init6502(BwidowRead, BwidowWrite, 0xffff, CPU0);
	avg_start();
	timer_set(TIME_IN_HZ(246), CPU0, bwidow_interrupt);
	return 1;
}
/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_spacduel()
{
	pokey_sh_start(&pokey_interface);
	init6502(SpaceDuelRead, SpaceDuelWrite, 0xffff, CPU0);
	avg_start();
	timer_set(TIME_IN_HZ(246), CPU0, bwidow_interrupt);

	sample_set_volume(1, 5);

	return 1;
}
void end_bwidow()
{
	pokey_sh_stop();
}
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////