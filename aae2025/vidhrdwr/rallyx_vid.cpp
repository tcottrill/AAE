#include "aae_mame_driver.h"
#include "rallyx_vid.h"
#include "old_mame_raster.h"

extern unsigned char* rallyx_videoram2;
extern unsigned char* rallyx_colorram2;
extern unsigned char* rallyx_radarx;
extern unsigned char* rallyx_radary;
extern unsigned char* rallyx_radarattr;
size_t rallyx_radarram_size;
extern unsigned char* rallyx_scrollx;
extern unsigned char* rallyx_scrolly;
//static unsigned char* dirtybuffer2;	/* keep track of modified portions of the screen */
extern unsigned char* rallyx_radarcarx, * rallyx_radarcary, * rallyx_radarcarcolor;
static struct osd_bitmap* tmpbitmap1;
extern int rallyx_flipscreen;

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

void rallyx_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
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
void rallyx_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	int offs, sx, sy;
	bool flipx = 0;
	bool flipy = 0;
	int scrollx = 0; int scrolly = 0;

	// copy the temporary bitmap to the screen
	{
		if (rallyx_flipscreen)
		{
			scrollx = (*rallyx_scrollx - 1) + 32;
			scrolly = (*rallyx_scrolly + 16) - 32;
		}
		else
		{
			scrollx = -(*rallyx_scrollx - 3);
			scrolly = -(*rallyx_scrolly + 16);
		}
	}

	// Characters
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		sx = offs % 32;
		sy = offs / 32;
		flipx = ~rallyx_colorram2[offs] & 0x40;
		flipy = rallyx_colorram2[offs] & 0x80;
		if (rallyx_flipscreen)
		{
			sx = 31 - sx;
			sy = 31 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}
		drawgfx(tmpbitmap1, Machine->gfx[0],
			rallyx_videoram2[offs],
			rallyx_colorram2[offs] & 0x3f,
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
		if (rallyx_flipscreen)
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
			rallyx_flipscreen ? &spritevisibleareaflip : &spritevisiblearea, TRANSPARENCY_COLOR, 0);
	}

	/* draw the above sprite priority characters */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int flipx, flipy;

		if (!(rallyx_colorram2[offs] & 0x20))  continue;

		sx = offs % 32;
		sy = offs / 32;
		flipx = ~rallyx_colorram2[offs] & 0x40;
		flipy = rallyx_colorram2[offs] & 0x80;
		if (rallyx_flipscreen)
		{
			sx = 31 - sx;
			sy = 31 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}

		drawgfx(main_bitmap, Machine->gfx[0],
			rallyx_videoram2[offs],
			rallyx_colorram2[offs] & 0x3f,
			flipx, flipy,
			(8 * sx + scrollx) & 0xff, (8 * sy + scrolly) & 0xff,
			0, TRANSPARENCY_NONE, 0);
		drawgfx(main_bitmap, Machine->gfx[0],
			rallyx_videoram2[offs],
			rallyx_colorram2[offs] & 0x3f,
			flipx, flipy,
			((8 * sx + scrollx) & 0xff) - 256, (8 * sy + scrolly) & 0xff,
			0, TRANSPARENCY_NONE, 0);
	}

	/* radar */
	if (rallyx_flipscreen)
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &radarvisibleareaflip, TRANSPARENCY_NONE, 0);
	else
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 28 * 8, 0, &radarvisiblearea, TRANSPARENCY_NONE, 0);

	// draw the cars on the radar (working perfectly, sub optimal rendering)
	for (offs = 0; offs < 9; offs++)
	{
		int x, y;
		int color;

		color = Machine->gfx[0]->colortable[3 + (rallyx_radarattr[offs] >> 1)];

		x = rallyx_radarx[offs] + ((~rallyx_radarattr[offs] & 0x01) << 8) - 2;
		y = 235 - rallyx_radary[offs];

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

int rallyx_vh_start(void)
{
	videoram_size = 0x400;
	LOG_INFO("TEMP BITMAP 1 CREATED");
	if ((tmpbitmap1 = osd_create_bitmap(32 * 8, 32 * 8)) == 0)
		return 1;
	return generic_vh_start();
}