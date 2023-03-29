#include "raster.h"


UINT8 galaga_starcontrol[6];
void plot_pixel(struct osd_bitmap *b,int x,int y,int p)  { b->line[x][y] = p; }
int read_pixel (struct osd_bitmap *b,int x,int y)  { return b->line[x][y]; }

void clearbitmap(struct osd_bitmap *bitmap)
{
	int i; 	for (i = 0;i < bitmap->height;i++)	memset(bitmap->line[i],0,bitmap->width);
}

// Create a bitmap. Also call clearbitmap() to appropriately initialize it to 
// the background color. 
struct osd_bitmap *osd_create_bitmap(int width,int height)
{
	struct osd_bitmap *bitmap;

	//allegro_message("Mallocing in osd Bitmap");
	if ((bitmap = malloc(sizeof(struct osd_bitmap) + (height+2)*sizeof(unsigned char *))) != 0)
	{
		int i;
		unsigned char *bm;
       // allegro_message("assigning in osd Bitmap");
		bitmap->width = width;
		bitmap->height = height;
		if ((bm = malloc(width * height * sizeof(unsigned char))) == 0)
		{free(bitmap);return 0;	}
        // allegro_message("data1 in osd Bitmap");
		for (i = 0;i < height;i++)
			bitmap->line[i] = &bm[i * width];
        //allegro_message("data2 in osd Bitmap");
		bitmap->private = bm;
		//allegro_message("clearing osd Bitmap");
		clearbitmap(bitmap);
	}
	//allegro_message("returning osd Bitmap");
	return bitmap;
}

void osd_free_bitmap(struct osd_bitmap *bitmap)
{
	if (bitmap)
	{free(bitmap->private);	free(bitmap);}
}

/*
void loadrom(char *fn, int addr, int size)
{
        FILE *f=fopen(fn,"rb");
        if (!f)
        {
                allegro_message("File %s not found\n",fn);
                _chdir("..");
                exit(-1);
        }
        fread(ram+addr,size,1,f);
        fclose(f);
}
*/

int readbit(const unsigned char *src,int bitnum)
{
	int bit;


	bit = src[bitnum / 8] << (bitnum % 8);

	if (bit & 0x80) return 1;
	else return 0;
}

void decodechar(struct GfxElement *gfx,int num,const unsigned char *src,const struct GfxLayout *gl)
{
	int plane;
    static int ch=0;

	//ch++;
	//allegro_message("decoding char %d",ch);
	for (plane = 0;plane < gl->planes;plane++)
	{
		int offs,y;


		offs = num * gl->charincrement + gl->planeoffset[plane];
		for (y = 0;y < gl->height;y++)
		{
			int x;


			for (x = 0;x < gl->width;x++)
			{
				unsigned char *dp;


				dp = gfx->gfxdata->line[num * gl->height + y];
				if (plane == 0) dp[x] = 0;
				else dp[x] <<= 1;

				dp[x] += readbit(src,offs + gl->yoffset[y] + gl->xoffset[x]);
			}
		}
	}
}

struct GfxElement *decodegfx(const unsigned char *src,const struct GfxLayout *gl)
{
	int c;
	struct osd_bitmap *bm;
	struct GfxElement *gfx;

   // allegro_message("Creating Bitmap, width %d height %d",gl->width,gl->total * gl->height);
	if ((bm = osd_create_bitmap(gl->width,gl->total * gl->height)) == 0)
	{allegro_message("ack1");return 0;}
	
	//allegro_message("Malloc'ing");
	
	if ((gfx = malloc(sizeof(struct GfxElement))) == 0)
	{
		allegro_message("ack2");
	    return 0;
	}
	//allegro_message("assigning");
	gfx->width = gl->width;
	gfx->height = gl->height;
	gfx->total_elements = gl->total;
	gfx->color_granularity = 1 << gl->planes;
	gfx->gfxdata = bm;
    
    //allegro_message("decoding");
	for (c = 0;c < gl->total;c++)
	{decodechar(gfx,c,src,gl);}
    //allegro_message("returning");
   	return gfx;
}


void freegfx(struct GfxElement *gfx)
{
	if (gfx)
	{
		osd_free_bitmap(gfx->gfxdata);
		free(gfx);
	}
}


void drawgfx(struct osd_bitmap *dest,const struct GfxElement *gfx,
		unsigned int code,unsigned int color,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color)
{
	int ox,oy,ex,ey,x,y,start;
	const unsigned char *sd;
	unsigned char *bm;
	int col;
    int me=0;

	if (!gfx) return;
	/* check bounds */
	ox = sx;
	oy = sy;
	ex = sx + gfx->width-1;
	if (sx < 0) sx = 0;
	if (clip && sx < clip->min_x) sx = clip->min_x;
	if (ex >= dest->width) ex = dest->width-1;
	if (clip && ex > clip->max_x) ex = clip->max_x;
	if (sx > ex) return;
	ey = sy + gfx->height-1;
	if (sy < 0) sy = 0;
	if (clip && sy < clip->min_y) sy = clip->min_y;
	if (ey >= dest->height) ey = dest->height-1;
	if (clip && ey > clip->max_y) ey = clip->max_y;
	if (sy > ey) return;
   	start = (code % gfx->total_elements) * gfx->height;
	
	if (gfx->colortable)	/* remap colors */
	{
		//const unsigned char *paldata;

       
        //paldata = &gfx->colortable[(gfx->color_granularity * color)+gfx->pal_start_this];//128
		const unsigned char *paldata;	

		paldata = &gfx->colortable[(gfx->color_granularity * color)+gfx->pal_start_this];

		switch (transparency)
		{
			case TRANSPARENCY_NONE:
				if (flipx)
				{
					if (flipy)	/* XY flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
								*(bm++) = paldata[*(sd--)];
						}
					}
					else 	/* X flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
								*(bm++) = paldata[*(sd--)];
						}
					}
				}
				else
				{
					if (flipy)	/* Y flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
								*(bm++) = paldata[*(sd++)];
						}
					}
					else		/* normal */
					{
						/* unrolled loop for the most common case */
						if (ex-sx+1 == 8)
						{
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
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
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
								for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
							{
								col = *(sd--);
								if (col != transparent_color) *bm = paldata[col];
								bm++;
							}
						}
					}
					else 	/* X flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
							{
								col = *(sd++);
								if (col != transparent_color) *bm = paldata[col];
								bm++;
							}
						}
					}
					else		/* normal */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
							{
								col = paldata[*(sd--)];
								if (col != transparent_color) *bm = col;
								bm++;
							}
						}
					}
					else 	/* X flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
							{
								col = paldata[*(sd++)];
								if (col != transparent_color) *bm = col;
								bm++;
							}
						}
					}
					else		/* normal */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
								*(bm++) = *(sd--);
						}
					}
					else 	/* X flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
								*(bm++) = *(sd--);
						}
					}
				}
				else
				{
					if (flipy)	/* Y flip */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							memcpy(bm,sd,ex-sx+1);
						}
					}
					else		/* normal */
					{
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
							memcpy(bm,sd,ex-sx+1);
						}
					}
				}
				break;

			case TRANSPARENCY_PEN:
			case TRANSPARENCY_COLOR:
				{
					int *sd4,x1;
					int trans4;


					trans4 = transparent_color * 0x01010101;

					if (flipx)
					{
						if (flipy)	/* XY flip */
						{
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd4 = (int *)(gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox) - 3);
								for (x = sx;x <= ex;x+=4)
								{
		/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
		/* might access memory outside it. The copy will be executed correctly, though. */
									if (*sd4 == trans4)
									{
										bm += 4;
									}
									else
									{
										sd = ((unsigned char *)sd4) + 3;
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
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd4 = (int *)(gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox) - 3);
								for (x = sx;x <= ex;x+=4)
								{
		/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
		/* might access memory outside it. The copy will be executed correctly, though. */
									if (*sd4 == trans4)
									{
										bm += 4;
									}
									else
									{
										sd = ((unsigned char *)sd4) + 3;
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
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd4 = (int *)(gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox));
								for (x = sx;x <= ex;x+=4)
								{
		/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
		/* might access memory outside it. The copy will be executed correctly, though. */
									if (*sd4 == trans4)
									{
										bm += 4;
									}
									else
									{
										sd = (unsigned char *)sd4;
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
							for (y = sy;y <= ey;y++)
							{
								bm = dest->line[y] + sx;
								sd4 = (int *)(gfx->gfxdata->line[start + (y-oy)] + (sx-ox));
								for (x = sx;x <= ex;x+=4)
								{
		/* WARNING: if the width of the area to copy is not a multiple of sizeof(int), this */
		/* might access memory outside it. The copy will be executed correctly, though. */
									if (*sd4 == trans4)
									{
										bm += 4;
									}
									else
									{
										sd = (unsigned char *)sd4;
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + gfx->width-1 - (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + gfx->height-1 - (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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
						for (y = sy;y <= ey;y++)
						{
							bm = dest->line[y] + sx;
							sd = gfx->gfxdata->line[start + (y-oy)] + (sx-ox);
							for (x = sx;x <= ex;x++)
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



void copybitmap(struct osd_bitmap *dest,struct osd_bitmap *src,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color)
{
	static struct GfxElement mygfx =
	{
		0,0,0,	/* filled in later */
		1,1,0,1
	};

	mygfx.width = src->width;
	mygfx.height = src->height;
	mygfx.gfxdata = src;
	drawgfx(dest,&mygfx,0,0,flipx,flipy,sx,sy,clip,transparency,transparent_color);
}