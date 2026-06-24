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

READ_HANDLER(gaplus_sharedram_r);
WRITE_HANDLER(gaplus_sharedram_w);
READ_HANDLER(gaplus_snd_sharedram_r);
WRITE_HANDLER(gaplus_snd_sharedram_w);

READ_HANDLER(gaplus_customio_1_r);
READ_HANDLER(gaplus_customio_2_r);
READ_HANDLER(gaplus_customio_3_r);
WRITE_HANDLER(gaplus_customio_1_w);
WRITE_HANDLER(gaplus_customio_2_w);
WRITE_HANDLER(gaplus_customio_3_w);

WRITE_HANDLER(gaplus_interrupt_enable_2_w);
WRITE_HANDLER(gaplus_interrupt_ctrl_2_w);
WRITE_HANDLER(gaplus_interrupt_ctrl_3a_w);
WRITE_HANDLER(gaplus_interrupt_ctrl_3b_w);
WRITE_HANDLER(gaplus_reset_2_3_w);
WRITE_HANDLER(gaplus_cpu_enable_w);

WRITE_HANDLER(gaplus_videoram_w);
WRITE_HANDLER(gaplus_colorram_w);
WRITE_HANDLER(gaplus_scroll_w);

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
WRITE_HANDLER_NS(gaplus_starfield_control_w);
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
