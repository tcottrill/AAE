//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================
// Moon Patrol (Irem M52, 1982).  Original MAME driver by Nicola Salmoria.
//   CPU0  Z80    main  @ 3.072 MHz  (IRQ/frame)
//   CPU1  M6803  sound @ 3.579545/4 MHz (Irem sound board: 2xAY8910 +
//                2xMSM5205 ADPCM; see sndhrdwr/irem_snd.cpp)
// Raster video: 8x8 chars (2bpp, row-scrolled play area), 16x16 sprites
// (2bpp, CLUT PROM), and three pre-rendered 256x64 background strips
// (mountains / hills / cityscape) with independent x/y positions.
//
// Ported from MAME 0.57 drivers/mpatrol.c + vidhrdw/mpatrol.c. The
// protection read uses the 0.286 m52.cpp implementation (popcount of the
// bg1 x-position) instead of 0.57's Z80 DE register peek. Flip screen /
// cocktail mode is not implemented (upright only).
//
// ROM sets use the MAME 0.286 file names / CRCs, so a current mpatrol.zip
// loads directly. NOTE the char ROMs load in 0.57 plane order (3e then 3f),
// which is the REVERSE of the 0.286 load order - 0.286 flipped both the
// load order and the plane offsets, so the net decode is identical.
//==========================================================================

#include "aae_mame_driver.h"
#include "mixer.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "memory.h"
#include "old_mame_raster.h"
#include "sound_latch.h"
#include "irem_snd.h"

#define BGHEIGHT 64

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static unsigned char scrollreg[16];
static unsigned char bg1xpos, bg1ypos, bg2xpos, bg2ypos, bgcontrol;
static struct osd_bitmap* bgbitmap[3] = { 0, 0, 0 };

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void mpatrol_interrupt() { cpu_do_int_imm(CPU0, INT_TYPE_INT); }
void mpatrol_snd_noop_interrupt() {}   // 6803: IRQ from sound cmd, NMI from ADPCM

// ---------------------------------------------------------------------------
// Main-CPU handlers
// ---------------------------------------------------------------------------
WRITE_HANDLER(mpatrol_videoram_w) { videoram_w(address, data); }
WRITE_HANDLER(mpatrol_colorram_w) { colorram_w(address, data); }

// Protection: a custom chip on the scroll board mangles the value written to
// the bg1 x-position port; the game checks the result. From MAME 0.286:
//   result = popcount(value & 0x7f) ^ (value >> 7)
READ_HANDLER(mpatrol_protection_r)
{
	int popcount = 0;
	for (int temp = bg1xpos & 0x7f; temp != 0; temp >>= 1)
		popcount += temp & 1;
	return (UINT8)(popcount ^ (bg1xpos >> 7));
}

// DSW1 read: the upper 4 bits come from the fake coin-mode port, selected by
// the "Coin Mode" switch in DSW2.
READ_HANDLER(mpatrol_input_port_3_r)
{
	int ret = readinputport(3);

	if (readinputport(4) & 0x04)
		ret |= (readinputport(5) << 4);     // Mode 1
	else
		ret |= (readinputport(5) & 0xf0);   // Mode 2

	return (UINT8)ret;
}

WRITE_HANDLER(mpatrol_flipscreen_w)
{
	// Flip screen (bit 0, combined with the DSW) is not implemented -
	// upright only. Bits 1 and 5 are the coin counters; nothing to do.
	(void)data;
}

// ---- Z80 I/O ports (scroll board) ----
static UINT16 mpatrol_port_nop_r(UINT16, struct z80PortRead*) { return 0; }

static void mpatrol_scroll_w(UINT16 port, UINT8 data, struct z80PortWrite*)
{
	scrollreg[port & 0x0f] = data;
}
static void mpatrol_bg1xpos_w(UINT16, UINT8 data, struct z80PortWrite*) { bg1xpos = data; }
static void mpatrol_bg1ypos_w(UINT16, UINT8 data, struct z80PortWrite*) { bg1ypos = data; }
static void mpatrol_bg2xpos_w(UINT16, UINT8 data, struct z80PortWrite*) { bg2xpos = data; }
static void mpatrol_bg2ypos_w(UINT16, UINT8 data, struct z80PortWrite*) { bg2ypos = data; }
static void mpatrol_bgcontrol_w(UINT16, UINT8 data, struct z80PortWrite*) { bgcontrol = data; }
static void mpatrol_port_nop_w(UINT16, UINT8, struct z80PortWrite*) {}

// ---------------------------------------------------------------------------
// GFX
// ---------------------------------------------------------------------------
static struct GfxLayout mpatrol_charlayout =
{
	8, 8,           // 8x8 characters
	512,            // 512 characters
	2,              // 2 bits per pixel
	{ 0, 512 * 8 * 8 },                       // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8
};

static struct GfxLayout mpatrol_spritelayout =
{
	16, 16,         // 16x16 sprites
	128,            // 128 sprites
	2,              // 2 bits per pixel
	{ 0, 128 * 16 * 16 },                     // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7,
		16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
		8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
	32 * 8          // every sprite takes 32 consecutive bytes
};

// 32x32 slices of the 256x64 background images; the two bitplanes for 4
// pixels are packed into one byte.
static struct GfxLayout mpatrol_bgcharlayout =
{
	32, 32,         // 32x32 "characters" (8 of them = one 256x32 half-image)
	8,
	2,
	{ 4, 0 },
	{ 0, 1, 2, 3, 8 + 0, 8 + 1, 8 + 2, 8 + 3, 2 * 8 + 0, 2 * 8 + 1, 2 * 8 + 2, 2 * 8 + 3, 3 * 8 + 0, 3 * 8 + 1, 3 * 8 + 2, 3 * 8 + 3,
		4 * 8 + 0, 4 * 8 + 1, 4 * 8 + 2, 4 * 8 + 3, 5 * 8 + 0, 5 * 8 + 1, 5 * 8 + 2, 5 * 8 + 3, 6 * 8 + 0, 6 * 8 + 1, 6 * 8 + 2, 6 * 8 + 3, 7 * 8 + 0, 7 * 8 + 1, 7 * 8 + 2, 7 * 8 + 3 },
	{ 0 * 512, 1 * 512, 2 * 512, 3 * 512, 4 * 512, 5 * 512, 6 * 512, 7 * 512, 8 * 512, 9 * 512, 10 * 512, 11 * 512, 12 * 512, 13 * 512, 14 * 512, 15 * 512,
		16 * 512, 17 * 512, 18 * 512, 19 * 512, 20 * 512, 21 * 512, 22 * 512, 23 * 512, 24 * 512, 25 * 512, 26 * 512, 27 * 512, 28 * 512, 29 * 512, 30 * 512, 31 * 512 },
	8 * 8
};

struct GfxDecodeInfo mpatrol_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &mpatrol_charlayout,                   0, 64 },
	{ REGION_GFX2, 0x0000, &mpatrol_spritelayout,            64 * 4, 16 },
	{ REGION_GFX3, 0x0000, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 0 * 4, 1 },  // mountains top
	{ REGION_GFX3, 0x0800, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 0 * 4, 1 },  // mountains bottom
	{ REGION_GFX4, 0x0000, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 1 * 4, 1 },  // hills top
	{ REGION_GFX4, 0x0800, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 1 * 4, 1 },  // hills bottom
	{ REGION_GFX5, 0x0000, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 2 * 4, 1 },  // city top
	{ REGION_GFX5, 0x0800, &mpatrol_bgcharlayout, 64 * 4 + 16 * 4 + 2 * 4, 1 },  // city bottom
	{ -1 }
};

/***************************************************************************

  Moon Patrol PROMs (0.286 layout in REGION_PROMS):
    0x000-0x1ff  mpc-4.2a  character palette (512x8; the game's colorram
                           only reaches codes 0-31, i.e. the first 128 bytes)
    0x200-0x21f  mpc-3.1m  background palette (32x8)
    0x220-0x23f  mpc-1.1f  sprite palette (32x8; R and B swapped)
    0x240-0x33f  mpc-2.2h  sprite lookup table (256x4, half unused)

  Character/background PROM bits: R = bits 0-2, G = 3-5, B = 6-7.
  Sprite PROM: R = bits 6-7, G = 3-5, B = 0-2.

  Palette layout: 0-127 characters, 128-159 background (entry 128 is the
  reserved transparent color), 160-191 sprites.

***************************************************************************/
void mpatrol_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	const unsigned char* char_pal = color_prom + 0x000;
	const unsigned char* bg_pal   = color_prom + 0x200;
	const unsigned char* spr_pal  = color_prom + 0x220;
	const unsigned char* spr_clut = color_prom + 0x240;

	// character palette (128 colors)
	for (i = 0; i < 128; i++)
	{
		int bit0, bit1, bit2;
		unsigned char p = char_pal[i];

		bit0 = (p >> 0) & 1; bit1 = (p >> 1) & 1; bit2 = (p >> 2) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;   // red
		bit0 = (p >> 3) & 1; bit1 = (p >> 4) & 1; bit2 = (p >> 5) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;   // green
		bit1 = (p >> 6) & 1; bit2 = (p >> 7) & 1;
		*(palette++) = 0x47 * bit1 + 0x97 * bit2;                 // blue
	}

	// character lookup table: color codes 0-31 opaque, 32-63 the same colors
	// but with pen 0 transparent (used for the scrolled play-area rows)
	for (i = 0; i < 128; i++)
	{
		COLOR(0, i) = (unsigned char)i;
		if (i % 4 == 0) COLOR(0, i + 128) = 0;
		else            COLOR(0, i + 128) = (unsigned char)i;
	}

	// background palette: entry 0 is replaced by the reserved transparent
	// color (no game color has these RGB components)
	*(palette++) = 1; *(palette++) = 1; *(palette++) = 1;
	for (i = 1; i < 32; i++)
	{
		int bit0, bit1, bit2;
		unsigned char p = bg_pal[i];

		bit0 = (p >> 0) & 1; bit1 = (p >> 1) & 1; bit2 = (p >> 2) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (p >> 3) & 1; bit1 = (p >> 4) & 1; bit2 = (p >> 5) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit1 = (p >> 6) & 1; bit2 = (p >> 7) & 1;
		*(palette++) = 0x47 * bit1 + 0x97 * bit2;
	}

	// sprite palette (RED and BLUE swapped wrt the usual configuration)
	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;
		unsigned char p = spr_pal[i];

		bit1 = (p >> 6) & 1; bit2 = (p >> 7) & 1;
		*(palette++) = 0x47 * bit1 + 0x97 * bit2;                 // red
		bit0 = (p >> 3) & 1; bit1 = (p >> 4) & 1; bit2 = (p >> 5) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;   // green
		bit0 = (p >> 0) & 1; bit1 = (p >> 1) & 1; bit2 = (p >> 2) & 1;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;   // blue
	}

	// sprite lookup table (16 codes x 4 pens; half of the PROM is unused)
	{
		const unsigned char* cp = spr_clut;
		for (i = 0; i < 16 * 4; i++)
		{
			COLOR(1, i) = (unsigned char)(128 + 32 + *cp++);
			if (i % 4 == 3) cp += 4;
		}
	}

	// background color codes. The 32x8 PROM has many colors repeated; the
	// address of the colors to pick: xbb00 = mountains, 0xxbb = hills,
	// 1xxbb = city.
	COLOR(2, 0) = 128;
	COLOR(2, 1) = 128 + 4;
	COLOR(2, 2) = 128 + 8;
	COLOR(2, 3) = 128 + 12;
	COLOR(4, 0) = 128;
	COLOR(4, 1) = 128 + 1;
	COLOR(4, 2) = 128 + 2;
	COLOR(4, 3) = 128 + 3;
	COLOR(6, 0) = 128;
	COLOR(6, 1) = 128 + 16 + 1;
	COLOR(6, 2) = 128 + 16 + 2;
	COLOR(6, 3) = 128 + 16 + 3;
#undef COLOR
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
int mpatrol_vh_start(void)
{
	int i, j;

	videoram_size = 0x400;
	if (generic_vh_start() != 0)
		return 1;

	// pre-render the three 256x64 background strips
	for (i = 0; i < 3; i++)
	{
		if ((bgbitmap[i] = osd_create_bitmap(256, BGHEIGHT)) == 0)
		{
			generic_vh_stop();
			return 1;
		}

		for (j = 0; j < 8; j++)
		{
			drawgfx(bgbitmap[i], Machine->gfx[2 + 2 * i],
				j, 0, 0, 0,
				32 * j, 0,
				0, TRANSPARENCY_NONE, 0);

			drawgfx(bgbitmap[i], Machine->gfx[2 + 2 * i + 1],
				j, 0, 0, 0,
				32 * j, (BGHEIGHT / 2),
				0, TRANSPARENCY_NONE, 0);
		}
	}

	return 0;
}

static void mpatrol_free_bgbitmaps(void)
{
	for (int i = 0; i < 3; i++)
	{
		if (bgbitmap[i]) {
			osd_free_bitmap(bgbitmap[i]);
			bgbitmap[i] = 0;
		}
	}
}

// Clamp a y-span into the visible bitmap and reject empty spans.
static int mpatrol_get_clip(struct rectangle* clip, int min_y, int max_y)
{
	clip->min_x = Machine->drv->visible_area.min_x;
	clip->max_x = Machine->drv->visible_area.max_x;

	if (min_y < 0) min_y = 0;
	if (max_y > 255) max_y = 255;
	if (max_y < min_y) return 0;

	clip->min_y = min_y;
	clip->max_y = max_y;
	return 1;
}

static void mpatrol_draw_background(int xpos, int ypos, int ypos_end, int image, int transparency)
{
	struct rectangle clip1, clip2;

	if (mpatrol_get_clip(&clip1, ypos, ypos + BGHEIGHT - 1))
	{
		copybitmap(main_bitmap, bgbitmap[image], 0, 0, xpos, ypos, &clip1, transparency, 128);
		copybitmap(main_bitmap, bgbitmap[image], 0, 0, xpos - 256, ypos, &clip1, transparency, 128);
	}

	// solid ground fill below the strip, in the strip's color 3
	if (mpatrol_get_clip(&clip2, ypos + BGHEIGHT, ypos_end))
		fillbitmap(main_bitmap, Machine->gfx[image * 2 + 2]->colortable[3], &clip2);
}

void mpatrol_vh_screenrefresh()
{
	int offs, i;

	// redraw dirty characters into the tile buffer
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, color;
			dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;

			color = colorram[offs] & 0x1f;
			if (sy >= 7) color += 32;   // lines 7-31 use the transparent codes

			drawgfx(tmpbitmap, Machine->gfx[0],
				videoram[offs] + 2 * (colorram[offs] & 0x80),
				color,
				0, 0,
				8 * sx, 8 * sy,
				0, TRANSPARENCY_NONE, 0);
		}
	}

	// the background strips
	if ((bgcontrol == 0x04) || (bgcontrol == 0x03))
	{
		struct rectangle clip;

		// sky above the far mountains
		if (mpatrol_get_clip(&clip, 7 * 8, bg2ypos - 1))
			fillbitmap(main_bitmap, 0, &clip);

		mpatrol_draw_background(bg2xpos, bg2ypos, bg1ypos + BGHEIGHT - 1, 0, TRANSPARENCY_NONE);
		mpatrol_draw_background(bg1xpos, bg1ypos, Machine->drv->visible_area.max_y,
			(bgcontrol == 0x04) ? 1 : 2, TRANSPARENCY_COLOR);
	}
	else
		fillbitmap(main_bitmap, 0, &Machine->drv->visible_area);

	// the character layer: top 7 rows fixed, the rest row-scrolled over the
	// backgrounds (pen 0 transparent there via the +32 color codes)
	{
		int scroll[32];
		struct rectangle clip;

		clip.min_x = Machine->drv->visible_area.min_x;
		clip.max_x = Machine->drv->visible_area.max_x;

		clip.min_y = 0;
		clip.max_y = 7 * 8 - 1;
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &clip, TRANSPARENCY_NONE, 0);

		clip.min_y = 7 * 8;
		clip.max_y = 32 * 8 - 1;

		for (i = 0; i < 32; i++)
			scroll[i] = scrollreg[i / 2];

		copyscrollbitmap(main_bitmap, tmpbitmap, 32, scroll, 0, 0, &clip, TRANSPARENCY_COLOR, 0);
	}

	// sprites: bank 2 (0xc8a0) first, then bank 1 (0xc820) on top
	for (offs = spriteram_size - 4; offs >= 0; offs -= 4)
	{
		if (spriteram_2)
			drawgfx(main_bitmap, Machine->gfx[1],
				spriteram_2[offs + 2],
				spriteram_2[offs + 1] & 0x3f,
				spriteram_2[offs + 1] & 0x40, spriteram_2[offs + 1] & 0x80,
				spriteram_2[offs + 3], 241 - spriteram_2[offs],
				&Machine->drv->visible_area, TRANSPARENCY_COLOR, 128 + 32);
	}
	for (offs = spriteram_size - 4; offs >= 0; offs -= 4)
	{
		drawgfx(main_bitmap, Machine->gfx[1],
			spriteram[offs + 2],
			spriteram[offs + 1] & 0x3f,
			spriteram[offs + 1] & 0x40, spriteram[offs + 1] & 0x80,
			spriteram[offs + 3], 241 - spriteram[offs],
			&Machine->drv->visible_area, TRANSPARENCY_COLOR, 128 + 32);
	}
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------
MEM_READ(MpatrolMainRead)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x8800, 0x8800, mpatrol_protection_r)
MEM_ADDR(0xd000, 0xd000, ip_port_0_r)             // IN0
MEM_ADDR(0xd001, 0xd001, ip_port_1_r)             // IN1
MEM_ADDR(0xd002, 0xd002, ip_port_2_r)             // IN2
MEM_ADDR(0xd003, 0xd003, mpatrol_input_port_3_r)  // DSW1 (+ coin mode bits)
MEM_ADDR(0xd004, 0xd004, ip_port_4_r)             // DSW2
MEM_END

MEM_WRITE(MpatrolMainWrite)
MEM_ADDR(0x0000, 0x7fff, MWA_ROM)
MEM_ADDR(0x8000, 0x83ff, mpatrol_videoram_w)
MEM_ADDR(0x8400, 0x87ff, mpatrol_colorram_w)
MEM_ADDR(0xd000, 0xd000, irem_sound_cmd_w)
MEM_ADDR(0xd001, 0xd001, mpatrol_flipscreen_w)    // + coin counters
MEM_END

PORT_READ(MpatrolMainPortRead)
PORT_ADDR(0x00, 0xff, mpatrol_port_nop_r)
PORT_END

PORT_WRITE(MpatrolMainPortWrite)
PORT_ADDR(0x10, 0x1f, mpatrol_scroll_w)
PORT_ADDR(0x40, 0x40, mpatrol_bg1xpos_w)
PORT_ADDR(0x60, 0x60, mpatrol_bg1ypos_w)
PORT_ADDR(0x80, 0x80, mpatrol_bg2xpos_w)
PORT_ADDR(0xa0, 0xa0, mpatrol_bg2ypos_w)
PORT_ADDR(0xc0, 0xc0, mpatrol_bgcontrol_w)
PORT_ADDR(0x00, 0xff, mpatrol_port_nop_w)         // catch-all (Z80 core has no null check)
PORT_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
int init_mpatrol()
{
	LOG_INFO("Starting Moon Patrol Init");

	memset(scrollreg, 0, sizeof(scrollreg));
	bg1xpos = bg1ypos = bg2xpos = bg2ypos = bgcontrol = 0;

	// Flat RAM lives in the CPU0 region; point the framework globals at it.
	videoram = &Machine->memory_region[CPU0][0x8000];
	videoram_size = 0x400;
	colorram = &Machine->memory_region[CPU0][0x8400];
	spriteram = &Machine->memory_region[CPU0][0xc820];
	spriteram_size = 0x60;
	spriteram_2 = &Machine->memory_region[CPU0][0xc8a0];

	if (irem_sound_start() != 0)
		return 1;

	if (mpatrol_vh_start() != 0)
		return 1;

	LOG_INFO("End Moon Patrol Init");
	return 0;
}

void run_mpatrol()
{
	watchdog_reset_w(0, 0, 0);
	mpatrol_vh_screenrefresh();
	irem_sound_update();
}

void end_mpatrol()
{
	irem_sound_stop();
	mpatrol_free_bgbitmaps();
	generic_vh_stop();
}

// ---------------------------------------------------------------------------
// Input ports
// ---------------------------------------------------------------------------
INPUT_PORTS_START(mpatrol)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START2)
// coin input must be active for ~17 frames to be consistently recognized
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_COIN3, 17)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN2")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x02, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x02, "3")
PORT_DIPSETTING(0x03, "5")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x0c, "10000 30000 50000")
PORT_DIPSETTING(0x08, "20000 40000 60000")
PORT_DIPSETTING(0x04, "10000")
PORT_DIPSETTING(0x00, "None")
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)   // filled in from the coin mode port

PORT_START("DSW1")
PORT_DIPNAME(0x01, 0x01, DEF_STR(Flip_Screen))
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x02, DEF_STR(Cocktail))
PORT_DIPNAME(0x04, 0x04, "Coin Mode")
PORT_DIPSETTING(0x04, "Mode 1")
PORT_DIPSETTING(0x00, "Mode 2")
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
// In stop mode, press 2 to stop and 1 to restart
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Stop Mode", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, 0x20, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Sector Selection", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x40, 0x40, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Invulnerability", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x80, IP_ACTIVE_LOW)

// Fake port to support the two different coin modes
PORT_START("FAKE")
PORT_DIPNAME(0x0f, 0x0f, "Coinage Mode 1")   // mapped on coin mode 1
PORT_DIPSETTING(0x09, DEF_STR(7C_1C))
PORT_DIPSETTING(0x0a, DEF_STR(6C_1C))
PORT_DIPSETTING(0x0b, DEF_STR(5C_1C))
PORT_DIPSETTING(0x0c, DEF_STR(4C_1C))
PORT_DIPSETTING(0x0d, DEF_STR(3C_1C))
PORT_DIPSETTING(0x0e, DEF_STR(2C_1C))
PORT_DIPSETTING(0x0f, DEF_STR(1C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_2C))
PORT_DIPSETTING(0x06, DEF_STR(1C_3C))
PORT_DIPSETTING(0x05, DEF_STR(1C_4C))
PORT_DIPSETTING(0x04, DEF_STR(1C_5C))
PORT_DIPSETTING(0x03, DEF_STR(1C_6C))
PORT_DIPSETTING(0x02, DEF_STR(1C_7C))
PORT_DIPSETTING(0x01, DEF_STR(1C_8C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x30, 0x30, "Coin A  Mode 2")   // mapped on coin mode 2
PORT_DIPSETTING(0x10, DEF_STR(3C_1C))
PORT_DIPSETTING(0x20, DEF_STR(2C_1C))
PORT_DIPSETTING(0x30, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0xc0, 0xc0, "Coin B  Mode 2")
PORT_DIPSETTING(0xc0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x80, DEF_STR(1C_3C))
PORT_DIPSETTING(0x40, DEF_STR(1C_5C))
PORT_DIPSETTING(0x00, DEF_STR(1C_6C))
INPUT_PORTS_END

// Identical to mpatrol except the Lives values
INPUT_PORTS_START(mpatrolw)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_COIN3, 17)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN2")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x01, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x01, "3")
PORT_DIPSETTING(0x02, "4")
PORT_DIPSETTING(0x03, "5")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x0c, "10000 30000 50000")
PORT_DIPSETTING(0x08, "20000 40000 60000")
PORT_DIPSETTING(0x04, "10000")
PORT_DIPSETTING(0x00, "None")
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW1")
PORT_DIPNAME(0x01, 0x01, DEF_STR(Flip_Screen))
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x02, DEF_STR(Cocktail))
PORT_DIPNAME(0x04, 0x04, "Coin Mode")
PORT_DIPSETTING(0x04, "Mode 1")
PORT_DIPSETTING(0x00, "Mode 2")
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Stop Mode", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, 0x20, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Sector Selection", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x40, 0x40, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Invulnerability", IP_KEY_NONE, IP_JOY_NONE)
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_SERVICE(0x80, IP_ACTIVE_LOW)

PORT_START("FAKE")
PORT_DIPNAME(0x0f, 0x0f, "Coinage Mode 1")
PORT_DIPSETTING(0x09, DEF_STR(7C_1C))
PORT_DIPSETTING(0x0a, DEF_STR(6C_1C))
PORT_DIPSETTING(0x0b, DEF_STR(5C_1C))
PORT_DIPSETTING(0x0c, DEF_STR(4C_1C))
PORT_DIPSETTING(0x0d, DEF_STR(3C_1C))
PORT_DIPSETTING(0x0e, DEF_STR(2C_1C))
PORT_DIPSETTING(0x0f, DEF_STR(1C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_2C))
PORT_DIPSETTING(0x06, DEF_STR(1C_3C))
PORT_DIPSETTING(0x05, DEF_STR(1C_4C))
PORT_DIPSETTING(0x04, DEF_STR(1C_5C))
PORT_DIPSETTING(0x03, DEF_STR(1C_6C))
PORT_DIPSETTING(0x02, DEF_STR(1C_7C))
PORT_DIPSETTING(0x01, DEF_STR(1C_8C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x30, 0x30, "Coin A  Mode 2")
PORT_DIPSETTING(0x10, DEF_STR(3C_1C))
PORT_DIPSETTING(0x20, DEF_STR(2C_1C))
PORT_DIPSETTING(0x30, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0xc0, 0xc0, "Coin B  Mode 2")
PORT_DIPSETTING(0xc0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x80, DEF_STR(1C_3C))
PORT_DIPSETTING(0x40, DEF_STR(1C_5C))
PORT_DIPSETTING(0x00, DEF_STR(1C_6C))
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs (MAME 0.286 names / CRCs / SHA1s)
// ---------------------------------------------------------------------------

ROM_START(mpatrol)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("mpa-1.3m", 0x0000, 0x1000, CRC(5873a860) SHA1(8c03726d6e049c3edbc277440184e31679f78258))
ROM_LOAD("mpa-2.3l", 0x1000, 0x1000, CRC(f4b85974) SHA1(dfb2efb57378a20af6f20569f4360cde95596f93))
ROM_LOAD("mpa-3.3k", 0x2000, 0x1000, CRC(2e1a598c) SHA1(112c3c9678db8a8540a8df3708020c87fd10c91b))
ROM_LOAD("mpa-4.3j", 0x3000, 0x1000, CRC(dd05b587) SHA1(727961b0dafa4a96b580d51013336db2a18aff1e))

ROM_REGION(0x10000, REGION_CPU2, 0) /* sound M6803 */
ROM_LOAD("mp-s1.1a", 0xf000, 0x1000, CRC(561d3108) SHA1(4998c68a9e9a8002251fa8f07aa1082444a9dc80))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE) /* chars - 0.57 plane order: 3e then 3f */
ROM_LOAD("mpe-5.3e", 0x0000, 0x1000, CRC(e3ee7f75) SHA1(b03d0d56150d3e9da4a4c871338097b4f450b649))
ROM_LOAD("mpe-4.3f", 0x1000, 0x1000, CRC(cca6d023) SHA1(fecb3059fb09897a096add9452b50aec55c07545))

ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE) /* sprites */
ROM_LOAD("mpb-2.3m", 0x0000, 0x1000, CRC(707ace5e) SHA1(93c682e13e74bce29ced3a87bffb29569c114c3b))
ROM_LOAD("mpb-1.3n", 0x1000, 0x1000, CRC(9b72133a) SHA1(1393ef92ae1ad58a4b62ca1660c0793d30a8b5e2))

ROM_REGION(0x1000, REGION_GFX3, ROMREGION_DISPOSE) /* background: mountains */
ROM_LOAD("mpe-1.3l", 0x0000, 0x1000, CRC(c46a7f72) SHA1(8bb7c9acaf6833fb6c0575b015991b873a305a84))

ROM_REGION(0x1000, REGION_GFX4, ROMREGION_DISPOSE) /* background: hills */
ROM_LOAD("mpe-2.3k", 0x0000, 0x1000, CRC(c7aa1fb0) SHA1(14c6c76e1d0db2c0745e5d6d33ea6945fac8e9ee))

ROM_REGION(0x1000, REGION_GFX5, ROMREGION_DISPOSE) /* background: cityscape */
ROM_LOAD("mpe-3.3h", 0x0000, 0x1000, CRC(a0919392) SHA1(8a090cb8d483a3d67c7360058e3fdd70e151cd62))

ROM_REGION(0x0340, REGION_PROMS, 0)
ROM_LOAD("mpc-4.2a", 0x0000, 0x0200, CRC(07f99284) SHA1(dfc52958f2520e1ce4446dd4c84c91413bbacf76)) /* character palette */
ROM_LOAD("mpc-3.1m", 0x0200, 0x0020, CRC(6a57eff2) SHA1(2d1c12dab5915da2ccd466e39436c88be434d634)) /* background palette */
ROM_LOAD("mpc-1.1f", 0x0220, 0x0020, CRC(26979b13) SHA1(8c41a8cce4f3384c392a9f7a223a50d7be0e14a5)) /* sprite palette */
ROM_LOAD("mpc-2.2h", 0x0240, 0x0100, CRC(7ae4cd97) SHA1(bc0662fac82ffe65f02092d912b2c2b0c7a8ac2b)) /* sprite lookup table */
ROM_END

ROM_START(mpatrolw)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("mpa-1w.3m", 0x0000, 0x1000, CRC(baa1a1d4) SHA1(7968a7f221e7f4c9c81ddc8de17f6568e17b9ea8))
ROM_LOAD("mpa-2w.3l", 0x1000, 0x1000, CRC(52459e51) SHA1(ae685b7848baa1b87a3f2bce97356286171e16d4))
ROM_LOAD("mpa-3w.3k", 0x2000, 0x1000, CRC(9b249fe5) SHA1(c01e0d572c4c163f3cf4b2aa9f4246427811b78d))
ROM_LOAD("mpa-4w.3j", 0x3000, 0x1000, CRC(fee76972) SHA1(c3166b027f89f61964ead804d3c2da387454c4c2))

ROM_REGION(0x10000, REGION_CPU2, 0) /* sound M6803 */
ROM_LOAD("mp-s1.1a", 0xf000, 0x1000, CRC(561d3108) SHA1(4998c68a9e9a8002251fa8f07aa1082444a9dc80))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE) /* chars - 0.57 plane order: 3e then 3f */
ROM_LOAD("mpe-5w.3e", 0x0000, 0x1000, CRC(f56e01fe) SHA1(93f582d63b9cd5c6dca207aa57b213c939cdda1d))
ROM_LOAD("mpe-4w.3f", 0x1000, 0x1000, CRC(caaba2d9) SHA1(7016a26c2d01e3209749598e993cd8ce91f12c88))

ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE) /* sprites */
ROM_LOAD("mpb-2.3m", 0x0000, 0x1000, CRC(707ace5e) SHA1(93c682e13e74bce29ced3a87bffb29569c114c3b))
ROM_LOAD("mpb-1.3n", 0x1000, 0x1000, CRC(9b72133a) SHA1(1393ef92ae1ad58a4b62ca1660c0793d30a8b5e2))

ROM_REGION(0x1000, REGION_GFX3, ROMREGION_DISPOSE) /* background: mountains */
ROM_LOAD("mpe-1.3l", 0x0000, 0x1000, CRC(c46a7f72) SHA1(8bb7c9acaf6833fb6c0575b015991b873a305a84))

ROM_REGION(0x1000, REGION_GFX4, ROMREGION_DISPOSE) /* background: hills */
ROM_LOAD("mpe-2.3k", 0x0000, 0x1000, CRC(c7aa1fb0) SHA1(14c6c76e1d0db2c0745e5d6d33ea6945fac8e9ee))

ROM_REGION(0x1000, REGION_GFX5, ROMREGION_DISPOSE) /* background: cityscape */
ROM_LOAD("mpe-3.3h", 0x0000, 0x1000, CRC(a0919392) SHA1(8a090cb8d483a3d67c7360058e3fdd70e151cd62))

ROM_REGION(0x0340, REGION_PROMS, 0)
ROM_LOAD("mpc-4a.2a", 0x0000, 0x0200, CRC(cb0a5ff3) SHA1(d3f88b4e0c4858abac8b52105656ecece0cf4df9)) /* character palette */
ROM_LOAD("mpc-3.1m", 0x0200, 0x0020, CRC(6a57eff2) SHA1(2d1c12dab5915da2ccd466e39436c88be434d634)) /* background palette */
ROM_LOAD("mpc-1.1f", 0x0220, 0x0020, CRC(26979b13) SHA1(8c41a8cce4f3384c392a9f7a223a50d7be0e14a5)) /* sprite palette */
ROM_LOAD("mpc-2.2h", 0x0240, 0x0100, CRC(7ae4cd97) SHA1(bc0662fac82ffe65f02092d912b2c2b0c7a8ac2b)) /* sprite lookup table */
ROM_END

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_mpatrol, "mpatrol", "Moon Patrol")
AAE_DRIVER_ROM(rom_mpatrol)
AAE_DRIVER_FUNCS(&init_mpatrol, &run_mpatrol, &end_mpatrol)
AAE_DRIVER_INPUT(input_ports_mpatrol)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_INT, &mpatrol_interrupt,
		MpatrolMainRead, MpatrolMainWrite, MpatrolMainPortRead, MpatrolMainPortWrite, nullptr, nullptr),
	// CPU1: M6803 sound (Irem board; on-chip port glue hooked post-init)
	AAE_CPU_ENTRY_EX(CPU_M6803, IREM_AUDIO_CPU_CLOCK, 100, 1, INT_TYPE_NONE, &mpatrol_snd_noop_interrupt,
		IremSoundRead, IremSoundWrite, nullptr, nullptr, nullptr, nullptr, &irem_sound_post_cpu_init),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, 1790, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(256, 256, 8, 247, 8, 255)
AAE_DRIVER_RASTER(mpatrol_gfxdecodeinfo, 128 + 32 + 32, 64 * 4 + 16 * 4 + 3 * 4, mpatrol_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_mpatrol)

AAE_DRIVER_BEGIN(drv_mpatrolw, "mpatrolw", "Moon Patrol (Williams)")
AAE_DRIVER_ROM(rom_mpatrolw)
AAE_DRIVER_FUNCS(&init_mpatrol, &run_mpatrol, &end_mpatrol)
AAE_DRIVER_INPUT(input_ports_mpatrolw)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_INT, &mpatrol_interrupt,
		MpatrolMainRead, MpatrolMainWrite, MpatrolMainPortRead, MpatrolMainPortWrite, nullptr, nullptr),
	// CPU1: M6803 sound (Irem board; on-chip port glue hooked post-init)
	AAE_CPU_ENTRY_EX(CPU_M6803, IREM_AUDIO_CPU_CLOCK, 100, 1, INT_TYPE_NONE, &mpatrol_snd_noop_interrupt,
		IremSoundRead, IremSoundWrite, nullptr, nullptr, nullptr, nullptr, &irem_sound_post_cpu_init),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, 1790, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(256, 256, 8, 247, 8, 255)
AAE_DRIVER_RASTER(mpatrol_gfxdecodeinfo, 128 + 32 + 32, 64 * 4 + 16 * 4 + 3 * 4, mpatrol_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_CLONE_OF("mpatrol")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_mpatrolw)
