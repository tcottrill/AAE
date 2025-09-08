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
#include "driver_registry.h"    // AAE_REGISTER_DRIVER
#include "aae_avg.h"
#include "earom.h"
#include "mathbox.h"
#include "aae_pokey.h"
#include "timer.h"

#define IN0_3KHZ (1<<7)
#define IN0_VG_HALT (1<<6)

static int soundEnable = 1;
int rb_input_select; 	// 0 is roll_data, 1 is pitch_data

static UINT8 analog_data;

int bzone_timer = -1;

ART_START(bzoneart)
ART_LOAD("bzone.zip", "bzone_overlay.png", ART_TEX, 1)
ART_LOAD("bzone.zip", "bzone.png", ART_TEX, 3)
ART_END

ART_START(redbaronart)
ART_LOAD("redbaron.zip", "redbaron_overlay.png", ART_TEX, 1)
ART_LOAD("redbaron.zip", "rbbezel.png", ART_TEX, 3)
ART_END

/* Constants for the sound names in the bzone sample array */
#define kFire1			0
#define kFire2			1
#define kEngine1		2
#define kEngine2		3
#define kExplode1		4
#define kExplode2		5

static const char* bzonesamples[] = {
	"bzone.zip",
	"fire1.wav",
	"fire2.wav",
	"engine1.wav",
	"engine2.wav",
	"explode1.wav",
	"explode2.wav",
	 0 };

static const char* redbaron_samples[] = {
	"redbaron.zip",
	"explode.wav",
	"shot.wav",
	"spin.wav",
	 0 };

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
		pokey_1_w(address, data, 0);
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
			sample_set_freq(3, (int) (sample_get_freq(3) * 1.66));
		}// Fast rumble
		else
		{//data is 0xa0
		 sample_set_freq(3, sample_get_freq(3) ); // Slow rumble  
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
	//if (!paused && soundEnable) { pokey_sh_update(); }
	pokey_sh_update();
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
	
	//init6502(RedBaronRead, RedBaronWrite, 0x7fff, CPU0);
	pokey_sh_start(&rb_pokey_interface);
	avg_start_redbaron();
	bzone_timer = timer_set(TIME_IN_HZ(240), CPU0, bzone_interrupt);

	return 0;
}

int init_bzone()
{
	//init6502(BzoneRead, BzoneWrite, 0x7fff, CPU0);
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


INPUT_PORTS_START(bzone)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x0c, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
/* bit 6 is the VG HALT bit. We set it to "low" */
/* per default (busy vector processor). */
/* handled by bzone_IN0_r() */
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* bit 7 is tied to a 3khz clock */
/* handled by bzone_IN0_r() */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("DSW0")	/* DSW0 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x01, "3")
PORT_DIPSETTING(0x02, "4")
PORT_DIPSETTING(0x03, "5")
PORT_DIPNAME(0x0c, 0x04, "Missile appears at")
PORT_DIPSETTING(0x00, "5000")
PORT_DIPSETTING(0x04, "10000")
PORT_DIPSETTING(0x08, "20000")
PORT_DIPSETTING(0x0c, "30000")
PORT_DIPNAME(0x30, 0x10, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x10, "15k and 100k")
PORT_DIPSETTING(0x20, "20k and 100k")
PORT_DIPSETTING(0x30, "50k and 100k")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0xc0, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x40, "German")
PORT_DIPSETTING(0x80, "French")
PORT_DIPSETTING(0xc0, "Spanish")

PORT_START("DSW1")	/* DSW1 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0xe0, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x20, "3 credits/2 coins")
PORT_DIPSETTING(0x40, "5 credits/4 coins")
PORT_DIPSETTING(0x60, "6 credits/4 coins")
PORT_DIPSETTING(0x80, "6 credits/5 coins")

PORT_START("IN3")	/* IN3 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_DOWN | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_UP | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_DOWN | IPF_2WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_UP | IPF_2WAY)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON3)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN4")	/* fake port for single joystick control */
/* This fake port is handled via bzone_IN3_r */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_CHEAT)
INPUT_PORTS_END

INPUT_PORTS_START(redbaron)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x0c, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
/* bit 6 is the VG HALT bit. We set it to "low" */
/* per default (busy vector processor). */
/* handled by bzone_IN0_r() */
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* bit 7 is tied to a 3khz clock */
/* handled by bzone_IN0_r() */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("DSW0")	/* DSW0 */
/* See the table above if you are really interested */
PORT_DIPNAME(0xff, 0xfd, DEF_STR(Coinage))
PORT_DIPSETTING(0xfd, "Normal")

PORT_START("DSW1")	/* DSW1 */
PORT_DIPNAME(0x03, 0x03, "Language")
PORT_DIPSETTING(0x00, "German")
PORT_DIPSETTING(0x01, "French")
PORT_DIPSETTING(0x02, "Spanish")
PORT_DIPSETTING(0x03, "English")
PORT_DIPNAME(0x0c, 0x04, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x0c, "2k 10k 30k")
PORT_DIPSETTING(0x08, "4k 15k 40k")
PORT_DIPSETTING(0x04, "6k 20k 50k")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x30, 0x20, DEF_STR(Lives))
PORT_DIPSETTING(0x30, "2")
PORT_DIPSETTING(0x20, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x40, 0x40, "One Play Minimum")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, "Self Adjust Diff")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

/* IN3 - the real machine reads either the X or Y axis from this port */
/* Instead, we use the two fake 5 & 6 ports and bank-switch the proper */
/* value based on the lsb of the byte written to the sound port */
PORT_START("IN3")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_4WAY)

PORT_START("IN4")	/* IN4 - misc controls */
PORT_BIT(0x3f, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_BUTTON1)

/* These 2 are fake - they are bank-switched from reads to IN3 */
/* Red Baron doesn't seem to use the full 0-255 range. */
PORT_START("IN5")	/* IN5 */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 25, 10, 0, 64, 192)

PORT_START("IN6")/* IN6 */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 25, 10, 0, 64, 192)
INPUT_PORTS_END


//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////

ROM_START(bzone)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036422-01.bc3", 0x3000, 0x0800, CRC(7414177b) SHA1(147d97a3b475e738ce00b1a7909bbd787ad06eda))
ROM_LOAD("036421-01.a3", 0x3800, 0x0800, CRC(8ea8f939) SHA1(b71e0ab0e220c3e64dc2b094c701fb1a960b64e4))
ROM_LOAD("036414-02.e1", 0x5000, 0x0800, CRC(13de36d5) SHA1(40e356ddc5c042bc1ce0b71f51e8b6de72daf1e4))
ROM_LOAD("036413-01.h1", 0x5800, 0x0800, CRC(5d9d9111) SHA1(42638cff53a9791a0f18d316f62a0ea8eea4e194))
ROM_LOAD("036412-01.j1", 0x6000, 0x0800, CRC(ab55cbd2) SHA1(6bbb8316d9f8588ea0893932f9174788292b8edc))
ROM_LOAD("036411-01.k1", 0x6800, 0x0800, CRC(ad281297) SHA1(54c5e06b2e69eb731a6c9b1704e4340f493e7ea5))
ROM_LOAD("036410-01.lm1", 0x7000, 0x0800, CRC(b7bfaa4) SHA1(33ae0f68b4e2eae9f3aecbee2d0b29003ce460b2))
ROM_LOAD("036409-01.n1", 0x7800, 0x0800, CRC(1e14e919) SHA1(448fab30535e6fad7e0ab4427bc06bbbe075e797))
ROM_RELOAD(0xf800, 0x0800)
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(bzonea)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036422-01.bc3", 0x3000, 0x0800, CRC(7414177b) SHA1(147d97a3b475e738ce00b1a7909bbd787ad06eda))
ROM_LOAD("036421-01.a3", 0x3800, 0x0800, CRC(8ea8f939) SHA1(b71e0ab0e220c3e64dc2b094c701fb1a960b64e4))

ROM_LOAD("036414-01.e1", 0x5000, 0x0800, CRC(efbc3fa0) SHA1(6d284fab34b09dde8aa0df7088711d4723f07970))
ROM_LOAD("036413-01.h1", 0x5800, 0x0800, CRC(5d9d9111) SHA1(42638cff53a9791a0f18d316f62a0ea8eea4e194))
ROM_LOAD("036412-01.j1", 0x6000, 0x0800, CRC(ab55cbd2) SHA1(6bbb8316d9f8588ea0893932f9174788292b8edc))
ROM_LOAD("036411-01.k1", 0x6800, 0x0800, CRC(ad281297) SHA1(54c5e06b2e69eb731a6c9b1704e4340f493e7ea5))
ROM_LOAD("036410-01.lm1", 0x7000, 0x0800, CRC(b7bfaa4) SHA1(33ae0f68b4e2eae9f3aecbee2d0b29003ce460b2))
ROM_LOAD("036409-01.n1", 0x7800, 0x0800, CRC(1e14e919) SHA1(448fab30535e6fad7e0ab4427bc06bbbe075e797))
ROM_RELOAD(0xf800, 0x0800)

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(bzonec)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036422-01.bc3", 0x3000, 0x0800, CRC(7414177b) SHA1(147d97a3b475e738ce00b1a7909bbd787ad06eda))
ROM_LOAD("bz3b3800", 0x3800, 0x0800, CRC(76cf57f6) SHA1(1b8f3fcd664ed04ce60d94fdf27e56b20d52bdbd))
ROM_LOAD("bz1g4800", 0x4800, 0x0800, CRC(e228dd64) SHA1(247c788b4ccadf6c1e9201ad4f31d55c0036ff0f))
ROM_LOAD("bz1f5000", 0x5000, 0x0800, CRC(dddfac9a) SHA1(e6f2761902e1ffafba437a1117e9ba40f116087d))
ROM_LOAD("bz1e5800", 0x5800, 0x0800, CRC(7e00e823) SHA1(008e491a8074dac16e56c3aedec32d4b340158ce))
ROM_LOAD("bz1d6000", 0x6000, 0x0800, CRC(c0f8c068) SHA1(66fff6b493371f0015c21b06b94637db12deced2))
ROM_LOAD("bz1c6800", 0x6800, 0x0800, CRC(5adc64bd) SHA1(4574e4fe375d4ab3151a988235efa11e8744e2c6))
ROM_LOAD("bz1b7000", 0x7000, 0x0800, CRC(ed8a860e) SHA1(316a3c4870ba44bb3e9cb9fc5200eb081318facf))
ROM_LOAD("bz1a7800", 0x7800, 0x0800, CRC(4babf45) SHA1(a59da5ff49fc398ca4a948e28f05250af776b898))
ROM_RELOAD(0xf800, 0x0800)

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(bzonep)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("036422.01.bin", 0x3000, 0x0800, CRC(7414177b) SHA1(147d97a3b475e738ce00b1a7909bbd787ad06eda))
ROM_LOAD("036421.01.bin", 0x3800, 0x0800, CRC(8ea8f939) SHA1(b71e0ab0e220c3e64dc2b094c701fb1a960b64e4))
ROM_LOAD("036414.01.bin", 0x5000, 0x0800, CRC(55e0b5d5) SHA1(88cbf5c98b41cde5524e9bef537a03db9e783f17))
ROM_LOAD("036413.01.bin", 0x5800, 0x0800, CRC(5d9d9111) SHA1(42638cff53a9791a0f18d316f62a0ea8eea4e194))
ROM_LOAD("036412.01.bin", 0x6000, 0x0800, CRC(3e0931d7) SHA1(1366a8e9b18a5b2d3f23d9069845403b5d65d32d))
ROM_LOAD("036411.01.bin", 0x6800, 0x0800, CRC(ad281297) SHA1(54c5e06b2e69eb731a6c9b1704e4340f493e7ea5))
ROM_LOAD("036410.01.bin", 0x7000, 0x0800, CRC(b7bfaa4) SHA1(33ae0f68b4e2eae9f3aecbee2d0b29003ce460b2))
ROM_LOAD("036409.01.bin", 0x7800, 0x0800, CRC(debaea12) SHA1(d23cf62182e76562f4d4e9023a8be6243183da00))
ROM_LOAD("036408.01.bin", 0x8000, 0x0800, CRC(7513fc40) SHA1(c01c8959df2c24435a50caa46104a88a9fb7f7fd))
ROM_RELOAD(0xf800, 0x0800)

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(redbaron)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("037006-01.bc3", 0x3000, 0x0800, CRC(9fcffea0) SHA1(69b76655ee75742fcaa0f39a4a1cf3aa58088343))
ROM_LOAD("037007-01.a3", 0x3800, 0x0800, CRC(60250ede) SHA1(9c48952bd69863bee0c6dce09f3613149e0151ef))
ROM_LOAD("037587-01.fh1", 0x4800, 0x1000, CRC(60f23983) SHA1(7a9e5380bf49bf50a2d8ab0e0bd1ba3ac8efde24))
ROM_LOAD("037587-01.fh1", 0x5000, 0x1000, CRC(60f23983) SHA1(7a9e5380bf49bf50a2d8ab0e0bd1ba3ac8efde24))
ROM_LOAD("037000-01.e1", 0x5000, 0x0800, CRC(69bed808) SHA1(27d99efc74113cdcbbf021734b8a5a5fdb78c04c))
ROM_LOAD("036998-01.j1", 0x6000, 0x0800, CRC(d1104dd7) SHA1(0eab47cb45ede9dcc4dd7498dcf3a8d8194460b4))
ROM_LOAD("036997-01.k1", 0x6800, 0x0800, CRC(7434acb4) SHA1(c950c4c12ab556b5051ad356ab4a0ed6b779ba1f))
ROM_LOAD("036996-01.lm1", 0x7000, 0x0800, CRC(c0e7589e) SHA1(c1aedc95966afffd860d7e0009d5a43e8b292036))
ROM_LOAD("036995-01.n1", 0x7800, 0x0800, CRC(ad81d1da) SHA1(8bd66e5f34fc1c75f31eb6b168607e52aa3aa4df))
ROM_RELOAD(0xf800, 0x0800)
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(bradley)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("btb3.bin", 0x3000, 0x0800, CRC(88206304) SHA1(6a2e2ff35a929acf460f244db7968f3978b1d239))
ROM_LOAD("bta3.bin", 0x3800, 0x0800, CRC(d669d796) SHA1(ad606882320cd13612c7962d4718680fe5a35dd3))
ROM_LOAD("btc1.bin", 0x4000, 0x0800, CRC(bb8e049) SHA1(158517ff9a4e8ae7270ccf7eab87bf77427a4a8c))
ROM_LOAD("btd1.bin", 0x4800, 0x0800, CRC(9e0566d4) SHA1(f14aa5c3d14136c5e9a317004f82d44a8d5d6815))
ROM_LOAD("bte1.bin", 0x5000, 0x0800, CRC(64ee6a42) SHA1(33d0713ed2a1f4c1c443dce1f053321f2c279293))
ROM_LOAD("bth1.bin", 0x5800, 0x0800, CRC(baab67be) SHA1(77ad1935bf252b401bb6bbb57bd2ed66a85f0a6d))
ROM_LOAD("btj1.bin", 0x6000, 0x0800, CRC(36adde4) SHA1(16a9fcf98a2aa287e0b7a665b88c9c67377a1203))
ROM_LOAD("btk1.bin", 0x6800, 0x0800, CRC(f5c2904e) SHA1(f2cbf720c4f5ce0fc912dbc2f0445cb2c51ffac1))
ROM_LOAD("btlm.bin", 0x7000, 0x0800, CRC(7d0313bf) SHA1(17e3d8df62b332cf889133f1943c8f27308df027))
ROM_LOAD("btn1.bin", 0x7800, 0x0800, CRC(182c8c64) SHA1(511af60d86551291d2dc28442970b4863c62624a))
ROM_RELOAD(0xf800, 0x0800)
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("036408-01.k7", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

// Battlezone (Revision 1)
AAE_DRIVER_BEGIN(drv_bzone, "bzone", "Battlezone (Revision 1)")
AAE_DRIVER_ROM(rom_bzone)
AAE_DRIVER_FUNCS(&init_bzone, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_bzone)
AAE_DRIVER_SAMPLES(bzonesamples)
AAE_DRIVER_ART(bzoneart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NMI,
		/*int cb*/   nullptr,
		/*r8*/       BzoneRead,
		/*w8*/       BzoneWrite,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Battlezone (Revision A)
AAE_DRIVER_BEGIN(drv_bzonea, "bzonea", "Battlezone (Revision A)")
AAE_DRIVER_ROM(rom_bzonea)
AAE_DRIVER_FUNCS(&init_bzone, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_bzone)
AAE_DRIVER_SAMPLES(bzonesamples)
AAE_DRIVER_ART(bzoneart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 1512000, 100, 1, INT_TYPE_NMI, nullptr,
		BzoneRead, BzoneWrite,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Battlezone Cocktail Proto
AAE_DRIVER_BEGIN(drv_bzonec, "bzonec", "Battlezone Cocktail Proto")
AAE_DRIVER_ROM(rom_bzonec)
AAE_DRIVER_FUNCS(&init_bzone, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_bzone)
AAE_DRIVER_SAMPLES(bzonesamples)
AAE_DRIVER_ART(bzoneart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 1512000, 100, 1, INT_TYPE_NMI, nullptr,
		BzoneRead, BzoneWrite,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Battlezone Plus (Clay Cowgill)
AAE_DRIVER_BEGIN(drv_bzonep, "bzonep", "Battlezone Plus (Clay Cowgill)")
AAE_DRIVER_ROM(rom_bzonep)
AAE_DRIVER_FUNCS(&init_bzone, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_bzone)
AAE_DRIVER_SAMPLES(bzonesamples)
AAE_DRIVER_ART(bzoneart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 1512000, 100, 1, INT_TYPE_NMI, nullptr,
		BzoneRead, BzoneWrite,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Red Baron
AAE_DRIVER_BEGIN(drv_redbaron, "redbaron", "Red Baron")
AAE_DRIVER_ROM(rom_redbaron)
AAE_DRIVER_FUNCS(&init_redbaron, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_redbaron)
AAE_DRIVER_SAMPLES(redbaron_samples)
AAE_DRIVER_ART(redbaronart)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 1512000, 100, 1, INT_TYPE_NMI, nullptr,
		RedBaronRead, RedBaronWrite,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

// Bradley Trainer
AAE_DRIVER_BEGIN(drv_bradley, "bradley", "Bradley Trainer")
AAE_DRIVER_ROM(rom_bradley)
AAE_DRIVER_FUNCS(&init_bzone, &run_bzone, &end_bzone)
AAE_DRIVER_INPUT(input_ports_bzone)
AAE_DRIVER_SAMPLES(bzonesamples)
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 1512000, 100, 1, INT_TYPE_NMI, nullptr,
		BzoneRead, BzoneWrite,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY(), AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(45, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 460, 0, 395)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x2000, 0x1000)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_bzone)
AAE_REGISTER_DRIVER(drv_bzonea)
AAE_REGISTER_DRIVER(drv_bzonec)
AAE_REGISTER_DRIVER(drv_bzonep)
AAE_REGISTER_DRIVER(drv_redbaron)
AAE_REGISTER_DRIVER(drv_bradley)