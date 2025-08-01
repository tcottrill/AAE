#include "rallyx.h"
#include "rallyx_vid.h"
#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "namco.h"

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
	init_z80(rallyx_readmem, rallyx_writemem, rallyx_readport, rallyx_writeport, 0);

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