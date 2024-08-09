/* Battlezone Emu */

#include "bzone.h"
#include "../aae_mame_driver.h"
#include "../vidhrdwr/vector.h"
#include "../vidhrdwr/aae_avg.h"
#include "../machine/earom.h"
#include "../machine/mathbox.h"
#include "../sndhrdw/samples.h"
#include "../sndhrdw/pokyintf.h"
#include "../acommon.h"

#define IN0_3KHZ (1<<7)
#define IN0_VG_HALT (1<<6)

static int soundEnable = 1;
int rb_input_select; 	// 0 is roll_data, 1 is pitch_data

static UINT8 analog_data;

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

void BZSaveScore(void)
{
	FILE* fp;
	int i;

	fp = fopen("hi\\bzone.aae", "w");
	if (fp == NULL) {
		wrlog("Error - I couldn't write Battlezone High Score");
	}
	for (i = 0; i < 60; i++)

	{
		fprintf(fp, "%c", GI[CPU0][(0x300 + i)]);
	}
	fclose(fp);
}
int BZLoadScore(void)
{
	FILE* fp;
	char c;
	int i = 0;
	fp = fopen("hi\\bzone.aae", "r");
	// check if the hi score table has already been initialized
	if (memcmp(&GI[CPU0][0x0300], "\x05\x00\x00", 3) == 0 &&
		memcmp(&GI[CPU0][0x0339], "\x22\x28\x38", 3) == 0)
	{
		if (fp != NULL)
		{
			do {
				c = getc(fp);    /* get one character from the file */
				GI[CPU0][(0x300 + i)] = c;         /* display it on the monitor       */
				i++;
			} while (i < 60);    /* repeat until EOF (end of file)  */
			fclose(fp);
		}

		return 1;
	}
	else return 0;
}

READ_HANDLER(BzoneIN0read)
{
	int res;

	res = readinputport(0);

	if (get_eterna_ticks(0) & 0x100) res |= IN0_3KHZ;
	else res &= ~IN0_3KHZ;

	if (!avg_check())	res |= IN0_VG_HALT;
	else res &= ~IN0_VG_HALT;

	return res;
}
/*
READ_HANDLER(RBJoyRead)
{
	return getport(rb_input_select ? 6 : 3);
}
*/
READ_HANDLER(BzoneControls)
{
	int res = bzone_IN3_r(address & 0x0f);
	return (res);
}

WRITE_HANDLER(watchdog_reset_w)
{
	WATCHDOG = data;
	wrlog("Watchdog write %d", data);
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

	set_aae_leds(~data & 0x08, 0, 0);

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

WRITE_HANDLER(BZgo)
{
	avg_go();
}

WRITE_HANDLER(BzoneSounds)
{
	static int lastValue = 0;

	set_aae_leds(~data & 0x40, 0, 0);

	// Enable/disable all sound output
	if (data & 0x20)
	{
		soundEnable = 1; if (!sample_playing(3)) { sample_start(3, 2, 1); }
	}
	else { soundEnable = 0; }

	// If sound is off, don't bother playing samples
	if (!soundEnable) { sample_stop(3); return; }

	if (lastValue == data) return;
	lastValue = data;

	// Enable explosion output
	if (data & 0x01)
	{
		if (data & 0x02) { sample_start(1, 4, 0); }
		else { sample_start(1, 5, 0); }
	}

	// Enable shell output
	if (data & 0x04)
	{
		if (data & 0x08) { sample_start(2, 0, 0); } // loud shell
		else { sample_start(2, 1, 0); } // soft shell
	}

	// Enable engine output
	if (data & 0x80)
	{
		if (data & 0x10) sample_set_freq(3, 33000); // Fast rumble
		else sample_set_freq(3, 22050); // Slow rumble
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
	static int k = 0;

	if (!(readinputport(0) & 0x10)) { cpu_disable_interrupts(0, 0); testsw = 1; }
	else { cpu_disable_interrupts(0, 1); testsw = 0; }

	/*
	if (WATCHDOG==4) k++;
	if (k > 60)
	{
		k = 0;
		cpu_needs_reset(0);
		WATCHDOG = 0;
	}
	*/

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
MEM_ADDR(0x1000, 0x1000, NoWrite)
MEM_ADDR(0x1200, 0x1200, BZgo)
MEM_ADDR(0x1400, 0x1400, NoWrite)
MEM_ADDR(0x1600, 0x1600, NoWrite)
MEM_ADDR(0x1820, 0x182f, pokey_1_w)
MEM_ADDR(0x1840, 0x1840, BzoneSounds)
MEM_ADDR(0x1848, 0x1850, analog_select_w)
MEM_ADDR(0x1860, 0x187f, MathboxGo)
MEM_ADDR(0x3000, 0xffff, NoWrite)
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
MEM_ADDR(0x1000, 0x1000, NoWrite)
MEM_ADDR(0x1200, 0x1200, BZgo)
MEM_ADDR(0x1400, 0x1400, watchdog_reset_w)
MEM_ADDR(0x1600, 0x1600, NoWrite)
MEM_ADDR(0x1820, 0x182f, bzone_pokey_w)
MEM_ADDR(0x1860, 0x187f, MathboxGo)
MEM_ADDR(0x1840, 0x1840, BzoneSounds)
MEM_ADDR(0x3000, 0xffff, NoWrite)
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
//MEM_ADDR( 0x0a00, 0x0a00, NoWrite)
//MEM_ADDR( 0x0c00, 0x0c00, NoWrite)
MEM_ADDR(0x1400, 0x1400, watchdog_reset_w)
MEM_ADDR(0x1808, 0x1808, RedBaronSoundsWrite)
MEM_ADDR(0x1810, 0x181f, bzone_pokey_w)
MEM_ADDR(0x180c, 0x180c, EaromCtrl)
MEM_ADDR(0x1820, 0x185f, EaromWrite)
MEM_ADDR(0x1860, 0x187f, MathboxGo)
MEM_ADDR(0x1200, 0x1200, BZgo)
MEM_ADDR(0x3000, 0x37ff, NoWrite)
MEM_ADDR(0x5000, 0x7fff, NoWrite)
MEM_END

//////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_bzone()
{
	if (gamenum == REDBARON)
	{
		init6502Z(RedBaronRead, RedBaronWrite, 0);
		pokey_sh_start(&rb_pokey_interface);
		LoadEarom();
	}
	else
	{
		init6502Z(BzoneRead, BzoneWrite, 0);
		pokey_sh_start(&bzone_pokey_interface);
	}

	avg_init();

	return 0;
}
void end_bzone()
{
	pokey_sh_stop();

	if (gamenum == REDBARON) SaveEarom();
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////