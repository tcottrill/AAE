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
#include "centiped_vid.h"
#include "old_mame_raster.h"

extern int centiped_flipscreen;

unsigned char*centiped_charpalette, *centiped_spritepalette;

const rectangle visible_area =
{
 0,
 240,
 0,
 256
};

static struct rectangle spritevisiblearea =
{
	1 * 8, 31 * 8 - 1,
	0 * 8, 30 * 8 - 1
};

static struct rectangle spritevisiblearea_flip =
{
	1 * 8, 31 * 8 - 1,
	2 * 8, 32 * 8 - 1
};

void centiped_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;


	for (i = 0; i < 16; i++)
	{
		if ((i & 0x08) == 0) /* luminance = 0 */
		{
			palette[3 * i] = 0xc0 * (i & 1);
			palette[3 * i + 1] = 0xc0 * ((i >> 1) & 1);
			palette[3 * i + 2] = 0xc0 * ((i >> 2) & 1);
		}
		else	/* luminance = 1 */
		{
			palette[3 * i] = 0xff * (i & 1);
			palette[3 * i + 1] = 0xff * ((i >> 1) & 1);
			palette[3 * i + 2] = 0xff * ((i >> 2) & 1);
		}
	}
}


/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void centiped_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	/*
	int offs;

	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int sx, sy;
		sx = offs % 32;
		sy = offs / 32;

		if (centiped_flipscreen){sy += 2;}

		drawgfx(tmpbitmap, Machine->gfx[0],
			(videoram[offs] & 0x3f) + 0x40,
			(sy + 1) / 8,	// support midframe palette changes in test mode 
			centiped_flipscreen, centiped_flipscreen,
			8 * sx, 8 * sy,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	// copy the temporary bitmap to the screen 
	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);


	// Draw the sprites 
	for (offs = 0; offs < 0x10; offs++)
	{
		int code = 0;
		int color = 0;
		int flipx = 0;
		int x = 0;
		int y = 0;


		code = ((spriteram[offs] & 0x3e) >> 1) | ((spriteram[offs] & 0x01) << 6);
		color = spriteram[offs + 0x30];
		flipx = (spriteram[offs] & 0x80);
		x = spriteram[offs + 0x20];
		y = 240 - spriteram[offs + 0x10];

		if (centiped_flipscreen)
		{
			y += 16;
		}

		drawgfx(main_bitmap, Machine->gfx[1],
			code,
			color & 0x3f,
			centiped_flipscreen, flipx,
			x, y,
			centiped_flipscreen ? &spritevisiblearea_flip : &spritevisiblearea,
			TRANSPARENCY_PEN, 0);
	}
	*/
	int offs;


	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		//if (dirtybuffer[offs])
	//	{
			int sx, sy;
			sx = offs % 32;
			sy = offs / 32;

			drawgfx(tmpbitmap, Machine->gfx[0],
				(videoram[offs] & 0x3f) + 0x40, 0,
				0, 0,
				8 * (sx + 1), 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		//}
	}


	/* copy the temporary bitmap to the screen */
	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);


	/* Draw the sprites 
	for (offs = 0; offs < 0x10; offs++)
	{
		if (spriteram[offs + 0x20] < 0xf8)
		{
			int spritenum, color;


			spritenum = spriteram[offs] & 0x3f;
			if (spritenum & 1) spritenum = spritenum / 2 + 64;
			else spritenum = spritenum / 2;

			color = spriteram[offs + 0x30];
			Machine->gfx[1]->colortable[3] =
				Machine->pens[15 - centiped_spritepalette[(color >> 4) & 3]];
			Machine->gfx[1]->colortable[2] =
				Machine->pens[15 - centiped_spritepalette[(color >> 2) & 3]];
			Machine->gfx[1]->colortable[1] =
				Machine->pens[15 - centiped_spritepalette[(color >> 0) & 3]];
			drawgfx(main_bitmap, Machine->gfx[1],
				spritenum, 0,
				spriteram[offs] & 0x80, 0,
				spriteram[offs + 0x20], 248 - spriteram[offs + 0x10],
				&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
		}
	}
	*/
	// Draw the sprites 
	for (offs = 0; offs < 0x10; offs++)
	{
		int code = 0;
		int color = 0;
		int flipx = 0;
		int x = 0;
		int y = 0;


		code = ((spriteram[offs] & 0x3e) >> 1) | ((spriteram[offs] & 0x01) << 6);
		color = spriteram[offs + 0x30];
		Machine->gfx[1]->colortable[3] =
			Machine->pens[15 - centiped_spritepalette[(color >> 4) & 3]];
		Machine->gfx[1]->colortable[2] =
			Machine->pens[15 - centiped_spritepalette[(color >> 2) & 3]];
		Machine->gfx[1]->colortable[1] =
			Machine->pens[15 - centiped_spritepalette[(color >> 0) & 3]];

		flipx = (spriteram[offs] & 0x80);
		x = spriteram[offs + 0x20];
		y = 240 - spriteram[offs + 0x10];

		if (centiped_flipscreen)
		{
			y += 16;
		}

		drawgfx(main_bitmap, Machine->gfx[1],
			code,
			color & 0x3f,
			centiped_flipscreen, flipx,
			x, y,
			centiped_flipscreen ? &spritevisiblearea_flip : &spritevisiblearea,
			TRANSPARENCY_PEN, 0);
	}

}

int centiped_vh_start(void)
{
	videoram = &Machine->memory_region[CPU0][0x0400];
	spriteram = &Machine->memory_region[CPU0][0x07c0];
	spriteram_size = 0x3f;

	return generic_vh_start();
}
