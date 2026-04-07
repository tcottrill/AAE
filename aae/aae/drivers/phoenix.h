#pragma once

/***************************************************************************

  phoenix.h  --  AAE driver header for Phoenix and Pleiads

  Hardware family: Phoenix (Amstar / Centuri / Tehkan)

  Games covered:
    phoenix   - Phoenix (Amstar, 1980)
    pleiads   - Pleiads (Tehkan, 1981)

  Hardware:
    CPU: Intel 8085A @ 2.75 MHz (11 MHz / 4), mapped to CPU_8080
    Video: Two 32x26 character tile layers (foreground + background)
           Background scrolls horizontally via phoenix_scroll_w
           Two 256-entry character banks (GFX1=bg chars, GFX2=fg chars)
           Dual-page video RAM (selected by bit 0 of videoreg)
           Palette: 2x 256x4-bit PROMs, banked (1-bit for Phoenix, 2-bit for Pleiads)
    Sound (Phoenix): Custom analog circuit (two tone generators + noise)
                     + MM6221AA melody chip (TMS36XX family)
    Sound (Pleiads): Custom analog circuit (same topology as Phoenix)
                     + TMS3615 melody chip (TMS36XX family)

  Memory map:
    0000-3fff  ROM (8x 2K banks)
    4000-4fff  Paged video RAM (2 pages, selected by videoreg bit 0)
               4000-07ff  foreground tile codes (32x26 visible)
               4800-0fff  background tile codes (32x26 visible)
    5000-53ff  Video register (page select, palette bank, protection)
    5800-5bff  Background horizontal scroll
    6000-63ff  Sound control A (custom analog tones + noise)
    6800-6bff  Sound control B (custom analog tone2 + MM6221AA tune)
    7000-73ff  Input port 0 (active player controls, active low)
    7800-7bff  Input port 2 (DIP switches + VBLANK)

  The screen is 32x26 tiles (256x208 visible pixels), displayed rotated
  90 degrees counterclockwise.

***************************************************************************/

#ifndef PHOENIX_H
#define PHOENIX_H

#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// init / run / end entry points used by the AAE driver descriptors
// ---------------------------------------------------------------------------

int  init_phoenix(void);
void run_phoenix(void);
void end_phoenix(void);

int  init_pleiads(void);
void run_pleiads(void);
void end_pleiads(void);

// ---------------------------------------------------------------------------
// Interrupt callback
// ---------------------------------------------------------------------------

void phoenix_interrupt(void);

// ---------------------------------------------------------------------------
// GFX decode tables
// ---------------------------------------------------------------------------

extern struct GfxDecodeInfo phoenix_gfxdecodeinfo[];
extern struct GfxDecodeInfo pleiads_gfxdecodeinfo[];

// ---------------------------------------------------------------------------
// Video (color PROM conversion)
// ---------------------------------------------------------------------------

void phoenix_vh_convert_color_prom(unsigned char* palette,
                                    unsigned char* colortable,
                                    const unsigned char* color_prom);

void pleiads_vh_convert_color_prom(unsigned char* palette,
                                    unsigned char* colortable,
                                    const unsigned char* color_prom);

// ---------------------------------------------------------------------------
// Memory handlers (exposed for memory map wiring)
// ---------------------------------------------------------------------------

READ_HANDLER(phoenix_paged_ram_r);
WRITE_HANDLER(phoenix_paged_ram_w);
WRITE_HANDLER(phoenix_videoreg_w);
WRITE_HANDLER(pleiads_videoreg_w);
WRITE_HANDLER(phoenix_scroll_w);

// ---------------------------------------------------------------------------
// Input port handlers
// ---------------------------------------------------------------------------

READ_HANDLER(phoenix_input_port_0_r);
READ_HANDLER(pleiads_input_port_0_r);



#endif // PHOENIX_H
