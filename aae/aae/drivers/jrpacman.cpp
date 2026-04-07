// ============================================================================
// jrpacman.cpp -- Jr. Pac-Man driver for AAE
//
// Derived from MAME 0.36 sources:
//   src/drivers/jrpacman.c  (machine, video, driver combined)
//   Original MAME code copyright the MAME Team.
//   
//
// Hardware notes are in jrpacman.h.  This file contains:
//   - ROM decryption (init_jrpacman)
//   - Speed cheat (applied each interrupt)
//   - Video RAM / bank register write handlers
//   - Screen refresh (character tilemap + per-column scroll + sprites)
//   - Color PROM decoder
//   - Memory maps, port maps, input ports
//   - AAE driver descriptor and auto-registration
//
// Bitmap ownership:
//   main_bitmap  -- allocated by the OSD layer (osd_video.cpp) at screen
//                   dimensions before init is called.  Do NOT allocate or
//                   free it here.
//   dirtybuffer  -- allocated by generic_vh_start() at videoram_size bytes.
//                   Freed by generic_vh_stop().
//   tmpbitmap1    -- generic_vh_start() allocates this at screen dimensions.
//                   Jr. Pac-Man needs a double-height virtual playfield so
//                   we free the one generic_vh_start created and replace it
//                   with a 2*screen_height version.  generic_vh_stop() then
//                   frees our replacement correctly since it just calls
//                   osd_free_bitmap on whatever tmpbitmap1 points to.
// ============================================================================

#include "jrpacman.h"

#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "cpu_control.h"
#include "inptport.h"
#include "memory.h"

// ----------------------------------------------------------------------------
// Video state -- all static, local to this translation unit.
// ----------------------------------------------------------------------------

// Pointer into CPU RAM for the per-column vertical scroll value (one byte).
// Mapped to 0x5080 in writemem; pointer set in init_jrpacman.
static unsigned char* jrpacman_scroll = nullptr;

// Background-priority flag (one byte at 0x5073).
// When bit 0 is set sprites render BEHIND tiles (TRANSPARENCY_THROUGH).
static unsigned char* jrpacman_bgpriority = nullptr;

// Character GFX bank select (one byte at 0x5074).
// Bit 0 selects which 256-entry half of the character ROM is active.
static unsigned char* jrpacman_charbank = nullptr;

// Sprite GFX bank select (one byte at 0x5075).
// Bit 0 selects which 64-entry half of the sprite ROM is active.
static unsigned char* jrpacman_spritebank = nullptr;

// Palette bank select (one byte at 0x5070).
// Bit 0 adds 0x40 to the color index, selecting one of two palette halves.
static unsigned char* jrpacman_palettebank = nullptr;

// Color table bank select (one byte at 0x5071).
// Bit 0 adds 0x20 to the color index, selecting one of two color table halves.
static unsigned char* jrpacman_colortablebank = nullptr;

// Current flip-screen state.  Tracked so we only dirty everything on change.
static int flipscreen = 0;

// Interrupt enable -- the game writes 0 here to mask the IRQ.
static int jrpacman_int_enable = 1;

// Speed cheat flag -- set in init if the loaded ROM supports the known hack.
// The cheat patches RAM[0x180B]: 0x01 enables it, 0xBE is the original byte.
static int speedcheat = 0;

// ----------------------------------------------------------------------------
// Namco sound interface (same hardware as Pengo / original Pac-Man)
// ----------------------------------------------------------------------------

static struct namco_interface namco_interface =
{
	3072000 / 32,	/* sample rate */
	3,			/* number of voices */
	220,		/* playback volume */
	REGION_SOUND1	/* memory region */
};
// ----------------------------------------------------------------------------
// GFX layouts
//
// Jr. Pac-Man uses the same bit-packing as original Pac-Man but with larger
// ROM sets to support banked char/sprite selection.
//
// Characters: 512 total (two 256-entry banks), 8x8, 2bpp.
//   Pixels are stored rotated 90 degrees in ROM -- the xoffset array undoes
//   this on decode.
//
// Sprites: 128 total (two 64-entry banks), 16x16, 2bpp.
// ----------------------------------------------------------------------------

static struct GfxLayout charlayout =
{
	8,8,
	512,	/* 512 characters */
	2,		/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },	/* bits are packed in groups of four */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	16 * 8	/* every char takes 16 bytes */
};
static struct GfxLayout spritelayout =
{
	16,16,	/* 16*16 sprites */
	128,	/* 128 sprites */
	2,		/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
			24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
	64 * 8	/* every sprite takes 64 bytes */
};

struct GfxDecodeInfo jrpacman_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,   0, 128 },
	{ REGION_GFX2, 0, &spritelayout, 0, 128 },
	{ -1 }
};

// ----------------------------------------------------------------------------
// Color PROM decoder
//
// Jr. Pac-Man has two 256x4 palette PROMs (only 32 entries used, top 3
// address bits grounded) and one 256x4 color lookup table PROM.
//
// PROM layout in REGION_PROMS (0x0140 bytes):
//   0x0000-0x001F  palette low PROM  (green[1:0], red[3:0])
//   0x0020-0x003F  palette high PROM (blue[3:0], green[3:2])
//   0x0040-0x013F  color lookup table (256 x 4-bit entries)
//
// Color lookup has two sections:
//   First  64*4 entries: normal pens
//   Second 64*4 entries: alternate bank -- non-zero pens get +0x10,
//                        pen 0 stays 0 (transparent black)
// ----------------------------------------------------------------------------

void jrpacman_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	/*
	int i;

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		// Red: bits 0-2 of low PROM
		bit0 = (color_prom[i]    >> 0) & 0x01;
		bit1 = (color_prom[i]    >> 1) & 0x01;
		bit2 = (color_prom[i]    >> 2) & 0x01;
		palette[3*i + 0] = (unsigned char)(0x21*bit0 + 0x47*bit1 + 0x97*bit2);

		// Green: bit 3 of low PROM + bits 0-1 of high PROM
		bit0 = (color_prom[i]    >> 3) & 0x01;
		bit1 = (color_prom[i+32] >> 0) & 0x01;
		bit2 = (color_prom[i+32] >> 1) & 0x01;
		palette[3*i + 1] = (unsigned char)(0x21*bit0 + 0x47*bit1 + 0x97*bit2);

		// Blue: bits 2-3 of high PROM (bit0 is always 0)
		bit0 = 0;
		bit1 = (color_prom[i+32] >> 2) & 0x01;
		bit2 = (color_prom[i+32] >> 3) & 0x01;
		palette[3*i + 2] = (unsigned char)(0x21*bit0 + 0x47*bit1 + 0x97*bit2);
	}
	color_prom += 2 * 256;
	// Normal color lookup section
	for (i = 0; i < 64*4; i++)
		colortable[i] = color_prom[i + 64];

	// Alternate bank: non-zero pens shifted by +0x10, pen 0 = transparent
	for (i = 64*4; i < 64*4 + 64*4; i++)
	{
		unsigned char pen = color_prom[i - 64*4 + 64];
		colortable[i] = pen ? (pen + 0x10) : 0;
	}
	*/
	int i;

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		bit0 = (color_prom[i] >> 0) & 0x01;
		bit1 = (color_prom[i] >> 1) & 0x01;
		bit2 = (color_prom[i] >> 2) & 0x01;
		palette[3 * i] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (color_prom[i] >> 3) & 0x01;
		bit1 = (color_prom[i + 256] >> 0) & 0x01;
		bit2 = (color_prom[i + 256] >> 1) & 0x01;
		palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = 0;
		bit1 = (color_prom[i + 256] >> 2) & 0x01;
		bit2 = (color_prom[i + 256] >> 3) & 0x01;
		palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	}

	color_prom += 2 * 256;

	for (i = 0; i < 64 * 4; i++)
	{
		/* chars */
		colortable[i] = color_prom[i];

		/* sprites */
		if (color_prom[i]) colortable[i + 64 * 4] = color_prom[i] + 0x10;
		else colortable[i + 64 * 4] = 0;
	}
	LOG_INFO("COLOR PROM CONVERSION COMPLETE");
}

// ----------------------------------------------------------------------------
// Video write handlers
// ----------------------------------------------------------------------------

// jrpacman_videoram_w -- 0x4000-0x47FF
//
// The first 32 bytes of VRAM (offsets 0x000-0x01F) are per-column color codes
// rather than tile indices.  Writing one of these must dirty all tiles in that
// column.  Writes above offset 0x700 affect border rows that are mirrored.
WRITE_HANDLER(jrpacman_videoram_w)
{
	int offset = address;
	if (videoram[offset] != data)
	{
		dirtybuffer[offset] = 1;

		videoram[offset] = data;

		if (offset < 32)	/* line color - mark whole line as dirty */
		{
			int i;

			for (i = 2 * 32; i < 56 * 32; i += 32)
				dirtybuffer[i + offset] = 1;
		}
		else if (offset > 1792)	/* colors for top and bottom two rows */
		{
			dirtybuffer[offset & ~0x80] = 1;
		}
	}
}

// jrpacman_palettebank_w -- 0x5070
WRITE_HANDLER(jrpacman_palettebank_w)
{
	if (*jrpacman_palettebank != data)
	{
		*jrpacman_palettebank = data;
		memset(dirtybuffer, 1, videoram_size);
	}
}

// jrpacman_colortablebank_w -- 0x5071
WRITE_HANDLER(jrpacman_colortablebank_w)
{
	if (*jrpacman_colortablebank != data)
	{
		*jrpacman_colortablebank = data;
		memset(dirtybuffer, 1, videoram_size);
	}
}

// jrpacman_charbank_w -- 0x5074
WRITE_HANDLER(jrpacman_charbank_w)
{
	if (*jrpacman_charbank != data)
	{
		*jrpacman_charbank = data;
		memset(dirtybuffer, 1, videoram_size);
	}
}

// jrpacman_flipscreen_w -- 0x5003
WRITE_HANDLER(jrpacman_flipscreen_w)
{
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		memset(dirtybuffer, 1, videoram_size);
	}
}

// jrpacman_namco_w -- 0x5040-0x505F
WRITE_HANDLER(jrpacman_namco_w)
{
	namco_sound_w(address & 0x1f, data);
}

// ----------------------------------------------------------------------------
// Screen refresh
//
// Jr. Pac-Man's VRAM is 32 bytes wide x 64 rows tall (0x800 bytes).
// The visible screen is 36 tile columns x 28 tile rows (288x224 pixels).
//
// VRAM layout (offset = my*32 + mx):
//   my  0-1  : column color code bytes, NOT tile indices
//   my  2-55 : scrolling playfield (54 rows)
//   my 56-57 : bottom border tiles; color from VRAM row my+4
//   my 58-63 : top border tiles;    color from VRAM row my+4
//
// tmpbitmap1 is double-height (288x448) to hold the full virtual playfield.
// copyscrollbitmap with rows=0, cols=36 then cuts the visible 288x224 window
// from it using per-column vertical scroll offsets.
// ----------------------------------------------------------------------------

void jrpacman_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh)
{
	int i, offs;
	int scrolly[36];

	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int mx, my;

			dirtybuffer[offs] = 0;

			/* Jr. Pac Man's screen layout is quite awkward */
			mx = offs % 32;
			my = offs / 32;

			if (my >= 2 && my < 60)
			{
				int sx, sy;

				if (my < 56)
				{
					sy = my;
					sx = mx + 2;
					if (flipscreen)
					{
						sx = 35 - sx;
						sy = 55 - sy;
					}

					drawgfx(tmpbitmap1, Machine->gfx[0],
						videoram[offs] + 256 * *jrpacman_charbank,
						/* color is set line by line */
						(videoram[mx] & 0x1f) + 0x20 * (*jrpacman_colortablebank & 1)
						+ 0x40 * (*jrpacman_palettebank & 1),
						flipscreen, flipscreen,
						8 * sx, 8 * sy,
						0, TRANSPARENCY_NONE, 0);
				}
				else
				{
					if (my >= 58)
					{
						sy = mx - 2;
						sx = my - 58;
						if (flipscreen)
						{
							sx = 35 - sx;
							sy = 55 - sy;
						}

						drawgfx(tmpbitmap1, Machine->gfx[0],
							videoram[offs],
							(videoram[offs + 4 * 32] & 0x1f) + 0x20 * (*jrpacman_colortablebank & 1)
							+ 0x40 * (*jrpacman_palettebank & 1),
							flipscreen, flipscreen,
							8 * sx, 8 * sy,
							0, TRANSPARENCY_NONE, 0);
					}
					else
					{
						sy = mx - 2;
						sx = my - 22;
						if (flipscreen)
						{
							sx = 35 - sx;
							sy = 55 - sy;
						}

						drawgfx(tmpbitmap1, Machine->gfx[0],
							videoram[offs] + 0x100 * (*jrpacman_charbank & 1),
							(videoram[offs + 4 * 32] & 0x1f) + 0x20 * (*jrpacman_colortablebank & 1)
							+ 0x40 * (*jrpacman_palettebank & 1),
							flipscreen, flipscreen,
							8 * sx, 8 * sy,
							0, TRANSPARENCY_NONE, 0);
					}
				}
			}
		}
	}

	/* copy the temporary bitmap to the screen */
	{
		for (i = 0; i < 2; i++)
			scrolly[i] = 0;
		for (i = 2; i < 34; i++)
			scrolly[i] = -*jrpacman_scroll - 16;
		for (i = 34; i < 36; i++)
			scrolly[i] = 0;

		if (flipscreen)
		{
			for (i = 0; i < 36; i++)
				scrolly[i] = 224 - scrolly[i];
		}
	}

	copyscrollbitmap(bitmap, tmpbitmap1, 0, 0, 36, scrolly, &Machine->visible_area, TRANSPARENCY_NONE, 0);

	/* Draw the sprites. Note that it is important to draw them exactly in this */
	/* order, to have the correct priorities. */
	for (offs = spriteram_size - 2; offs > 2 * 2; offs -= 2)
	{
		drawgfx(bitmap, Machine->gfx[1],
			(spriteram[offs] >> 2) + 0x40 * (*jrpacman_spritebank & 1),
			(spriteram[offs + 1] & 0x1f) + 0x20 * (*jrpacman_colortablebank & 1)
			+ 0x40 * (*jrpacman_palettebank & 1),
			spriteram[offs] & 1, spriteram[offs] & 2,
			272 - spriteram_2[offs + 1], spriteram_2[offs] - 31,
			&Machine->drv->visible_area,
			(*jrpacman_bgpriority & 1) ? TRANSPARENCY_THROUGH : TRANSPARENCY_COLOR,
			(*jrpacman_bgpriority & 1) ? Machine->pens[0] : 0);
	}
	/* the first two sprites must be offset one pixel to the left */
	for (offs = 2 * 2; offs > 0; offs -= 2)
	{
		drawgfx(bitmap, Machine->gfx[1],
			(spriteram[offs] >> 2) + 0x40 * (*jrpacman_spritebank & 1),
			(spriteram[offs + 1] & 0x1f) + 0x20 * (*jrpacman_colortablebank & 1)
			+ 0x40 * (*jrpacman_palettebank & 1),
			spriteram[offs] & 1, spriteram[offs] & 2,
			272 - spriteram_2[offs + 1], spriteram_2[offs] - 30,
			&Machine->drv->visible_area,
			(*jrpacman_bgpriority & 1) ? TRANSPARENCY_THROUGH : TRANSPARENCY_COLOR,
			(*jrpacman_bgpriority & 1) ? Machine->pens[0] : 0);
	}

}

WRITE_HANDLER(jrpac_sound_enable_w)
{
	/* address offset 0 = disable, 1 = enable; data is unused by hardware */
	pengo_sound_enable_w(address, data);
}

// ----------------------------------------------------------------------------
// Interrupt callback -- called once per frame by the CPU scheduler.
// ----------------------------------------------------------------------------

void jrpacman_interrupt(void)
{
	//if (!jrpacman_int_enable) return;

	if (speedcheat)
	{
		unsigned char* RAM = Machine->memory_region[REGION_CPU1];
		if (readinputport(3) & 1)
			RAM[0x180b] = 0x01;     // enable speed cheat
		else
			RAM[0x180b] = 0xbe;     // restore original limiter byte
	}

	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

// ----------------------------------------------------------------------------
// ROM decryption -- pre-computed XOR table, avoids per-cycle overhead.
// Table by David Caldwell (david@indigita.com).
// ----------------------------------------------------------------------------
static void jrpacman_decode(void)
{
	LOG_INFO("JRPAC DECRYPT CALLED");
	/* The encryption PALs garble bits 0, 2 and 7 of the ROMs. The encryption */
	 /* scheme is complex (basically it's a state machine) and can only be */
	 /* faithfully emulated at run time. To avoid the performance hit that would */
	 /* cause, here we have a table of the values which must be XORed with */
	 /* each memory region to obtain the decrypted bytes. */
	 /* Decryption table provided by David Caldwell (david@indigita.com) */
	 /* For an accurate reproduction of the encryption, see jrcrypt.c IN M.A.M.E !*/
	struct {
		int count;
		int value;
	} table[] =
	{
		{ 0x00C1, 0x00 },{ 0x0002, 0x80 },{ 0x0004, 0x00 },{ 0x0006, 0x80 },
		{ 0x0003, 0x00 },{ 0x0002, 0x80 },{ 0x0009, 0x00 },{ 0x0004, 0x80 },
		{ 0x9968, 0x00 },{ 0x0001, 0x80 },{ 0x0002, 0x00 },{ 0x0001, 0x80 },
		{ 0x0009, 0x00 },{ 0x0002, 0x80 },{ 0x0009, 0x00 },{ 0x0001, 0x80 },
		{ 0x00AF, 0x00 },{ 0x000E, 0x04 },{ 0x0002, 0x00 },{ 0x0004, 0x04 },
		{ 0x001E, 0x00 },{ 0x0001, 0x80 },{ 0x0002, 0x00 },{ 0x0001, 0x80 },
		{ 0x0002, 0x00 },{ 0x0002, 0x80 },{ 0x0009, 0x00 },{ 0x0002, 0x80 },
		{ 0x0009, 0x00 },{ 0x0002, 0x80 },{ 0x0083, 0x00 },{ 0x0001, 0x04 },
		{ 0x0001, 0x01 },{ 0x0001, 0x00 },{ 0x0002, 0x05 },{ 0x0001, 0x00 },
		{ 0x0003, 0x04 },{ 0x0003, 0x01 },{ 0x0002, 0x00 },{ 0x0001, 0x04 },
		{ 0x0003, 0x01 },{ 0x0003, 0x00 },{ 0x0003, 0x04 },{ 0x0001, 0x01 },
		{ 0x002E, 0x00 },{ 0x0078, 0x01 },{ 0x0001, 0x04 },{ 0x0001, 0x05 },
		{ 0x0001, 0x00 },{ 0x0001, 0x01 },{ 0x0001, 0x04 },{ 0x0002, 0x00 },
		{ 0x0001, 0x01 },{ 0x0001, 0x04 },{ 0x0002, 0x00 },{ 0x0001, 0x01 },
		{ 0x0001, 0x04 },{ 0x0002, 0x00 },{ 0x0001, 0x01 },{ 0x0001, 0x04 },
		{ 0x0001, 0x05 },{ 0x0001, 0x00 },{ 0x0001, 0x01 },{ 0x0001, 0x04 },
		{ 0x0002, 0x00 },{ 0x0001, 0x01 },{ 0x0001, 0x04 },{ 0x0002, 0x00 },
		{ 0x0001, 0x01 },{ 0x0001, 0x04 },{ 0x0001, 0x05 },{ 0x0001, 0x00 },
		{ 0x01B0, 0x01 },{ 0x0001, 0x00 },{ 0x0002, 0x01 },{ 0x00AD, 0x00 },
		{ 0x0031, 0x01 },{ 0x005C, 0x00 },{ 0x0005, 0x01 },{ 0x604E, 0x00 },
		{ 0,0 }
	};
	int i, j, A;
	unsigned char* RAM = memory_region(REGION_CPU1);

	A = 0;
	i = 0;
	while (table[i].count)
	{
		for (j = 0; j < table[i].count; j++)
		{
			RAM[A] ^= table[i].value;
			A++;
		}
		i++;
	}
}

// ----------------------------------------------------------------------------
// Memory maps
// ----------------------------------------------------------------------------

MEM_READ(jrpacman_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x4000, 0x4fff, MRA_RAM)
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)   // IN0: P1 joystick, coins
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)   // IN1: P2 joystick, start, cabinet
MEM_ADDR(0x5080, 0x50bf, ip_port_2_r)   // DSW1: coinage, lives, bonus
MEM_ADDR(0x8000, 0xdfff, MRA_ROM)
MEM_END

MEM_WRITE(jrpacman_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x47ff, jrpacman_videoram_w)
MEM_ADDR(0x4800, 0x4fef, MWA_RAM)
MEM_ADDR(0x4ff0, 0x4fff, MWA_RAM)           // sprite RAM (ptr set in init)
MEM_ADDR(0x5000, 0x5000, interrupt_enable_w)
MEM_ADDR(0x5001, 0x5001, jrpac_sound_enable_w)
MEM_ADDR(0x5003, 0x5003, jrpacman_flipscreen_w)
MEM_ADDR(0x5040, 0x505f, jrpacman_namco_w)
MEM_ADDR(0x5060, 0x506f, MWA_RAM)           // sprite coordinates (ptr set in init)
MEM_ADDR(0x5070, 0x5070, jrpacman_palettebank_w)
MEM_ADDR(0x5071, 0x5071, jrpacman_colortablebank_w)
MEM_ADDR(0x5073, 0x5073, MWA_RAM)           // bgpriority (ptr set in init)
MEM_ADDR(0x5074, 0x5074, jrpacman_charbank_w)
MEM_ADDR(0x5075, 0x5075, MWA_RAM)           // spritebank (ptr set in init)
MEM_ADDR(0x5080, 0x5080, MWA_RAM)           // scroll register (ptr set in init)
MEM_ADDR(0x50c0, 0x50c0, watchdog_reset_w)
MEM_ADDR(0x8000, 0xdfff, MWA_ROM)
MEM_END

// Z80 port map: OUT port 0 sets the interrupt vector.
PORT_READ(jrpacman_readport)
PORT_END

PORT_WRITE(jrpacman_writeport)
PORT_ADDR(0x00, 0x00, interrupt_vector_w)
PORT_END

// ----------------------------------------------------------------------------
// init_jrpacman
//
// 1. Decrypt ROMs.
// 2. Set VRAM/sprite RAM pointers and bank register pointers.
// 3. Call generic_vh_start() to get dirtybuffer + a baseline tmpbitmap1.
// 4. Replace tmpbitmap1 with a double-height one for the virtual playfield.
// 5. Start Namco sound.
// 6. Initialize Z80.
// ----------------------------------------------------------------------------

int init_jrpacman(void)
{
	LOG_INFO("INIT: Jr. Pac-Man driver init");

	jrpacman_decode();

	{
		unsigned char* RAM = Machine->memory_region[REGION_CPU1];
		speedcheat = (RAM[0x180b] == 0xbe || RAM[0x180b] == 0x01) ? 1 : 0;
	}

	// VRAM: 32 cols x 64 rows = 0x800 bytes at 0x4000
	videoram = &Machine->memory_region[REGION_CPU1][0x4000];
	videoram_size = 0x800;

	// Sprite RAM: 6 sprites x 2 bytes at 0x4FF0
	spriteram = &Machine->memory_region[REGION_CPU1][0x4ff0];
	spriteram_size = 6 * 2;

	// Sprite coordinate RAM: 6 sprites x 2 bytes at 0x5060
	spriteram_2 = &Machine->memory_region[REGION_CPU1][0x5060];
	spriteram_2_size = 6 * 2;

	// Bank register pointers into the CPU RAM window.
	// MWA_RAM in the write table lets stores fall through; we read back via these.
	jrpacman_palettebank = &Machine->memory_region[REGION_CPU1][0x5070];
	jrpacman_colortablebank = &Machine->memory_region[REGION_CPU1][0x5071];
	jrpacman_bgpriority = &Machine->memory_region[REGION_CPU1][0x5073];
	jrpacman_charbank = &Machine->memory_region[REGION_CPU1][0x5074];
	jrpacman_spritebank = &Machine->memory_region[REGION_CPU1][0x5075];
	jrpacman_scroll = &Machine->memory_region[REGION_CPU1][0x5080];

	flipscreen = 0;
	jrpacman_int_enable = 0;

	// generic_vh_start allocates dirtybuffer (videoram_size bytes, all 1) and
	// tmpbitmap1 at screen_width x screen_height.
	if (generic_vh_start() != 0)
	{
		LOG_ERROR("jrpacman: generic_vh_start failed");
		return 1;
	}

	// Jr. Pac-Man needs a double-height tmpbitmap1 for the virtual playfield.
	// Free the one generic_vh_start just created and replace it.
	// generic_vh_stop will free the replacement correctly.

	tmpbitmap1 = osd_create_bitmap(Machine->gamedrv->screen_width,
		Machine->gamedrv->screen_height * 2);
	if (!tmpbitmap1)
	{
		LOG_ERROR("jrpacman: failed to allocate double-height tmpbitmap1");
		generic_vh_stop();  // cleans up dirtybuffer
		return 1;
	}

	namco_sh_start(&namco_interface);

	LOG_INFO("INIT: Jr. Pac-Man init complete (speedcheat=%d)", speedcheat);
	return 0;
}

// ----------------------------------------------------------------------------
// run_jrpacman -- called once per frame after CPU execution
// ----------------------------------------------------------------------------

void run_jrpacman(void)
{
	watchdog_reset_w(0, 0, nullptr);
	jrpacman_vh_screenrefresh(Machine->scrbitmap, 0);
	namco_sh_update();
}

// ----------------------------------------------------------------------------
// end_jrpacman -- called on driver shutdown
// ----------------------------------------------------------------------------

void end_jrpacman(void)
{
	LOG_DEBUG("jrpacman: shutdown");
	namco_sh_stop();
	osd_free_bitmap(tmpbitmap1);
	generic_vh_stop();
}

// ----------------------------------------------------------------------------
// Input ports
// ----------------------------------------------------------------------------

INPUT_PORTS_START(jrpacman)

PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("DSW1")
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x04, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x10, "15000")
PORT_DIPSETTING(0x20, "20000")
PORT_DIPSETTING(0x30, "30000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Normal")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("FAKE")  // speed cheat fake DIP, no PCB connection
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))

INPUT_PORTS_END

/*************************************
 *
 *	ROM definitions
 *
 *************************************/

	ROM_START(jrpacman)
	ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
	ROM_LOAD("jrp8d.8d", 0x0000, 0x2000, CRC(e3fa972e) SHA1(5ea34621213c649ca2848ab31aab2cbe751723d4))
	ROM_LOAD("jrp8e.8e", 0x2000, 0x2000, CRC(ec889e94) SHA1(8294e9e79f8fd19a419431fa690e6ac4a1302f58))
	ROM_LOAD("jrp8h.8h", 0x8000, 0x2000, CRC(35f1fc6e) SHA1(b84b34560b9aae18b24274712b052283faa01730))
	ROM_LOAD("jrp8j.8j", 0xa000, 0x2000, CRC(9737099e) SHA1(07d912a61824323c8fc1b8bd0da89172d4f70b91))
	ROM_LOAD("jrp8k.8k", 0xc000, 0x2000, CRC(5252dd97) SHA1(18bd4d5381656120e4242811006c20776774de4d))

	ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
	ROM_LOAD("jrp2c.2c", 0x0000, 0x2000, CRC(0527ff9b) SHA1(37fe3176b0d125b7d629e108e7ebdc1196e4a132))

	ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
	ROM_LOAD("jrp2e.2e", 0x0000, 0x2000, CRC(73477193) SHA1(f00a488958ea0438642d345693787bdf771219ad))

	ROM_REGION(0x0300, REGION_PROMS, 0)
	ROM_LOAD("a290-27axv-bxhd.9e", 0x0000, 0x0100, CRC(029d35c4) SHA1(d9aa2dc442e9ac36cf3c346b9fb1aa745eaf3cb8)) /* palette low bits */
	ROM_LOAD("a290-27axv-cxhd.9f", 0x0100, 0x0100, CRC(eee34a79) SHA1(7561f8ccab2af85c111af6a02af6986eb67503e5)) /* palette high bits */
	ROM_LOAD("a290-27axv-axhd.9p", 0x0200, 0x0100, CRC(9f6ea9d8) SHA1(62cf15513934d34641433c891a7f73bef82e2fb1)) /* color lookup table */

	ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound prom */
	ROM_LOAD("a290-27axv-dxhd.7p", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
	ROM_LOAD("a290-27axv-exhd.5s", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
	ROM_END

	// ----------------------------------------------------------------------------
	// AAE driver descriptor
	//
	// Screen: 36x28 tiles = 288x224 pixels, ROT90 (upright cabinet).
	// Colors: 32 palette entries; 512 color table entries (two 64*4 banks).
	// CPU: Z80 at 18.432 MHz / 6 = 3.072 MHz, one IRQ per frame.
	// ----------------------------------------------------------------------------

	AAE_DRIVER_BEGIN(drv_jrpacman, "jrpacman", "Jr. Pac-Man")
	AAE_DRIVER_ROM(rom_jrpacman)
	AAE_DRIVER_FUNCS(&init_jrpacman, &run_jrpacman, &end_jrpacman)
	AAE_DRIVER_INPUT(input_ports_jrpacman)
	AAE_DRIVER_SAMPLES_NONE()
	AAE_DRIVER_ART_NONE()

	AAE_DRIVER_CPUS(
		AAE_CPU_ENTRY(
			/*type*/     CPU_MZ80,
			/*freq*/     3072000,
			/*div*/      100,
			/*ipf*/      1,
			/*int type*/ INT_TYPE_INT,
			/*int cb*/   &jrpacman_interrupt,
			/*r8*/       jrpacman_readmem,
			/*w8*/       jrpacman_writemem,
			/*pr*/       jrpacman_readport,
			/*pw*/       jrpacman_writeport,
			/*r16*/      nullptr,
			/*w16*/      nullptr
		),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY()
	)

	AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
		VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY,
		ORIENTATION_ROTATE_90)

	AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0 * 8, 36 * 8 - 1, 0 * 8, 28 * 8 - 1)
	AAE_DRIVER_RASTER(jrpacman_gfxdecodeinfo, 32, 128 * 4, jrpacman_vh_convert_color_prom)
	AAE_DRIVER_HISCORE_NONE()
	AAE_DRIVER_VECTORRAM(0, 0)
	AAE_DRIVER_NVRAM_NONE()
	//AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
	AAE_DRIVER_END()

	AAE_REGISTER_DRIVER(drv_jrpacman)