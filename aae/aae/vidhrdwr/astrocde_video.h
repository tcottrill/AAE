/***************************************************************************

  astrocde_video.h

  Bally Astrocade style video hardware (Space Zap, Wizard of Wor, Gorf,
  Robby Roto, ...). Ported from MAME 0.57 src/vidhrdw/astrocde.c to AAE.

  Hardware: 320x204 bitmapped display, 2bpp packed (4 pixels per byte,
  80 bytes per line), with:
    - "magic RAM": writes to 0x0000-0x3fff pass through a barrel
      shifter / expander / OR / XOR unit and land in the video RAM
      (0x4000-0x7fff maps the same 16KB directly).
    - a pattern board blitter on ports 0x78-0x7e.
    - 8 color registers (4 left / 4 right of a programmable split),
      re-latched per scanline, out of a fixed 256-entry YUV palette.
    - a programmable scanline interrupt (ports 0x0d/0x0e/0x0f).

  Differences from the MAME original:
  - MAME's wow_interrupt() (an interrupt callback run 256 times per
    frame) becomes astrocde_scanline_interrupt(): wire it as the
    driver's int_cpu with div=256 / ipf=256. It renders the current
    line into main_bitmap, advances the scan counter, and fires the
    Z80 INT (vector from interrupt_vector_w, port 0x0d) when the
    programmed scanline matches.
  - Port handlers use the AAE z80PortWrite/z80PortRead signatures;
    the register offset is decoded from the port number.
  - The pattern board writes through a small address dispatcher
    (magic RAM / video RAM / plain RAM) instead of cpu_writemem16.
  - The Gorf cycle-steal kludge (z80_ICount) and the Gorf special
    interrupt bits are omitted until a Gorf driver exists.

***************************************************************************/
#ifndef ASTROCDE_VIDEO_H
#define ASTROCDE_VIDEO_H

#include "deftypes.h"   /* UINT8/UINT16/UINT32, MemoryWriteByte, z80Port* */

/* Points into the CPU region at 0x4000 (set by astrocde_vh_start). */
extern unsigned char* wow_videoram;

/* Palette: fixed 256-color YUV-derived palette (32 hues x 8 luma). */
void astrocde_init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);

/* Lifecycle */
int  astrocde_vh_start(void);        /* Space Zap, Robby Roto */
int  astrocde_stars_vh_start(void);  /* Wizard of Wor, Gorf (star field) */
void astrocde_vh_stop(void);

/* B&W monitor simulation. The palette is 32 hues x 8 luminances, so a
   B&W monitor is pen & 7: keep the luminance, drop the hue ("all colors
   will appear as the first 8 grayscales" per the MAME palette comment).
   Cabinets like Space Zap shipped with a B&W monitor and a color gel
   overlay; drivers refresh this each frame from their Monitor dip. */
void astrocde_set_bw_monitor(int enable);

/* Per-scanline tick: register as int_cpu with div=256, ipf=256. */
void astrocde_scanline_interrupt(void);

/* Memory write handlers */
void wow_videoram_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);   /* 0x4000-0x7fff */
void wow_magicram_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);   /* 0x0000-0x3fff */

/* Z80 port write handlers */
void astrocde_colour_register_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);    /* 0x00-0x07 */
void astrocde_mode_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);               /* 0x08 */
void astrocde_colour_split_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);       /* 0x09 */
void astrocde_vertical_blank_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);     /* 0x0a */
void astrocde_colour_block_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);       /* 0x0b */
void astrocde_magic_control_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);      /* 0x0c */
void astrocde_interrupt_enable_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);   /* 0x0e */
void astrocde_interrupt_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);          /* 0x0f */
void astrocde_magic_expand_color_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW); /* 0x19 */
void astrocde_pattern_board_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);      /* 0x78-0x7e */

/* Z80 port read handlers */
UINT16 wow_intercept_r(UINT16 port, struct z80PortRead* pPR);      /* 0x08: collision flags */
UINT16 wow_video_retrace_r(UINT16 port, struct z80PortRead* pPR);  /* 0x0e: current scanline */

#endif
