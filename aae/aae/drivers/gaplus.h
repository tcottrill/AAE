#pragma once

/***************************************************************************

  gaplus.h  --  AAE driver header for gaplus

  All games share the same triple-6809 Namco Super Pac-Man board, custom I/O
  chips, and Namco 8-voice sound hardware.

***************************************************************************/

#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// Shared hardware state (defined in gaplus.cpp, used by gaplus_video.cpp)
// ---------------------------------------------------------------------------

extern unsigned char* gaplus_sharedram;
extern unsigned char* gaplus_snd_sharedram;
extern unsigned char* gaplus_customio_1;
extern unsigned char* gaplus_customio_2;
extern unsigned char* gaplus_customio_3;

/* current horizontal scroll value (set by gaplus_scroll_w, read by screenrefresh) */
extern unsigned char gaplus_scroll;

// ---------------------------------------------------------------------------
// Machine init
// ---------------------------------------------------------------------------

void gaplus_init_machine(void);

// ---------------------------------------------------------------------------
// Memory handlers (declared for use in MEM_READ/MEM_WRITE tables)
// ---------------------------------------------------------------------------





/* defined in gaplus_video.cpp but referenced by the CPU1 memory map in
   gaplus.cpp, so it must have external (non-static) linkage */


// ---------------------------------------------------------------------------
// Interrupt callbacks (called by cpu_control each VBLANK)
// ---------------------------------------------------------------------------

void gaplus_interrupt_1(void);
void gaplus_interrupt_2(void);
void gaplus_interrupt_3(void);

// ---------------------------------------------------------------------------
// Starfield (implemented in gaplus_video.cpp)
// ---------------------------------------------------------------------------

void gaplus_starfield_update(void);
void gaplus_flipscreen_w(int data);

// ---------------------------------------------------------------------------
// Video (implemented in gaplus_video.cpp)
// ---------------------------------------------------------------------------

int  gaplus_vh_start(void);
void gaplus_vh_stop(void);
void gaplus_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh);

void gaplus_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);

// ---------------------------------------------------------------------------
// GFX decode tables (referenced by AAE_DRIVER_RASTER in driver descriptor)
// ---------------------------------------------------------------------------

extern struct GfxDecodeInfo gaplus_gfxdecodeinfo[];
