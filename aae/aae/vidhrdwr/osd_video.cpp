// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------
//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================

// -----------------------------------------------------------------------------
// osd_video.cpp
// OSD video module for AAE (palette helpers, display bootstrap,
// and MAME-style vh_open/vh_close).
//
// -----------------------------------------------------------------------------

#include "framework.h"
#include "osd_video.h"
#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "sys_log.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

//OSD VIDEO THINGS
int game_width = 0;
int game_height = 0;
int game_attributes = 0;

int vector_game;
int use_dirty;

//New Palette functions 9/25/25

static unsigned char* g_game_palette = nullptr; // 3 * total_colors (authoritative RGB)
static unsigned char* g_new_palette = nullptr; // deferred RGB
static unsigned char* g_palette_dirty = nullptr; // 1 byte per color (0/1)
static unsigned char* g_just_remapped = nullptr; // 1 byte per color (0/1) return value
static int            g_total_colors = 0;


//int gfx_mode;
//int gfx_width;
//int gfx_height;

// -----------------------------------------------------------------------------
// Required globals (as requested)
// -----------------------------------------------------------------------------
unsigned char current_palette[640][3];

// From MAME .30 for VH Hardware (limits already declared in header)
static unsigned char remappedtable[MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES];

// -----------------------------------------------------------------------------
// Externals provided by the core  these symbols already exist in your project.
// We only reference them here.
// -----------------------------------------------------------------------------
extern RunningMachine* Machine;       // Global machine descriptor
extern osd_bitmap* main_bitmap;   // Primary display bitmap created by OSD

extern osd_bitmap* osd_create_bitmap(int width, int height);
extern void        osd_free_bitmap(osd_bitmap* bmp);
extern void        freegfx(struct GfxElement* gfx);
extern GfxElement* decodegfx(const unsigned char* src, const struct GfxLayout* gl);

extern int vector_game; // 0 = raster, 1 = vector (set based on video_attributes)

// -----------------------------------------------------------------------------
// Small local helpers
// -----------------------------------------------------------------------------
static inline int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }


// -----------------------------------------------------------------------------
// osd_modify_pen
// Update the public current_palette table, avoiding redundant writes.
// -----------------------------------------------------------------------------
void osd_modify_pen(int pen, unsigned char r, unsigned char g, unsigned char b)
{
	if (pen < 0 || pen >= (int)(sizeof(current_palette) / sizeof(current_palette[0])))
		return;

	unsigned char* p = current_palette[pen];
	if (p[0] != r || p[1] != g || p[2] != b)
	{
		p[0] = r;
		p[1] = g;
		p[2] = b;
	}
}

// -----------------------------------------------------------------------------
// osd_get_pen
// Read back the current_palette triplet for a pen index.
// -----------------------------------------------------------------------------
void osd_get_pen(int pen, unsigned char* r, unsigned char* g, unsigned char* b)
{
	if (pen >= 0 && pen < (int)(sizeof(current_palette) / sizeof(current_palette[0])))
	{
		if (r) *r = current_palette[pen][0];
		if (g) *g = current_palette[pen][1];
		if (b) *b = current_palette[pen][2];
	}
	else
	{
		if (r) *r = 0;
		if (g) *g = 0;
		if (b) *b = 0;
	}
}

// -----------------------------------------------------------------------------
// osd_allocate_colors
// This is an older MAME-style API that returns pens for the palette.
// Here we just fill pens directly since AAE uses identity pens.
// -----------------------------------------------------------------------------
int osd_allocate_colors(unsigned int totalcolors, const unsigned char* palette, unsigned char* pens, const unsigned short* colortable)
{
	if (!pens) return 1;

	// Identity mapping preferred by AAE
	for (unsigned int i = 0; i < totalcolors && i < MAX_PENS; i++)
		pens[i] = (unsigned char)i;

	// Optionally prime current_palette from passed-in palette
	if (palette)
	{
		for (unsigned int i = 0; i < totalcolors && i < MAX_PENS; i++)
		{
			current_palette[pens[i]][0] = palette[3 * i + 0];
			current_palette[pens[i]][1] = palette[3 * i + 1];
			current_palette[pens[i]][2] = palette[3 * i + 2];
		}
	}

	// Remap table if provided
	if (colortable && Machine && Machine->gamedrv)
	{
		for (unsigned int i = 0; i < Machine->gamedrv->color_table_len; i++)
			remappedtable[i] = pens[colortable[i]];
	}
	return 0;
}

/*
 Create a display screen, or window, large enough to accomodate a bitmap
 max(max_x - min_x + 1, width) by max(max_y - min_y + 1, height). Attributes
 are the ones defined in driver.h, they can be used to perform optimizations,
 e.g. dirty rectangle handling if the game supports it, or faster blitting routines
 for games that support it. palette is an array of 'totalcolors' R,G,B triplets. The function returns
 in //pens the pen values corresponding to the requested colors.
 Return a osd_bitmap pointer or 0 in case of error. */

struct osd_bitmap* osd_create_display(int width, int height, unsigned int totalcolors,
	const unsigned char* palette, unsigned char* pens, int attributes)
{
	LOG_INFO("New Display width %d, height %d", width, height);

	/* Look if this is a vector game */
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		LOG_INFO("Init: Vector game starting");
		vector_game = 1;
	}
	else
		vector_game = 0;

	/* Is the game telling us that it can support dirty? */
	if (Machine->drv->video_attributes & VIDEO_SUPPORTS_DIRTY)
	{
		use_dirty = 1;
		LOG_INFO("Video supports dirty");
	}
	else
	{
		use_dirty = 0;
		LOG_INFO("Video does not support dirty");
	}

	LOG_INFO("MIN Y:%d ", Machine->drv->visible_area.min_y);
	LOG_INFO("MIN X:%d ", Machine->drv->visible_area.min_x);
	LOG_INFO("MAX Y:%d ", Machine->drv->visible_area.max_y);
	LOG_INFO("MAX X:%d ", Machine->drv->visible_area.max_x);

	LOG_INFO("VH_OPEN_1");
	unsigned char* convtable = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);
	if (!convtable) return 0;

	int attributes2 = 0;

	/* Determine the window size */
	if (vector_game)
	{
		//use_double = 1;
		/* center display */
	}
	else /* center display based on visible area */
	{
		/* struct rectangle vis = Machine->drv->visible_area; */ /* unused */
	}

	game_width = width;
	game_height = height;
	game_attributes = attributes;

	//Create the main bitmap screen
	LOG_INFO("Creating Main Bitmap with X:%d Y:%d", width + 2, height + 2);
	main_bitmap = osd_create_bitmap(width + 2, height + 2);
	LOG_INFO("Main Bitmap Created");
	if (!main_bitmap)
	{
		LOG_INFO("Bitmap create failed, why?");
		return nullptr;
	}
	LOG_INFO("exiting create display");
	return main_bitmap;
}
// -----------------------------------------------------------------------------
// vh_close
// Free GFX resources. DO NOT free Machine->pens (array member).
// -----------------------------------------------------------------------------
void vh_close(void)
{
	for (int i = 0; i < MAX_GFX_ELEMENTS; i++)
		freegfx(Machine->gfx[i]);
	//free(Machine->pens);
	//osd_close_display();
	aae_palette_stop();
}

// -----------------------------------------------------------------------------
// vh_open
// decode GFX, build palette, create OSD.
// -----------------------------------------------------------------------------
int vh_open(void)
{
	int i;
	unsigned char* palette_heap = nullptr;
	unsigned char* palette = nullptr;
	unsigned char* colortable = nullptr;
	unsigned char convpalette[3 * MAX_PENS];
	unsigned char* convtable;
	bool using_convpalette = false;

	LOG_INFO("Running vh_open");

	LOG_INFO("MIN Y:%d ", Machine->drv->visible_area.min_y);
	LOG_INFO("MIN X:%d ", Machine->drv->visible_area.min_x);
	LOG_INFO("MAX Y:%d ", Machine->drv->visible_area.max_y);
	LOG_INFO("MAX X:%d ", Machine->drv->visible_area.max_x);

	LOG_INFO("VH_OPEN_1");
	convtable = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);
	if (!convtable) return 1;

	LOG_INFO("VH_OPEN_2");
	if (Machine->gamedrv->gfxdecodeinfo)
	{
		for (i = 0; i < MAX_GFX_ELEMENTS && Machine->gamedrv->gfxdecodeinfo[i].memory_region != -1; i++)
		{
			if ((Machine->gfx[i] = decodegfx(Machine->memory_region[Machine->gamedrv->gfxdecodeinfo[i].memory_region]
				+ Machine->gamedrv->gfxdecodeinfo[i].start,
				Machine->gamedrv->gfxdecodeinfo[i].gfxlayout)) == 0)
			{
				LOG_ERROR("Some Sort of Graphics Failure here in vh_open/gfxdecodeinfo");
				vh_close();
				free(convtable);
				return 1;
			}
			LOG_INFO("I here at gfx convert is %d, memregion is %d", i, Machine->gamedrv->gfxdecodeinfo[i].memory_region);
			Machine->gfx[i]->colortable = &remappedtable[Machine->gamedrv->gfxdecodeinfo[i].color_codes_start];


			Machine->gfx[i]->total_colors = Machine->gamedrv->gfxdecodeinfo[i].total_color_codes;

			LOG_INFO("Colortable here is remap table at is %d,total color codes is %d", Machine->gamedrv->gfxdecodeinfo[i].color_codes_start, Machine->gamedrv->gfxdecodeinfo[i].total_color_codes);
		}
	}
	LOG_INFO("VH_OPEN_3");
	//Create a default palette
	palette_heap = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);
	palette = palette_heap;

	for (int x = 0; x < (MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES); x++)
	{
		palette[x] = 1;
	}
	/* convert the palette */
	/* now the driver can modify the default values if it wants to. */
	if (Machine->drv->vh_convert_color_prom)
	{
		(*Machine->drv->vh_convert_color_prom)(convpalette, convtable, memory_region(REGION_PROMS));
		palette = convpalette;
		colortable = convtable;
		using_convpalette = true;
	}
	LOG_INFO("VH_OPEN_4");
	/* create the display bitmap, and allocate the palette */
	if ((Machine->scrbitmap = osd_create_display(Machine->gamedrv->screen_width, Machine->gamedrv->screen_height, Machine->gamedrv->total_colors, palette, Machine->pens, Machine->gamedrv->video_attributes)) == 0)
	{
		LOG_INFO("Error in Bitmap Display Creation, returning and AAE should end");
		free(convtable);
		if (!using_convpalette && palette_heap) { free(palette_heap); palette_heap = nullptr; }
		return 1;
	}
	else
	{
		LOG_INFO("Created Display surface, Width: %d Height: %d", Machine->gamedrv->screen_width, Machine->gamedrv->screen_height);
	}
	/* initialize the palette */
	for (i = 0; i < MAX_PENS; i++)
	{
		current_palette[i][0] = current_palette[i][1] = current_palette[i][2] = 0;
	}
	/* fill the palette starting from the end, so we mess up badly written */
	/* drivers which don't go through Machine->pens[]
	NOT DOING THIS, I need to be able to access the palette directly!!
	*/
	for (i = 0; i < MAX_PENS; i++) //totalcolors
	{
		Machine->pens[i] = i;// 255 - i;
	}
	LOG_INFO("TotalColors here is %d", Machine->gamedrv->total_colors);
	for (i = 0; i < (int)Machine->gamedrv->total_colors; i++)
	{
		current_palette[Machine->pens[i]][0] = palette[3 * i];
		current_palette[Machine->pens[i]][1] = palette[3 * i + 1];
		current_palette[Machine->pens[i]][2] = palette[3 * i + 2];
	}

	LOG_INFO("Color Table Len %d", Machine->gamedrv->color_table_len);
	if (colortable) {
		for (i = 0; i < (int)Machine->gamedrv->color_table_len; i++)
			remappedtable[i] = Machine->pens[colortable[i]];
	}
	else if (Machine->gamedrv->color_table_len) {
		LOG_INFO("No colortable provided by vh_convert_color_prom; skipping remap of %d entries", (int)Machine->gamedrv->color_table_len);
	}

	// free memory regions allocated with ROMREGION_DISPOSE (typically gfx roms)
	for (int region = 0; region < MAX_MEMORY_REGIONS; region++)
	{
		if (Machine->memory_region_type[region] == ROMREGION_DISPOSE)
		{
			LOG_INFO("FREEING MEMORY REGION %s", rom_regions[region]);
			// invalidate contents to avoid subtle bugs
			unsigned char* const mr = memory_region(region);
			const int mlen = Machine->memory_region_length[region];
			if (mr && mlen > 0)
			{
				memset(mr, 0xA5, (size_t)mlen);
			}
			free(Machine->memory_region[region]);
			Machine->memory_region[region] = nullptr;
		}
	}

	// after you’ve filled current_palette from the game’s palette:
	if (Machine->drv->video_attributes & VIDEO_MODIFIES_PALETTE)
	{
		// from palette
		if (aae_palette_start(Machine->gamedrv->total_colors) != 0) {
			LOG_ERROR("palette_start failed");
			return 1; // or suitable error path (placeholder, come back to this)
		}
	}

	free(convtable);
	convtable = nullptr;
	if (!using_convpalette && palette_heap) { free(palette_heap); palette_heap = nullptr; }
	LOG_INFO("Returning from vh_open");
	return 0;
}

// -----------------------------------------------------------------------------
// aae_palette_start
// Allocate and initialize the minimal 8-bit deferred palette data.
// -----------------------------------------------------------------------------
bool aae_palette_start(int total_colors)
{
	g_total_colors = total_colors;
	if (g_total_colors <= 0) return false;

	const size_t rgb_bytes = static_cast<size_t>(g_total_colors) * 3u;

	g_game_palette = (unsigned char*)malloc(rgb_bytes);
	g_new_palette = (unsigned char*)malloc(rgb_bytes);
	g_palette_dirty = (unsigned char*)malloc(g_total_colors);
	g_just_remapped = (unsigned char*)malloc(g_total_colors);

	if (!g_game_palette || !g_new_palette || !g_palette_dirty || !g_just_remapped)
		return false;

	// Initialize to current_palette[] so comparisons work on first write.
	for (int i = 0; i < g_total_colors; ++i)
	{
		g_game_palette[3 * i + 0] = current_palette[i][0];
		g_game_palette[3 * i + 1] = current_palette[i][1];
		g_game_palette[3 * i + 2] = current_palette[i][2];

		g_new_palette[3 * i + 0] = current_palette[i][0];
		g_new_palette[3 * i + 1] = current_palette[i][1];
		g_new_palette[3 * i + 2] = current_palette[i][2];
	}
	memset(g_palette_dirty, 0, g_total_colors);
	memset(g_just_remapped, 0, g_total_colors);
	return true;
}

// -----------------------------------------------------------------------------
// aae_palette_stop
// Free palette data.
// -----------------------------------------------------------------------------
void aae_palette_stop()
{
	free(g_game_palette);   g_game_palette = nullptr;
	free(g_new_palette);    g_new_palette = nullptr;
	free(g_palette_dirty);  g_palette_dirty = nullptr;
	free(g_just_remapped);  g_just_remapped = nullptr;
	g_total_colors = 0;
}

// -----------------------------------------------------------------------------
// palette_change_color
// Deferred 8-bit update (like MAME .36 palette_change_color_8()):
// - If RGB unchanged, clear dirty and return.
// - Otherwise stash into g_new_palette and mark dirty.
//   (Actual hardware/OS color is updated in palette_recalc().)
// -----------------------------------------------------------------------------

void palette_change_color(int color,
	unsigned char red,
	unsigned char green,
	unsigned char blue)
{
	if ((unsigned)color >= (unsigned)g_total_colors || !g_game_palette) return;

	const unsigned char* cur = &g_game_palette[3 * color];
	if (cur[0] == red && cur[1] == green && cur[2] == blue)
	{
		// Nothing to do this frame for this color
		if (g_palette_dirty) g_palette_dirty[color] = 0;
		return;
	}

	// Defer the change; apply in palette_recalc()
	g_new_palette[3 * color + 0] = red;
	g_new_palette[3 * color + 1] = green;
	g_new_palette[3 * color + 2] = blue;
	g_palette_dirty[color] = 1;
}

// -----------------------------------------------------------------------------
// palette_recalc
// Apply all deferred color changes this frame. For each dirty color:
// - Copy new -> game palette
// - Update current_palette[] (your public RGB cache)
// - Push to OSD via osd_modify_pen(color, r, g, b)
// Returns:
//   nullptr if nothing changed,
//   else a pointer to g_just_remapped[] (nonzero entries indicate which colors).
//
// This mirrors MAME .36’s “return non-null if any visible/cached colors were
// remapped”, which Millipede uses to trigger a full redraw when needed. 
// -----------------------------------------------------------------------------
const unsigned char* palette_recalc(void)
{
	if (!g_game_palette || !g_palette_dirty) return nullptr;

	bool any_changed = false;
	memset(g_just_remapped, 0, g_total_colors);

	for (int color = 0; color < g_total_colors; ++color)
	{
		if (!g_palette_dirty[color]) continue;

		const unsigned char r = g_new_palette[3 * color + 0];
		const unsigned char g = g_new_palette[3 * color + 1];
		const unsigned char b = g_new_palette[3 * color + 2];

		// Commit to authoritative arrays
		g_game_palette[3 * color + 0] = r;
		g_game_palette[3 * color + 1] = g;
		g_game_palette[3 * color + 2] = b;

		// Keep your public cache in sync
		current_palette[color][0] = r;
		current_palette[color][1] = g;
		current_palette[color][2] = b;

		// Identity pens: pen == color index
		osd_modify_pen(color, r, g, b);

		g_palette_dirty[color] = 0;
		g_just_remapped[color] = 1;
		any_changed = true;
	}

	return any_changed ? g_just_remapped : nullptr;
}
