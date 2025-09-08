#include "rallyx.h"
#include "rallyx_vid.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "osd_video.h"
#include "namco.h"

static const char* rallyx_samples[] =
{
	"rallyx.zip",
	"bang.wav",
	0	/* end of array */
};


unsigned char* colortable2;
int rallyx_flipscreen;
unsigned char* rallyx_scrollx;
unsigned char* rallyx_scrolly;
unsigned char* rallyx_videoram2;
unsigned char* rallyx_colorram2;
unsigned char* rallyx_radarx;
unsigned char* rallyx_radary;
unsigned char* rallyx_radarattr;
int rallyx_radarram_size = 0x0b;
static int irq_vector = 0xff;
static int irq_pending = 0;
int irq_enable = 1;


static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 }, /* bits are packed in groups of four */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	16 * 8	/* every char takes 16 bytes */
};
static struct GfxLayout spritelayout =
{
	16,16,	/* 16*16 sprites */
	64, /* 64 sprites */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,	/* bits are packed in groups of four */
			 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
	64 * 8	/* every sprite takes 64 bytes */
};

static struct GfxLayout dotlayout =
{
	4,4,	/* 4*4 characters */
	8,	/* 8 characters */
	2,	/* 2 bits per pixel */
	{ 6, 7 },
	{ 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 3 * 32, 2 * 32, 1 * 32, 0 * 32 },
	16 * 8	/* every char takes 16 consecutive bytes */
};

struct GfxDecodeInfo rallyx_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,		  0, 64 },
	{ REGION_GFX1, 0, &spritelayout,	  0, 64 },
	{ REGION_GFX2, 0, &dotlayout,	   64 * 4,  1 }, 	  //64 * 4,  1 },
	{ -1 } /* end of array */
};



static struct namco_interface namco_interface =
{
	3072000 / 32,	// sample rate
	3,			// number of voices
	32,			// gain adjustment
	245			// playback volume
};
/*
void galaga_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		bit0 = (color_prom[31 - i] >> 0) & 0x01;
		bit1 = (color_prom[31 - i] >> 1) & 0x01;
		bit2 = (color_prom[31 - i] >> 2) & 0x01;
		palette[3 * i] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (color_prom[31 - i] >> 3) & 0x01;
		bit1 = (color_prom[31 - i] >> 4) & 0x01;
		bit2 = (color_prom[31 - i] >> 5) & 0x01;
		palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = 0;
		bit1 = (color_prom[31 - i] >> 6) & 0x01;
		bit2 = (color_prom[31 - i] >> 7) & 0x01;
		palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	}

	color_prom += 32;

	// characters and sprites color data
	for (i = 0; i < 256; i++)
	{
		colortable[i] = 31 - (*(color_prom++) & 0x0f);
	}
}
*/
//////////////////////////////////////////////////////////////
//MAIN RALLYX HANDLERS
//////////////////////////////////////////////////////////////

WRITE_HANDLER(rallyx_flipscreen_w)
{
	if (rallyx_flipscreen != (data & 1))
	{
		rallyx_flipscreen = data & 1;
	}
}

WRITE_HANDLER(rallyx_sound_w)
{
	namco_sound_w(address & 0x1f, data);
}

PORT_WRITE_HANDLER(rallyx_int_vector_w)
{
	if (irq_vector != data) {
		irq_vector = data;
		m_cpu_z80[0]->mz80ClearPendingInterrupt();
	}
}

WRITE_HANDLER(rallyx_interrupt_enable_w)
{
	irq_enable = data & 1;
	m_cpu_z80[0]->mz80ClearPendingInterrupt();
}

void rallyx_interrupt()
{
	if (irq_enable)
	{
		m_cpu_z80[0]->mz80int(irq_vector);
	}
}

WRITE_HANDLER(rallyx_coin_lockout_w)
{
	//coin_lockout_w(offset, data ^ 1);
}

WRITE_HANDLER(rallyx_coin_counter_w)
{
	//coin_counter_w(offset, data);
}

WRITE_HANDLER(rallyx_leds_w)
{
	//set_led_status(offset, data & 1);
}

WRITE_HANDLER(rallyx_play_sound_w)
{
	static int last;

	if (data == 0 && last != 0)
		sample_start(0, 0, 0);
		last = data;
}


void run_rallyx()
{
	rallyx_vh_screenrefresh();
	namco_sh_update();
}


////////////////////////////////////////////////////


PORT_READ(rallyx_readport)
PORT_END

PORT_WRITE(rallyx_writeport)
PORT_ADDR(0x00, 0x00, rallyx_int_vector_w)
PORT_END

MEM_READ(rallyx_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x8000, 0x8fff, MRA_RAM )
MEM_ADDR(0x9800, 0x9fff, MRA_RAM )
MEM_ADDR(0xa000, 0xa000, ip_port_0_r) /* IN0 */
MEM_ADDR(0xa080, 0xa080, ip_port_1_r) /* IN1 */
MEM_ADDR(0xa100, 0xa100, ip_port_2_r) /* DSW1 */
MEM_END

MEM_WRITE(rallyx_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
//MEM_ADDR(0x8000, 0x83ff, videoram_w, &videoram, &videoram_size )
//MEM_ADDR(0x8400, 0x87ff, rallyx_videoram2_w, &rallyx_videoram2 )
//MEM_ADDR(0x8800, 0x8bff, colorram_w, &colorram )
//MEM_ADDR(0x8c00, 0x8fff, rallyx_colorram2_w, &rallyx_colorram2 )
MEM_ADDR(0x9800, 0x9fff, MWA_RAM )
//MEM_ADDR(0xa004, 0xa00f, MWA_RAM, &rallyx_radarattr )
MEM_ADDR(0xa080, 0xa080, watchdog_reset_w)
MEM_ADDR(0xa100, 0xa11f, rallyx_sound_w)
//MEM_ADDR(0xa130, 0xa130, MWA_RAM, &rallyx_scrollx )
//MEM_ADDR(0xa140, 0xa140, MWA_RAM, &rallyx_scrolly )
MEM_ADDR( 0xa170, 0xa170, MWA_NOP )
MEM_ADDR(0xa180, 0xa180, rallyx_play_sound_w)
MEM_ADDR(0xa181, 0xa181, rallyx_interrupt_enable_w)
MEM_ADDR(0xa182, 0xa182, MWA_NOP)
MEM_ADDR(0xa183, 0xa183, rallyx_flipscreen_w)
MEM_ADDR(0xa184, 0xa185, rallyx_leds_w)
MEM_ADDR(0xa186, 0xa186, rallyx_coin_lockout_w)
MEM_ADDR(0xa187, 0xa187, rallyx_coin_counter_w)
//MEM_ADDR(0x8014, 0x801f, MWA_RAM), &spriteram, &spriteram_size )	/* these are here just to initialize */
//MEM_ADDR(0x8034, 0x803f, MWA_RAM, &rallyx_radarx, &rallyx_radarram_size ) /* ditto */
//MEM_ADDR(0x8834, 0x883f, MWA_RAM, &rallyx_radary )
MEM_END

int init_rallyx()
{
		
	//Init CPU's
	//init_z80(rallyx_readmem, rallyx_writemem, rallyx_readport, rallyx_writeport, 0);

	//Ram Pointers for video routines
	videoram = &Machine->memory_region[0][0x8000];
	videoram_size = 0x400;
	colorram = &Machine->memory_region[0][0x8800];
	rallyx_videoram2 = &Machine->memory_region[0][0x8400];
	rallyx_colorram2 = &Machine->memory_region[0][0x8C00];

	spriteram = &Machine->memory_region[0][0x8014];
	spriteram_size = 0x0b;
	spriteram_2 = &Machine->memory_region[0][0x8814];
	spriteram_2_size = 0x0b;

	rallyx_radarx = &Machine->memory_region[0][0x8034];
	rallyx_radary = &Machine->memory_region[0][0x8834];
	rallyx_radarattr = &Machine->memory_region[0][0xa004];

	rallyx_scrollx = &Machine->memory_region[0][0xa130];
	rallyx_scrolly = &Machine->memory_region[0][0xa140];

	/*
	game_rect->max_x = visible_area.max_x;
	game_rect->max_y = visible_area.max_y;
	game_rect->min_x = visible_area.min_x;
	game_rect->min_y = visible_area.min_y;
	*/

	rallyx_vh_start();
	//Start Namco Sound interface
	namco_sh_start(&namco_interface);

	return 0;
}

void end_rallyx()
{

}

INPUT_PORTS_START(rallyx)
PORT_START("IN0")		/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("IN1")	/* IN1 */
PORT_DIPNAME(0x01, 0x01, DEF_STR(Cabinet))
PORT_DIPSETTING(0x01, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)

PORT_START("DSW")	/* DSW0 */
PORT_SERVICE(0x01, IP_ACTIVE_LOW)
/* TODO: the bonus score depends on the number of lives */
PORT_DIPNAME(0x06, 0x02, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x02, "A")
PORT_DIPSETTING(0x04, "B")
PORT_DIPSETTING(0x06, "C")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x38, 0x08, DEF_STR(Difficulty))
PORT_DIPSETTING(0x10, "1 Car, Medium")
PORT_DIPSETTING(0x28, "1 Car, Hard")
PORT_DIPSETTING(0x00, "2 Cars, Easy")
PORT_DIPSETTING(0x18, "2 Cars, Medium")
PORT_DIPSETTING(0x30, "2 Cars, Hard")
PORT_DIPSETTING(0x08, "3 Cars, Easy")
PORT_DIPSETTING(0x20, "3 Cars, Medium")
PORT_DIPSETTING(0x38, "3 Cars, Hard")
PORT_DIPNAME(0xc0, 0xc0, DEF_STR(Coinage))
PORT_DIPSETTING(0x40, DEF_STR(2C_1C))
PORT_DIPSETTING(0xc0, DEF_STR(1C_1C))
PORT_DIPSETTING(0x80, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
INPUT_PORTS_END



ROM_START(rallyx)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("1b", 0x0000, 0x1000, CRC(5882700d) SHA1(b6029e9730f1694894fe8b729ac0ba8d6712dea9))
ROM_LOAD("rallyxn.1e", 0x1000, 0x1000, CRC(ed1eba2b) SHA1(82d3a4b34b0ff5cfdb8ca7c18ad5c63d943b8484))
ROM_LOAD("rallyxn.1h", 0x2000, 0x1000, CRC(4f98dd1c) SHA1(8a20fadcea76802d1c412ba62086abb846ad54a8))
ROM_LOAD("rallyxn.1k", 0x3000, 0x1000, CRC(9aacccf0) SHA1(9b22079972c0f9970d62d62751db4783a87796d5))

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("8e", 0x0000, 0x1000, CRC(277c1de5) SHA1(30bc57263e8dad870c501c76bce6f42d69ab9e00))

ROM_REGION(0x0100, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("rx1-6.8m", 0x0000, 0x0100, CRC(3c16f62c) SHA1(7a3800be410e306cf85753b9953ffc5575afbcd6))  // Prom type: IM5623    - dots

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("rx1-1.11n", 0x0000, 0x0020, CRC(c7865434) SHA1(70c1c9610ba6f1ead77f347e7132958958bccb31))  // Prom type: M3-7603-5 - palette
ROM_LOAD("rx1-7.8p", 0x0020, 0x0100, CRC(834d4fda) SHA1(617864d3df0917a513e8255ad8d96ae7a04da5a1))  // Prom type: IM5623    - lookup table
//ROM_LOAD("rx1-2.4n", 0x0120, 0x0020, CRC(8f574815) SHA1(4f84162db9d58b64742c67dc689eb665b9862fb3))  // Prom type: N82S123N  - video layout (not used)
//ROM_LOAD("rx1-3.7k", 0x0140, 0x0020, CRC(b8861096) SHA1(26fad384ed7a1a1e0ba719b5578e2dbb09334a25))  // Prom type: M3-7603-5 - video timing (not used)

ROM_REGION(0x0200, REGION_SOUND1, 0)
ROM_LOAD("rx1-5.3p", 0x0000, 0x0100, CRC(4bad7017) SHA1(3e6da9d798f5e07fa18d6ce7d0b148be98c766d5))  // Prom type: IM5623
ROM_LOAD("rx1-4.2m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))  // Prom type: IM5623 - not used
ROM_END

// Rally-X
AAE_DRIVER_BEGIN(drv_rallyx, "rallyx", "Rally-X")
AAE_DRIVER_ROM(rom_rallyx)
AAE_DRIVER_FUNCS(&init_rallyx, &run_rallyx, &end_rallyx)
AAE_DRIVER_INPUT(input_ports_rallyx)
AAE_DRIVER_SAMPLES(rallyx_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3072000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &rallyx_interrupt,
		/*r8*/       rallyx_readmem,
		/*w8*/       rallyx_writemem,
		/*pr*/       rallyx_readport,
		/*pw*/       rallyx_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0 * 8, 36 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(rallyx_gfxdecodeinfo, 32, 64 * 4 + 4 * 2, rallyx_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_rallyx)