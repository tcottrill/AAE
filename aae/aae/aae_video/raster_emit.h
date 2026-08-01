#pragma once
// ===========================================================================
// raster_emit.h - backend-neutral raster pixel emitter (Phase 4a Plan 3).
//
// Walks Machine->drv->visible_area of main_bitmap, applies the MAME
// orientation flags (SWAP_XY first, then FLIP_X/FLIP_Y against destination
// extents), converts pens to RGBA via osd_get_pen, and emits one quad per
// pixel (sized config.prescale) into the caller's sink. Both render chains
// share this loop; only the sink (GL Fpoly vs FpolyVK) and the Y direction
// differ.
// ===========================================================================
#include <stdint.h>

// One quad per visible pixel: top-left x/y in output space, edge size, RGBA.
typedef void (*RasterPolySink)(void* user, float x, float y, float size, uint32_t rgba);

// Post-orientation destination dimensions in SOURCE pixels (before any
// scale a caller may apply on top). Returns 0 if no machine/bitmap is
// available.
int raster_dst_dims(int* outW, int* outH);

// yFlip = 0: y grows downward (GL chain's Y-down raster ortho, today's
// behavior). yFlip = 1: y is flipped to bottom-left origin, matching the
// Bosconian vk_blit_scrbitmap convention consumed with flipViewportY=true.
void raster_emit_polys(RasterPolySink sink, void* user, int yFlip);
