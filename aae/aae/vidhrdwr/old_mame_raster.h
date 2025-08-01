#ifndef RASTER_H
#define RASTER_H

#pragma warning(disable:4996 4102)

#include "sys_log.h"
#include "aae_mame_driver.h"

#define MAX_PENS 256
#define TRANSPARENCY_NONE 0
#define TRANSPARENCY_PEN 1
#define TRANSPARENCY_COLOR 2
#define TRANSPARENCY_THROUGH 3

//GLOBAL Raster Variables
extern struct GfxElement* gfx[MAX_GFX_ELEMENTS];

//extern UINT8* raster_pal;
//extern UINT8* colortable;

extern unsigned char* videoram;
extern int videoram_size;
extern unsigned char* colorram;
extern unsigned char* spriteram;
extern int spriteram_size;
extern unsigned char* spriteram_2;
extern int spriteram_2_size;
extern unsigned char* spriteram_3;
extern int spriteram_3_size;
extern unsigned char* flip_screen;
extern unsigned char* flip_screen_x;
extern unsigned char* flip_screen_y;
extern unsigned char* dirtybuffer;

extern struct osd_bitmap* main_bitmap;

extern struct osd_bitmap* tmpbitmap;
extern struct osd_bitmap* tmpbitmap1;



/*
struct rectangle
{
	int min_x,max_x;
	int min_y,max_y;
};

struct osd_bitmap
{
	int width,height;	// width and hegiht of the bitmap
	void *privatebm;	// don't touch! - reserved for osdepend use
	unsigned char *line[0];// pointers to the start of each line
};
*/

struct GfxLayout
{
	int width, height;	/* width and height of chars/sprites */
	int total;	/* total numer of chars/sprites in the rom */
	int planes;	/* number of bitplanes */
	int planeoffset[8];	/* start of every bitplane */
	int xoffset[64];	/* coordinates of the bit corresponding to the pixel */
	int yoffset[64];	/* of the given coordinates */
	int charincrement;	/* distance between two consecutive characters/sprites */
};

struct GfxElement
{
	int width, height;

	struct osd_bitmap* gfxdata;	/* graphic data */
	int total_elements;	/* total number of characters/sprites */

	int color_granularity;	/* number of colors for each color code */
							/* (for example, 4 for 2 bitplanes gfx) */
	unsigned char* colortable;	/* map color codes to screen pens */
								/* if this is 0, the function does a verbatim copy */
	int total_colors;
	int total_colors_this;
	int pal_start_this;
};

struct GfxDecodeInfo
{
	int memory_region;	/* memory region where the data resides (usually 1) */
						/* -1 marks the end of the array */
	int start;	/* beginning of data to decode */
	struct GfxLayout* gfxlayout;
	int color_codes_start;	/* offset in the color lookup table where color codes start */
	int total_color_codes;	/* total number of color codes */
};

//Generic.c raster video functions below
int generic_vh_start(void);
void generic_vh_stop(void);
int videoram_r(int offset);
void videoram_w(int offset, int data);
int colorram_r(int offset);
void colorram_w(int offset, int data);

//Bitmap functions
struct osd_bitmap* osd_create_bitmap(int width, int height);
void osd_free_bitmap(struct osd_bitmap* bitmap);
void osd_clearbitmap(struct osd_bitmap* bitmap);
void fillbitmap(struct osd_bitmap* dest, int pen, const struct rectangle* clip);
void copybitmap(struct osd_bitmap* dest, struct osd_bitmap* src, int flipx, int flipy, int sx, int sy,
	const struct rectangle* clip, int transparency, int transparent_color);
void copyscrollbitmap(struct osd_bitmap* dest, struct osd_bitmap* src,
	int rows, const int* rowscroll, int cols, const int* colscroll,
	const struct rectangle* clip, int transparency, int transparent_color);

//Graphics functions
void freegfx(struct GfxElement* gfx);
void drawgfx(struct osd_bitmap* dest, const struct GfxElement* gfx,
	unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
	const struct rectangle* clip, int transparency, int transparent_color);
struct GfxElement* decodegfx(const unsigned char* src, const struct GfxLayout* gl);
void decodechar(struct GfxElement* gfx, int num, const unsigned char* src, const struct GfxLayout* gl);

//Pixel Functions
void plot_pixel(struct osd_bitmap* b, int x, int y, int p);
int read_pixel(struct osd_bitmap* b, int x, int y);

#endif