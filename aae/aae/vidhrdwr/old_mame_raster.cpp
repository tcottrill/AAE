//============================================================================
// AAE raster module (speed-first modernized drop-in)
// Derived from early MAME code. See project headers for attribution.
//============================================================================

#include "old_mame_raster.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER)
#include <malloc.h> // _aligned_malloc/_aligned_free
#endif

//------------------------------------------------------------------------------
// Public globals (compatibility; keep names/layout)
//------------------------------------------------------------------------------
unsigned char* videoram;
int   videoram_size;
unsigned char* colorram;
unsigned char* spriteram;
int   spriteram_size;
unsigned char* spriteram_2;
int   spriteram_2_size;
unsigned char* spriteram_3;
int   spriteram_3_size;
unsigned char* flip_screen;
unsigned char* flip_screen_x;
unsigned char* flip_screen_y;
unsigned char* dirtybuffer;

// Bitmaps
struct osd_bitmap* tmpbitmap;
struct osd_bitmap* tmpbitmap1;
struct osd_bitmap* main_bitmap;

// Gfx sets
struct GfxElement* gfx[MAX_GFX_ELEMENTS];

//------------------------------------------------------------------------------
// Internal helpers
//------------------------------------------------------------------------------
namespace
{
    // Safety border for unclipped routines (unchanged value)
    constexpr int kSafety = 16;

    // Round up to multiple of 8 for better row strides (quadword friendly)
    inline int round_up_8(int v) { return (v + 7) & ~7; }

    // Allocate aligned memory (64B) for cache/SIMD friendliness
    static inline unsigned char* aae_aligned_alloc(size_t bytes)
    {
#if defined(_MSC_VER)
        return reinterpret_cast<unsigned char*>(_aligned_malloc(bytes, 64));
#else
        void* p = nullptr;
        if (posix_memalign(&p, 64, bytes) != 0) return nullptr;
        return reinterpret_cast<unsigned char*>(p);
#endif
    }

    static inline void aae_aligned_free(void* p)
    {
#if defined(_MSC_VER)
        _aligned_free(p);
#else
        free(p);
#endif
    }

    // Clip rectangle according to Machine orientation (kept behavior)
    inline void apply_orientation_clip(const struct osd_bitmap* dest, const struct rectangle*& clipRef,
        rectangle& tmp)
    {
        const int orient = Machine ? Machine->orientation : 0;

        if (orient & ORIENTATION_SWAP_XY)
        {
            if (clipRef)
            {
                tmp.min_x = clipRef->min_y;
                tmp.max_x = clipRef->max_y;
                tmp.min_y = clipRef->min_x;
                tmp.max_y = clipRef->max_x;
                clipRef = &tmp;
            }
        }
        if (orient & ORIENTATION_FLIP_X)
        {
            if (clipRef)
            {
                rectangle t{};
                t.min_x = dest->width - 1 - clipRef->max_x;
                t.max_x = dest->width - 1 - clipRef->min_x;
                t.min_y = clipRef->min_y;
                t.max_y = clipRef->max_y;
                tmp = t;
                clipRef = &tmp;
            }
        }
        if (orient & ORIENTATION_FLIP_Y)
        {
            if (clipRef)
            {
                rectangle t{};
                t.min_x = clipRef->min_x;
                t.max_x = clipRef->max_x;
                t.min_y = dest->height - 1 - clipRef->max_y;
                t.max_y = dest->height - 1 - clipRef->min_y;
                tmp = t;
                clipRef = &tmp;
            }
        }
    }

    // Tight remap copy: bm[x..x+n) = pal[sd[x..x+n))
    inline void copy_remap_u8(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, const unsigned char* AAE_RESTRICT pal)
    {
        // Unroll by 8 for typical tile widths
        int i = 0;
        for (; i + 8 <= n; i += 8)
        {
            bm[0] = pal[sd[0]];
            bm[1] = pal[sd[1]];
            bm[2] = pal[sd[2]];
            bm[3] = pal[sd[3]];
            bm[4] = pal[sd[4]];
            bm[5] = pal[sd[5]];
            bm[6] = pal[sd[6]];
            bm[7] = pal[sd[7]];
            bm += 8; sd += 8;
        }
        for (; i < n; ++i) { *bm++ = pal[*sd++]; }
    }

    // Tight selective copy with transparent PEN match on source index
    inline void copy_remap_trans_pen(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, const unsigned char* AAE_RESTRICT pal,
        unsigned char transparent_color)
    {
        // Process in 4s where possible; keep branch light
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            unsigned char s0 = sd[0], s1 = sd[1], s2 = sd[2], s3 = sd[3];
            if (s0 != transparent_color) bm[0] = pal[s0];
            if (s1 != transparent_color) bm[1] = pal[s1];
            if (s2 != transparent_color) bm[2] = pal[s2];
            if (s3 != transparent_color) bm[3] = pal[s3];
            bm += 4; sd += 4;
        }
        for (; i < n; ++i)
        {
            unsigned char s = *sd++;
            if (s != transparent_color) *bm = pal[s];
            ++bm;
        }
    }

    // Tight selective copy with transparent COLOR match on dest
    inline void copy_remap_trans_color(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, const unsigned char* AAE_RESTRICT pal,
        unsigned char transparent_color)
    {
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            unsigned char p0 = pal[sd[0]], p1 = pal[sd[1]];
            unsigned char p2 = pal[sd[2]], p3 = pal[sd[3]];
            if (p0 != transparent_color) bm[0] = p0;
            if (p1 != transparent_color) bm[1] = p1;
            if (p2 != transparent_color) bm[2] = p2;
            if (p3 != transparent_color) bm[3] = p3;
            bm += 4; sd += 4;
        }
        for (; i < n; ++i)
        {
            unsigned char p = pal[*sd++];
            if (p != transparent_color) *bm = p;
            ++bm;
        }
    }

    // "Through" blending: copy only where dest == key
    inline void copy_remap_through(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, const unsigned char* AAE_RESTRICT pal,
        unsigned char key)
    {
        for (int i = 0; i < n; ++i)
        {
            if (bm[i] == key) bm[i] = pal[sd[i]];
        }
    }

    // Non-remap variants (verbatim)
    inline void copy_verbatim(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd, int n)
    {
        // memcpy is optimal here
        std::memcpy(bm, sd, static_cast<size_t>(n));
    }

    inline void copy_verbatim_trans_pen(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, unsigned char transparent_color)
    {
        for (int i = 0; i < n; ++i)
        {
            unsigned char s = sd[i];
            if (s != transparent_color) bm[i] = s;
        }
    }

    inline void copy_verbatim_through(unsigned char* AAE_RESTRICT bm,
        const unsigned char* AAE_RESTRICT sd,
        int n, unsigned char key)
    {
        for (int i = 0; i < n; ++i)
        {
            if (bm[i] == key) bm[i] = sd[i];
        }
    }
} // anonymous namespace

//------------------------------------------------------------------------------
// Start/Stop
//------------------------------------------------------------------------------
int generic_vh_start(void)
{
    if (videoram_size == 0)
    {
        LOG_INFO("Error: generic_vh_start() called but videoram_size not initialized\n");
        return 1;
    }

    // Dirty flags (1 = needs redraw)
    if ((dirtybuffer = (unsigned char*)std::malloc((size_t)videoram_size)) == nullptr)
        return 1;
    std::memset(dirtybuffer, 1, (size_t)videoram_size);

    // Primary scratch/back buffer
    tmpbitmap = osd_create_bitmap(Machine->gamedrv->screen_width, Machine->gamedrv->screen_height);
    if (!tmpbitmap)
    {
        LOG_INFO("ERROR: tmpbitmap create failed");
        std::free(dirtybuffer);
        dirtybuffer = nullptr;
        return 1;
    }

    LOG_INFO("INIT: Raster Video Init Completed");
    return 0;
}

void generic_vh_stop(void)
{
    if (dirtybuffer) { std::free(dirtybuffer); dirtybuffer = nullptr; }
    osd_free_bitmap(tmpbitmap);  tmpbitmap = nullptr;
    // tmpbitmap1/main_bitmap (if used) are managed by their owners
}

//------------------------------------------------------------------------------
// VRAM access (dirty tracking re-enabled for incremental redraws)
//------------------------------------------------------------------------------
int videoram_r(int offset) { return videoram[offset]; }
int colorram_r(int offset) { return colorram[offset]; }

void videoram_w(int offset, int data)
{
    if (videoram[offset] != (unsigned char)data)
    {
        videoram[offset] = (unsigned char)data;
        if (dirtybuffer) dirtybuffer[offset] = 1;
    }
}

void colorram_w(int offset, int data)
{
    if (colorram[offset] != (unsigned char)data)
    {
        colorram[offset] = (unsigned char)data;
        if (dirtybuffer) dirtybuffer[offset] = 1;
    }
}

//------------------------------------------------------------------------------
// Bitmap allocation (aligned, leak-free, fast)
//------------------------------------------------------------------------------
void osd_clearbitmap(struct osd_bitmap* bitmap)
{
    for (int y = 0; y < bitmap->height; ++y)
        std::memset(bitmap->line[y], 0, (size_t)bitmap->width);
}

struct osd_bitmap* osd_create_bitmap(int width, int height)
{
    struct osd_bitmap* bitmap = (struct osd_bitmap*)std::malloc(sizeof(struct osd_bitmap));
    if (!bitmap) return nullptr;

    bitmap->width = width;
    bitmap->height = height;
    bitmap->depth = 8;

    // Row length with left/right safety
    const int rounded_w = round_up_8(width);
    const int rowlen = rounded_w + 2 * kSafety;

    // Allocate pixel plane (with top/bottom safety rows)
    const size_t bytes = (size_t)(height + 2 * kSafety) * (size_t)rowlen;
    unsigned char* bm = aae_aligned_alloc(bytes);
    if (!bm)
    {
        std::free(bitmap);
        return nullptr;
    }
    std::memset(bm, 0, bytes);

    // Allocate row pointer table (with safety rows)
    const int rows_with_safety = height + 2 * kSafety;
    unsigned char** line_base =
        (unsigned char**)aae_aligned_alloc((size_t)rows_with_safety * sizeof(unsigned char*));
    if (!line_base)
    {
        aae_aligned_free(bm);
        std::free(bitmap);
        return nullptr;
    }

    // Build row pointers; leave kSafety rows above and below
    for (int i = 0; i < rows_with_safety; ++i)
        line_base[i] = &bm[i * rowlen + kSafety];

    // Expose only the visible rows through 'line' (skip top safety)
    bitmap->line = line_base + kSafety;
    bitmap->privatebm = bm;
    bitmap->line_base = line_base;

    osd_clearbitmap(bitmap);
    LOG_INFO("INIT: Screen bitmap created (%dx%d, rowlen=%d, safety=%d)", width, height, rowlen, kSafety);
    return bitmap;
}

void osd_free_bitmap(struct osd_bitmap* bitmap)
{
    if (!bitmap) return;

    // Undo the +kSafety offset to free the original pointer table
    if (bitmap->line_base) aae_aligned_free(bitmap->line_base);
    if (bitmap->privatebm) aae_aligned_free(bitmap->privatebm);
    std::free(bitmap);
}

//------------------------------------------------------------------------------
// Graphics decode
//------------------------------------------------------------------------------
static inline int readbit(const unsigned char* src, int bitnum)
{
    // Same semantics, tighter
    return (src[bitnum >> 3] << (bitnum & 7)) & 0x80 ? 1 : 0;
}

void decodechar(struct GfxElement* ge, int num, const unsigned char* src, const struct GfxLayout* gl)
{
    for (int plane = 0; plane < gl->planes; ++plane)
    {
        const int offs = num * gl->charincrement + gl->planeoffset[plane];
        for (int y = 0; y < gl->height; ++y)
        {
            unsigned char* dp = ge->gfxdata->line[num * gl->height + y];
            // First plane clears, subsequent planes shift then add
            if (plane == 0)
            {
                for (int x = 0; x < gl->width; ++x)
                    dp[x] = (unsigned char)readbit(src, offs + gl->yoffset[y] + gl->xoffset[x]);
            }
            else
            {
                for (int x = 0; x < gl->width; ++x)
                    dp[x] = (unsigned char)((dp[x] << 1) + readbit(src, offs + gl->yoffset[y] + gl->xoffset[x]));
            }
        }
    }
}

struct GfxElement* decodegfx(const unsigned char* src, const struct GfxLayout* gl)
{
    LOG_INFO("decodegfx, creating bitmap W:%d T:%d", gl->width, gl->total * gl->height);
    struct osd_bitmap* bm = osd_create_bitmap(gl->width, gl->total * gl->height);
    if (!bm) { LOG_INFO("Error creating gfx bitmap"); return nullptr; }
    else { LOG_INFO("gfx bitmap created, continuing"); }
    struct GfxElement* ge = (struct GfxElement*)std::malloc(sizeof(struct GfxElement));
    if (!ge) { osd_free_bitmap(bm); LOG_INFO("Error allocating GfxElement"); return nullptr; }

    ge->width = gl->width;
    ge->height = gl->height;
    ge->total_elements = gl->total;
    ge->color_granularity = 1 << gl->planes;
    ge->gfxdata = bm;
    ge->colortable = nullptr;
    ge->total_colors = 0;
    ge->total_colors_this = 0;
    ge->pal_start_this = 0;

    for (int c = 0; c < gl->total; ++c) decodechar(ge, c, src, gl);
    return ge;
}

void freegfx(struct GfxElement* ge)
{
    if (!ge) return;
    osd_free_bitmap(ge->gfxdata);
    std::free(ge);
}

//------------------------------------------------------------------------------
// drawgfx (hot path) — vectorized-and-unrolled inner loops, same behavior
//------------------------------------------------------------------------------
void drawgfx(struct osd_bitmap* dest, const struct GfxElement* ge,
    unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
    const struct rectangle* clip, int transparency, int transparent_color)
{
    if (!ge) return;

    // Compute visible bounds
    int ox = sx, oy = sy;
    int ex = sx + ge->width - 1;
    int ey = sy + ge->height - 1;

    if (sx < 0) sx = 0;
    if (ex >= dest->width)  ex = dest->width - 1;
    if (sy < 0) sy = 0;
    if (ey >= dest->height) ey = dest->height - 1;

    if (clip)
    {
        if (sx < clip->min_x) sx = clip->min_x;
        if (ex > clip->max_x) ex = clip->max_x;
        if (sy < clip->min_y) sy = clip->min_y;
        if (ey > clip->max_y) ey = clip->max_y;
    }
    if (sx > ex || sy > ey) return;

    // Source start row
    const int start = (int)((code % (unsigned)ge->total_elements) * ge->height);

    // Palette remap (if provided)
    const unsigned char* paldata = nullptr;
    if (ge->colortable)
    {
        // original behavior: color-code group by granularity (mod clamp)
        const int idx = ge->color_granularity * (int)(color % (unsigned)ge->total_colors);
        paldata = &ge->colortable[idx];
    }

    const int span = ex - sx + 1;

    // Precompute X deltas for source addressing
    const int dx_left = sx - ox;
    const int dx_right = ge->width - 1 - (sx - ox);

    for (int y = sy; y <= ey; ++y)
    {
        unsigned char* bm = dest->line[y] + sx;
        const unsigned char* sd;

        if (flipy)
            sd = ge->gfxdata->line[start + ge->height - 1 - (y - oy)];
        else
            sd = ge->gfxdata->line[start + (y - oy)];

        // Horizontal flip picks direction
        if (flipx) sd += dx_right;
        else       sd += dx_left;

        if (!paldata)
        {
            // Verbatim copy
            if (transparency == TRANSPARENCY_NONE)
            {
                if (!flipx)
                    copy_verbatim(bm, sd, span);
                else
                {
                    // reverse walk
                    for (int x = 0; x < span; ++x) bm[x] = sd[-x];
                }
            }
            else if (transparency == TRANSPARENCY_PEN)
            {
                if (!flipx)
                    copy_verbatim_trans_pen(bm, sd, span, (unsigned char)transparent_color);
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        unsigned char s = sd[-x];
                        if (s != (unsigned char)transparent_color) bm[x] = s;
                    }
                }
            }
            else if (transparency == TRANSPARENCY_COLOR)
            {
                // Apply as if paldata equals identity; this path is rarely used without colortable,
                // but we retain the exact behavior for compatibility.
                if (!flipx)
                {
                    for (int x = 0; x < span; ++x)
                    {
                        unsigned char s = sd[x];
                        if (s != (unsigned char)transparent_color) bm[x] = s;
                    }
                }
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        unsigned char s = sd[-x];
                        if (s != (unsigned char)transparent_color) bm[x] = s;
                    }
                }
            }
            else // TRANSPARENCY_THROUGH
            {
                if (!flipx)
                    copy_verbatim_through(bm, sd, span, (unsigned char)transparent_color);
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        if (bm[x] == (unsigned char)transparent_color) bm[x] = sd[-x];
                    }
                }
            }
        }
        else
        {
            // Palette remap copy
            if (transparency == TRANSPARENCY_NONE)
            {
                if (!flipx)
                    copy_remap_u8(bm, sd, span, paldata);
                else
                {
                    for (int x = 0; x < span; ++x) bm[x] = paldata[sd[-x]];
                }
            }
            else if (transparency == TRANSPARENCY_PEN)
            {
                if (!flipx)
                    copy_remap_trans_pen(bm, sd, span, paldata, (unsigned char)transparent_color);
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        unsigned char s = sd[-x];
                        if (s != (unsigned char)transparent_color) bm[x] = paldata[s];
                    }
                }
            }
            else if (transparency == TRANSPARENCY_COLOR)
            {
                if (!flipx)
                    copy_remap_trans_color(bm, sd, span, paldata, (unsigned char)transparent_color);
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        unsigned char p = paldata[sd[-x]];
                        if (p != (unsigned char)transparent_color) bm[x] = p;
                    }
                }
            }
            else // TRANSPARENCY_THROUGH
            {
                if (!flipx)
                    copy_remap_through(bm, sd, span, paldata, (unsigned char)transparent_color);
                else
                {
                    for (int x = 0; x < span; ++x)
                    {
                        if (bm[x] == (unsigned char)transparent_color)
                            bm[x] = paldata[sd[-x]];
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// copybitmap: minimal wrapper around drawgfx 
//------------------------------------------------------------------------------
void copybitmap(struct osd_bitmap* dest, struct osd_bitmap* src, int flipx, int flipy, int sx, int sy,
    const struct rectangle* clip, int transparency, int transparent_color)
{
    GfxElement mygfx{};                 // zero-initialize all fields
    mygfx.width = src->width;
    mygfx.height = src->height;
    mygfx.total_elements = 1;
    mygfx.color_granularity = 1;
    mygfx.gfxdata = src;
    mygfx.colortable = nullptr;
    mygfx.total_colors = 0;
    mygfx.total_colors_this = 0;
    mygfx.pal_start_this = 0;

    drawgfx(dest, &mygfx, 0, 0, flipx, flipy, sx, sy, clip, transparency, transparent_color);
}

//------------------------------------------------------------------------------
// copyscrollbitmap (logic preserved; tightened slightly)
//------------------------------------------------------------------------------
void copyscrollbitmap(struct osd_bitmap* dest, struct osd_bitmap* src,
    int rows, const int* rowscroll, int cols, const int* colscroll,
    const struct rectangle* clip, int transparency, int transparent_color)
{
    int srcwidth = (Machine && (Machine->orientation & ORIENTATION_SWAP_XY)) ? src->height : src->width;
    int srcheight = (Machine && (Machine->orientation & ORIENTATION_SWAP_XY)) ? src->width : src->height;

    if (rows == 0)
    {
        // Scrolling columns
        const int colwidth = srcwidth / cols;
        rectangle myclip{ 0, 0, 0, 0 };
        myclip.min_y = clip->min_y;
        myclip.max_y = clip->max_y;

        for (int col = 0; col < cols; )
        {
            int scroll = colscroll[col], cons = 1;
            while (col + cons < cols && colscroll[col + cons] == scroll) ++cons;
            if (scroll < 0) scroll = srcheight - (-scroll % srcheight);
            else            scroll = scroll % srcheight;

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
        // Scrolling rows
        const int rowheight = srcheight / rows;
        rectangle myclip{ 0, 0, 0, 0 };
        myclip.min_x = clip->min_x;
        myclip.max_x = clip->max_x;

        for (int row = 0; row < rows; )
        {
            int scroll = rowscroll[row], cons = 1;
            while (row + cons < rows && rowscroll[row + cons] == scroll) ++cons;
            if (scroll < 0) scroll = srcwidth - (-scroll % srcwidth);
            else            scroll = scroll % srcwidth;

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
        // XY scrolling playfield
        int scrollx = (rowscroll[0] < 0) ? (srcwidth - ((-rowscroll[0]) % srcwidth)) : (rowscroll[0] % srcwidth);
        int scrolly = (colscroll[0] < 0) ? (srcheight - ((-colscroll[0]) % srcheight)) : (colscroll[0] % srcheight);

        copybitmap(dest, src, 0, 0, scrollx, scrolly, clip, transparency, transparent_color);
        copybitmap(dest, src, 0, 0, scrollx, scrolly - srcheight, clip, transparency, transparent_color);
        copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly, clip, transparency, transparent_color);
        copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly - srcheight, clip, transparency, transparent_color);
    }
    else if (rows == 1)
    {
        // Scrolling columns + horizontal scroll
        const int colwidth = srcwidth / cols;
        int scrollx = (rowscroll[0] < 0) ? (srcwidth - ((-rowscroll[0]) % srcwidth)) : (rowscroll[0] % srcwidth);

        rectangle myclip{ 0, 0, 0, 0 };
        myclip.min_y = clip->min_y;
        myclip.max_y = clip->max_y;

        for (int col = 0; col < cols; )
        {
            int scroll = colscroll[col], cons = 1;
            while (col + cons < cols && colscroll[col + cons] == scroll) ++cons;
            if (scroll < 0) scroll = srcheight - ((-scroll) % srcheight);
            else            scroll = scroll % srcheight;

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
        // Scrolling rows + vertical scroll
        const int rowheight = srcheight / rows;
        int scrolly = (colscroll[0] < 0) ? (srcheight - ((-colscroll[0]) % srcheight)) : (colscroll[0] % srcheight);

        rectangle myclip{ 0, 0, 0, 0 };
        myclip.min_x = clip->min_x;
        myclip.max_x = clip->max_x;

        for (int row = 0; row < rows; )
        {
            int scroll = rowscroll[row], cons = 1;
            while (row + cons < rows && rowscroll[row + cons] == scroll) ++cons;
            if (scroll < 0) scroll = srcwidth - ((-scroll) % srcwidth);
            else            scroll = scroll % srcwidth;

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

//------------------------------------------------------------------------------
// fillbitmap (behavior preserved; orientation handled via helper)
//------------------------------------------------------------------------------
void fillbitmap(struct osd_bitmap* dest, int pen, const struct rectangle* clip_in)
{
    rectangle tmpClip{};
    const rectangle* clip = clip_in;
    apply_orientation_clip(dest, clip, tmpClip);

    int sx = 0, sy = 0, ex = dest->width - 1, ey = dest->height - 1;
    if (clip)
    {
        if (sx < clip->min_x) sx = clip->min_x;
        if (ex > clip->max_x) ex = clip->max_x;
        if (sy < clip->min_y) sy = clip->min_y;
        if (ey > clip->max_y) ey = clip->max_y;
    }
    if (sx > ex || sy > ey) return;

    const int span = ex - sx + 1;
    for (int y = sy; y <= ey; ++y)
        std::memset(&dest->line[y][sx], pen, (size_t)span);
}
