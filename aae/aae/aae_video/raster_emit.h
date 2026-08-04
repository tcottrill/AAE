#pragma once
// ===========================================================================
// raster_emit.h - backend-neutral raster pixel emitter.
//
// Walks Machine->drv->visible_area of main_bitmap, applies the MAME
// orientation flags (SWAP_XY first, then FLIP_X/FLIP_Y against destination
// extents), converts pens to RGBA via osd_get_pen, and hands each pixel to
// the caller's sink. Both render chains share this loop; only the sink
// differs - GL builds one Fpoly quad per pixel, VK writes the pixel into a
// linear RGBA8 buffer that becomes a streamed texture (RasterTexVK).
// ===========================================================================
#include <stdint.h>

// One call per visible pixel: top-left x/y in output space, edge size
// (config.prescale - the GL quad's edge; the texture sink ignores it), RGBA.
typedef void (*RasterPolySink)(void* user, float x, float y, float size, uint32_t rgba);

// Post-orientation destination dimensions in SOURCE pixels (before any
// scale a caller may apply on top). Returns 0 if no machine/bitmap is
// available.
int raster_dst_dims(int* outW, int* outH);

// yFlip = 0: y grows downward, top-down image order - what the GL chain's
// Y-down raster ortho wants, and what the VK texture sink wants for row 0.
// Both current callers pass 0. yFlip = 1 flips to a bottom-left origin, for
// a sink drawing under a y-up projection.
void raster_emit_polys(RasterPolySink sink, void* user, int yFlip);
