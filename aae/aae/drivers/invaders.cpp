#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "invaders.h"
#include "old_mame_raster.h"
#include "rawinput.h"

// This is only a test.

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

ART_START(invaders_art)
ART_LOAD("invaders.zip", "invaders.png", ART_TEX, 0)
ART_LOAD("invaders.zip", "tintover2.png", ART_TEX, 1)
ART_END

ART_START(invaddlx_art)
ART_LOAD("invaddlx.zip", "invaddlx.png", ART_TEX, 0)
ART_LOAD("invaddlx.zip", "tintover.png", ART_TEX, 1)
ART_END

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
	return readinputportbytag("DSW1");
}

PORT_READ_HANDLER(deluxe_ip_port_1_r)
{
	return readinputportbytag("IN1");
}

PORT_READ_HANDLER(deluxe_ip_port_2_r)
{
	return readinputportbytag("DSW0");
}

PORT_READ_HANDLER(invaders_shift_data_r)
{
	return ((((shift_data1 << 8) | shift_data2) << shift_amount) >> 8) & 0xff;
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

MEM_READ(invaders_readmem)

MEM_END

MEM_WRITE(invaders_writemem)
MEM_ADDR(0x2000, 0x23ff, MWA_RAM)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x2400, 0x3fff, invaders_videoram_w)
MEM_ADDR(0x4000, 0x57ff, MWA_ROM)
MEM_END

PORT_READ(invaders_readport)
PORT_ADDR(0x01, 0x01, invaders_ip_port_0_r)
PORT_ADDR(0x02, 0x02, invaders_ip_port_1_r)
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
PORT_ADDR(0x01, 0x01, deluxe_ip_port_1_r)
PORT_ADDR(0x02, 0x02, deluxe_ip_port_2_r)
PORT_ADDR(0x03, 0x03, invaders_shift_data_r)
PORT_END

PORT_WRITE(invaddlx_writeport)
PORT_ADDR(0x02, 0x02, invaders_shift_amount_w)
PORT_ADDR(0x03, 0x03, invaders_sh_port_w)
PORT_ADDR(0x04, 0x04, invaders_shift_data_w)
PORT_ADDR(0x05, 0x05, invaders_sh_port5_w)
PORT_ADDR(0x06, 0x06, invaders_dummy_write)
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
	//init8080(invaders_readmem, invaders_writemem, invaddlx_readport, invaddlx_writeport, CPU0);
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
	//init8080(invaders_readmem, invaders_writemem, invaders_readport, invaders_writeport, CPU0);
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

INPUT_PORTS_START(invaders)
PORT_START("IN0")    /* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_START("DSW1")     /* DSW0 */
PORT_DIPNAME(0x03, 0x00, "Lives")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_DIPNAME(0x08, 0x00, "Bonus")
PORT_DIPSETTING(0x00, "1500")
PORT_DIPSETTING(0x08, "1000")
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT)
PORT_DIPNAME(0x80, 0x00, "Coin Info")
PORT_DIPSETTING(0x00, "On")
PORT_DIPSETTING(0x80, "Off")
INPUT_PORTS_END

INPUT_PORTS_START(invaddlx)
PORT_START("IN0")      /* IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_START("IN1")       /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_START("DSW0")      /* DSW0 */
PORT_DIPNAME(0x01, 0x00, "Lives")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPNAME(0x02, 0x00, "Unknown")
PORT_DIPSETTING(0x00, "Off")
PORT_DIPSETTING(0x02, "On")
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_DIPNAME(0x08, 0x00, "Preset Mode")
PORT_DIPSETTING(0x00, "Off")
PORT_DIPSETTING(0x08, "On")
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT)
PORT_DIPNAME(0x80, 0x00, "Coin Info")
PORT_DIPSETTING(0x00, "On")
PORT_DIPSETTING(0x80, "Off")
INPUT_PORTS_END

// Space Invaders
AAE_DRIVER_BEGIN(drv_invaders, "invaders", "Space Invaders")
AAE_DRIVER_ROM(rom_invaders)
AAE_DRIVER_FUNCS(&init_invaders, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_invaders)
AAE_DRIVER_SAMPLES(invaders_samples)
AAE_DRIVER_ART(invaders_art)

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

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Space Invaders Deluxe
AAE_DRIVER_BEGIN(drv_invaddlx, "invaddlx", "Space Invaders Deluxe")
AAE_DRIVER_ROM(rom_invaddlx)
AAE_DRIVER_FUNCS(&init_invaddlx, &run_invaders, &end_invaders)
AAE_DRIVER_INPUT(input_ports_invaddlx)
AAE_DRIVER_SAMPLES(invaders_samples)
AAE_DRIVER_ART(invaddlx_art)

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

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(0, 21 / 3, 0, init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
AAE_REGISTER_DRIVER(drv_invaders)
AAE_REGISTER_DRIVER(drv_invaddlx)