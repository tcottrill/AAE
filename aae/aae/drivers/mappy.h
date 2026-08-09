#pragma once

/***************************************************************************

  mappy.h  --  AAE driver header for Mappy / Dig Dug 2 / Motos / Tower of Druaga

  All games share the same dual-6809 Namco Super Pac-Man board, custom I/O
  chips, and Namco 8-voice sound hardware.

***************************************************************************/

#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// Shared hardware state (defined in mappy.cpp, used by mappy_video.cpp)
// ---------------------------------------------------------------------------

extern unsigned char* mappy_sharedram;
extern unsigned char* mappy_customio_1;
extern unsigned char* mappy_customio_2;
extern unsigned char* mappy_soundregs;

/* current horizontal scroll value (set by mappy_scroll_w, read by screenrefresh) */
extern unsigned char mappy_scroll;

extern int mappy_flipscreen;
// ---------------------------------------------------------------------------
// Machine init
// ---------------------------------------------------------------------------

void mappy_init_machine(void);
void motos_init_machine(void);

// ---------------------------------------------------------------------------
// Memory handlers (declared for use in MEM_READ/MEM_WRITE tables)
// ---------------------------------------------------------------------------








// ---------------------------------------------------------------------------
// Interrupt callbacks (called by cpu_control each VBLANK)
// ---------------------------------------------------------------------------

void mappy_interrupt_1(void);
void mappy_interrupt_2(void);

// ---------------------------------------------------------------------------
// Video (implemented in mappy_video.cpp)
// ---------------------------------------------------------------------------

int  mappy_vh_start(void);
int  motos_vh_start(void);


void mappy_vh_stop(void);
void mappy_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh);

void mappy_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable,const unsigned char* color_prom);

// ---------------------------------------------------------------------------
// GFX decode tables (referenced by AAE_DRIVER_RASTER in driver descriptor)
// ---------------------------------------------------------------------------

extern struct GfxDecodeInfo mappy_gfxdecodeinfo[];
extern struct GfxDecodeInfo digdug2_gfxdecodeinfo[];
extern struct GfxDecodeInfo todruaga_gfxdecodeinfo[];
