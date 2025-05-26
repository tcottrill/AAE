#include "aae_mame_driver.h"
#include "invaders.h"
#include "old_mame_raster.h"
#include "rawinput.h"

// This is only a test.

#pragma warning( disable : 4838 4003 )

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
	wrlog("Init Palette Called");
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

WRITE_HANDLER(invaders_videoram_w)
{
	int offset = address;

	if (invaders_videoram[offset] != data)
	{
		int i, x, y;

		invaders_videoram[offset] = data;

		//x = offset / 32 + 16;
		//y = 256 - 8 - 8 * (offset % 32);
	    //x = offset / 32;
	    //y = 256 - 8 - 8 * (offset % 32);
		
		// This completely screwed up arrangement is due to my current horrible back end rendering system. 
		y = (offset / 32);
		x = 262 - (8 * (offset % 32));

		for (i = 0; i < 8; i++)
		{
			int col = Machine->pens[WHITE];
			if (data & 0x01) plot_pixel_p(x, y, col);
			else plot_pixel_p(x, y, Machine->pens[BLACK]);
			x--;
			data >>= 1;
		}
	}
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
MEM_ADDR( 0x2000, 0x23ff, MWA_RAM)
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
	init8080(invaders_readmem, invaders_writemem, invaddlx_readport, invaddlx_writeport, CPU0);
	screen_red_enabled = 0;
	flipscreen = 0;
	flip = 0;
	invaders_vh_start();
	return 0;
}

int init_invaders()
{
	wrlog("Invaders init called");

	invaders_videoram = &Machine->memory_region[0][0x2400];
	init8080(invaders_readmem, invaders_writemem, invaders_readport, invaders_writeport, CPU0);
	screen_red_enabled = 0;
	flipscreen = 0;
	flip = 0;
	invaders_vh_start();

	return 0;
}
