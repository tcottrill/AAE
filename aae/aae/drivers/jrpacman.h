#pragma once

#ifndef JRPACMAN_H
#define JRPACMAN_H

#pragma warning(disable:4996 4102)

// ----------------------------------------------------------------------------
// jrpacman.h -- Jr. Pac-Man AAE driver declarations
//
// Jr. Pac-Man (Bally Midway, 1983) is a Z80-based Pac-Man hardware variant
// with several video extensions over the original:
//
//   - Virtual playfield twice the screen height, scrolled per-column.
//   - Two character ROM banks (jrpacman_charbank at 0x5074).
//   - Two sprite ROM banks (jrpacman_spritebank at 0x5075).
//   - Switchable palette bank (0x5070) and color table bank (0x5071).
//   - Background-priority register (0x5073) for sprites-behind-tiles.
//   - Pengo-compatible Namco 3-voice sound.
//   - A well-known speed cheat patching RAM[0x180B].
//
// ROM encryption: XOR-based PAL state machine.  init_jrpacman applies a
// pre-computed decryption table at startup (no per-cycle overhead).
//
// Memory map summary:
//   0x0000-0x3FFF  ROM (lower banks, encrypted)
//   0x4000-0x47FF  Video RAM (32x64 virtual playfield)
//   0x4800-0x4FEF  Work RAM
//   0x4FF0-0x4FFF  Sprite RAM (6 sprites x 2 bytes)
//   0x5000         Interrupt enable write / IN0 read
//   0x5001         Sound enable
//   0x5003         Flip screen
//   0x5040-0x505F  Namco sound registers / IN1 read
//   0x5060-0x506F  Sprite coordinate RAM
//   0x5070         Palette bank
//   0x5071         Color table bank
//   0x5073         Background priority
//   0x5074         Character GFX bank
//   0x5075         Sprite GFX bank
//   0x5080         Vertical scroll / DSW1 read
//   0x50C0         Watchdog reset
//   0x8000-0xDFFF  ROM (upper banks, encrypted)
// ----------------------------------------------------------------------------

int  init_jrpacman(void);
void run_jrpacman(void);
void end_jrpacman(void);

// Color PROM decoder -- called once at startup by the raster palette subsystem.
void jrpacman_vh_convert_color_prom(unsigned char* palette,
                                    unsigned char* colortable,
                                    const unsigned char* color_prom);

// GFX decode table pointer -- referenced by AAE_DRIVER_RASTER macro.
extern struct GfxDecodeInfo jrpacman_gfxdecodeinfo[];

#endif // JRPACMAN_H
