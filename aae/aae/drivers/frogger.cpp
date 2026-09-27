//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================
//
// frogger.cpp - Frogger (Konami 1981), from MAME 0.36's frogger.c /
// vidhrdw/frogger.c / sndhrdw/frogger.c.
//
// Hardware notes that shaped this port:
//   * Two Z80s: main at 18.432MHz/6, sound at 14.318MHz/8 driving one
//     AY-3-8910. The main CPU takes one gated NMI per frame; the sound CPU
//     has no periodic interrupt - the main CPU pulses its IRQ through the
//     0xd002 trigger latch.
//   * The sound command uses the engine's generic soundlatch 0
//     (sndhrdwr/sound_latch.h): main writes 0xd000, the AY reads it back
//     through port A (the sound program reads the AY data port with port A
//     selected).
//   * AY port B is a clock-derived timer: the sound Z80's clock divided by
//     5120 through an LS90 bi-quinary counter. Emulated from the sound CPU's
//     cycle count exactly as MAME does, with one AAE twist - cycle counters
//     reset at frame end, so the delta is re-based when it goes negative.
//   * Konami's copy protection: the first sound ROM and the first gfx ROM
//     have data lines D0 and D1 swapped on the board. Both are descrambled
//     in frogger_rom_decrypt(), the Step 3b hook - it MUST run there, not in
//     init_frogger(): vh_open decodes the gfx region (and may dispose it)
//     before init_game runs, so an init-time descramble never reaches the
//     decoded tiles. Symptom of getting this wrong: stray wrong-color pixel
//     rows on the tile edges (bits 0/1 are pixel columns 0/1, which the ROT90
//     view shows as the bottom rows of every tile).
//============================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"
#include "ay8910.h"
#include "sound_latch.h"
#include "frogger.h"

// Z80-map homes of the video structures; init_frogger() points the shared
// raster globals at these, so keep them in sync with the memory map below.
#define FROGGER_VIDEORAM_BASE    0xa800
#define FROGGER_VIDEORAM_SIZE    0x0400   // 32 * 32 tiles
#define FROGGER_ATTRIBUTES_BASE  0xb000   // 32 x (scroll, color) pairs
#define FROGGER_SPRITERAM_BASE   0xb040
#define FROGGER_SPRITERAM_SIZE   0x0020   // 8 sprites * 4 bytes

#define FROGGER_MAIN_CLOCK   (18432000 / 6)   // 3.072 MHz
#define FROGGER_SOUND_CLOCK  (14318000 / 8)   // 1.78975 MHz, also the AY clock

static int frogger_flipscreen = 0;
unsigned char* frogger_attributesram;

// Edge state for the sound IRQ trigger; reset in init so a relaunch does not
// inherit a stale level.
static int frogger_irq_last = 0;

// ---------------------------------------------------------------------------
// Graphics layout - two separated bitplanes, chars and sprites sharing one
// 0x1000 region.
// ---------------------------------------------------------------------------
static struct GfxLayout charlayout =
{
	8, 8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 256 * 8 * 8, 0 },	/* the two bitplanes are separated */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8	/* every char takes 8 consecutive bytes */
};
static struct GfxLayout spritelayout =
{
	16, 16,	/* 16*16 sprites */
	64,	/* 64 sprites */
	2,	/* 2 bits per pixel */
	{ 64 * 16 * 16, 0 },	/* the two bitplanes are separated */
	{ 0, 1, 2, 3, 4, 5, 6, 7,
			8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 8 * 8 + 4, 8 * 8 + 5, 8 * 8 + 6, 8 * 8 + 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8 },
	32 * 8	/* every sprite takes 32 consecutive bytes */
};

struct GfxDecodeInfo frogger_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,     0, 16 },
	{ REGION_GFX1, 0, &spritelayout,   0,  8 },
	{ -1 } /* end of array */
};

// ---------------------------------------------------------------------------
// Palette (MAME vidhrdw/frogger.c)
//
// One 32-byte PROM: bits 0-2 red, 3-5 green, 6-7 blue, through the usual
// 1k/470/220 ohm ladder. An extra hardware bit forces blue on in half the
// display to turn the river blue - pen 4 is repurposed for that, and the
// second half of the colortable points its background pens at it.
// ---------------------------------------------------------------------------
void frogger_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		bit0 = (color_prom[i] >> 0) & 0x01;
		bit1 = (color_prom[i] >> 1) & 0x01;
		bit2 = (color_prom[i] >> 2) & 0x01;
		palette[3 * i] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (color_prom[i] >> 3) & 0x01;
		bit1 = (color_prom[i] >> 4) & 0x01;
		bit2 = (color_prom[i] >> 5) & 0x01;
		palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = 0;
		bit1 = (color_prom[i] >> 6) & 0x01;
		bit2 = (color_prom[i] >> 7) & 0x01;
		palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	}

	/* use an otherwise unused pen for the river background */
	palette[3 * 4] = 0;
	palette[3 * 4 + 1] = 0;
	palette[3 * 4 + 2] = 0x47;

	/* normal */
	for (i = 0; i < 4 * 8; i++)
	{
		if (i & 3) colortable[i] = i;
		else colortable[i] = 0;
	}
	/* blue background (river) */
	for (i = 4 * 8; i < 4 * 16; i++)
	{
		if (i & 3) colortable[i] = i - 4 * 8;
		else colortable[i] = 4;
	}
}

// ---------------------------------------------------------------------------
// Video (MAME vidhrdw/frogger.c)
//
// Tiles are cached in tmpbitmap via dirtybuffer, then copied with per-column
// scroll from the attribute RAM (frogger scrolls the river/road columns, and
// both scroll offset and sprite Y pass through a nibble swap - more of the
// board's D0/D1-flavored scrambling). Sprites draw over the top.
//
// tmpbitmap here is the square 256x256 case, so copyscrollbitmap is safe
// under system rotation (the non-square corruption trap does not apply).
// ---------------------------------------------------------------------------
static int frogger_video_start(void)
{
	videoram_size = FROGGER_VIDEORAM_SIZE;
	return generic_vh_start();   // engine helper, old_mame_raster.h
}

static void frogger_vh_screenrefresh(void)
{
	int i, offs;

	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, col;

			dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;
			col = frogger_attributesram[2 * sx + 1] & 7;
			col = ((col >> 1) & 0x03) | ((col << 2) & 0x04);

			if (frogger_flipscreen)
			{
				sx = 31 - sx;
				sy = 31 - sy;
				drawgfx(tmpbitmap, Machine->gfx[0],
					videoram[offs],
					col + (sx >= 16 ? 8 : 0),	/* blue background in the lower 128 lines */
					frogger_flipscreen, frogger_flipscreen, 8 * sx, 8 * sy,
					0, TRANSPARENCY_NONE, 0);
			}
			else
			{
				drawgfx(tmpbitmap, Machine->gfx[0],
					videoram[offs],
					col + (sx <= 15 ? 8 : 0),	/* blue background in the upper 128 lines */
					frogger_flipscreen, frogger_flipscreen, 8 * sx, 8 * sy,
					0, TRANSPARENCY_NONE, 0);
			}
		}
	}

	/* copy the temporary bitmap to the screen */
	{
		int scroll[32], s;

		for (i = 0; i < 32; i++)
		{
			s = frogger_attributesram[2 * i];
			if (frogger_flipscreen)
			{
				scroll[31 - i] = (((s << 4) & 0xf0) | ((s >> 4) & 0x0f));
			}
			else
			{
				scroll[i] = -(((s << 4) & 0xf0) | ((s >> 4) & 0x0f));
			}
		}

		copyscrollbitmap(main_bitmap, tmpbitmap, 0, 0, 32, scroll, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	/* Draw the sprites. Note that it is important to draw them exactly in this */
	/* order, to have the correct priorities. */
	for (offs = spriteram_size - 4; offs >= 0; offs -= 4)
	{
		if (spriteram[offs + 3] != 0)
		{
			int x, y, col;

			x = spriteram[offs + 3];
			y = spriteram[offs];
			y = ((y << 4) & 0xf0) | ((y >> 4) & 0x0f);
			col = spriteram[offs + 2] & 7;
			col = ((col >> 1) & 0x03) | ((col << 2) & 0x04);

			if (frogger_flipscreen)
			{
				x = 242 - x;
				y = 240 - y;
				drawgfx(main_bitmap, Machine->gfx[1],
					spriteram[offs + 1] & 0x3f,
					col,
					!(spriteram[offs + 1] & 0x40), !(spriteram[offs + 1] & 0x80),
					x, 30 * 8 - y,
					&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
			}
			else
			{
				drawgfx(main_bitmap, Machine->gfx[1],
					spriteram[offs + 1] & 0x3f,
					col,
					spriteram[offs + 1] & 0x40, spriteram[offs + 1] & 0x80,
					x, 30 * 8 - y,
					&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Write handlers (AAE signatures; address arrives RANGE-RELATIVE)
// ---------------------------------------------------------------------------

// Video RAM through the engine's videoram_w so dirtybuffer gets marked.
WRITE_HANDLER(frogger_videoram_w)
{
	videoram_w(address, data);
}

// Attribute RAM: even bytes scroll a column, odd bytes color it. A color
// change dirties the whole column so the tile cache redraws it.
WRITE_HANDLER(frogger_attributes_w)
{
	if ((address & 1) && frogger_attributesram[address] != data)
	{
		int i;
		for (i = (int)(address / 2); i < videoram_size; i += 32)
			dirtybuffer[i] = 1;
	}

	frogger_attributesram[address] = data;
}

WRITE_HANDLER(frogger_flipscreen_w)
{
	if (frogger_flipscreen != (data & 1))
	{
		frogger_flipscreen = data & 1;
		memset(dirtybuffer, 1, videoram_size);
	}
}

// Setting bit 3 low then high pulses the sound CPU's IRQ (Z80 IM1, so the
// 0xff vector MAME passed is implicit). cpu_do_int_imm dispatches in the
// target CPU's context, so the cross-CPU call is safe.
WRITE_HANDLER(frogger_sh_irqtrigger_w)
{
	if (frogger_irq_last == 0 && (data & 0x08) != 0)
		cpu_do_int_imm(1, INT_TYPE_INT);

	frogger_irq_last = data & 0x08;
}

// ---------------------------------------------------------------------------
// AY-3-8910 (MAME sndhrdw/frogger.c)
// ---------------------------------------------------------------------------

// Port A: the command byte the main CPU last wrote to 0xd000 (generic
// soundlatch 0; the engine clears it before every init_game).
static uint8_t frogger_ay_porta_r(void)
{
	return soundlatch_get(0);
}

/* The timer clock which feeds the upper 4 bits of AY port A is the sound
   CPU's own clock divided by 5120: a divide by 512 followed by a divide by
   10 in an LS90's bi-quinary sequence. The table is the resulting bit
   pattern per tenth. */
static const int frogger_timer[10] =
{
	0x00, 0x10, 0x08, 0x18, 0x40, 0x90, 0x88, 0x98, 0x88, 0xd0
};

static uint8_t frogger_ay_portb_r(void)
{
	static int last_totalcycles = 0;
	static int clock = 0;
	int current_totalcycles, delta;

	current_totalcycles = get_exact_cyclecount(1);
	delta = current_totalcycles - last_totalcycles;
	last_totalcycles = current_totalcycles;

	// AAE cycle counters restart each frame (MAME's ran monotonically and
	// only overflowed); a negative delta means a frame boundary passed, so
	// re-base it by one frame's worth of sound-CPU cycles.
	if (delta < 0) delta += FROGGER_SOUND_CLOCK / 60;

	clock = (clock + delta) % 5120;

	return (uint8_t)frogger_timer[clock / 512];
}

static AY8910Config frogger_ay8910_cfg =
{
	1,                          // num_chips
	FROGGER_SOUND_CLOCK,        // base_clock, 1.78975 MHz
	{ 255 },                    // mixing_level, 0..255
	{ frogger_ay_porta_r },     // port A: soundlatch
	{ frogger_ay_portb_r },     // port B: LS90 timer
	{ nullptr },
	{ nullptr }
};

// ---------------------------------------------------------------------------
// Main CPU memory map (MAME drivers/frogger.c)
// ---------------------------------------------------------------------------
MEM_READ(frogger_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x8000, 0x87ff, MRA_RAM)
MEM_ADDR(0x8800, 0x8800, watchdog_reset_r)
MEM_ADDR(0xa800, 0xabff, MRA_RAM)        /* video RAM */
MEM_ADDR(0xb000, 0xb05f, MRA_RAM)        /* screen attributes, sprites */
MEM_ADDR(0xe000, 0xe000, ip_port_0_r)    /* IN0 */
MEM_ADDR(0xe002, 0xe002, ip_port_1_r)    /* IN1 */
MEM_ADDR(0xe004, 0xe004, ip_port_2_r)    /* IN2 */
MEM_END

MEM_WRITE(frogger_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x8000, 0x87ff, MWA_RAM)
MEM_ADDR(0xa800, 0xabff, frogger_videoram_w)      /* marks dirtybuffer */
MEM_ADDR(0xb000, 0xb03f, frogger_attributes_w)
MEM_ADDR(0xb040, 0xb05f, MWA_RAM)                 /* sprite RAM */
MEM_ADDR(0xb808, 0xb808, interrupt_enable_w)      /* gates the per-frame NMI */
MEM_ADDR(0xb80c, 0xb80c, frogger_flipscreen_w)
MEM_ADDR(0xb818, 0xb81c, MWA_NOP)                 /* coin counters */
MEM_ADDR(0xd000, 0xd000, soundlatch_w)
MEM_ADDR(0xd002, 0xd002, frogger_sh_irqtrigger_w)
MEM_END

// Main CPU has no I/O ports; everything is memory mapped.
PORT_READ(frogger_readport)
PORT_END

PORT_WRITE(frogger_writeport)
PORT_END

// ---------------------------------------------------------------------------
// Sound CPU maps (MAME drivers/frogger.c)
//
// AY on I/O ports: 0x40 = data (read AND write), 0x80 = register select.
// ---------------------------------------------------------------------------
MEM_READ(frogger_sound_readmem)
MEM_ADDR(0x0000, 0x17ff, MRA_ROM)
MEM_ADDR(0x4000, 0x43ff, MRA_RAM)
MEM_END

MEM_WRITE(frogger_sound_writemem)
MEM_ADDR(0x0000, 0x17ff, MWA_ROM)
MEM_ADDR(0x4000, 0x43ff, MWA_RAM)
MEM_END

PORT_READ(frogger_sound_readport)
PORT_ADDR(0x40, 0x40, ay8910_0_data_port_r)
PORT_END

PORT_WRITE(frogger_sound_writeport)
PORT_ADDR(0x40, 0x40, ay8910_0_data_port_w)
PORT_ADDR(0x80, 0x80, ay8910_0_control_port_w)
PORT_END

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------

// One NMI per frame; cpu_do_int_imm itself honors the 0xb808 enable gate.
void frogger_interrupt(void)
{
	cpu_do_int_imm(0, INT_TYPE_NMI);
}

// The sound CPU has no periodic interrupt - the main CPU pulses its IRQ
// through frogger_sh_irqtrigger_w.
void frogger_sound_interrupt(void)
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
// ROM decrypt hook: runs from Step 3b, before vh_open decodes the gfx and
// frees the DISPOSE region - init_frogger() would be too late (the tiles
// would already be decoded from scrambled data).
static void frogger_rom_decrypt(void)
{
	int A;
	unsigned char* RAM;

	/* the first ROM of the second CPU has data lines D0 and D1 swapped. Decode it. */
	RAM = memory_region(REGION_CPU2);
	for (A = 0; A < 0x0800; A++)
		RAM[A] = (RAM[A] & 0xfc) | ((RAM[A] & 1) << 1) | ((RAM[A] & 2) >> 1);

	/* likewise, the first gfx ROM has data lines D0 and D1 swapped. Decode it. */
	RAM = memory_region(REGION_GFX1);
	for (A = 0; A < 0x0800; A++)
		RAM[A] = (RAM[A] & 0xfc) | ((RAM[A] & 1) << 1) | ((RAM[A] & 2) >> 1);
}

int init_frogger(void)
{
	// Point the shared raster globals into the Z80 address space; the maps
	// above pass these ranges through MWA_RAM / thin wrappers, so the bytes
	// live in the CPU0 region and the video code reads them from here.
	videoram              = &Machine->memory_region[CPU0][FROGGER_VIDEORAM_BASE];
	videoram_size         = FROGGER_VIDEORAM_SIZE;
	spriteram             = &Machine->memory_region[CPU0][FROGGER_SPRITERAM_BASE];
	spriteram_size        = FROGGER_SPRITERAM_SIZE;
	frogger_attributesram = &Machine->memory_region[CPU0][FROGGER_ATTRIBUTES_BASE];

	frogger_flipscreen = 0;
	frogger_irq_last = 0;

	if (frogger_video_start() != 0)
		return 1;

	ay8910_sh_start(&frogger_ay8910_cfg);

	LOG_INFO("frogger: init complete");
	return 0;
}

void run_frogger(void)
{
	frogger_vh_screenrefresh();
	ay8910_sh_update();
}

void end_frogger(void)
{
	ay8910_sh_stop();
}

// ---------------------------------------------------------------------------
// Inputs (MAME drivers/frogger.c; all bits inverted)
// ---------------------------------------------------------------------------
INPUT_PORTS_START(frogger)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNKNOWN) /* 1P shoot2 - unused */
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)   /* CREDIT */
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN) /* 1P shoot1 - unused */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("IN1")	/* IN1 */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "5")
PORT_DIPSETTING(0x02, "7")
PORT_BITX(0, 0x03, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "256", IP_KEY_NONE, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN) /* 2P shoot2 - unused */
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN) /* 2P shoot1 - unused */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START1)

PORT_START("IN2")	/* IN2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_DIPNAME(0x06, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x02, "A 2/1 B 2/1 C 2/1")
PORT_DIPSETTING(0x04, "A 2/1 B 1/3 C 2/1")
PORT_DIPSETTING(0x00, "A 1/1 B 1/1 C 1/1")
PORT_DIPSETTING(0x06, "A 1/1 B 1/6 C 1/1")
PORT_DIPNAME(0x08, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x08, DEF_STR(Cocktail))
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs (MAME 0.36 frogger set)
//
// Load order matters beyond the offsets: frogger.606 must be the FIRST gfx
// ROM because init_frogger() descrambles the first 0x800 bytes of the
// region (it is the ROM with the swapped data lines).
// ---------------------------------------------------------------------------
ROM_START(frogger)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("frogger.26", 0x0000, 0x1000, CRC(597696d6) SHA1(e7e021776cad00f095a1ebbef407b7c0a8f5d835))
ROM_LOAD("frogger.27", 0x1000, 0x1000, CRC(b6e6fcc3) SHA1(5e8692f2b0c7f4b3642b3ee6670e1c3b20029cdc))
ROM_LOAD("frsm3.7", 0x2000, 0x1000, CRC(aca22ae0) SHA1(5a99060ea2506a3ac7d61ca5876ce5cb3e493565))

ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the audio CPU */
ROM_LOAD("frogger.608", 0x0000, 0x0800, CRC(e8ab0256) SHA1(f090afcfacf5f13cdfa0dfda8e3feb868c6ce8bc))
ROM_LOAD("frogger.609", 0x0800, 0x0800, CRC(7380a48f) SHA1(75582a94b696062cbdb66a4c5cf0bc0bb94f81ee))
ROM_LOAD("frogger.610", 0x1000, 0x0800, CRC(31d7eb27) SHA1(2e1d34ae4da385fd7cac94707d25eeddf4604e1a))

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("frogger.606", 0x0000, 0x0800, CRC(f524ee30) SHA1(dd768967add61467baa08d5929001f157d6cd911))
ROM_LOAD("frogger.607", 0x0800, 0x0800, CRC(05f7d883) SHA1(78831fd287da18928651a8adb7e578d291493eff))

ROM_REGION(0x0020, REGION_PROMS, 0)
ROM_LOAD("pr-91.6l", 0x0000, 0x0020, CRC(413703bf) SHA1(66648b2b28d3dcbda5bdb2605d1977428939dd3c))
ROM_END

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_frogger, "frogger", "Frogger (Konami)")
AAE_DRIVER_ROM(rom_frogger)
AAE_DRIVER_FUNCS(&init_frogger, &run_frogger, &end_frogger)
AAE_DRIVER_INPUT(input_ports_frogger)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: main Z80
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     FROGGER_MAIN_CLOCK,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NMI,
		/*int cb*/   &frogger_interrupt,
		/*r8*/       frogger_readmem,
		/*w8*/       frogger_writemem,
		/*pr*/       frogger_readport,
		/*pw*/       frogger_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: sound Z80, IRQ pulsed by the main CPU
	AAE_CPU_ENTRY(
		CPU_MZ80, FROGGER_SOUND_CLOCK, 100, 1, INT_TYPE_NONE, &frogger_sound_interrupt,
		frogger_sound_readmem, frogger_sound_writemem,
		frogger_sound_readport, frogger_sound_writeport,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 2500, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90 | ORIENTATION_FLIP_X)
AAE_DRIVER_SCREEN(32 * 8, 32 * 8, 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1)
AAE_DRIVER_RASTER(frogger_gfxdecodeinfo, 32, 64, frogger_vh_convert_color_prom)

AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&frogger_rom_decrypt)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_frogger)
