#include "aae_mame_driver.h"
#include "galaxian_vid.h"
#include "old_mame_raster.h"

static struct rectangle spritevisiblearea =
{
	2 * 8 + 1, 32 * 8 - 1,
	2 * 8, 30 * 8 - 1
};
static struct rectangle spritevisibleareaflipx =
{
	0 * 8, 30 * 8 - 2,
	2 * 8, 30 * 8 - 1
};

#define MAX_STARS 250
#define STARS_COLOR_BASE 32

unsigned char* galaxian_attributesram;
unsigned char* galaxian_bulletsram;

int galaxian_bulletsram_size;
static int stars_on, stars_blink;
static int stars_type;	/* 0 = Galaxian stars */
/* 1 = Scramble stars */
/* 2 = Rescue stars (same as Scramble, but only half screen) */
static unsigned int stars_scroll;

struct star
{
	int x, y, code, col;
};
static struct star stars[MAX_STARS];
static int total_stars;
static int gfx_bank;	/* used by Pisces and "japirem" only */
static int gfx_extend;	/* used by Moon Cresta only */
static int bank_mask;	/* different games have different gfx bank switching */
static int flipscreen[2];

static int BackGround;					/* MJC 051297 */
static unsigned char backcolour[256];  	/* MJC 220198 */
void galaxian_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
	unsigned char* opalette;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	opalette = palette;

	/* first, the char acter/sprite palette */
	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		/* red component */
		bit0 = (*color_prom >> 0) & 0x01;
		bit1 = (*color_prom >> 1) & 0x01;
		bit2 = (*color_prom >> 2) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* green component */
		bit0 = (*color_prom >> 3) & 0x01;
		bit1 = (*color_prom >> 4) & 0x01;
		bit2 = (*color_prom >> 5) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* blue component */
		bit0 = 0;
		bit1 = (*color_prom >> 6) & 0x01;
		bit2 = (*color_prom >> 7) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		color_prom++;
	}

	/* now the stars */
	for (i = 0; i < 64; i++)
	{
		int bits;
		int map[4] = { 0x00, 0x88, 0xcc, 0xff };

		bits = (i >> 0) & 0x03;
		*(palette++) = map[bits];
		bits = (i >> 2) & 0x03;
		*(palette++) = map[bits];
		bits = (i >> 4) & 0x03;
		*(palette++) = map[bits];
	}

	/* characters and sprites use the same palette */
	for (i = 0; i < TOTAL_COLORS(0); i++)
	{
		if (i & 3) COLOR(0, i) = i;
		else COLOR(0, i) = 0;	/* 00 is always black, regardless of the contents of the PROM */
	}

	/* bullets can be either white or yellow */

	COLOR(2, 0) = 0;
	COLOR(2, 1) = 0x0f + STARS_COLOR_BASE;	/* yellow */
	COLOR(2, 2) = 0;
	COLOR(2, 3) = 0x3f + STARS_COLOR_BASE;	/* white */
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void galaxian_vh_screenrefresh()
{
	int i, offs;
	
	// for every character in the Video RAM, check if it has been modified
	// since last time and update it accordingly.
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int sx, sy, charcode;

		sx = offs % 32;
		sy = offs / 32;

		if (flipscreen[0]) sx = 31 - sx;
		if (flipscreen[1]) sy = 31 - sy;

		charcode = videoram[offs];

		drawgfx(tmpbitmap, Machine->gfx[0],
			charcode,
			galaxian_attributesram[2 * (offs % 32) + 1] & 0x07,
			flipscreen[0], flipscreen[1],
			8 * sx, 8 * sy,
			0, TRANSPARENCY_NONE, 0);
	}

	// copy the temporary bitmap to the screen
	{
		int scroll[32];

		if (flipscreen[0])
		{
			for (i = 0; i < 32; i++)
			{
				scroll[31 - i] = -galaxian_attributesram[2 * i];
				if (flipscreen[1]) scroll[31 - i] = -scroll[31 - i];
			}
		}
		else
		{
			for (i = 0; i < 32; i++)
			{
				scroll[i] = -galaxian_attributesram[2 * i];
				if (flipscreen[1]) scroll[i] = -scroll[i];
			}
		}

		copyscrollbitmap(main_bitmap, tmpbitmap, 0, 0, 32, scroll, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	// Draw the bullets
	for (offs = 0; offs < 4 * 8; offs += 4)
	{
		int x, y;
		int color;

		if (offs == 4 * 7)
			color = Machine->pens[0x0f + STARS_COLOR_BASE];	/* yellow */
		else color = Machine->pens[0x3f + STARS_COLOR_BASE];	/* white */

		x = 255 - galaxian_bulletsram[offs + 1];

		if (x >= Machine->drv->visible_area.min_x && x <= Machine->drv->visible_area.max_x)
		{
			y = 256 - galaxian_bulletsram[offs + 3] - 4;

			if (y >= 0)
			{
				int j;

				for (j = 0; j < 3; j++)
				{
					main_bitmap->line[x][y + j] = color;
				}
			}
		}
	}

	for (offs = spriteram_size - 4; offs >= 0; offs -= 4)
	{
		int flipx, flipy, sx, sy, spritecode;

		sx = spriteram[offs + 3];
		sy = 240 - spriteram[offs];
		flipx = spriteram[offs + 1] & 0x40;
		flipy = spriteram[offs + 1] & 0x80;

		if (flipscreen[0])
		{
			flipx = !flipx;
			sx = 240 - sx;
		}
		if (flipscreen[1])
		{
			flipy = !flipy;
			sy = 240 - sy;
		}

		spritecode = spriteram[offs + 1] & 0x3f;

		/* Moon Quasar and Crazy Kong have different bank selection bits*/
		if (spriteram[offs + 2] & bank_mask)
			spritecode += 64;

		/* gfx_bank is used by Pisces and japirem/Uniwars only */
		if (gfx_bank)
			spritecode += 64;

		/* gfx_extend is used by Moon Cresta only */
		if ((gfx_extend & 4) && (spritecode & 0x30) == 0x20)
			spritecode = (spritecode & 0x0f) | (gfx_extend << 4);

		drawgfx(main_bitmap, Machine->gfx[1],
			spritecode,
			spriteram[offs + 2] & 0x07,
			flipx, flipy,
			sx, sy,
			flipscreen[0] ? &spritevisibleareaflipx : &spritevisiblearea, TRANSPARENCY_PEN, 0);
	}

	if (stars_on)
	{
		// Galaxian stars
		for (offs = 0; offs < total_stars; offs++)
		{
			int x, y;

			x = (stars[offs].x + stars_scroll / 2) % 256;
			y = stars[offs].y;

			if ((y & 1) ^ ((x >> 4) & 1))
			{
				if (Machine->orientation & ORIENTATION_SWAP_XY)
				{
					int temp;

					temp = x;
					x = y;
					y = temp;
				}
				if (Machine->orientation & ORIENTATION_FLIP_X)
					x = 255 - x;
				if (Machine->orientation & ORIENTATION_FLIP_Y)
					y = 255 - y;

				if (main_bitmap->line[y][x] == Machine->pens[0] ||
					main_bitmap->line[y][x] == backcolour[x])
					main_bitmap->line[y][x] = stars[offs].col;
			}
		}
	}
}

void galaxian_flipx_w(int offset, int data)
{
	if (flipscreen[0] != (data & 1))
	{
		flipscreen[0] = data & 1;
		//memset(dirtybuffer, 1, videoram_size);
	}
}

void galaxian_flipy_w(int offset, int data)
{
	if (flipscreen[1] != (data & 1))
	{
		flipscreen[1] = data & 1;
		//memset(dirtybuffer, 1, videoram_size);
	}
}

void galaxian_attributes_w(int offset, int data)
{
	if ((offset & 1) && galaxian_attributesram[offset] != data)
	{
		int i;

		//for (i = offset / 2; i < videoram_size; i += 32)
			//dirtybuffer[i] = 1;
	}

	galaxian_attributesram[offset] = data;
}

void galaxian_stars_w(int offset, int data)
{
	stars_on = (data & 1);
	stars_scroll = 0;
}

void galaxian_vh_interrupt()
{
	stars_scroll++;
	cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

static int common_vh_start(void)
{
	int generator;
	int x, y;

	gfx_bank = 0;
	gfx_extend = 0;
	stars_on = 0;
	flipscreen[0] = 0;
	flipscreen[1] = 0;

	if (generic_vh_start() != 0)
		return 1;

	/* Default alternate background - Solid Blue - MJC 220198 */

	for (x = 0; x < 256; x++) backcolour[x] = Machine->pens[4];
	BackGround = 0;

	/* precalculate the star background */

	total_stars = 0;
	generator = 0;

	for (y = 255; y >= 0; y--)
	{
		for (x = 511; x >= 0; x--)
		{
			int bit1, bit2;

			generator <<= 1;
			bit1 = (~generator >> 17) & 1;
			bit2 = (generator >> 5) & 1;

			if (bit1 ^ bit2) generator |= 1;

			if (y >= Machine->drv->visible_area.min_y &&
				y <= Machine->drv->visible_area.max_y &&
				((~generator >> 16) & 1) &&
				(generator & 0xff) == 0xff)
			{
				int color;

				color = (~(generator >> 8)) & 0x3f;
				if (color && total_stars < MAX_STARS)
				{
					stars[total_stars].x = x;
					stars[total_stars].y = y;
					stars[total_stars].code = color;
					stars[total_stars].col = Machine->pens[color + STARS_COLOR_BASE];

					total_stars++;
				}
			}
		}
	}

	return 0;
}

int galaxian_vh_start(void)
{
	gfx_bank = 0;
	gfx_extend = 0;
	bank_mask = 0;
	stars_type = 0;
	videoram_size = 0x400;
	return common_vh_start();
}