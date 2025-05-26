#include "aae_mame_driver.h"
#include "vicdual.h"
#include "raster.h"
#include "rawinput.h"

// This is only a test.

#pragma warning( disable : 4838 4003 )



static unsigned char depthch_color_prom[] =
{
	/* Depth Charge is a b/w game, here is the PROM for Head On */
	0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,
	0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,
};



unsigned char* vicdual_characterram;
static unsigned char dirtycharacter[256];

static int palette_bank = 0;

void vicdual_vh_convert_color_prom(unsigned char* palette, unsigned short* colortable, const unsigned char* color_prom)
{
	int i;

	for (i = 0; i < Machine->drv->total_colors / 2; i++)
	{
		int bit;

		/* background red component */
		bit = (*color_prom >> 3) & 0x01;
		*(palette++) = 0xff * bit;
		/* background green component */
		bit = (*color_prom >> 1) & 0x01;
		*(palette++) = 0xff * bit;
		/* background blue component */
		bit = (*color_prom >> 2) & 0x01;
		*(palette++) = 0xff * bit;

		/* foreground red component */
		bit = (*color_prom >> 7) & 0x01;
		*(palette++) = 0xff * bit;
		/* foreground green component */
		bit = (*color_prom >> 5) & 0x01;
		*(palette++) = 0xff * bit;
		/* foreground blue component */
		bit = (*color_prom >> 6) & 0x01;
		*(palette++) = 0xff * bit;

		color_prom++;
	}
}

WRITE_HANDLER(vicdual_characterram_w)
{
	if (vicdual_characterram[address] != data)
	{
		dirtycharacter[(address / 8) & 0xff] = 1;

		vicdual_characterram[address] = data;
	}
}

WRITE_HANDLER(vicdual_palette_bank_w)
{
	if (palette_bank != (data & 3))
	{
		palette_bank = data & 3;
		//memset(dirtybuffer, 1, videoram_size);
	}
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void vicdual_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh)
{
	int offs;


	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int charcode;


		charcode = videoram[offs];

		if (dirtycharacter[charcode])
		{
			int sx, sy;


			/* decode modified characters */
			if (dirtycharacter[charcode] == 1)
			{
				decodechar(Machine->gfx[0], charcode, vicdual_characterram, Machine->drv->gfxdecodeinfo[0].gfxlayout);
				dirtycharacter[charcode] = 2;
			}


			//dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;

			drawgfx(tmpbitmap, Machine->gfx[0],
				charcode,
				(charcode >> 5) + 8 * palette_bank,
				0, 0,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);

		}
	}


	for (offs = 0; offs < 256; offs++)
	{
		if (dirtycharacter[offs] == 2) dirtycharacter[offs] = 0;
	}


	/* copy the character mapped graphics */
	copybitmap(bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}

/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int vicdual_vh_start(void)
{
	if ((tmpbitmap = osd_create_bitmap(Machine->drv->screen_width, Machine->drv->screen_height)) == 0)
		return 1;

	return 0;
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void vicdual_vh_stop(void)
{
	osd_free_bitmap(tmpbitmap);
}



/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void vicdual_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	/* copy the character mapped graphics */
	copybitmap(main_bitmap, tmpbitmap, 0, 1, 0, 0, &Machine->gamedrv->visible_area, TRANSPARENCY_NONE, 0);
}



void vicdual_interrupt()
{
	//set_interrupt_vector(0x08);
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

PORT_READ_HANDLER(vicdual_ip_port_0_r)
{
	return  readinputportbytag("IN0");
}

PORT_READ_HANDLER(vicdual_ip_port_1_r)
{
	return readinputportbytag("IN1");
}




MEM_READ(vicdual_readmem)
MEM_ADDR(0x0000, 0x7fff, MRA_ROM )
MEM_ADDR(0xe000, 0xefff, MRA_RAM )
MEM_END

MEM_WRITE(vicdual_writemem) 
MEM_ADDR( 0x0000, 0x7fff, MWA_ROM )
//MEM_ADDR( 0xe000, 0xe3ff, videoram_w )// & videoram, & videoram_size },
MEM_ADDR( 0xe400, 0xe7ff, MWA_RAM )
MEM_ADDR(0xe800, 0xefff, vicdual_characterram_w)// & vicdual_characterram },
MEM_END

PORT_READ(vicdual_readport)
PORT_ADDR(0x01, 0x01, vicdual_ip_port_0_r)
PORT_ADDR(0x08, 0x08, vicdual_ip_port_1_r)
PORT_END

PORT_WRITE(vicdual_writeport)
{ 0x01, 0x01, carnival_sh_port1_w },
{ 0x02, 0x02, carnival_sh_port2_w },
{ 0x40, 0x40, vicdual_palette_bank_w },
PORT_END

void run_vicdual()
{
	watchdog_reset_w(0, 0, 0);
	vicdual_vh_screenrefresh();
}

void end_vicdual()
{
	vicdual_vh_stop();
}

int init_vicdual()
{
	wrlog("vicdual init called");

	vicdual_videoram = &Machine->memory_region[0][0x2400];
	init8080(vicdual_readmem, vicdual_writemem, vicdual_readport, vicdual_writeport, CPU0);
	
	vicdual_vh_start();

	return 0;
}
