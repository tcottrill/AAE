#include "aae_mame_driver.h"
#include "bosco_vid.h"
#include "old_mame_raster.h"

extern unsigned char* bosco_videoram2;
extern unsigned char* bosco_colorram2;
extern unsigned char* bosco_radarx;
extern unsigned char* bosco_radary;
extern unsigned char* bosco_radarattr;
size_t bosco_radarram_size;
extern unsigned char* bosco_scrollx;
extern unsigned char* bosco_scrolly;
//static unsigned char* dirtybuffer2;	/* keep track of modified portions of the screen */
extern unsigned char* bosco_radarcarx, * bosco_radarcary, * bosco_radarcarcolor;									
static struct osd_bitmap* tmpbitmap1;
extern int bosco_flipscreen;

static struct rectangle spritevisiblearea =
{
	0, 28 * 8 - 1,
	0, 28 * 8 - 1
};

static struct rectangle spritevisibleareaflip =
{
	8 * 8, 36 * 8 - 1,
	0, 28 * 8 - 1
};

static struct rectangle radarvisiblearea =
{
	28 * 8, 36 * 8 - 1,
	0, 28 * 8 - 1
};

static struct rectangle radarvisibleareaflip =
{
	0, 8 * 8 - 1,
	0, 28 * 8 - 1
};

const rectangle visible_area =
{ 0, 287, 0, 223 };


void bosco_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	for (i = 0; i < Machine->drv->total_colors; i++)
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

	/* color_prom now points to the beginning of the lookup table */

	/* character lookup table */
	/* sprites use the same color lookup table as characters */
	/* characters use colors 0-15 */
	for (i = 0; i < TOTAL_COLORS(0); i++)
		COLOR(0, i) = *(color_prom++) & 0x0f;

	/* radar dots lookup table */
	/* they use colors 16-19 */
	for (i = 0; i < 4; i++)
		COLOR(2, i) = 16 + i;
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void bosco_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	int offs, sx, sy;
	int flipx, flipy;
	int scrollx = 0; int scrolly = 0;

	// copy the temporary bitmap to the screen
	{
		if (bosco_flipscreen)
		{
			scrollx = (*bosco_scrollx-1) + 32;
			scrolly = (*bosco_scrolly + 16) - 32;
		}
		else
		{
			scrollx = -(*bosco_scrollx - 3);
			scrolly = -(*bosco_scrolly + 16);
		}
	}

	// Characters
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		sx = offs % 32;
		sy = offs / 32;
		flipx = ~bosco_colorram2[offs] & 0x40;
		flipy = bosco_colorram2[offs] & 0x80;
		if (bosco_flipscreen)
		{
			sx = 31 - sx;
			sy = 31 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}
		drawgfx(tmpbitmap1, Machine->gfx[0],
			bosco_videoram2[offs],
			bosco_colorram2[offs] & 0x3f,
			flipx, flipy,
			8 * sx, 8 * sy,
			//(8 * sx + scrollx) & 0xff, (8 * sy + scrolly) & 0xff,
			0, TRANSPARENCY_NONE, 0);
	}

	// update radar
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int flipx, flipy;
		sx = (offs % 32) ^ 4;
		sy = offs / 32 - 2;
		flipx = ~colorram[offs] & 0x40;
		flipy = colorram[offs] & 0x80;
		if (bosco_flipscreen)
		{
			sx = 7 - sx;
			sy = 27 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}
		drawgfx(tmpbitmap, Machine->gfx[0],
			videoram[offs],
			colorram[offs] & 0x3f,
			flipx, flipy,
			8 * sx, 8 * sy,
			&radarvisibleareaflip, TRANSPARENCY_NONE, 0);
	}

	copyscrollbitmap(main_bitmap, tmpbitmap1, 1, &scrollx, 1, &scrolly, &visible_area, TRANSPARENCY_NONE, 0);
	

	// draw the sprites

	/* draw the sprites */
	for (offs = 0; offs < spriteram_size; offs += 2)
	{
		sx = spriteram[offs + 1] + ((spriteram_2[offs + 1] & 0x80) << 1) - 1;
		sy = 225 - spriteram_2[offs] - 1;

		drawgfx(main_bitmap, Machine->gfx[1],
			(spriteram[offs] & 0xfc) >> 2,
			spriteram_2[offs + 1] & 0x3f,
			spriteram[offs] & 1, spriteram[offs] & 2,
			sx, sy,
			bosco_flipscreen ? &spritevisibleareaflip : &spritevisiblearea, TRANSPARENCY_COLOR, 0);
	}

	/* draw the above sprite priority characters */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int flipx, flipy;


		if (!(bosco_colorram2[offs] & 0x20))  continue;

		sx = offs % 32;
		sy = offs / 32;
		flipx = ~bosco_colorram2[offs] & 0x40;
		flipy = bosco_colorram2[offs] & 0x80;
		if (bosco_flipscreen)
		{
			sx = 31 - sx;
			sy = 31 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}

		drawgfx(main_bitmap, Machine->gfx[0],
			bosco_videoram2[offs],
			bosco_colorram2[offs] & 0x3f,
			flipx, flipy,
			(8 * sx + scrollx) & 0xff, (8 * sy + scrolly) & 0xff,
			0, TRANSPARENCY_NONE, 0);
		drawgfx(main_bitmap, Machine->gfx[0],
			bosco_videoram2[offs],
			bosco_colorram2[offs] & 0x3f,
			flipx, flipy,
			((8 * sx + scrollx) & 0xff) - 256, (8 * sy + scrolly) & 0xff,
			0, TRANSPARENCY_NONE, 0);
	}


	/* radar */
	if (bosco_flipscreen)
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &radarvisibleareaflip, TRANSPARENCY_NONE, 0);
	else
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 28 * 8, 0, &radarvisiblearea, TRANSPARENCY_NONE, 0);


	// draw the cars on the radar (working perfectly, sub optimal rendering)
	for (offs = 0; offs < 9; offs++)
	{
		int x, y;
		int color;

		color = Machine->gfx[0]->colortable[3 + (bosco_radarattr[offs] >> 1)];

		x = bosco_radarx[offs] + ((~bosco_radarattr[offs] & 0x01) << 8) - 2;
		y = 235 - bosco_radary[offs];

		if (x >= radarvisiblearea.min_x && x < radarvisiblearea.max_x &&
			y > radarvisiblearea.min_y && y <= radarvisiblearea.max_y)
		{
			main_bitmap->line[y - 1][x] = color;
			main_bitmap->line[y - 1][x + 1] = color;
			main_bitmap->line[y][x] = color;
			main_bitmap->line[y][x + 1] = color;
		}
	}
}

int bosco_vh_start(void)
{
	videoram_size = 0x400;
	wrlog("TEMP BITMAP 1 CREATED");
	if ((tmpbitmap1 = osd_create_bitmap(32 * 8, 32 * 8)) == 0)
		return 1;
	return generic_vh_start();
}