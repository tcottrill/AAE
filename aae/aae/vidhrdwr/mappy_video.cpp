/***************************************************************************

  mappy_video.cpp  --  AAE video hardware for Mappy / Dig Dug 2 / Motos / Tower of Druaga

  The hardware is a 60x36 tile display (28x32 active tiles plus 2 top / 2
  bottom rows) running at 60 Hz.  Tiles are 8x8, 2bpp, drawn from a single
  ROM bank.  Sprites are 16x16, 4bpp.  A per-scanline horizontal scroll
  register covers the central 32 rows (rows 2-33); the top 2 and bottom 2
  rows do not scroll.

  Converted from MAME 0.33 vidhrdw/mappy.c (Aaron Giles / Mirko Buffoni / JROK)

 
***************************************************************************/

#include "mappy.h"
#include "old_mame_raster.h"

/* motos_special_display: when set, the sprite draw helper subtracts 1 from
   the sprite X position, matching the Motos hardware's slightly shifted
   sprite position relative to the tile grid. */
static int motos_special_display = 0;

/* transparency cache: one bit per (tile_index * 64 + color_set) combination.
   A set bit means that tile drawn with that color set is entirely transparent
   (all pixels map to the background color index 0) and can be skipped. */
static unsigned char* transparency = nullptr;


// ---------------------------------------------------------------------------
// vh_start / vh_stop
// ---------------------------------------------------------------------------

static int common_vh_start(void)
{
    if ((dirtybuffer = (unsigned char*)malloc(videoram_size)) == nullptr)
        return 1;
    memset(dirtybuffer, 1, videoram_size);

    /* 60 tiles wide, 36 tiles tall (pixels) */
    if ((tmpbitmap = osd_create_bitmap(60 * 8, 36 * 8)) == nullptr)
    {
        free(dirtybuffer);
        dirtybuffer = nullptr;
        return 1;
    }

    return 0;
}

int mappy_vh_start(void)
{
    motos_special_display = 0;
    return common_vh_start();
}

int motos_vh_start(void)
{
    motos_special_display = 1;
    return common_vh_start();
}

void mappy_vh_stop(void)
{
    if (transparency)
    {
        free(transparency);
        transparency = nullptr;
    }
    if (dirtybuffer)
    {
        free(dirtybuffer);
        dirtybuffer = nullptr;
    }
    if (tmpbitmap)
    {
        osd_free_bitmap(tmpbitmap);
        tmpbitmap = nullptr;
    }
}


// ---------------------------------------------------------------------------
// Color PROM conversion
// This must be called after mappy_vh_start so that the GFX elements are
// already decoded.  It also builds the transparency cache used by the
// screen refresh to skip fully-transparent tiles.
// ---------------------------------------------------------------------------

void mappy_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable,
                                 const unsigned char* color_prom)
{
    int i;

    /* 32-entry hardware palette: RGB from resistor-weighted PROM bits */
    for (i = 0; i < 32; i++)
    {
        int bit0, bit1, bit2;

        bit0 = (color_prom[31 - i] >> 0) & 0x01;
        bit1 = (color_prom[31 - i] >> 1) & 0x01;
        bit2 = (color_prom[31 - i] >> 2) & 0x01;
        palette[3 * i + 0] = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);

        bit0 = (color_prom[31 - i] >> 3) & 0x01;
        bit1 = (color_prom[31 - i] >> 4) & 0x01;
        bit2 = (color_prom[31 - i] >> 5) & 0x01;
        palette[3 * i + 1] = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);

        bit1 = (color_prom[31 - i] >> 6) & 0x01;
        bit2 = (color_prom[31 - i] >> 7) & 0x01;
        palette[3 * i + 2] = (unsigned char)(0x47 * bit1 + 0x97 * bit2);
    }

    /* character color lookup table: 64 sets * 4 colors each */
    for (i = 0; i < 64 * 4; i++)
        colortable[i] = (unsigned char)(31 - ((color_prom[(i ^ 3) + 32] & 0x0f) + 0x10));

    /* sprite color lookup table: rest of the color table */
    for (i = 64 * 4; i < (int)Machine->drv->color_table_len; i++)
        colortable[i] = (unsigned char)(31 - (color_prom[i + 32] & 0x0f));

    /* build transparency cache for each (tile, color_set) combination.
       A tile/color combination is transparent when every pixel maps to
       palette index 0 (the hardware background pen). */
    {
        struct GfxElement* gfx = Machine->gfx[0];
        int tile, y, x, base_color;
        unsigned char* dp;

        /* 64 color sets, up to 256 tiles for the char set */
        transparency = (unsigned char*)malloc(64 * gfx->total_elements);
        if (!transparency)
            return;
        memset(transparency, 0, 64 * gfx->total_elements);

        for (tile = 0; tile < gfx->total_elements; tile++)
        {
            /* get the pixel value of the first pixel of this tile */
            base_color = gfx->gfxdata->line[tile * gfx->height][0];

            /* check if all pixels have the same value */
            for (y = 0; y < gfx->height; y++)
            {
                dp = gfx->gfxdata->line[tile * gfx->height + y];
                for (x = 0; x < gfx->width; x++)
                    if (dp[x] != base_color)
                        goto done;
            }

            /* tile is solid -- mark transparent for any color set where
               that solid color maps to palette index 0 */
            for (y = 0; y < 64; y++)
                if (colortable[y * 4 + base_color] == 0)
                    transparency[(tile << 6) + y] = 1;

        done:
            ;
        }
    }
}


// ---------------------------------------------------------------------------
// Sprite draw helper
// Wraps drawgfx with the transparency color pen and the optional Motos
// 1-pixel X offset.
// ---------------------------------------------------------------------------

static void mappy_draw_sprite(struct osd_bitmap* dest,
                              unsigned int code, unsigned int color,
                              int flipx, int flipy, int sx, int sy)
{
    if (motos_special_display) sx--;

    drawgfx(dest, Machine->gfx[1], code, color,
            flipx, flipy, sx, sy,
            &Machine->drv->visible_area,
            TRANSPARENCY_COLOR, 16);
}


// ---------------------------------------------------------------------------
// Screen refresh
// ---------------------------------------------------------------------------

void mappy_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh)
{
    /* save list of high-priority tile offsets to redraw after sprites */
    static unsigned short overoffset[2048];
    unsigned short* save = overoffset;
    int offs;

    /* --- tile layer pass --- */
    for (offs = videoram_size - 1; offs >= 0; offs--)
    {
        int color = colorram[offs];
        int video = videoram[offs];

        /* tiles with bit 0x40 in colorram are drawn above sprites;
           remember them (unless they are fully transparent) */
        if (color & 0x40)
            if (!transparency[(video << 6) + (color & 0x3f)])
                *save++ = (unsigned short)offs;

        if (dirtybuffer[offs])
        {
            int sx, sy, mx, my;

            dirtybuffer[offs] = 0;

            /* the tile map is stored in a non-linear layout:
               - top 2 rows:    last 64 bytes of videoram
               - bottom 2 rows: second-to-last 64 bytes
               - middle 32 rows: the rest, column-major */

            if (offs >= videoram_size - 64)
            {
                int off = offs;

                /* Motos swaps a few tile positions in the top rows */
                if (motos_special_display)
                {
                    if (off == 0x07d1 || off == 0x07d0 ||
                        off == 0x07f1 || off == 0x07f0)
                        off -= 0x10;
                    if (off == 0x07c1 || off == 0x07c0 ||
                        off == 0x07e1 || off == 0x07e0)
                        off += 0x10;
                }

                mx = off % 32;
                my = (off - (videoram_size - 64)) / 32;
                sx = 29 - mx;
                sy = my;
            }
            else if (offs >= videoram_size - 128)
            {
                mx = offs % 32;
                my = (offs - (videoram_size - 128)) / 32;
                sx = 29 - mx;
                sy = my + 34;
            }
            else
            {
                mx = offs / 32;
                my = offs % 32;
                sx = 59 - mx;
                sy = my + 2;
            }

            drawgfx(tmpbitmap, Machine->gfx[0],
                    video, color,
                    0, 0, 8 * sx, 8 * sy,
                    nullptr, TRANSPARENCY_NONE, 0);
        }
    }

    /* --- copy the tile layer to the screen with horizontal scroll --- */
    {
        int scroll[36];

        /* top 2 and bottom 2 rows are unscrolled */
        for (offs = 0;  offs < 2;  offs++) scroll[offs] = 0;
        for (offs = 2;  offs < 34; offs++) scroll[offs] = mappy_scroll - 256;
        for (offs = 34; offs < 36; offs++) scroll[offs] = 0;

        copyscrollbitmap(bitmap, tmpbitmap,
                         36, scroll,
                         0, 0,
                         &Machine->drv->visible_area,
                         TRANSPARENCY_NONE, 0);
    }

    /* --- sprite pass --- */
    for (offs = 0; offs < spriteram_size; offs += 2)
    {
        /* bit 1 of spriteram_3 byte 1 = inactive (sprite disabled) */
        if ((spriteram_3[offs + 1] & 2) == 0)
        {
            int sprite =  spriteram[offs];
            int color  =  spriteram[offs + 1];
            int x      =  spriteram_2[offs]     - 16;
            int y      = (spriteram_2[offs + 1] - 40)
                        + 0x100 * (spriteram_3[offs + 1] & 1);
            int flipx  =  spriteram_3[offs] & 2;
            int flipy  =  spriteram_3[offs] & 1;

            switch (spriteram_3[offs] & 0x0c)
            {
            case 0:     /* normal (16x16) */
                mappy_draw_sprite(bitmap, sprite, color, flipx, flipy, x, y);
                break;

            case 4:     /* double height (16x32) */
                sprite &= ~1;
                if (!flipy)
                {
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x, y);
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x, y + 16);
                }
                else
                {
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x, y + 16);
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x, y);
                }
                break;

            case 8:     /* double width (32x16) */
                sprite &= ~2;
                if (!flipx)
                {
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x,       y);
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x + 16,  y);
                }
                else
                {
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x,       y);
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x + 16,  y);
                }
                break;

            case 12:    /* double size (32x32) */
                sprite &= ~3;
                if (!flipy && !flipx)
                {
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x,      y);
                    mappy_draw_sprite(bitmap, sprite + 3, color, flipx, flipy, x,      y + 16);
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x + 16, y);
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x + 16, y + 16);
                }
                else if (flipy && flipx)
                {
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x,      y);
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x,      y + 16);
                    mappy_draw_sprite(bitmap, sprite + 3, color, flipx, flipy, x + 16, y);
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x + 16, y + 16);
                }
                else if (flipx)
                {
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x,      y);
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x,      y + 16);
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x + 16, y);
                    mappy_draw_sprite(bitmap, sprite + 3, color, flipx, flipy, x + 16, y + 16);
                }
                else /* flipy only */
                {
                    mappy_draw_sprite(bitmap, sprite + 3, color, flipx, flipy, x,      y);
                    mappy_draw_sprite(bitmap, sprite + 2, color, flipx, flipy, x,      y + 16);
                    mappy_draw_sprite(bitmap, sprite + 1, color, flipx, flipy, x + 16, y);
                    mappy_draw_sprite(bitmap, sprite,     color, flipx, flipy, x + 16, y + 16);
                }
                break;
            }
        }
    }

    /* --- high-priority tile pass (drawn over sprites) --- */
    while (save > overoffset)
    {
        int sx, sy, mx, my;

        offs = *--save;

        if (offs >= videoram_size - 64)
        {
            mx = offs % 32;
            my = (offs - (videoram_size - 64)) / 32;
            sx = (29 - mx) * 8;
            sy = my;
        }
        else if (offs >= videoram_size - 128)
        {
            mx = offs % 32;
            my = (offs - (videoram_size - 128)) / 32;
            sx = (29 - mx) * 8;
            sy = my + 34;
        }
        else
        {
            mx = offs / 32;
            my = offs % 32;
            sx = (8 * (59 - mx)) + mappy_scroll - 256;
            sy = my + 2;
        }

        drawgfx(bitmap, Machine->gfx[0],
                videoram[offs], colorram[offs],
                0, 0, sx, 8 * sy,
                nullptr, TRANSPARENCY_COLOR, 0);
    }
}
