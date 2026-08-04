//==========================================================================
// AAE - Another Arcade Emulator
// A MAME (TM) derivative based on early MAME code (0.29 through 0.90)
// mixed with original code. Created for amusement and archival purposes.
//
// All MAME code used in this emulator remains the copyright of the MAME
// Team. All MAME-derived code should be considered as belonging to them.
//
// Original AAE code copyright (C) 2025/2026 Tim Cottrill, released under
// the GNU GPL v3 or later. See accompanying source files for full details.
//==========================================================================
//
// raster_emit.cpp
//
// Backend-neutral raster pixel-emit loop: the body of raster_poly_update()
// (aae_video/opengl_renderer.cpp) behind a sink callback, so the GL and VK
// render chains share exactly one orientation/pen-lookup implementation.
//
//==========================================================================

#include "raster_emit.h"
#include "aae_mame_driver.h"   // Machine, RunningMachine, rectangle, ORIENTATION_*, config
#include "old_mame_raster.h"   // main_bitmap extern, osd_bitmap
#include "osdepend.h"          // osd_get_pen
#include "colordefs.h"         // MAKE_RGBA

// ---------------------------------------------------------------------------
// compute_raster_geom
// Shared geometry helper: reads Machine->drv->visible_area and rotation,
// and derives the source-visible-area bounds plus the post-orientation
// destination dimensions. Both raster_dst_dims() and raster_emit_polys()
// call this so there is exactly one place that computes dstW/dstH.
// Returns false if no machine/bitmap is available or the visible area is
// degenerate.
// ---------------------------------------------------------------------------
static bool compute_raster_geom(int& minX, int& maxX, int& minY, int& maxY,
	int& rot, int& dstW, int& dstH)
{
	if (!Machine || !Machine->drv || !main_bitmap)
		return false;

	const rectangle& va = Machine->drv->visible_area;
	minX = va.min_x;
	maxX = va.max_x;
	minY = va.min_y;
	maxY = va.max_y;

	const int srcW = (maxX - minX + 1);
	const int srcH = (maxY - minY + 1);

	if (srcW <= 0 || srcH <= 0)
		return false;

	rot = Machine->drv->rotation;

	// Destination extents after orientation.
	dstW = srcW;
	dstH = srcH;

	if (rot & ORIENTATION_SWAP_XY)
	{
		dstW = srcH;
		dstH = srcW;
	}

	return true;
}

// ---------------------------------------------------------------------------
// raster_dst_dims
// ---------------------------------------------------------------------------
int raster_dst_dims(int* outW, int* outH)
{
	int minX, maxX, minY, maxY, rot, dstW, dstH;
	if (!compute_raster_geom(minX, maxX, minY, maxY, rot, dstW, dstH))
		return 0;

	if (outW) *outW = dstW;
	if (outH) *outH = dstH;
	return 1;
}

// ---------------------------------------------------------------------------
// raster_emit_polys
// Reads the MAME bitmap (main_bitmap) for the current frame, converts each
// pixel to an RGBA color via osd_get_pen(), and submits it to the caller's
// sink in visible-area-local coordinates.
//
// Handles all four MAME orientation flags so rotated/flipped games display
// correctly without needing separate draw paths.
// ---------------------------------------------------------------------------
void raster_emit_polys(RasterPolySink sink, void* user, int yFlip)
{
	unsigned char r1, g1, b1;

	int minX, maxX, minY, maxY, rot, dstW, dstH;
	if (!compute_raster_geom(minX, maxX, minY, maxY, rot, dstW, dstH))
		return;

	for (int srcY = minY; srcY <= maxY; ++srcY)
	{
		unsigned char* srcRow = main_bitmap->line[srcY];
		if (!srcRow)
			continue;

		for (int srcX = minX; srcX <= maxX; ++srcX)
		{
			const unsigned char c = srcRow[srcX];

			// Every pixel is emitted, black ones included - the sinks
			// paint an opaque image, they do not composite over anything.

			osd_get_pen(Machine->pens[c], &r1, &g1, &b1);

			// Convert source bitmap coords to local visible-area coords.
			int x = srcX - minX;
			int y = srcY - minY;

			// Apply MAME orientation flags.
			// IMPORTANT: FLIP is performed after SWAP_XY.
			if (rot & ORIENTATION_SWAP_XY)
			{
				const int t = x;
				x = y;
				y = t;
			}

			// Flip against destination extents, not source extents.
			if (rot & ORIENTATION_FLIP_X)
			{
				x = (dstW - 1) - x;
			}

			if (rot & ORIENTATION_FLIP_Y)
			{
				y = (dstH - 1) - y;
			}

			// yFlip = 0: y grows downward (both current callers).
			// yFlip = 1: flip to a bottom-left origin.
			float fy = (float)y;
			if (yFlip)
				fy = (float)(dstH - 1 - y);

			// Submit in visible-area-local coordinates.
			sink(user, (float)x, fy, config.prescale, MAKE_RGBA(r1, g1, b1, 0xff));
		}
	}
}
