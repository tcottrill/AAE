// ============================================================================
// phoenix.cpp -- Phoenix / Pleiads driver for AAE
//
// Derived from MAME 0.35/0.36 sources:
//   src/drivers/phoenix.c   (driver, memory maps, input ports)
//   src/vidhrdw/phoenix.c   (video hardware -- pre-tilemap 0.35 approach)
//   src/sndhrdw/phoenix.c   (custom analog sound)
//   Original MAME code copyright the MAME Team.
//   
//
// Hardware notes:
//   CPU: Intel 8085A @ 2.75 MHz (11 MHz / 4)
//   Video: Two 32x26 character tilemaps (foreground + background)
//          Background scrolls horizontally
//          Two 256-entry character banks (GFX1=bg, GFX2=fg)
//          Palette: 2x 256x4-bit PROMs -> 256 colors, banked
//   Sound: Custom analog circuit (tone1, tone2, noise)
//          + MM6221AA melody chip (TMS36XX family)
//
// Games covered:
//   phoenix   - Phoenix (Amstar, 1980)
//   pleiads   - Pleiads (Tehkan, 1981)
//
// ============================================================================

#include "phoenix.h"

#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "tms36xx.h"
#include "cpu_control.h"
#include "inptport.h"
#include "mixer.h"
#include "config.h"
#include "sys_log.h"
#include "phoenix_audio.h"
#include "pleiads_audio.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>

#pragma warning(disable : 4838 4003)

// ============================================================================
// Video state
// ============================================================================

static unsigned char* ram_page1 = nullptr;
static unsigned char* ram_page2 = nullptr;
static unsigned char* current_ram_page = nullptr;
static int current_ram_page_index = -1;
static unsigned char bg_scroll = 0;
static int palette_bank = 0;
static int cocktail_mode = 0;
static int pleiads_protection_question = 0;

#define BACKGROUND_VIDEORAM_OFFSET 0x0800

static int sound_latch_a = 0;
static int sound_latch_b = 0;

// ============================================================================
// Pleiads sample playback
//
// The real Pleiads PCB has sounds that aren't part of the analog circuit:
//   - Player shot: triggered by the i8085 SOD pin (SIM instruction)
//   - Melody 1 (F r Elise): from Epson 7910E melody IC, stage 1
//   - Melody 2 (Romance d'Amour): from Epson 7910E melody IC, stage 4
//
// None of these are emulated in MAME. We use WAV samples from PCB recordings.
//
// Sample list (indices are assigned in order, starting from 0):
//   0 = shot.wav        - player laser shot (one-shot, SOD triggered)
//   1 = melody1.wav     - F r Elise / stage 1 background music (looping)
//   2 = melody2.wav     - Romance d'Amour / stage 4 music (looping)
//
// Add more samples as needed -- just add the filename to the list below
// and use the corresponding index with the helper functions.
//
// Channels (voice-path, direct XAudio2):
//   Channel 0 = shot
//   Channel 1 = melody
//   (Channels 5,6 are used by TMS36XX and pleiads custom audio streams)
// ============================================================================

static const char* pleiads_samples[] =
{
	"pleiads.zip",      // archive name (must be first)
	"shot.wav",         // sample 0 - player shot
	"melody1.wav",      // sample 1 - F r Elise
	"melody2.wav",      // sample 2 - Romance d'Amour
	0                   // end of list
};

// --- Channel assignments ---
static const int PLEIADS_CH_SHOT = 0;
static const int PLEIADS_CH_MELODY = 1;

// --- Sample indices (match order in pleiads_samples above, 0-based after zip name) ---
static const int PLEIADS_SND_SHOT = 0;
static const int PLEIADS_SND_MELODY1 = 1;
static const int PLEIADS_SND_MELODY2 = 2;

static bool pleiads_sod_hooked = false;
static int  pleiads_current_melody = -1;  // which melody is playing (-1 = none)

// --- SOD callback: shot sound ---
static void pleiads_sod_callback(int state)
{
	if (state) {
		// if (!sample_playing(PLEIADS_CH_SHOT))
		sample_start(PLEIADS_CH_SHOT, PLEIADS_SND_SHOT, 0);
	}
}

// --- Melody helpers (call from driver code or latch write handlers) ---

// Start a melody sample looping on the melody channel.
// melody_id: PLEIADS_SND_MELODY1 or PLEIADS_SND_MELODY2
static void pleiads_melody_start(int melody_id)
{
	if (pleiads_current_melody == melody_id)
		return;  // already playing this one

	// Stop any current melody first
	if (sample_playing(PLEIADS_CH_MELODY))
		sample_stop(PLEIADS_CH_MELODY);

	sample_start(PLEIADS_CH_MELODY, melody_id, 1);  // loop=1
	pleiads_current_melody = melody_id;
}

// Stop the melody
static void pleiads_melody_stop(void)
{
	if (pleiads_current_melody >= 0) {
		sample_stop(PLEIADS_CH_MELODY);
		pleiads_current_melody = -1;
	}
}

// --- Wrapper for sound_control_b that also drives melody samples ---
// Bits 6-7 of latch_b select the "pitch" / melody on real hardware.
// On a Phoenix PCB with MM6221AA, these bits select tunes 0-3.
// On a Pleiads PCB with Epson 7910E, these bits trigger the melody IC.
// We intercept the write here, trigger melody samples, then forward
// to the real pleiads_sound_control_b_w for analog sound processing.
/*
WRITE_HANDLER(pleiads_sound_control_b_wrapper)
{
	int melody_select = (data >> 6) & 3;
	LOG_INFO("MELODY SELECT Data address %x data %x", address, data);
	// Map pitch bits to melody samples:
	//   0 = no melody (silence)
	//   1 = melody 1 (F r Elise)
	//   2 = melody 2 (Romance d'Amour)
	//   3 = same as 2 (hardware ties them together)
	switch (melody_select) {
	case 0:
		pleiads_melody_stop();
		break;
	case 1:
		pleiads_melody_start(PLEIADS_SND_MELODY1);
		break;
	case 2:
	case 3:
		pleiads_melody_start(PLEIADS_SND_MELODY2);
		break;
	}

	// Forward to the real handler for TMS3615 notes + analog sound
	pleiads_sound_control_b_w(address, data, psMemWrite);
}
*/

WRITE_HANDLER(pleiads_sound_control_b_wrapper)
{
	static bool melody1_started = false;

	LOG_INFO("MELODY SELECT Data address %x data %x", address, data);

	if (data == 0x0f && !melody1_started) {
		// First time we see 0x0f - start melody 1
		melody1_started = true;
		//pleiads_melody_start(PLEIADS_SND_MELODY1);
		sample_start(1, 1, 1);
	}
	else if (data == 0x2f && melody1_started) {
		// First time we see 0x8f after melody 1 started - stop it
		melody1_started = false;
		//pleiads_melody_stop();
		sample_stop(1);
	}

	// Forward to the real handler for TMS3615 notes + analog sound
	pleiads_sound_control_b_w(address, data, psMemWrite);
}
// ============================================================================
// Color PROM conversion
// ============================================================================

void phoenix_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable,
	const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS_G(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR_G(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	for (i = 0; i < (int)Machine->drv->total_colors; i++)
	{
		int bit0, bit1;

		bit0 = (color_prom[0] >> 0) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 0) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;
		bit0 = (color_prom[0] >> 2) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 2) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;
		bit0 = (color_prom[0] >> 1) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 1) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;

		color_prom++;
	}

	/* first bank of characters use colors 0-31 and 64-95 */
	for (i = 0; i < 8; i++)
	{
		int j;
		for (j = 0; j < 2; j++)
		{
			COLOR_G(0, 4 * i + j * 4 * 8) = i + j * 64;
			COLOR_G(0, 4 * i + j * 4 * 8 + 1) = 8 + i + j * 64;
			COLOR_G(0, 4 * i + j * 4 * 8 + 2) = 2 * 8 + i + j * 64;
			COLOR_G(0, 4 * i + j * 4 * 8 + 3) = 3 * 8 + i + j * 64;
		}
	}

	/* second bank of characters use colors 32-63 and 96-127 */
	for (i = 0; i < 8; i++)
	{
		int j;
		for (j = 0; j < 2; j++)
		{
			COLOR_G(1, 4 * i + j * 4 * 8) = i + 32 + j * 64;
			COLOR_G(1, 4 * i + j * 4 * 8 + 1) = 8 + i + 32 + j * 64;
			COLOR_G(1, 4 * i + j * 4 * 8 + 2) = 2 * 8 + i + 32 + j * 64;
			COLOR_G(1, 4 * i + j * 4 * 8 + 3) = 3 * 8 + i + 32 + j * 64;
		}
	}

#undef TOTAL_COLORS_G
#undef COLOR_G
}

void pleiads_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

	// =========================================================================
	// Step 1: Convert PROM data to RGB palette entries (256 colors)
	//
	// The hardware has two 256x4 PROMs (low bits and high bits).
	// Each color channel (R, G, B) gets 2 bits: one from each PROM.
	// With 270 ohm pulldown and 100 ohm pullup resistors on all lines,
	// the resistor network produces these voltage levels:
	//   bit0=0, bit1=0 -> 0x00 (black)
	//   bit0=1, bit1=0 -> 0x55
	//   bit0=0, bit1=1 -> 0xAA
	//   bit0=1, bit1=1 -> 0xFF (white)
	// =========================================================================
	for (i = 0; i < (int)Machine->drv->total_colors; i++)
	{
		int bit0, bit1;

		bit0 = (color_prom[0] >> 0) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 0) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;
		bit0 = (color_prom[0] >> 2) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 2) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;
		bit0 = (color_prom[0] >> 1) & 0x01;
		bit1 = (color_prom[Machine->drv->total_colors] >> 1) & 0x01;
		*(palette++) = 0x55 * bit0 + 0xaa * bit1;

		color_prom++;
	}

	// =========================================================================
	// Step 2: Build colortable using the bitswap mapping from modern MAME.
	//
	// Modern MAME (0.122u7+) eliminated the colortable entirely and instead
	// does: palette.set_pen_color(i, rgb[bitswap<8>(i, 7,6,5, 1,0, 4,3,2)]);
	//
	// This bitswap remaps pen index i -> PROM address by swapping:
	//   bits [7:5] stay the same  (palette bank + fg/bg select)
	//   bits [4:3] <- bits [1:0]  (pixel value within tile)
	//   bits [2:0] <- bits [4:2]  (code >> 5, the char color group)
	//
	// In our colortable system, pen index = color_group * 4 + pixel.
	// The color_group for Pleiads is:
	//   BG: (code >> 5) | (palette_bank << 4)       -- range 0..7 + bank*16
	//   FG: (code >> 5) | 0x08 | (palette_bank << 4) -- range 8..15 + bank*16
	//
	// We need 64 color groups per GFX layer, 4 pens each = 256 entries per layer.
	// colortable[gfx_start + group*4 + pixel] = PROM palette index
	//
	// The PROM palette index = bitswap of the pen index:
	//   pen = group * 4 + pixel
	//   prom_idx = (pen & 0xE0) | ((pen & 0x03) << 3) | ((pen & 0x1C) >> 2)
	// =========================================================================
	{
		int bg_start = Machine->drv->gfxdecodeinfo[0].color_codes_start;
		int fg_start = Machine->drv->gfxdecodeinfo[1].color_codes_start;

		for (i = 0; i < 64 * 4; i++)
		{
			// Apply the bitswap<8>(i, 7,6,5, 1,0, 4,3,2) mapping:
			// output bit 7 = input bit 7
			// output bit 6 = input bit 6
			// output bit 5 = input bit 5
			// output bit 4 = input bit 1
			// output bit 3 = input bit 0
			// output bit 2 = input bit 4
			// output bit 1 = input bit 3
			// output bit 0 = input bit 2
			int col = (i & 0xE0)               // bits 7,6,5 stay
				| ((i & 0x02) << 3)         // bit 1 -> bit 4
				| ((i & 0x01) << 3)         // bit 0 -> bit 3
				| ((i & 0x10) >> 2)         // bit 4 -> bit 2
				| ((i & 0x08) >> 2)         // bit 3 -> bit 1
				| ((i & 0x04) >> 2);        // bit 2 -> bit 0

			colortable[bg_start + i] = col;
			colortable[fg_start + i] = col;
		}
	}
}

// ============================================================================
// Video hardware
// ============================================================================

static int phoenix_vh_start(void)
{
	ram_page1 = (unsigned char*)std::malloc(0x1000);
	if (!ram_page1) return 1;
	std::memset(ram_page1, 0, 0x1000);

	ram_page2 = (unsigned char*)std::malloc(0x1000);
	if (!ram_page2) return 1;
	std::memset(ram_page2, 0, 0x1000);

	current_ram_page = ram_page1;
	current_ram_page_index = -1;
	bg_scroll = 0;
	palette_bank = 0;
	cocktail_mode = 0;

	videoram_size = 0x0340;
	return generic_vh_start();
}

static void phoenix_vh_stop(void)
{
	if (ram_page1) { std::free(ram_page1); ram_page1 = nullptr; }
	if (ram_page2) { std::free(ram_page2); ram_page2 = nullptr; }
	current_ram_page = nullptr;
	generic_vh_stop();
}

// ============================================================================
// Memory handlers
// ============================================================================

READ_HANDLER(phoenix_paged_ram_r)
{
	return current_ram_page[address];
}

WRITE_HANDLER(phoenix_paged_ram_w)
{
	if ((address >= BACKGROUND_VIDEORAM_OFFSET) &&
		(address < BACKGROUND_VIDEORAM_OFFSET + videoram_size))
	{
		if (data != current_ram_page[address])
			dirtybuffer[address - BACKGROUND_VIDEORAM_OFFSET] = 1;
	}

	current_ram_page[address] = (unsigned char)data;
}

WRITE_HANDLER(phoenix_videoreg_w)
{
	if (current_ram_page_index != (data & 1))
	{
		current_ram_page_index = data & 1;
		current_ram_page = current_ram_page_index ? ram_page2 : ram_page1;

		cocktail_mode = current_ram_page_index && (input_port_3_r(0) & 0x01);

		std::memset(dirtybuffer, 1, videoram_size);
	}

	/* Phoenix: single palette bank bit */
	if (palette_bank != ((data >> 1) & 1))
	{
		palette_bank = (data >> 1) & 1;
		std::memset(dirtybuffer, 1, videoram_size);
	}
}

WRITE_HANDLER(pleiads_videoreg_w)
{
	if (current_ram_page_index != (data & 1))
	{
		current_ram_page_index = data & 1;
		current_ram_page = current_ram_page_index ? ram_page2 : ram_page1;

		cocktail_mode = current_ram_page_index && (input_port_3_r(0) & 0x01);

		std::memset(dirtybuffer, 1, videoram_size);
	}

	/* Pleiads: 2-bit palette bank (NOT inverted) */
	if (palette_bank != ((data >> 1) & 3))
	{
		palette_bank = (data >> 1) & 3;
		std::memset(dirtybuffer, 1, videoram_size);
	}

	pleiads_protection_question = data & 0xfc;

	/* send two bits to sound control C */
	pleiads_audio_control_c_w(data);
}

WRITE_HANDLER(phoenix_scroll_w)
{
	bg_scroll = (unsigned char)data;
}

// ============================================================================
// Input port read handlers
// ============================================================================

READ_HANDLER(phoenix_input_port_0_r)
{
	if (cocktail_mode)
		return (input_port_0_r(0) & 0x07) | (input_port_1_r(0) & 0xf8);
	else
		return input_port_0_r(0);
}

READ_HANDLER(pleiads_input_port_0_r)
{
	int ret = phoenix_input_port_0_r(0, 0) & 0xf7;

	switch (pleiads_protection_question)
	{
	case 0x00:
	case 0x20:
		break;
	case 0x0c:
	case 0x30:
		ret |= 0x08;
		break;
	default:
		LOG_INFO("Unknown protection question %02X at %04X", pleiads_protection_question, cpu_getpc());
	}
	return ret;
}

// ============================================================================
// Screen refresh 
// ============================================================================

static void phoenix_vh_screenrefresh(void)
{
	int offs;

	/* Background layer: draw dirty chars into tmpbitmap */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, code;

			dirtybuffer[offs] = 0;

			code = current_ram_page[offs + BACKGROUND_VIDEORAM_OFFSET];

			sx = offs % 32;
			sy = offs / 32;

			drawgfx(tmpbitmap, Machine->gfx[0],
				code,
				(code >> 5) + 8 * palette_bank,
				0, 0,
				8 * sx, 8 * sy,
				0, TRANSPARENCY_NONE, 0);
		}
	}

	/* Copy background with horizontal scroll */
	{
		int scroll = -bg_scroll;
		copyscrollbitmap(main_bitmap, tmpbitmap,
			1, &scroll, 0, 0,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	/* Foreground layer: drawn directly to main_bitmap */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int sx, sy, code;

		code = current_ram_page[offs];

		sx = offs % 32;
		sy = offs / 32;

		if (sx >= 1)
		{
			drawgfx(main_bitmap, Machine->gfx[1],
				code,
				(code >> 5) + 8 * palette_bank,
				0, 0,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
		}
		else
		{
			/* First column is opaque (no transparency) */
			drawgfx(main_bitmap, Machine->gfx[1],
				code,
				(code >> 5) + 8 * palette_bank,
				0, 0,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}
}

// ============================================================================
// Screen refresh for Pleiads (uses different color attribute formula)
//
// Modern MAME (0.122u7+) uses:
//   BG color: (code >> 5) | 0x00 | (palette_bank << 4)
//   FG color: (code >> 5) | 0x08 | (palette_bank << 4)
//
// The 0x08 bit distinguishes FG from BG in the palette mapping.
// palette_bank is 2 bits (0-3), shifted left 4 = range 0,16,32,48.
// ============================================================================

static void pleiads_vh_screenrefresh(void)
{
	int offs;

	/* Background layer: draw dirty chars into tmpbitmap */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, code;

			dirtybuffer[offs] = 0;

			code = current_ram_page[offs + BACKGROUND_VIDEORAM_OFFSET];

			sx = offs % 32;
			sy = offs / 32;

			drawgfx(tmpbitmap, Machine->gfx[0],
				code,
				(code >> 5) | (palette_bank << 4),
				0, 0,
				8 * sx, 8 * sy,
				0, TRANSPARENCY_NONE, 0);
		}
	}

	/* Copy background with horizontal scroll */
	{
		int scroll = -bg_scroll;
		copyscrollbitmap(main_bitmap, tmpbitmap,
			1, &scroll, 0, 0,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	/* Foreground layer: drawn directly to main_bitmap */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int sx, sy, code;

		code = current_ram_page[offs];

		sx = offs % 32;
		sy = offs / 32;

		if (sx >= 1)
		{
			drawgfx(main_bitmap, Machine->gfx[1],
				code,
				(code >> 5) | 0x08 | (palette_bank << 4),
				0, 0,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
		}
		else
		{
			/* First column is opaque (no transparency) */
			drawgfx(main_bitmap, Machine->gfx[1],
				code,
				(code >> 5) | 0x08 | (palette_bank << 4),
				0, 0,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}
}

// ============================================================================
// Interrupt handler
// Phoenix uses VBLANK-driven interrupts via the 8085's RST 7.5 input.
// The MAME driver uses ignore_interrupt (no CPU interrupt generated).
// Instead, the hardware generates video timing directly.
// We trigger NMI each frame to drive game logic.
// ============================================================================

void phoenix_interrupt(void)
{
	/* The original hardware uses a simple VBLANK interrupt.
	   8085A RST 7.5 is directly wired to VBLANK.
	   In AAE with CPU_8080 mode, we fire INT each frame. */
	// TODO: Fix this
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

// ============================================================================
// GFX decode
// ============================================================================

static struct GfxLayout phoenix_charlayout =
{
	8, 8,       /* 8x8 characters */
	256,        /* 256 characters */
	2,          /* 2 bits per pixel */
	{ 256 * 8 * 8, 0 },
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8
};

struct GfxDecodeInfo phoenix_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &phoenix_charlayout,     0, 16 },
	{ REGION_GFX2, 0, &phoenix_charlayout, 16 * 4, 16 },
	{ -1 }
};

struct GfxDecodeInfo pleiads_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &phoenix_charlayout,       0, 64 },
	{ REGION_GFX2, 0, &phoenix_charlayout, 64 * 4, 64 },
	{ -1 }
};

// ============================================================================
// TMS36XX interface structures
// ============================================================================

static struct TMS36XXinterface phoenix_tms36xx_interface =
{
	MM6221AA,       /* subtype */
	372,            /* base frequency */
	{ 0.50, 0, 0, 1.05, 0, 0 },   /* decay times */
	0.21            /* tune speed */
};

static struct TMS36XXinterface pleiads_tms36xx_interface =
{
	TMS3615,        /* subtype */
	247,            /* base frequency (one octave below A) */
	{ 0.33, 0.33, 0, 0.33, 0, 0.33 },   /* decay times */
	0.0             /* tune speed (not used for TMS3615 single notes) */
};

// ============================================================================
// Memory maps
// ============================================================================

MEM_READ(phoenix_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x4000, 0x4fff, phoenix_paged_ram_r)
MEM_ADDR(0x7000, 0x73ff, phoenix_input_port_0_r)
MEM_ADDR(0x7800, 0x7bff, ip_port_2_r)
MEM_END

MEM_READ(pleiads_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x4000, 0x4fff, phoenix_paged_ram_r)
MEM_ADDR(0x7000, 0x73ff, pleiads_input_port_0_r)
MEM_ADDR(0x7800, 0x7bff, ip_port_2_r)
MEM_END

MEM_WRITE(phoenix_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x4fff, phoenix_paged_ram_w)
MEM_ADDR(0x5000, 0x53ff, phoenix_videoreg_w)
MEM_ADDR(0x5800, 0x5bff, phoenix_scroll_w)
MEM_ADDR(0x6000, 0x63ff, phoenix_sound_control_a_w)
MEM_ADDR(0x6800, 0x6bff, phoenix_sound_control_b_w)
MEM_END

MEM_WRITE(pleiads_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x4fff, phoenix_paged_ram_w)
MEM_ADDR(0x5000, 0x53ff, pleiads_videoreg_w)
MEM_ADDR(0x5800, 0x5bff, phoenix_scroll_w)
MEM_ADDR(0x6000, 0x63ff, pleiads_sound_control_a_w)
MEM_ADDR(0x6800, 0x6bff, pleiads_sound_control_b_wrapper)
MEM_END

// ============================================================================
// Input ports
// ============================================================================

INPUT_PORTS_START(phoenix)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON2)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "3K 30K")
PORT_DIPSETTING(0x04, "4K 40K")
PORT_DIPSETTING(0x08, "5K 50K")
PORT_DIPSETTING(0x0c, "6K 60K")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x10, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_VBLANK)

PORT_START("CAB")   /* fake port for cabinet dip */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x01, DEF_STR(Cocktail))
INPUT_PORTS_END

INPUT_PORTS_START(pleiads)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)    /* protection */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON2)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "3K 30K")
PORT_DIPSETTING(0x04, "4K 40K")
PORT_DIPSETTING(0x08, "5K 50K")
PORT_DIPSETTING(0x0c, "6K 60K")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x10, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_VBLANK)

PORT_START("CAB")   /* fake port for cabinet dip */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x01, DEF_STR(Cocktail))
INPUT_PORTS_END

// ============================================================================
// ROMs
// ============================================================================

ROM_START(phoenix)
ROM_REGION(0x10000, REGION_CPU1, 0)    /* 64k for code */
ROM_LOAD("ic45", 0x0000, 0x0800, CRC(9f68086b) SHA1(fc3cef299bf03bf0586c4047c6b96ca666846220))
ROM_LOAD("ic46", 0x0800, 0x0800, CRC(273a4a82) SHA1(6f3019a074e73ff50ceb92f655fcf15659f34919))
ROM_LOAD("ic47", 0x1000, 0x0800, CRC(3d4284b9) SHA1(6e69f8f0d537fe89140cd95d2398531d7e93d102))
ROM_LOAD("ic48", 0x1800, 0x0800, CRC(cb5d9915) SHA1(49bcf55a5721cfcc02c3b811a4b601e35ea576db))
ROM_LOAD("h5-ic49.5a", 0x2000, 0x0800, CRC(a105e4e7) SHA1(b35142a91b6b7fdf7535202671793393c9f4685f))
ROM_LOAD("h6-ic50.6a", 0x2800, 0x0800, CRC(ac5e9ec1) SHA1(0402e5241d99759d804291998efd43f37ce99917))
ROM_LOAD("h7-ic51.7a", 0x3000, 0x0800, CRC(2eab35b4) SHA1(849bf8273317cc869bdd67e50c68399ee8ece81d))
ROM_LOAD("h8-ic52.8a", 0x3800, 0x0800, CRC(aff8e9c5) SHA1(e4164f85ec12d4d9bcbffba27ab1f51b3599f6d0))

ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("ic23.3d", 0x0000, 0x0800, CRC(3c7e623f) SHA1(e7ff5fc371664af44785c079e92eeb2d8530187b))
ROM_LOAD("ic24.4d", 0x0800, 0x0800, CRC(59916d3b) SHA1(71aec70a8e096ed1f0c2297b3ae7dca1b8ecc38d))

ROM_REGION(0x1000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("b1-ic39.3b", 0x0000, 0x0800, CRC(53413e8f) SHA1(d772358505b973b10da840d204afb210c0c746ec))
ROM_LOAD("b2-ic40.4b", 0x0800, 0x0800, CRC(0be2ba91) SHA1(af9243ee23377b632b9b7d0b84d341d06bf22480))

ROM_REGION(0x0200, REGION_PROMS, 0)
ROM_LOAD("mmi6301.ic40", 0x0000, 0x0100, CRC(79350b25) SHA1(57411be4c1d89677f7919ae295446da90612c8a8))  /* palette low bits */
ROM_LOAD("mmi6301.ic41", 0x0100, 0x0100, CRC(e176b768) SHA1(e2184dd495ed579f10b6da0b78379e02d7a6229f))  /* palette high bits */
ROM_END

ROM_START(pleiads)
ROM_REGION(0x10000, REGION_CPU1, 0)    /* 64k for code */
ROM_LOAD("ic47.r1", 0x0000, 0x0800, CRC(960212c8) SHA1(52a3232e99920805ce9e195b8a6338ae7044dd18))
ROM_LOAD("ic48.r2", 0x0800, 0x0800, CRC(b254217c) SHA1(312a33cca09d5d2d18992f28eb051230a90db6e3))
ROM_LOAD("ic47.bin", 0x1000, 0x0800, CRC(87e700bb) SHA1(0f352b5461da957c564920fd1da83bc81f41ffb9))   /* IC 49 on real board */
ROM_LOAD("ic48.bin", 0x1800, 0x0800, CRC(2d5198d0) SHA1(6bfdc6c965199c5d4d687fe35dda057ec38cd8e0))   /* IC 50 on real board */
ROM_LOAD("ic51.r5", 0x2000, 0x0800, CRC(49c629bc) SHA1(fd7937d0c114c8d9c1efaa9918ae3df2af41f032))
ROM_LOAD("ic50.bin", 0x2800, 0x0800, CRC(f1a8a00d) SHA1(5c183e3a73fa882ffec3cb9219fb5619e625591a))   /* IC 52 on real board */
ROM_LOAD("ic53.r7", 0x3000, 0x0800, CRC(b5f07fbc) SHA1(2ae687c84732942e69ad4dfb7a4ac1b97b77487a))
ROM_LOAD("ic52.bin", 0x3800, 0x0800, CRC(b1b5a8a6) SHA1(7e4ef298c8ddefc7dc0cbf94a9c9f36a4b807ba0))   /* IC 54 on real board */

ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("ic23.bin", 0x0000, 0x0800, CRC(4e30f9e7) SHA1(da023a94725dc40107cd97e4decfd4dc0f9f00ee))   /* IC 45 on real board */
ROM_LOAD("ic24.bin", 0x0800, 0x0800, CRC(5188fc29) SHA1(421dedc674c6dde7abf01412df035a8eb8e6db9b))   /* IC 44 on real board */

ROM_REGION(0x1000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("ic39.bin", 0x0000, 0x0800, CRC(85866607) SHA1(cd240bd056f761b2f9e2142049434f02cae3e315))   /* IC 27 on real board */
ROM_LOAD("ic40.bin", 0x0800, 0x0800, CRC(a841d511) SHA1(8349008ab1d8ef08775b54170c37deb1d391fffc))   /* IC 26 on real board */

ROM_REGION(0x0200, REGION_PROMS, 0)
ROM_LOAD("7611-5.26", 0x0000, 0x0100, CRC(7a1bcb1e) SHA1(bdfab316ea26e2063879e7aa78b6ae2b55eb95c8))   /* palette low bits */
ROM_LOAD("7611-5.33", 0x0100, 0x0100, CRC(e38eeb83) SHA1(252880d80425b2e697146e76efdc6cb9f3ba0378))   /* palette high bits */
ROM_END

// ============================================================================
// init / run / end
// ============================================================================

int init_phoenix(void) {
	phoenix_vh_start();
	tms36xx_sh_start(&phoenix_tms36xx_interface);
	phoenix_audio_sh_start();
	return 0;
}

void run_phoenix(void) {
	watchdog_reset_w(0, 0, 0);
	phoenix_vh_screenrefresh();
	tms36xx_sh_update();       // Update melody
	phoenix_audio_sh_update(); // Update analog tones
}

void end_phoenix(void) {
	phoenix_audio_sh_stop();
	tms36xx_sh_stop();
	phoenix_vh_stop();
}
// --- PLEIADS ---
int init_pleiads(void) {
	phoenix_vh_start();
	tms36xx_sh_start(&pleiads_tms36xx_interface);
	pleiads_audio_sh_start();
	pleiads_sod_hooked = false;
	pleiads_current_melody = -1;
	return 0;
}

void run_pleiads(void) {
	// Hook the SOD callback once the i8085 CPU instance exists (created after init)
	if (!pleiads_sod_hooked && m_cpu_i8085[0]) {
		m_cpu_i8085[0]->set_SOD_callback(pleiads_sod_callback);
		pleiads_sod_hooked = true;
	}
	watchdog_reset_w(0, 0, 0);
	pleiads_vh_screenrefresh();
	tms36xx_sh_update();        // Update melody
	pleiads_audio_sh_update();  // Update analog tones
}
void end_pleiads(void) {
	pleiads_melody_stop();
	if (pleiads_sod_hooked && m_cpu_i8085[0]) {
		m_cpu_i8085[0]->set_SOD_callback(nullptr);
	}
	pleiads_sod_hooked = false;
	pleiads_audio_sh_stop();
	tms36xx_sh_stop();
	phoenix_vh_stop();
}
// ============================================================================
// Driver descriptors
// ============================================================================

// --- Phoenix (Amstar) ---
AAE_DRIVER_BEGIN(drv_phoenix, "phoenix", "Phoenix (Amstar)")
AAE_DRIVER_ROM(rom_phoenix)
AAE_DRIVER_FUNCS(&init_phoenix, &run_phoenix, &end_phoenix)
AAE_DRIVER_INPUT(input_ports_phoenix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_8085,
		/*freq*/     11000000 / 4,   /* 2.75 MHz */
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &phoenix_interrupt,
		/*r8*/       phoenix_readmem,
		/*w8*/       phoenix_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 2500, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 207)   /* 32*8 x 26*8 visible */
AAE_DRIVER_RASTER(phoenix_gfxdecodeinfo, 256, 16 * 4 + 16 * 4, phoenix_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_phoenix)

// --- Pleiads (Tehkan) ---
AAE_DRIVER_BEGIN(drv_pleiads, "pleiads", "Pleiads (Tehkan)")
AAE_DRIVER_ROM(rom_pleiads)
AAE_DRIVER_FUNCS(&init_pleiads, &run_pleiads, &end_pleiads)
AAE_DRIVER_INPUT(input_ports_pleiads)
AAE_DRIVER_SAMPLES(pleiads_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_8085,
		/*freq*/     11000000 / 4,   /* 2.75 MHz */
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &phoenix_interrupt,
		/*r8*/       pleiads_readmem,
		/*w8*/       pleiads_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 2500, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 207)   /* 32*8 x 26*8 visible */
AAE_DRIVER_RASTER(pleiads_gfxdecodeinfo, 256, 64 * 4 + 64 * 4, pleiads_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_pleiads)