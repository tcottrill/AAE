// ============================================================================
// xevious.h -- AAE driver declarations for Xevious
//
// Derived from MAME 0.34 sources:
//   src/drivers/xevious.c
//   src/machine/xevious.c
//   src/vidhrdw/xevious.c
//   Original MAME code copyright the MAME Team.
//   AAE port and adaptations copyright the AAE project.
//
// Hardware notes:
//   Xevious (Namco, 1982) -- triple Z80 with Namco custom I/O,
//   Namco WSG 3-voice sound, background scrolling, foreground scrolling,
//   and the "planet map" ROM lookup (schematic 9B).
//
//   CPU 1 (Master):  Z80 @ 3.125 MHz, 0x0000-0x3FFF ROM
//   CPU 2 (Motion):  Z80 @ 3.125 MHz, 0x0000-0x1FFF ROM
//   CPU 3 (Sound):   Z80 @ 3.125 MHz, 0x0000-0x0FFF ROM
//   Shared area:     0x7800-0xCFFF across all three CPUs
//
//   Video: 28x36 tile display (224x288 pixels)
//     Foreground: 512 chars, 1bpp, scrollable
//     Background: 512 tiles, 2bpp, scrollable
//     Sprites:    3 sets (128+128+64), 3bpp, multi-size support
//     Palette:    128 colors from 3x256x4 PROMs + lookup tables
// ============================================================================

#pragma once

#ifndef XEVIOUS_H
#define XEVIOUS_H

#pragma warning(disable:4996 4102)

// ---------------------------------------------------------------------------
// Driver entry points
// ---------------------------------------------------------------------------
int  init_xevious(void);
void run_xevious(void);
void end_xevious(void);

// ---------------------------------------------------------------------------
// Interrupt callbacks (one per CPU)
// ---------------------------------------------------------------------------
void xevious_interrupt_1(void);
void xevious_interrupt_2(void);
void xevious_interrupt_3(void);

// ---------------------------------------------------------------------------
// Color PROM decoder
// ---------------------------------------------------------------------------
void xevious_vh_convert_color_prom(unsigned char* palette,
                                   unsigned char* colortable,
                                   const unsigned char* color_prom);

// ---------------------------------------------------------------------------
// GFX decode table (referenced by AAE_DRIVER_RASTER macro)
// ---------------------------------------------------------------------------
extern struct GfxDecodeInfo xevious_gfxdecodeinfo[];

// ---------------------------------------------------------------------------
// Video hardware (implemented in xevious_video.cpp)
// ---------------------------------------------------------------------------
int  xevious_vh_start(void);
void xevious_vh_stop(void);
void xevious_vh_screenrefresh(void);

#endif // XEVIOUS_H
