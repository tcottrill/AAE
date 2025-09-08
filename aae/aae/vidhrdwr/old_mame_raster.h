#ifndef RASTER_H
#define RASTER_H

#pragma warning(disable:4996 4102)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sys_log.h"
#include "aae_mame_driver.h"  // uses RunningMachine, orientation flags, MAX_GFX_ELEMENTS, etc.

// Public constants (unchanged)
#define MAX_PENS 256
#define TRANSPARENCY_NONE    0
#define TRANSPARENCY_PEN     1
#define TRANSPARENCY_COLOR   2
#define TRANSPARENCY_THROUGH 3

// MSVC restrict hint for hot loops
#if defined(_MSC_VER)
#define AAE_RESTRICT __restrict
#else
#define AAE_RESTRICT
#endif

//------------------------------------------------------------------------------
// Public globals (kept for compatibility; moved impl details into .cpp)
//------------------------------------------------------------------------------
extern struct GfxElement* gfx[10];

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

//------------------------------------------------------------------------------
// Data structures (compatible shapes; documented and stabilized)
//------------------------------------------------------------------------------
/*
struct rectangle
{
    int min_x, max_x;
    int min_y, max_y;
};
*/

// Keep fields expected by other units and add minimal, safe extras.
struct osd_bitmap
{
    int width;                      // pixels
    int height;                     // pixels
    int depth;                      // bits per pixel (we use 8)
    unsigned char** line;           // row pointers, indexed by y; each points to start of that scanline
    void* privatebm;                // base pixel allocation (with safety margins)
    void* line_base;                // base allocation for the 'line' pointer table (needed to free)
};

// Gfx layout/element info 
struct GfxLayout
{
    int width, height;              // char/sprite dimensions
    int total;                      // number of chars/sprites
    int planes;                     // number of bitplanes
    int planeoffset[8];
    int xoffset[64];
    int yoffset[64];
    int charincrement;              // byte distance between consecutive chars/sprites
};

struct GfxElement
{
    int width, height;
    struct osd_bitmap* gfxdata;     // decoded pixel data
    int total_elements;
    int color_granularity;          // colors per code (e.g., 4 for 2 bitplanes)
    unsigned char* colortable;      // color-code -> pen map (0 for verbatim)
    int total_colors;
    int total_colors_this;
    int pal_start_this;
};

struct GfxDecodeInfo
{
    int memory_region;              // source region (unused here)
    int start;                      // offset in source
    struct GfxLayout* gfxlayout;
    int color_codes_start;          // offset in color lookup table
    int total_color_codes;
};

//------------------------------------------------------------------------------
// Generic tile RAM video helpers (unchanged API)
//------------------------------------------------------------------------------
int  generic_vh_start(void);
void generic_vh_stop(void);
int  videoram_r(int offset);
void videoram_w(int offset, int data);
int  colorram_r(int offset);
void colorram_w(int offset, int data);

//------------------------------------------------------------------------------
// Bitmap API (unchanged names; implementations optimized)
//------------------------------------------------------------------------------
struct osd_bitmap* osd_create_bitmap(int width, int height);
void osd_free_bitmap(struct osd_bitmap* bitmap);
void osd_clearbitmap(struct osd_bitmap* bitmap);
void fillbitmap(struct osd_bitmap* dest, int pen, const struct rectangle* clip);
void copybitmap(struct osd_bitmap* dest, struct osd_bitmap* src, int flipx, int flipy, int sx, int sy,
    const struct rectangle* clip, int transparency, int transparent_color);
void copyscrollbitmap(struct osd_bitmap* dest, struct osd_bitmap* src,
    int rows, const int* rowscroll, int cols, const int* colscroll,
    const struct rectangle* clip, int transparency, int transparent_color);

//------------------------------------------------------------------------------
// Graphics decode/draw (unchanged names; faster implementations)
//------------------------------------------------------------------------------
void freegfx(struct GfxElement* gfx);
void drawgfx(struct osd_bitmap* dest, const struct GfxElement* gfx,
    unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
    const struct rectangle* clip, int transparency, int transparent_color);
struct GfxElement* decodegfx(const unsigned char* src, const struct GfxLayout* gl);
void decodechar(struct GfxElement* gfx, int num, const unsigned char* src, const struct GfxLayout* gl);

//------------------------------------------------------------------------------
// Pixel helpers (fixed correctness; inline for speed)
//------------------------------------------------------------------------------
inline void plot_pixel(struct osd_bitmap* b, int x, int y, int p)
{
    // Correct address order: line[y][x]
    b->line[y][x] = static_cast<unsigned char>(p);
}

inline int read_pixel(const struct osd_bitmap* b, int x, int y)
{
    return b->line[y][x];
}

#endif // RASTER_H
