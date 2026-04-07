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

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "invaders.h"
#include "old_mame_raster.h"
#include "rawinput.h"

// This is only a test.

uint8_t m_p1=0;
uint8_t m_p2=0;


#pragma warning( disable : 4838 4003 )

static const char* invaders_samples[] =
{
	"invaders.zip",
	"0.wav",	/* Shot/Missle */
	"1.wav",	/* Shot/Missle */
	"2.wav",	/* Base Hit/Explosion */
	"3.wav",	/* Invader Hit */
	"4.wav",	/* Fleet move 1 */
	"5.wav",	/* Fleet move 2 */
	"6.wav",	/* Fleet move 3 */
	"7.wav",	/* Fleet move 4 */
	"8.wav",	/* UFO/Saucer Hit */
	"9.wav",	/* Bonus Base */
	0       /* end of array */
};

// Samples recorded from MAME since I don't have a real PCB or an advanced discreet sound system

static const char* clowns_samples[] =
{
	"clowns_aae.zip",
	"springboard_hit.wav",	
	"balloon_hit.wav",	
	"miss.wav",	
	"midway_music.wav",	
	0       /* end of array */
};



#define BITSWAP8(val,B7,B6,B5,B4,B3,B2,B1,B0) \
		(((((val) >> (B7)) & 1) << 7) | \
		 ((((val) >> (B6)) & 1) << 6) | \
		 ((((val) >> (B5)) & 1) << 5) | \
		 ((((val) >> (B4)) & 1) << 4) | \
		 ((((val) >> (B3)) & 1) << 3) | \
		 ((((val) >> (B2)) & 1) << 2) | \
		 ((((val) >> (B1)) & 1) << 1) | \
		 ((((val) >> (B0)) & 1) << 0))

#define SHIFT  (((((shift_data1 << 8) | shift_data2) << (shift_amount & 0x07)) >> 8) & 0xff)

UINT8 flip = 0;

static int flipscreen;
static int screen_red;
static int screen_red_enabled;		/* 1 for games that can turn the screen red */
static int color_map_select;
static int shift_data1, shift_data2, shift_amount;

static unsigned char invaders_palette[] = /* V.V */ /* Smoothed pure colors, overlays are not so contrasted */
{
	0x00,0x00,0x00, /* BLACK */
	0xff,0x20,0x20, /* RED */
	0x20,0xff,0x20, /* GREEN */
	0xff,0xff,0x20, /* YELLOW */
	0xff,0xff,0xff, /* WHITE */
	0x20,0xff,0xff, /* CYAN */
	0xff,0x20,0xff,  /* PURPLE */
};

void init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	LOG_INFO("Init Palette Called");
	memcpy(palette, invaders_palette, sizeof(invaders_palette));
}

unsigned char* invaders_videoram;

/* palette colors (see drivers/invaders.c) */
enum { BLACK, RED, GREEN, YELLOW, WHITE, CYAN, PURPLE };

/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int invaders_vh_start(void)
{
	if ((tmpbitmap = osd_create_bitmap(Machine->drv->screen_width, Machine->drv->screen_height)) == 0)
		return 1;

	return 0;
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void invaders_vh_stop(void)
{
	osd_free_bitmap(tmpbitmap);
}

static void plot_pixel_p(int x, int y, int col)
{
	if (flipscreen)
	{
		x = 255 - x;
		y = 223 - y;
	}

	plot_pixel(tmpbitmap, x, y, Machine->pens[col]);
}

void plot_byte(int x, int y, int data, int fore_color, int back_color)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		plot_pixel_p(x, y, (data & 0x01) ? fore_color : back_color);

		x++;
		data >>= 1;
	}
}
WRITE_HANDLER(invaders_videoram_w)
{
	int x, y;

	invaders_videoram[address] = data;

	y = address / 32 + (256-224);
	x = 8 * (address % 32);

	plot_byte(x, y, data, WHITE, 0);
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void invaders_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	/* copy the character mapped graphics */
	copybitmap(main_bitmap, tmpbitmap, 0, 1, 0, 0, &Machine->gamedrv->visible_area, TRANSPARENCY_NONE, 0);
}

void invaders_flipscreen_w(int data)
{
	if (data != color_map_select)
	{
		color_map_select = data;
		//redraw_screen = 1;
	}

	if (input_port_3_r(0) & 0x01)
	{
		if (data != flipscreen)
		{
			flipscreen = data;
			//redraw_screen = 1;
		}
	}
}
void invaders_screen_red_w(int data)
{
	if (screen_red_enabled && (data != screen_red))
	{
		screen_red = data;
		//redraw_screen = 1;
	}
}

//FROM invaders Machine.c

PORT_READ_HANDLER(invaders_ip_port_0_r)
{
	return  readinputportbytag("IN0");
}

PORT_READ_HANDLER(invaders_ip_port_1_r)
{
	return readinputportbytag("IN1");
}

PORT_READ_HANDLER(invaders_ip_port_2_r)
{
	return readinputportbytag("DSW0");
}

PORT_READ_HANDLER(invaders_shift_data_r)
{
	return ((((shift_data1 << 8) | shift_data2) << shift_amount) >> 8) & 0xff;
}

int invaders_shift_data_rev_r(int offset)
{
	int	ret = SHIFT;

	ret = ((ret & 0x01) << 7)
		| ((ret & 0x02) << 5)
		| ((ret & 0x04) << 3)
		| ((ret & 0x08) << 1)
		| ((ret & 0x10) >> 1)
		| ((ret & 0x20) >> 3)
		| ((ret & 0x40) >> 5)
		| ((ret & 0x80) >> 7);

	return ret;
}


/* Catch the write to unmapped I/O port 6 */
PORT_WRITE_HANDLER(invaders_dummy_write)
{
}

PORT_WRITE_HANDLER(invaders_shift_amount_w)
{
	shift_amount = data;
}

PORT_WRITE_HANDLER(invaders_shift_data_w)
{
	shift_data2 = shift_data1;
	shift_data1 = data;
}

/****************************************************************************
	Extra / Different functions for Boot Hill                (MJC 300198)
****************************************************************************/

int boothill_shift_data_r(int address)
{
	if (shift_amount < 0x10)
		return invaders_shift_data_r(0,0);
	else
		return invaders_shift_data_rev_r(0);
}

/* Grays Binary again! */

static const int BootHillTable[8] = {
	0x00, 0x40, 0x60, 0x70, 0x30, 0x10, 0x50, 0x50
};

PORT_READ_HANDLER(boothill_port_0_r)
{
	return (input_port_0_r(0) & 0x8F) | BootHillTable[input_port_3_r(0) >> 5];
}

PORT_READ_HANDLER(boothill_port_1_r)
{
	return (input_port_1_r(0) & 0x8F) | BootHillTable[input_port_4_r(0) >> 5];
}

/*
 * Space Encounters uses rotary controllers on input ports 0 & 1
 * each controller responds 0-63 for reading, with bit 7 as
 * fire button.
 *
 * The controllers look like they returns Grays binary,
 * so I use a table to translate my simple counter into it!
 */

static const int ControllerTable[64] = {
	0  , 1  , 3  , 2  , 6  , 7  , 5  , 4  ,
	12 , 13 , 15 , 14 , 10 , 11 , 9  , 8  ,
	24 , 25 , 27 , 26 , 30 , 31 , 29 , 28 ,
	20 , 21 , 23 , 22 , 18 , 19 , 17 , 16 ,
	48 , 49 , 51 , 50 , 54 , 55 , 53 , 52 ,
	60 , 61 , 63 , 62 , 58 , 59 , 57 , 56 ,
	40 , 41 , 43 , 42 , 46 , 47 , 45 , 44 ,
	36 , 37 , 39 , 38 , 34 , 35 , 33 , 32
};

PORT_READ_HANDLER(gray6bit_controller0_r)
{
	return (input_port_0_r(0) & 0xc0) + (ControllerTable[input_port_0_r(0) & 0x3f] ^ 0x3f);
}

PORT_READ_HANDLER(gray6bit_controller1_r)
{
	return (input_port_1_r(0) & 0xc0) + (ControllerTable[input_port_1_r(0) & 0x3f] ^ 0x3f);
}

PORT_READ_HANDLER(seawolf_port_0_r)
{
	return (input_port_0_r(0) & 0xe0) + ControllerTable[input_port_0_r(0) & 0x1f];
}



void invaders_interrupt()
{
	if (flip)
	{
		set_interrupt_vector(0x08);
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
	}
	else
	{
		set_interrupt_vector(0x10);
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
	}

	flip ^= 1;
}

PORT_WRITE_HANDLER(invaders_sh_port_w)
{
	static unsigned char Sound = 0;

	if (data & 0x01 && ~Sound & 0x01)
		sample_start(0, 0, 1);

	if (~data & 0x01 && Sound & 0x01)
		sample_stop(0);

	if (data & 0x02 && ~Sound & 0x02)
		sample_start(1, 1, 0);

	if (~data & 0x02 && Sound & 0x02)
		sample_stop(1);

	if (data & 0x04 && ~Sound & 0x04)
		sample_start(2, 2, 0);

	if (~data & 0x04 && Sound & 0x04)
		sample_stop(2);

	if (data & 0x08 && ~Sound & 0x08)
		sample_start(3, 3, 0);

	if (~data & 0x08 && Sound & 0x08)
		sample_stop(3);

	if (data & 0x10 && ~Sound & 0x10)
		sample_start(9, 9, 0);

	if (~data & 0x10 && Sound & 0x10)
		sample_stop(9);

	invaders_screen_red_w(data & 0x04);

	Sound = data;
}

PORT_WRITE_HANDLER(invaders_sh_port5_w)
{
	static unsigned char Sound = 0;

	if (data & 0x01 && ~Sound & 0x01)
		sample_start(4, 4, 0);

	if (data & 0x02 && ~Sound & 0x02)
		sample_start(5, 5, 0);

	if (data & 0x04 && ~Sound & 0x04)
		sample_start(6, 6, 0);

	if (data & 0x08 && ~Sound & 0x08)
		sample_start(7, 7, 0);

	if (data & 0x10 && ~Sound & 0x10)
		sample_start(8, 8, 0);

	if (~data & 0x10 && Sound & 0x10)
		sample_stop(5);

	invaders_flipscreen_w(data & 0x20);

	Sound = data;
}


PORT_WRITE_HANDLER(clowns_sh_port5_w)
{
	static int last_p5 = 0;

	if (data != 0 && last_p5 == 0)
	{
		if (!sample_playing(3))
		{
			sample_start(3, 3, 0);
		}
	}
	last_p5 = data;
}


PORT_WRITE_HANDLER(clowns_sh_port7_w)
{
	uint8_t const rising = data & ~m_p2;
	m_p2 = data;

	// Bits 0-2: balloon pops (bottom, middle, top) - all use same sample
	if (rising & 0x07)
		sample_start(1, 1, 0);

	// Bit 4: springboard hit
	if (rising & 0x10)
		sample_start(0, 0, 0);

	// Bit 5: springboard miss
	if (rising & 0x20)
		sample_start(2, 2, 0);
}



/*******************************************************/
/*                                                     */
/* Midway "Space Encounters"                           */
/*                                                     */
/*                                                     */
/*******************************************************/

PORT_WRITE(spcenctr_writeport)
	{ 0x01, 0x01, invaders_shift_amount_w },
	{ 0x02, 0x02, invaders_shift_data_w },
PORT_END

PORT_READ(spcenctr_readport)
	{ 0x00, 0x00, gray6bit_controller0_r }, /* These 2 ports use Gray's binary encoding */
	{ 0x01, 0x01, gray6bit_controller1_r },
	{ 0x02, 0x02, invaders_ip_port_2_r },
PORT_END

PORT_WRITE(writeport_1_2)
PORT_ADDR(0x01, 0x01, invaders_shift_amount_w )
PORT_ADDR(0x02, 0x02, invaders_shift_data_w )
PORT_END



MEM_READ(invaders_readmem)

MEM_END

MEM_WRITE(invaders_writemem)
MEM_ADDR(0x2000, 0x23ff, MWA_RAM)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x2400, 0x3fff, invaders_videoram_w)
MEM_ADDR(0x4000, 0x57ff, MWA_ROM)
MEM_END

PORT_READ(invaders_readport)
PORT_ADDR(0x00, 0x00, invaders_ip_port_0_r)
PORT_ADDR(0x01, 0x01, invaders_ip_port_1_r)
PORT_ADDR(0x02, 0x02, invaders_ip_port_2_r)
PORT_ADDR(0x03, 0x03, invaders_shift_data_r)
PORT_END

PORT_WRITE(invaders_writeport)
PORT_ADDR(0x02, 0x02, invaders_shift_amount_w)
PORT_ADDR(0x03, 0x03, invaders_sh_port_w)
PORT_ADDR(0x04, 0x04, invaders_shift_data_w)
PORT_ADDR(0x05, 0x05, invaders_sh_port5_w)
PORT_ADDR(0x06, 0x06, invaders_dummy_write)
PORT_END

PORT_READ(invaddlx_readport)
PORT_ADDR(0x00, 0x00, invaders_ip_port_0_r)
PORT_ADDR(0x01, 0x01, invaders_ip_port_1_r)
PORT_ADDR(0x02, 0x02, invaders_ip_port_2_r)
PORT_ADDR(0x03, 0x03, invaders_shift_data_r)
PORT_END

PORT_WRITE(invaddlx_writeport)
PORT_ADDR(0x02, 0x02, invaders_shift_amount_w)
PORT_ADDR(0x03, 0x03, invaders_sh_port_w)
PORT_ADDR(0x04, 0x04, invaders_shift_data_w)
PORT_ADDR(0x05, 0x05, invaders_sh_port5_w)
PORT_ADDR(0x06, 0x06, invaders_dummy_write)
PORT_END

PORT_WRITE(clowns_writeport)
PORT_ADDR(0x01, 0x01, invaders_shift_amount_w)
PORT_ADDR(0x02, 0x02, invaders_shift_data_w)
PORT_ADDR(0x05, 0x05, clowns_sh_port5_w)
PORT_ADDR(0x07, 0x07, clowns_sh_port7_w)
PORT_END


void run_invaders()
{
	watchdog_reset_w(0, 0, 0);
	invaders_vh_screenrefresh();
}

void end_invaders()
{
	invaders_vh_stop();
}

int init_invaddlx()
{
	invaders_videoram = &Machine->memory_region[0][0x2400];
	screen_red_enabled = 0;
	flipscreen = 0;
	flip = 0;
	invaders_vh_start();
	return 0;
}

int init_invaders()
{
	LOG_INFO("Invaders init called");

	invaders_videoram = &Machine->memory_region[0][0x2400];
	screen_red_enabled = 0;
	flipscreen = 0;
	flip = 0;
	invaders_vh_start();

	return 0;
}

ROM_START(invaders)
ROM_REGION(0x10000, REGION_CPU1, 0)     /* 64k for code */
ROM_LOAD("invaders.h", 0x0000, 0x0800, CRC(734f5ad8) SHA1(ff6200af4c9110d8181249cbcef1a8a40fa40b7f))
ROM_LOAD("invaders.g", 0x0800, 0x0800, CRC(6bfaca4a) SHA1(16f48649b531bdef8c2d1446c429b5f414524350))
ROM_LOAD("invaders.f", 0x1000, 0x0800, CRC(0ccead96) SHA1(537aef03468f63c5b9e11dd61e253f7ae17d9743))
ROM_LOAD("invaders.e", 0x1800, 0x0800, CRC(14e538b0) SHA1(1d6ca0c99f9df71e2990b610deb9d7da0125e2d8))
ROM_END

ROM_START(invaddlx)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("invdelux.h", 0x0000, 0x0800, CRC(e690818f) SHA1(0860fb03a64d34a9704a1459a5e96929eafd39c7))
ROM_LOAD("invdelux.g", 0x0800, 0x0800, CRC(4268c12d) SHA1(df02419f01cf0874afd1f1aa16276751acd0604a))
ROM_LOAD("invdelux.f", 0x1000, 0x0800, CRC(f4aa1880) SHA1(995d77b67cb4f2f3781c2c8747cb058b7c1b3412))
ROM_LOAD("invdelux.e", 0x1800, 0x0800, CRC(408849c1) SHA1(f717e81017047497a2e9f33f0aafecfec5a2ed7d))
ROM_LOAD("invdelux.d", 0x4000, 0x0800, CRC(e8d5afcd) SHA1(91fde9a9e7c3dd53aac4770bd169721a79b41ed1))
ROM_END

ROM_START(clowns)
ROM_REGION(0x10000, REGION_CPU1, 0)     /* 64k for code */
ROM_LOAD("h2.cpu", 0x0000, 0x0400, CRC(ff4432eb) SHA1(997aee1e3669daa1d8169b4e103d04baaab8ea8d))
ROM_LOAD("g2.cpu", 0x0400, 0x0400, CRC(676c934b) SHA1(72b681ca9ef23d820fdd297cc417932aecc9677b))
ROM_LOAD("f2.cpu", 0x0800, 0x0400, CRC(00757962) SHA1(ef39211493393e97284a08eea63be0757643ac88))
ROM_LOAD("e2.cpu", 0x0c00, 0x0400, CRC(9e506a36) SHA1(8aad486a72d148d8b03e7bec4c12abd14e425c5f))
ROM_LOAD("d2.cpu", 0x1000, 0x0400, CRC(d61b5b47) SHA1(6051c0a2e81d6e975e82c2d48d0e52dc0d4723e3))
ROM_LOAD("c2.cpu", 0x1400, 0x0400, CRC(154d129a) SHA1(61eebb319ee3a6be598b764b295c18a93a953c1e))
ROM_END

ROM_START(clowns1)
ROM_REGION(0x10000, REGION_CPU1, 0)     /* 64k for code */
ROM_LOAD("clownsv1.h", 0x0000, 0x0400, CRC(5560c951) SHA1(b6972e1918604263579de577ec58fa6a91e8ff3e))
ROM_LOAD("clownsv1.g", 0x0400, 0x0400, CRC(6a571d66) SHA1(e825f95863e901a1b648c74bb47098c8e74f179b))
ROM_LOAD("clownsv1.f", 0x0800, 0x0400, CRC(a2d56cea) SHA1(61bc07e6a24a1980216453b4dd2688695193a4ae))
ROM_LOAD("clownsv1.e", 0x0c00, 0x0400, CRC(bbd606f6) SHA1(1cbaa21d9834c8d76cf335fd118851591e815c86))
ROM_LOAD("clownsv1.d", 0x1000, 0x0400, CRC(37b6ff0e) SHA1(bf83bebb6c14b3663ca86a180f9ae3cddb84e571))
ROM_LOAD("clownsv1.c", 0x1400, 0x0400, CRC(12968e52) SHA1(71e4f09d30b992a4ac44b0e88e83b4f8a0f63caa))
ROM_END


INPUT_PORTS_START(invaders)
PORT_START("IN0")      /* IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START ("IN1")     /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* must be ACTIVE_HIGH Super Invaders */

PORT_START("DSW0")      /* DSW0 */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_TILT)
PORT_DIPNAME(0x08, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x08, "1000")
PORT_DIPSETTING(0x00, "1500")
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_DIPNAME(0x80, 0x00, "Coin Info")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("FAKE")		/* Dummy port for cocktail mode */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x01, DEF_STR(Cocktail))
INPUT_PORTS_END



/*******************************************************/
/*                                                     */
/* Midway "Space Invaders Part II"                     */
/*                                                     */
/*******************************************************/

INPUT_PORTS_START(invadpt2)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)	/* otherwise high score entry ends right away */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("DSW0")
PORT_DIPNAME(0x01, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPNAME(0x02, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x02, DEF_STR(On))
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_TILT)
PORT_DIPNAME(0x08, 0x00, "High Score Preset Mode")
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x08, DEF_STR(On))
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_DIPNAME(0x80, 0x00, "Coin Info")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("FAKE")		/* Dummy port for cocktail mode */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x01, DEF_STR(Cocktail))
INPUT_PORTS_END


/*******************************************************/
/*                                                     */
/* Midway "Clowns"                                     */
/*                                                     */
/*******************************************************/

/*
 * Clowns (EPROM version)
 */
	INPUT_PORTS_START(clowns)
	PORT_START("IN0")    /* IN0 */
	PORT_ANALOG(0xff, 0x7f, IPT_PADDLE, 100, 10, 0x01, 0xfe)

	PORT_START("IN1")    /* IN1 */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN1)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START("DSW0")   /* IN2 Dips & Coins */
	PORT_DIPNAME(0x03, 0x00, "Coinage")
	PORT_DIPSETTING(0x03, "2 Coins/1 Credit")
	PORT_DIPSETTING(0x02, "2 Coins/1 or 2 Players Game")
	PORT_DIPSETTING(0x01, "1 Coin/1 or 2 Players Game")
	PORT_DIPSETTING(0x00, "1 Coin/1 Credit")
	PORT_DIPNAME(0x04, 0x00, "Unknown")
	PORT_DIPSETTING(0x00, "Off")
	PORT_DIPSETTING(0x04, "On")
	PORT_DIPNAME(0x08, 0x00, "Unknown")
	PORT_DIPSETTING(0x00, "Off")
	PORT_DIPSETTING(0x08, "On")
	PORT_DIPNAME(0x10, 0x00, "Balloon Resets")
	PORT_DIPSETTING(0x00, "Each row")
	PORT_DIPSETTING(0x10, "All rows")
	PORT_DIPNAME(0x20, 0x00, "Bonus Life")
	PORT_DIPSETTING(0x00, "3000")
	PORT_DIPSETTING(0x20, "4000")
	PORT_DIPNAME(0x40, 0x00, "Lives")
	PORT_DIPSETTING(0x00, "3")
	PORT_DIPSETTING(0x40, "4")
	PORT_DIPNAME(0x80, 0x00, "Test Mode")
	PORT_DIPSETTING(0x80, "On")
	PORT_DIPSETTING(0x00, "Off")
	INPUT_PORTS_END


// Space Invaders
AAE_DRIVER_BEGIN(drv_invaders, "invaders", "Space Invaders")
AAE_DRIVER_ROM(rom_invaders)
AAE_DRIVER_FUNCS(&init_invaders, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_invaders)
AAE_DRIVER_SAMPLES(invaders_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_8080,
		/*freq*/     2000000,
		/*div*/      100,
		/*ipf*/      2,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &invaders_interrupt,
		/*r8*/       invaders_readmem,
		/*w8*/       invaders_writemem,
		/*pr*/       invaders_readport,
		/*pw*/       invaders_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_270|ORIENTATION_FLIP_X)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

// Space Invaders Deluxe
AAE_DRIVER_BEGIN(drv_invaddlx, "invaddlx", "Space Invaders Deluxe")
AAE_DRIVER_ROM(rom_invaddlx)
AAE_DRIVER_FUNCS(&init_invaddlx, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_invadpt2)
AAE_DRIVER_SAMPLES(invaders_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_8080,
		/*freq*/     2000000,
		/*div*/      100,
		/*ipf*/      2,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &invaders_interrupt,
		/*r8*/       invaders_readmem,
		/*w8*/       invaders_writemem,
		/*pr*/       invaddlx_readport,   // deluxe ports
		/*pw*/       invaddlx_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
	VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1,
	ORIENTATION_ROTATE_270 | ORIENTATION_FLIP_X)
	//AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
//AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 28 * 8 - 1, 0 * 8, 32 * 8 - 1)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 223)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()


// Clowns
AAE_DRIVER_BEGIN(drv_clowns, "clowns", "Clowns")
AAE_DRIVER_ROM(rom_clowns)
AAE_DRIVER_FUNCS(&init_invaders, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_clowns)
AAE_DRIVER_SAMPLES(clowns_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_8080,
		/*freq*/     2000000,
		/*div*/      100,
		/*ipf*/      2,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &invaders_interrupt,
		/*r8*/       invaders_readmem,
		/*w8*/       invaders_writemem,
		/*pr*/       invaders_readport,
		/*pw*/       clowns_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT | ORIENTATION_FLIP_Y)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()


// Clowns1
AAE_DRIVER_BEGIN(drv_clowns1, "clowns1", "Clowns")
AAE_DRIVER_ROM(rom_clowns1)
AAE_DRIVER_FUNCS(&init_invaders, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_clowns)
AAE_DRIVER_SAMPLES(clowns_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
AAE_CPU_ENTRY(
/*type*/     CPU_8080,
/*freq*/     2000000,
/*div*/      100,
/*ipf*/      2,
/*int type*/ INT_TYPE_INT,
/*int cb*/   &invaders_interrupt,
/*r8*/       invaders_readmem,
/*w8*/       invaders_writemem,
/*pr*/       invaders_readport,
/*pw*/       clowns_writeport,
/*r16*/      nullptr,
/*w16*/      nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT | ORIENTATION_FLIP_Y)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()




AAE_REGISTER_DRIVER(drv_invaders)
AAE_REGISTER_DRIVER(drv_invaddlx)
AAE_REGISTER_DRIVER(drv_clowns)
AAE_REGISTER_DRIVER(drv_clowns1)