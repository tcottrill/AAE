#pragma once

/***************************************************************************

  superpac.h  --  AAE driver header for Super Pac-Man and Pac & Pal

  Hardware family: Namco Super Pac-Man board (dual 6809, custom I/O,
                   Namco 8-voice sound)

  Games covered:
    superpac  - Super Pac-Man (US)             1982 Namco
    superpcm  - Super Pac-Man (Midway license) 1982 Namco / Bally Midway
    pacnpal   - Pac & Pal                      1983 Namco
    pacnchmp  - Pac-Man & Chomp Chomp          1983 Namco

  The Super Pac-Man board is closely related to the Mappy board, sharing
  the same dual-6809 arrangement, custom I/O chips, and Namco sound core.
  Key differences from Mappy:
    - Video RAM at 0x0000-0x03ff (1K tiles) vs Mappy's 0x0000-0x07ff (2K)
    - Color RAM at 0x0400-0x07ff
    - Separate GFX ROMs for chars (GFX1) and sprites (GFX2)
    - Sprites are 2bpp (not 4bpp like Mappy)
    - 64 sprite color sets (vs 16 in basic Mappy)
    - Watchdog at 0x2000 (CPU1) and 0x8000; also a CPU2 reset at 0x5000
    - Sound enable at 0x5008-0x5009 (not 0x2006-0x2007)
    - pacnpal variant adds CPU2 interrupt enable at 0x2000-0x2001

  The screen is 28 tiles wide x 36 tiles tall, displayed rotated 90 degrees.
  Memory layout is 32x32 tiles; the screen-to-memory mapping is non-trivial
  (see superpac_vh_screenrefresh for the conversion).

***************************************************************************/

#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// Shared RAM and custom I/O chip buffers
// These pointers are set in init_superpac() to point into CPU0 address space.
// ---------------------------------------------------------------------------

extern unsigned char* superpac_sharedram;
extern unsigned char* superpac_customio_1;
extern unsigned char* superpac_customio_2;

// ---------------------------------------------------------------------------
// Machine init
// ---------------------------------------------------------------------------

void superpac_init_machine(void);

// ---------------------------------------------------------------------------
// Memory handlers
// ---------------------------------------------------------------------------

READ_HANDLER(superpac_sharedram_r);
READ_HANDLER(superpac_sharedram_r2);
WRITE_HANDLER(superpac_sharedram_w);

READ_HANDLER(pacnpal_sharedram_r2);
WRITE_HANDLER(pacnpal_sharedram_w2);

WRITE_HANDLER(superpac_customio_w_1);
WRITE_HANDLER(superpac_customio_w_2);
READ_HANDLER(superpac_customio_r_1);
READ_HANDLER(superpac_customio_r_2);
READ_HANDLER(pacnpal_customio_r_1);
READ_HANDLER(pacnpal_customio_r_2);

WRITE_HANDLER(superpac_interrupt_enable_1_w);
WRITE_HANDLER(superpac_cpu_enable_w);
WRITE_HANDLER(superpac_reset_2_w);

// ---------------------------------------------------------------------------
// Interrupt callbacks
// ---------------------------------------------------------------------------

void superpac_interrupt_1(void);
void superpac_interrupt_2(void);    /* pacnpal CPU2 only */

// ---------------------------------------------------------------------------
// Video (color PROM conversion and screen refresh)
// ---------------------------------------------------------------------------

void superpac_vh_convert_color_prom(unsigned char* palette,
                                     unsigned char* colortable,
                                     const unsigned char* color_prom);

void superpac_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh);

// ---------------------------------------------------------------------------
// GFX decode tables
// ---------------------------------------------------------------------------

extern struct GfxDecodeInfo superpac_gfxdecodeinfo[];

// ---------------------------------------------------------------------------
// init / run / end entry points used by the AAE driver descriptors
// ---------------------------------------------------------------------------

int  init_superpac(void);
void run_superpac(void);
void end_superpac(void);

int  init_pacnpal(void);
void run_pacnpal(void);
void end_pacnpal(void);
