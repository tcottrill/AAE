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
// THE CODE BELOW IS DERIVED FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include "old_mame_raster.h"

unsigned char* videoram;
int videoram_size;
unsigned char* colorram;
unsigned char* spriteram;
int spriteram_size;
unsigned char* spriteram_2;
int spriteram_2_size;
unsigned char* spriteram_3;
int spriteram_3_size;
unsigned char* flip_screen;
unsigned char* flip_screen_x;
unsigned char* flip_screen_y;
unsigned char* dirtybuffer;

//Bitmaps 
struct osd_bitmap* tmpbitmap;
struct osd_bitmap* tmpbitmap1;
struct osd_bitmap* main_bitmap;

struct GfxElement* gfx[MAX_GFX_ELEMENTS];



/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int generic_vh_start(void)
{
	if (videoram_size == 0)
	{
		LOG_INFO("Error: generic_vh_start() called but videoram_size not initialized\n");
		return 1;
	}

	if ((dirtybuffer = (unsigned char*)malloc(videoram_size)) == 0)
		return 1;
	memset(dirtybuffer, 1, videoram_size);

	if ((tmpbitmap = osd_create_bitmap(Machine->gamedrv->screen_width, Machine->gamedrv->screen_height)) == 0)
	{
		LOG_INFO("ERROR----- tmpbitmap create failed");
		free(dirtybuffer);
		return 1;
	}
	LOG_INFO("INIT: Raster Video Init Completed");
	return 0;
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void generic_vh_stop(void)
{
	free(dirtybuffer);
	osd_free_bitmap(tmpbitmap);
	dirtybuffer = 0;
	tmpbitmap = 0;
}

int videoram_r(int offset)
{
	return videoram[offset];
}

int colorram_r(int offset)
{
	return colorram[offset];
}

void videoram_w(int offset, int data)
{
	if (videoram[offset] != data)
	{
		//dirtybuffer[offset] = 1;
		videoram[offset] = data;
	}
}

void colorram_w(int offset, int data)
{
	if (colorram[offset] != data)
	{
		//dirtybuffer[offset] = 1;
		colorram[offset] = data;
	}
}

void plot_pixel(struct osd_bitmap* b, int x, int y, int p) { b->line[x][y] = p; }
int read_pixel(struct osd_bitmap* b, int x, int y) { return b->line[x][y]; }

void osd_clearbitmap(struct osd_bitmap* bitmap)
{
	int i; 	for (i = 0; i < bitmap->height; i++)	memset(bitmap->line[i], 0, bitmap->width);
}

/* Create a bitmap. Also calls osd_clearbitmap() to appropriately initialize */
/* it to the background color. */
/* VERY IMPORTANT: the function must allocate also a "safety area" 16 pixels wide all */
/* around the bitmap. This is required because, for performance reasons, some graphic */
/* routines don't clip at boundaries of the bitmap. */

const int safety = 16;

struct osd_bitmap* osd_create_bitmap(int width, int height)       // ASG 980209 
{
	struct osd_bitmap* bitmap;

	if ((bitmap = (struct osd_bitmap*)malloc(sizeof(struct osd_bitmap))) != 0)
	{
		int i, rowlen, rdwidth;
		unsigned char* bm;

		int depth = 8;

		bitmap->width = width;
		bitmap->height = height;
		bitmap->depth = 8;

		rdwidth = (width + 7) & ~7;     /* round width to a quadword */
		if (depth == 16)
			rowlen = 2 * (rdwidth + 2 * safety) * sizeof(unsigned char);
		else
			rowlen = (rdwidth + 2 * safety) * sizeof(unsigned char);

		if ((bm = (unsigned char*)malloc((height + 2 * safety) * rowlen)) == 0)
		{
			free(bitmap);
			return 0;
		}

		/* clear ALL bitmap, including safety area, to avoid garbage on right */
		/* side of screen is width is not a multiple of 4 */
		memset(bm, 0, (height + 2 * safety) * rowlen);

		if ((bitmap->line = (unsigned char**)malloc((height + 2 * safety) * sizeof(unsigned char*))) == 0)
		{
			free(bm);
			free(bitmap);
			return 0;
		}

		for (i = 0; i < height + 2 * safety; i++)
		{
			if (depth == 16)
				bitmap->line[i] = &bm[i * rowlen + 2 * safety];
			else
				bitmap->line[i] = &bm[i * rowlen + safety];
		}
		bitmap->line += safety;

		bitmap->privatebm = bm;

		osd_clearbitmap(bitmap);
	}
	LOG_INFO("INIT:Screen bitmap created");
	return bitmap;
}


void osd_free_bitmap(struct osd_bitmap* bitmap)
{
	if (bitmap)
	{
		free(bitmap->privatebm);	free(bitmap);
	}
}

int readbit(const unsigned char* src, int bitnum)
{
	int bit;

	bit = src[bitnum / 8] << (bitnum % 8);

	if (bit & 0x80) return 1;
	else return 0;
}

void decodechar(struct GfxElement* gfx, int num, const unsigned char* src, const struct GfxLayout* gl)
{
	int plane;
	static int ch = 0;

	for (plane = 0; plane < gl->planes; plane++)
	{
		int offs, y;

		offs = num * gl->charincrement + gl->planeoffset[plane];
		for (y = 0; y < gl->height; y++)
		{
			int x;

			for (x = 0; x < gl->width; x++)
			{
				unsigned char* dp;

				dp = gfx->gfxdata->line[num * gl->height + y];
				if (plane == 0) dp[x] = 0;
				else dp[x] <<= 1;

				dp[x] += readbit(src, offs + gl->yoffset[y] + gl->xoffset[x]);
			}
		}
	}
}

struct GfxElement* decodegfx(const unsigned char* src, const struct GfxLayout* gl)
{
	int c;
	struct osd_bitmap* bm;
	struct GfxElement* gfx;

	// allegro_message("Creating Bitmap, width %d height %d",gl->width,gl->total * gl->height);
	if ((bm = osd_create_bitmap(gl->width, gl->total * gl->height)) == 0)
	{
		LOG_INFO("Error Creating gfx bitmap"); return 0;
	}

	if ((gfx = (struct GfxElement*)malloc(sizeof(struct GfxElement))) == 0)
	{
		LOG_INFO("Error Malloc'ing memoey for main bitmap");
		return 0;
	}

	gfx->width = gl->width;
	gfx->height = gl->height;
	gfx->total_elements = gl->total;
	gfx->color_granularity = 1 << gl->planes;
	gfx->gfxdata = bm;

	for (c = 0; c < gl->total; c++)
	{
		decodechar(gfx, c, src, gl);
	}
	return gfx;
}

void freegfx(struct GfxElement* gfx)
{
	if (gfx)
	{
		osd_free_bitmap(gfx->gfxdata);
		free(gfx);
	}
}

void drawgfx(struct osd_bitmap* dest, const struct GfxElement* gfx,
	unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
	const struct rectangle* clip, int transparency, int transparent_color)
{
	int ox, oy, ex, ey, x, y, start;

	const unsigned char* sd;
	unsigned char* bm;
	int col;
	int me = 0;

	if (!gfx) return;
	/* check bounds */
	ox = sx;
	oy = sy;
	ex = sx + gfx->width - 1;
	if (sx < 0) sx = 0;
	if (clip && sx < clip->min_x) sx = clip->min_x;
	if (ex >= dest->width) ex = dest->width - 1;
	if (clip && ex > clip->max_x) ex = clip->max_x;
	if (sx > ex) return;
	ey = sy + gfx->height - 1;
	if (sy < 0) sy = 0;
	if (clip && sy < clip->min_y) sy = clip->min_y;
	if (ey >= dest->height) ey = dest->height - 1;
	if (clip && ey > clip->max_y) ey = clip->max_y;
	if (sy > ey) return;
	start = (code % gfx->total_elements) * gfx->height;

	if (gfx->colortable)	/* remap colors */
	{
		const unsigned char* paldata;

		//paldata = &gfx->colortable[(gfx->color_granularity * color) + gfx->pal_start_this];
		paldata = &gfx->colortable[gfx->color_granularity * (color % gfx->total_colors)];
		//LOG_INFO("PALDATA START HERE is %d");
		switch (transparency)
		{
		case TRANSPARENCY_NONE:
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
							*(bm++) = paldata[*(sd--)];
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
							*(bm++) = paldata[*(sd--)];
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
							*(bm++) = paldata[*(sd++)];
					}
				}
				else		/* normal */
				{
					/* unrolled loop for the most common case */
					if (ex - sx + 1 == 8)
					{
						for (y = sy; y <= ey; y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*(bm++) = paldata[*(sd++)];
							*bm = paldata[*sd];
						}
					}
					else
					{
						for (y = sy; y <= ey; y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
							for (x = sx; x <= ex; x++)
								*(bm++) = paldata[*(sd++)];
						}
					}
				}
			}
			break;

		case TRANSPARENCY_PEN:
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = *(sd--);
							if (col != transparent_color) *bm = paldata[col];
							bm++;
						}
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = *(sd--);
							if (col != transparent_color) *bm = paldata[col];
							bm++;
						}
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = *(sd++);
							if (col != transparent_color) *bm = paldata[col];
							bm++;
						}
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = *(sd++);
							if (col != transparent_color) *bm = paldata[col];
							bm++;
						}
					}
				}
			}
			break;

		case TRANSPARENCY_COLOR:
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = paldata[*(sd--)];
							if (col != transparent_color) *bm = col;
							bm++;
						}
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = paldata[*(sd--)];
							if (col != transparent_color) *bm = col;
							bm++;
						}
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = paldata[*(sd++)];
							if (col != transparent_color) *bm = col;
							bm++;
						}
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							col = paldata[*(sd++)];
							if (col != transparent_color) *bm = col;
							bm++;
						}
					}
				}
			}
			break;

		case TRANSPARENCY_THROUGH:
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = paldata[*sd];
							bm++;
							sd--;
						}
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = paldata[*sd];
							bm++;
							sd--;
						}
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = paldata[*sd];
							bm++;
							sd++;
						}
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = paldata[*sd];
							bm++;
							sd++;
						}
					}
				}
			}
			break;
		}
	}
	else
	{
		switch (transparency)
		{
		case TRANSPARENCY_NONE:		/* do a verbatim copy (faster) */
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
							*(bm++) = *(sd--);
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
							*(bm++) = *(sd--);
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						memcpy(bm, sd, ex - sx + 1);
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
						memcpy(bm, sd, ex - sx + 1);
					}
				}
			}
			break;

		case TRANSPARENCY_PEN:
		case TRANSPARENCY_COLOR:
		{
			int* sd4, x1;
			int trans4;

			trans4 = transparent_color * 0x01010101;

			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd4 = (int*)(gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox) - 3);
						for (x = sx; x <= ex; x += 4)
						{
							/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
							/* might access memory outside it. The copy will be executed correctly, though. */
							if (*sd4 == trans4)
							{
								bm += 4;
							}
							else
							{
								sd = ((unsigned char*)sd4) + 3;
								x1 = ex - x;
								if (x1 > 3) x1 = 3;
								while (x1 >= 0)
								{
									col = *(sd--);
									if (col != transparent_color) *bm = col;
									bm++;
									x1--;
								}
							}
							sd4--;
						}
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd4 = (int*)(gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox) - 3);
						for (x = sx; x <= ex; x += 4)
						{
							/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
							/* might access memory outside it. The copy will be executed correctly, though. */
							if (*sd4 == trans4)
							{
								bm += 4;
							}
							else
							{
								sd = ((unsigned char*)sd4) + 3;
								x1 = ex - x;
								if (x1 > 3) x1 = 3;
								while (x1 >= 0)
								{
									col = *(sd--);
									if (col != transparent_color) *bm = col;
									bm++;
									x1--;
								}
							}
							sd4--;
						}
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd4 = (int*)(gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox));
						for (x = sx; x <= ex; x += 4)
						{
							/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
							/* might access memory outside it. The copy will be executed correctly, though. */
							if (*sd4 == trans4)
							{
								bm += 4;
							}
							else
							{
								sd = (unsigned char*)sd4;
								x1 = ex - x;
								if (x1 > 3) x1 = 3;
								while (x1 >= 0)
								{
									col = *(sd++);
									if (col != transparent_color) *bm = col;
									bm++;
									x1--;
								}
							}
							sd4++;
						}
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd4 = (int*)(gfx->gfxdata->line[start + (y - oy)] + (sx - ox));
						for (x = sx; x <= ex; x += 4)
						{
							/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
							/* might access memory outside it. The copy will be executed correctly, though. */
							if (*sd4 == trans4)
							{
								bm += 4;
							}
							else
							{
								sd = (unsigned char*)sd4;
								x1 = ex - x;
								if (x1 > 3) x1 = 3;
								while (x1 >= 0)
								{
									col = *(sd++);
									if (col != transparent_color) *bm = col;
									bm++;
									x1--;
								}
							}
							sd4++;
						}
					}
				}
			}
		}
		break;

		case TRANSPARENCY_THROUGH:
			if (flipx)
			{
				if (flipy)	/* XY flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = *sd;
							bm++;
							sd--;
						}
					}
				}
				else 	/* X flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + gfx->width - 1 - (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = *sd;
							bm++;
							sd--;
						}
					}
				}
			}
			else
			{
				if (flipy)	/* Y flip */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + gfx->height - 1 - (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = *sd;
							bm++;
							sd++;
						}
					}
				}
				else		/* normal */
				{
					for (y = sy; y <= ey; y++)
					{
						bm = dest->line[y] + sx;
						sd = gfx->gfxdata->line[start + (y - oy)] + (sx - ox);
						for (x = sx; x <= ex; x++)
						{
							if (*bm == transparent_color)
								*bm = *sd;
							bm++;
							sd++;
						}
					}
				}
			}
			break;
		}
	}
}

void copybitmap(struct osd_bitmap* dest, struct osd_bitmap* src, int flipx, int flipy, int sx, int sy,
	const struct rectangle* clip, int transparency, int transparent_color)
{
	static struct GfxElement mygfx =
	{
		0,0,0,	/* filled in later */
		1,1,0,1
	};

	mygfx.width = src->width;
	mygfx.height = src->height;
	mygfx.gfxdata = src;
	drawgfx(dest, &mygfx, 0, 0, flipx, flipy, sx, sy, clip, transparency, transparent_color);
}

/***************************************************************************

  Copy a bitmap onto another with scroll and wraparound.
  This function supports multiple independently scrolling rows/columns.
  "rows" is the number of indepentently scrolling rows. "rowscroll" is an
  array of integers telling how much to scroll each row. Same thing for
  "cols" and "colscroll".
  If the bitmap cannot scroll in one direction, set rows or columns to 0.
  If the bitmap scrolls as a whole, set rows and/or cols to 1.
  Bidirectional scrolling is, of course, supported only if the bitmap
  scrolls as a whole in at least one direction.

***************************************************************************/
void copyscrollbitmap(struct osd_bitmap* dest, struct osd_bitmap* src,
	int rows, const int* rowscroll, int cols, const int* colscroll,
	const struct rectangle* clip, int transparency, int transparent_color)
{
	int srcwidth, srcheight;

	if (Machine->orientation & ORIENTATION_SWAP_XY)
	{
		srcwidth = src->height;
		srcheight = src->width;
	}
	else
	{
		srcwidth = src->width;
		srcheight = src->height;
	}

	if (rows == 0)
	{
		/* scrolling columns */
		int col, colwidth;
		struct rectangle myclip;

		colwidth = srcwidth / cols;

		myclip.min_y = clip->min_y;
		myclip.max_y = clip->max_y;

		col = 0;
		while (col < cols)
		{
			int cons, scroll;

			/* count consecutive columns scrolled by the same amount */
			scroll = colscroll[col];
			cons = 1;
			while (col + cons < cols && colscroll[col + cons] == scroll)
				cons++;

			if (scroll < 0) scroll = srcheight - (-scroll) % srcheight;
			else scroll %= srcheight;

			myclip.min_x = col * colwidth;
			if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
			myclip.max_x = (col + cons) * colwidth - 1;
			if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

			copybitmap(dest, src, 0, 0, 0, scroll, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, 0, scroll - srcheight, &myclip, transparency, transparent_color);

			col += cons;
		}
	}
	else if (cols == 0)
	{
		/* scrolling rows */
		int row, rowheight;
		struct rectangle myclip;

		rowheight = srcheight / rows;

		myclip.min_x = clip->min_x;
		myclip.max_x = clip->max_x;

		row = 0;
		while (row < rows)
		{
			int cons, scroll;

			/* count consecutive rows scrolled by the same amount */
			scroll = rowscroll[row];
			cons = 1;
			while (row + cons < rows && rowscroll[row + cons] == scroll)
				cons++;

			if (scroll < 0) scroll = srcwidth - (-scroll) % srcwidth;
			else scroll %= srcwidth;

			myclip.min_y = row * rowheight;
			if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
			myclip.max_y = (row + cons) * rowheight - 1;
			if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

			copybitmap(dest, src, 0, 0, scroll, 0, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, scroll - srcwidth, 0, &myclip, transparency, transparent_color);

			row += cons;
		}
	}
	else if (rows == 1 && cols == 1)
	{
		/* XY scrolling playfield */
		int scrollx, scrolly;

		if (rowscroll[0] < 0) scrollx = srcwidth - (-rowscroll[0]) % srcwidth;
		else scrollx = rowscroll[0] % srcwidth;

		if (colscroll[0] < 0) scrolly = srcheight - (-colscroll[0]) % srcheight;
		else scrolly = colscroll[0] % srcheight;

		copybitmap(dest, src, 0, 0, scrollx, scrolly, clip, transparency, transparent_color);
		copybitmap(dest, src, 0, 0, scrollx, scrolly - srcheight, clip, transparency, transparent_color);
		copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly, clip, transparency, transparent_color);
		copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly - srcheight, clip, transparency, transparent_color);
	}
	else if (rows == 1)
	{
		/* scrolling columns + horizontal scroll */
		int col, colwidth;
		int scrollx;
		struct rectangle myclip;

		if (rowscroll[0] < 0) scrollx = srcwidth - (-rowscroll[0]) % srcwidth;
		else scrollx = rowscroll[0] % srcwidth;

		colwidth = srcwidth / cols;

		myclip.min_y = clip->min_y;
		myclip.max_y = clip->max_y;

		col = 0;
		while (col < cols)
		{
			int cons, scroll;

			/* count consecutive columns scrolled by the same amount */
			scroll = colscroll[col];
			cons = 1;
			while (col + cons < cols && colscroll[col + cons] == scroll)
				cons++;

			if (scroll < 0) scroll = srcheight - (-scroll) % srcheight;
			else scroll %= srcheight;

			myclip.min_x = col * colwidth + scrollx;
			if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
			myclip.max_x = (col + cons) * colwidth - 1 + scrollx;
			if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

			copybitmap(dest, src, 0, 0, scrollx, scroll, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, scrollx, scroll - srcheight, &myclip, transparency, transparent_color);

			myclip.min_x = col * colwidth + scrollx - srcwidth;
			if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
			myclip.max_x = (col + cons) * colwidth - 1 + scrollx - srcwidth;
			if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

			copybitmap(dest, src, 0, 0, scrollx - srcwidth, scroll, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, scrollx - srcwidth, scroll - srcheight, &myclip, transparency, transparent_color);

			col += cons;
		}
	}
	else if (cols == 1)
	{
		/* scrolling rows + vertical scroll */
		int row, rowheight;
		int scrolly;
		struct rectangle myclip;

		if (colscroll[0] < 0) scrolly = srcheight - (-colscroll[0]) % srcheight;
		else scrolly = colscroll[0] % srcheight;

		rowheight = srcheight / rows;

		myclip.min_x = clip->min_x;
		myclip.max_x = clip->max_x;

		row = 0;
		while (row < rows)
		{
			int cons, scroll;

			/* count consecutive rows scrolled by the same amount */
			scroll = rowscroll[row];
			cons = 1;
			while (row + cons < rows && rowscroll[row + cons] == scroll)
				cons++;

			if (scroll < 0) scroll = srcwidth - (-scroll) % srcwidth;
			else scroll %= srcwidth;

			myclip.min_y = row * rowheight + scrolly;
			if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
			myclip.max_y = (row + cons) * rowheight - 1 + scrolly;
			if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

			copybitmap(dest, src, 0, 0, scroll, scrolly, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, scroll - srcwidth, scrolly, &myclip, transparency, transparent_color);

			myclip.min_y = row * rowheight + scrolly - srcheight;
			if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
			myclip.max_y = (row + cons) * rowheight - 1 + scrolly - srcheight;
			if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

			copybitmap(dest, src, 0, 0, scroll, scrolly - srcheight, &myclip, transparency, transparent_color);
			copybitmap(dest, src, 0, 0, scroll - srcwidth, scrolly - srcheight, &myclip, transparency, transparent_color);

			row += cons;
		}
	}
}

/* fill a bitmap using the specified pen */
void fillbitmap(struct osd_bitmap* dest, int pen, const struct rectangle* clip)
{
	int sx, sy, ex, ey, y;
	struct rectangle myclip;

	if (Machine->orientation & ORIENTATION_SWAP_XY)
	{
		if (clip)
		{
			myclip.min_x = clip->min_y;
			myclip.max_x = clip->max_y;
			myclip.min_y = clip->min_x;
			myclip.max_y = clip->max_x;
			clip = &myclip;
		}
	}
	if (Machine->orientation & ORIENTATION_FLIP_X)
	{
		if (clip)
		{
			int temp;

			temp = clip->min_x;
			myclip.min_x = dest->width - 1 - clip->max_x;
			myclip.max_x = dest->width - 1 - temp;
			myclip.min_y = clip->min_y;
			myclip.max_y = clip->max_y;
			clip = &myclip;
		}
	}
	if (Machine->orientation & ORIENTATION_FLIP_Y)
	{
		if (clip)
		{
			int temp;

			myclip.min_x = clip->min_x;
			myclip.max_x = clip->max_x;
			temp = clip->min_y;
			myclip.min_y = dest->height - 1 - clip->max_y;
			myclip.max_y = dest->height - 1 - temp;
			clip = &myclip;
		}
	}

	sx = 0;
	ex = dest->width - 1;
	sy = 0;
	ey = dest->height - 1;

	if (clip && sx < clip->min_x) sx = clip->min_x;
	if (clip && ex > clip->max_x) ex = clip->max_x;
	if (sx > ex) return;
	if (clip && sy < clip->min_y) sy = clip->min_y;
	if (clip && ey > clip->max_y) ey = clip->max_y;
	if (sy > ey) return;

	//osd_mark_dirty(sx, sy, ex, ey, 0);	/* ASG 971011 */

	for (y = sy; y <= ey; y++)
		memset(&dest->line[y][sx], pen, ex - sx + 1);
}