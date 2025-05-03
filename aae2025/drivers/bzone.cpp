/* Battlezone Emu */

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
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include "bzone.h"
#include "aae_mame_driver.h"
#include "aae_avg.h"
#include "earom.h"
#include "vector.h"
#include "mathbox.h"
#include "pokyintf.h"
#include "timer.h"

#define IN0_3KHZ (1<<7)
#define IN0_VG_HALT (1<<6)

static int soundEnable = 1;
int rb_input_select; 	// 0 is roll_data, 1 is pitch_data

static UINT8 analog_data;

int bzone_timer = -1;


/* Constants for the sound names in the bzone sample array */
#define kFire1			0
#define kFire2			1
#define kEngine1		2
#define kEngine2		3
#define kExplode1		4
#define kExplode2		5


void bzone_interrupt(int dummy)
{
	if (readinputport(0) & 0x10)
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

// Translation table for one-joystick emulation
static int one_joy_trans[32] = {
		0x00,0x0A,0x05,0x00,0x06,0x02,0x01,0x00,
		0x09,0x08,0x04,0x00,0x00,0x00,0x00,0x00 };

static int bzone_IN3_r(int offset)
{
	int res, res1;

	res = readinputport(3);
	res1 = readinputport(4);

	res |= one_joy_trans[res1 & 0x1f];

	return (res);
}

static int redbaron_joy_r(int offset)
{
	if (rb_input_select)
		return readinputport(5);
	else
		return readinputport(6);
}

static struct POKEYinterface bzone_pokey_interface =
{
	1,			/* 4 chips */
	1512000,
	255,	/* volume */
	6,
	NO_CLIP,
	/* The 8 pot handlers */
	{0},{0},{0},{0},{0},{0},{0},{0},
	/* The allpot handler */
	{ bzone_IN3_r }, //Dip here
};

static struct POKEYinterface rb_pokey_interface =
{
	1,			/* 4 chips */
	1512000,
	255,	/* volume */
	6,
	NO_CLIP,
	/* The 8 pot handlers */
	{0},{0},{0},{0},{0},{0},{0},{0},
	/* The allpot handler */
	{ redbaron_joy_r }, //Dip here
};

WRITE_HANDLER(bzone_pokey_w)
{
	if (soundEnable)
		pokey1_w(address, data);
}

READ_HANDLER(BzoneIN0read)
{
	int res;

	res = readinputport(0);

	if (get_eterna_ticks(0) & 0x100) res |= IN0_3KHZ;
	else res &= ~IN0_3KHZ;

	if (avg_check())	res |= IN0_VG_HALT;
	else res &= ~IN0_VG_HALT;

	return res;
}

READ_HANDLER(BzoneControls)
{
	int res = bzone_IN3_r(address & 0x0f);
	return (res);
}


WRITE_HANDLER(RedBaronSoundsWrite)
{
	static int lastValue = 0;

	rb_input_select = (data & 0x01);

	if (data & 0x20)
	{
		soundEnable = 1;
	}
	if (lastValue == data) return;
	lastValue = data;

	/* Enable explosion output */
	if (data & 0xf0)
	{
		if (sample_playing(2) == 0) { sample_start(2, 0, 0); }
	}

	//set_aae_leds(~data & 0x08, 0, 0);
	set_led_status(0, ~data & 0x08);

	/* Enable nosedive output */
	if (data & 0x02)
	{
		if (sample_playing(2) == 0) { sample_start(2, 2, 0); }
	}

	/* Enable firing output */
	if (data & 0x04)
	{
		if (sample_playing(1) == 0) { sample_start(1, 1, 0); }
	}
}

WRITE_HANDLER(BzoneSounds)
{
	static int lastValue = 0;

	//set_aae_leds(~data & 0x40, 0, 0);
	set_led_status(0, ~data & 0x40);

	// Enable/disable all sound output
	if (data & 0x20)
	{
		soundEnable = 1; 
		if (!sample_playing(3)) 
		{ sample_start(3, 2, 1); }
	}
	else { soundEnable = 0; }

	// If sound is off, don't bother playing samples
	if (!soundEnable) { sample_stop(3); sample_stop(7); return; }

	if (lastValue == data) return;
	lastValue = data;

	// Enable explosion output
	if (data & 0x01)
	{
		if (data & 0x02) { sample_start(7, 4, 0); }
		else { sample_start(7, 5, 0); }
	}

	// Enable shell output
	if (data & 0x04)
	{
		if (data & 0x08) { sample_start(2, 0, 0); } // loud shell
		else { sample_start(2, 1, 0); } // soft shell
	}

	// Enable engine output, really missing the volume ramp here. 
	if (data & 0x80)
	{
		if (data & 0x10)
		{ 	//Data is 0xb0
			sample_set_freq(3, config.samplerate * 1.66); 
		}// Fast rumble
		else
		{//data is 0xa0
		 sample_set_freq(3, config.samplerate); // Slow rumble  
		}	
	}
}

READ_HANDLER(analog_data_r)
{
	return analog_data;
}

WRITE_HANDLER(analog_select_w)
{
	if ((address & 0x0f) <= 2)
		analog_data = readinputport(5 + (address & 0x0f)); //6
}

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_bzone()
{
	if (!paused && soundEnable) { pokey_sh_update(); }
}

MEM_READ(BradleyRead)
MEM_ADDR(0x0800, 0x0800, BzoneIN0read)
MEM_ADDR(0x1800, 0x1800, MathboxStatusRead)
MEM_ADDR(0x1808, 0x1808, ip_port_4_r)
MEM_ADDR(0x1809, 0x1809, ip_port_5_r)
MEM_ADDR(0x180a, 0x180a, analog_data_r)
MEM_ADDR(0x1810, 0x1810, MathboxLowbitRead)
MEM_ADDR(0x1818, 0x1818, MathboxHighbitRead)
MEM_ADDR(0x1820, 0x182f, pokey_1_r)
MEM_ADDR(0x1828, 0x1828, BzoneControls)
MEM_END

MEM_WRITE(BradleyWrite)
MEM_ADDR(0x1000, 0x1000, MWA_ROM)
MEM_ADDR(0x1200, 0x1200, advdvg_go_w)
MEM_ADDR(0x1400, 0x1400, MWA_ROM)
MEM_ADDR(0x1600, 0x1600, MWA_ROM)
MEM_ADDR(0x1820, 0x182f, pokey_1_w)
MEM_ADDR(0x1840, 0x1840, BzoneSounds)
MEM_ADDR(0x1848, 0x1850, analog_select_w)
MEM_ADDR(0x1860, 0x187f, MathboxGo)
MEM_ADDR(0x3000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(BzoneRead)
MEM_ADDR(0x0800, 0x0800, BzoneIN0read)
MEM_ADDR(0x0a00, 0x0a00, ip_port_1_r)	/* DSW1 */
MEM_ADDR(0x0c00, 0x0c00, ip_port_2_r)	/* DSW2 */
MEM_ADDR(0x1800, 0x1800, MathboxStatusRead)
MEM_ADDR(0x1810, 0x1810, MathboxLowbitRead)
MEM_ADDR(0x1818, 0x1818, MathboxHighbitRead)
MEM_ADDR(0x1820, 0x182f, pokey_1_r)
MEM_END

MEM_WRITE(BzoneWrite)
MEM_ADDR(0x1000, 0x1000, MWA_ROM)
MEM_ADDR(0x1200, 0x1200, advdvg_go_w)
MEM_ADDR(0x1400, 0x1400, watchdog_reset_w)
MEM_ADDR(0x1600, 0x1600, avgdvg_reset_w)
MEM_ADDR(0x1820, 0x182f, bzone_pokey_w)
MEM_ADDR(0x1860, 0x187f, MathboxGo)
MEM_ADDR(0x1840, 0x1840, BzoneSounds)
MEM_ADDR(0x3000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(RedBaronRead)
MEM_ADDR(0x0800, 0x0800, BzoneIN0read)
MEM_ADDR(0x0a00, 0x0a00, ip_port_1_r)
MEM_ADDR(0x0c00, 0x0c00, ip_port_2_r)
MEM_ADDR(0x1800, 0x1800, MathboxStatusRead)
MEM_ADDR(0x1802, 0x1802, ip_port_4_r)
MEM_ADDR(0x1804, 0x1804, MathboxLowbitRead)
MEM_ADDR(0x1806, 0x1806, MathboxHighbitRead)
MEM_ADDR(0x1810, 0x181f, pokey_1_r)
MEM_ADDR(0x1820, 0x185f, EaromRead)
MEM_END

MEM_WRITE(RedBaronWrite)
//MEM_ADDR( 0x0a00, 0x0a00, MWA_ROM)
//MEM_ADDR( 0x0c00, 0x0c00, MWA_ROM)
MEM_ADDR(0x1200, 0x1200, advdvg_go_w)
MEM_ADDR(0x1600, 0x1600, avgdvg_reset_w)
MEM_ADDR(0x1400, 0x1400, watchdog_reset_w)
MEM_ADDR(0x1808, 0x1808, RedBaronSoundsWrite)
MEM_ADDR(0x1810, 0x181f, bzone_pokey_w)
MEM_ADDR(0x180c, 0x180c, EaromCtrl)
MEM_ADDR(0x1820, 0x185f, EaromWrite)
MEM_ADDR(0x1860, 0x187f, MathboxGo)

MEM_ADDR(0x3000, 0x37ff, MWA_ROM)
MEM_ADDR(0x5000, 0x7fff, MWA_ROM)
MEM_END

//////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_redbaron()
{
	
	init6502(RedBaronRead, RedBaronWrite, 0x7fff, CPU0);
	pokey_sh_start(&rb_pokey_interface);
	avg_start_redbaron();
	bzone_timer = timer_set(TIME_IN_HZ(240), CPU0, bzone_interrupt);

	return 0;
}

int init_bzone()
{
	init6502(BzoneRead, BzoneWrite, 0x7fff, CPU0);
	pokey_sh_start(&bzone_pokey_interface);
	avg_start_bzone();
	bzone_timer = timer_set(TIME_IN_HZ(240), CPU0, bzone_interrupt);

	return 0;
}
void end_bzone()
{
	if (bzone_timer)
	{
		timer_remove(bzone_timer);
	}

	pokey_sh_stop();
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////