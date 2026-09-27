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
// generic.cpp - TEMPLATE driver: Z80 + AY-8910, colour raster video.
//
// This file is a starting point for a new driver, not a working game. It is
// deliberately NOT in aae_core.vcxproj / CMakeLists.txt, so it does not
// compile or register until you add it.
//
// ---------------------------------------------------------------------------
// HOW TO USE
// ---------------------------------------------------------------------------
//   1. Copy to drivers/<yourgame>.cpp and replace every "generic" with your
//      game's short name (the string in AAE_DRIVER_BEGIN is what the user
//      types on the command line, so keep it lowercase and zip-name shaped).
//   2. Fill in ROM_START from the MAME driver: names, offsets, lengths, CRCs.
//      Region layout matters more than the CRCs - a wrong offset boots to
//      garbage, a wrong CRC only warns.
//   3. Fix the memory map. Everything below is a plausible 1980-ish Z80 board
//      but the addresses ARE made up - take them from the real driver.
//   4. Match the gfx layouts to the real ROMs (bpp, plane offsets, char count).
//      Wrong plane offsets are the classic "sprites look like confetti" bug.
//   5. Rewrite convert_color_prom for the board's actual resistor network.
//   6. Add the file to aae/aae_core.vcxproj AND CMakeLists.txt
//      (AAE_CORE_SOURCES + bump its source-count assertion), then build.
//
// ---------------------------------------------------------------------------
// WHAT IS WIRED
// ---------------------------------------------------------------------------
//   * One Z80 (CPU_MZ80). A second Z80 sound CPU is scaffolded but commented.
//   * One AY-8910 on Z80 I/O ports, with the memory-mapped wiring commented
//     out beside it - both idioms are common, pick one and delete the other.
//   * Colour raster video kept in this file (tiles + sprites), the way
//     dkong.cpp does it, rather than a separate vidhrdwr/ module.
//
// AAE handler offset convention, worth knowing before you debug anything:
//   memory handlers get RANGE-RELATIVE offsets (address - lowAddr), port
//   handlers get the ABSOLUTE port number. Do not subtract the base again in
//   a memory handler.
//
// ASCII-only comments.
//============================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"
#include "ay8910.h"

// ---------------------------------------------------------------------------
// Board constants - the first things to change for a real game.
// ---------------------------------------------------------------------------
#define GENERIC_MAIN_CLOCK   3072000      // Z80, 3.072 MHz (18.432MHz / 6)
#define GENERIC_AY_CLOCK     1789750      // AY-8910, ~1.79 MHz
#define GENERIC_FPS          60

// Screen is 32x32 tiles of 8x8; the visible window trims the top and bottom
// two tile rows, which is typical of this era.
#define GENERIC_SCREEN_W     (32 * 8)
#define GENERIC_SCREEN_H     (32 * 8)

// Where in the Z80 map the video RAM and sprite RAM live. init_generic()
// points the shared raster globals at these, so keep them in sync with the
// memory map below.
#define GENERIC_VIDEORAM_BASE   0x4000
#define GENERIC_VIDEORAM_SIZE   0x0400    // 32 * 32 tiles
#define GENERIC_SPRITERAM_BASE  0x4400
#define GENERIC_SPRITERAM_SIZE  0x0040    // 16 sprites * 4 bytes

static int generic_flipscreen = 0;

// ---------------------------------------------------------------------------
// Graphics layout
//
// The two most common 1980s arrangements: 8x8 two-bitplane characters and
// 16x16 two-bitplane sprites, with the planes stored as separate halves of
// the ROM region. If the real board interleaves planes instead, the plane
// offsets become { 0, 4 } style bit offsets rather than byte halves.
// ---------------------------------------------------------------------------
static struct GfxLayout generic_charlayout =
{
	8, 8,                       // 8x8 characters
	256,                        // 256 of them
	2,                          // 2 bits per pixel
	{ 0, 256 * 8 * 8 },         // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8                       // every char takes 8 consecutive bytes
};

static struct GfxLayout generic_spritelayout =
{
	16, 16,                     // 16x16 sprites
	64,                         // 64 of them
	2,                          // 2 bits per pixel
	{ 0, 64 * 16 * 16 },        // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7,
	  8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  8 * 8 + 4, 8 * 8 + 5, 8 * 8 + 6, 8 * 8 + 7 },
	{ 0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
	  16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8 },
	32 * 8                      // every sprite takes 32 consecutive bytes
};

// Both layouts read the same REGION_GFX1 here. Real boards often split
// characters and sprites across REGION_GFX1 / REGION_GFX2 - add a second
// ROM_REGION and point the second entry at it if so.
//
// The trailing two numbers are the first colour index and how many colour
// codes that element uses.
struct GfxDecodeInfo generic_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &generic_charlayout,   0, 8 },
	{ REGION_GFX1, 0x0000, &generic_spritelayout, 0, 8 },
	{ -1 } // end of array
};

// ---------------------------------------------------------------------------
// Palette
//
// A 32-byte PROM holding one byte per colour: 3 bits red, 3 bits green,
// 2 bits blue, driven through the usual resistor ladder. This is the most
// common arrangement of the period, but it IS a guess for any given board -
// check the schematic for resistor values and whether the PROM is inverted
// (some boards, e.g. Donkey Kong, drive the network through an inverter and
// need 255 - value).
//
// colortable maps gfx pen numbers to palette entries. The identity-ish map
// below assumes each 4-colour gfx element uses a contiguous run.
// ---------------------------------------------------------------------------
void generic_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;

		// red - 3 bits, 1k / 470 / 220 ohm
		bit0 = (color_prom[i] >> 0) & 1;
		bit1 = (color_prom[i] >> 1) & 1;
		bit2 = (color_prom[i] >> 2) & 1;
		*(palette++) = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);

		// green - 3 bits
		bit0 = (color_prom[i] >> 3) & 1;
		bit1 = (color_prom[i] >> 4) & 1;
		bit2 = (color_prom[i] >> 5) & 1;
		*(palette++) = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);

		// blue - 2 bits
		bit0 = (color_prom[i] >> 6) & 1;
		bit1 = (color_prom[i] >> 7) & 1;
		*(palette++) = (unsigned char)(0x55 * bit0 + 0xaa * bit1);
	}

	// 8 colour codes of 4 pens each, laid out consecutively in the palette.
	for (i = 0; i < 8 * 4; i++)
		colortable[i] = (unsigned char)i;
}

// ---------------------------------------------------------------------------
// Video
//
// Tiles are cached into tmpbitmap and only redrawn where dirtybuffer says the
// video RAM changed, then blitted whole; sprites are drawn over the top every
// frame.
//
// Note the name: the engine already exports generic_vh_start() from
// old_mame_raster.h (it allocates tmpbitmap and dirtybuffer), so this
// driver-side starter is generic_video_start() to avoid colliding with it.
// videoram_size MUST be set before calling it - that is what dirtybuffer is
// sized from.
// ---------------------------------------------------------------------------
static int generic_video_start(void)
{
	videoram_size = GENERIC_VIDEORAM_SIZE;
	return generic_vh_start();   // engine helper, old_mame_raster.h
}

static void generic_draw_tiles(void)
{
	int offs;

	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, charcode, color;

			dirtybuffer[offs] = 0;

			// Linear 32x32 layout. Many boards scramble this - Pac-Man style
			// hardware, for instance, wraps the top and bottom two rows down
			// the right-hand edge.
			sx = offs % 32;
			sy = offs / 32;

			charcode = videoram[offs];
			color    = 0;   // real boards take this from an attribute RAM byte

			if (generic_flipscreen) { sx = 31 - sx; sy = 31 - sy; }

			drawgfx(tmpbitmap, Machine->gfx[0],
				charcode, color,
				generic_flipscreen, generic_flipscreen,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0,
		&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}

// Sprite RAM as 4 bytes per sprite: y, code, colour, x. Byte order and the
// meaning of the high bits vary board to board - check the real driver.
static void generic_draw_sprites(void)
{
	int offs;

	for (offs = 0; offs < spriteram_size; offs += 4)
	{
		int sy    = spriteram[offs + 0];
		int code  = spriteram[offs + 1] & 0x3f;
		int color = spriteram[offs + 2] & 0x07;
		int sx    = spriteram[offs + 3];

		if (sy == 0) continue;   // convention: y == 0 means "slot unused"

		if (generic_flipscreen) { sx = 240 - sx; sy = 240 - sy; }

		drawgfx(main_bitmap, Machine->gfx[1],
			code, color,
			generic_flipscreen, generic_flipscreen,
			sx, sy,
			&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
	}
}

static void generic_vh_screenrefresh(void)
{
	generic_draw_tiles();
	generic_draw_sprites();
}

// ---------------------------------------------------------------------------
// Sound - AY-8910
//
// The AY's two 8-bit parallel ports are very often wired to DIP switches or
// extra inputs rather than to audio. Return the right ip_port_N_r here (or
// nullptr in the config if the board leaves them unconnected).
// ---------------------------------------------------------------------------
static uint8_t generic_ay_port_a_r(void)
{
	return 0xff;   // e.g. ip_port_2_r(0, nullptr) for a DIP bank
}

static uint8_t generic_ay_port_b_r(void)
{
	return 0xff;
}

static AY8910Config generic_ay8910_cfg =
{
	1,                              // num_chips
	GENERIC_AY_CLOCK,               // base_clock
	{ 255 },                        // mixing_level, 0..255
	{ generic_ay_port_a_r },        // port_a_read
	{ generic_ay_port_b_r },        // port_b_read
	{ nullptr },                    // port_a_write
	{ nullptr }                     // port_b_write
};

// ---------------------------------------------------------------------------
// Main CPU memory map
//
// Made-up but conventional: ROM low, work RAM, video RAM, sprite RAM, then
// memory-mapped input reads and latch writes. Replace wholesale.
// ---------------------------------------------------------------------------
MEM_READ(generic_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)                        // program ROM
MEM_ADDR(0x4000, 0x47ff, MRA_RAM)                        // video + sprite + work RAM
MEM_ADDR(0x4800, 0x4fff, MRA_RAM)                        // work RAM
MEM_ADDR(0x5000, 0x5000, ip_port_0_r)                    // coins / start
MEM_ADDR(0x5001, 0x5001, ip_port_1_r)                    // player 1 controls
MEM_ADDR(0x5002, 0x5002, ip_port_2_r)                    // DIP bank 0
MEM_ADDR(0x5003, 0x5003, ip_port_3_r)                    // DIP bank 1
// Memory-mapped AY-8910 alternative - use INSTEAD of the port map below if
// the board addresses the AY through the memory bus:
//MEM_ADDR(0x6001, 0x6001, ay8910_0_data_r)
MEM_END

MEM_WRITE(generic_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x47ff, MWA_RAM)
MEM_ADDR(0x4800, 0x4fff, MWA_RAM)
MEM_ADDR(0x5800, 0x5800, interrupt_enable_w)             // IRQ/NMI enable latch
//MEM_ADDR(0x5801, 0x5801, generic_flipscreen_w)
// Memory-mapped AY-8910 alternative (see the read map above):
//MEM_ADDR(0x6000, 0x6000, ay8910_0_control_w)
//MEM_ADDR(0x6001, 0x6001, ay8910_0_data_w)
MEM_END

// ---------------------------------------------------------------------------
// Main CPU I/O port map
//
// The wired-up AY arrangement: write the register number to the control port,
// then the value to the data port. Reading the data port returns whichever
// register was last selected - that is how the parallel ports get read.
//
// Port handlers receive the ABSOLUTE port number, unlike memory handlers.
// ---------------------------------------------------------------------------
PORT_READ(generic_readport)
PORT_ADDR(0x01, 0x01, ay8910_0_data_port_r)
PORT_END

PORT_WRITE(generic_writeport)
PORT_ADDR(0x00, 0x00, ay8910_0_control_port_w)
PORT_ADDR(0x01, 0x01, ay8910_0_data_port_w)
PORT_END

// ---------------------------------------------------------------------------
// Second Z80 (sound CPU) - scaffolding, commented out.
//
// A very common arrangement: the main CPU writes a command to a latch and
// pulses the sound CPU's NMI; the sound CPU reads the latch and drives the
// AY itself. If you enable this, move the AY wiring above onto THIS CPU's
// maps and replace the AAE_CPU_NONE_ENTRY() in slot 1 further down.
// ---------------------------------------------------------------------------
//static UINT8 generic_soundlatch = 0;
//
//WRITE_HANDLER(generic_soundlatch_w)
//{
//	generic_soundlatch = data;
//	cpu_do_int_imm(1, INT_TYPE_NMI);   // kick the sound CPU
//}
//
//READ_HANDLER(generic_soundlatch_r)
//{
//	return generic_soundlatch;
//}
//
//MEM_READ(generic_sound_readmem)
//MEM_ADDR(0x0000, 0x1fff, MRA_ROM)
//MEM_ADDR(0x2000, 0x23ff, MRA_RAM)
//MEM_ADDR(0x3000, 0x3000, generic_soundlatch_r)
//MEM_END
//
//MEM_WRITE(generic_sound_writemem)
//MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
//MEM_ADDR(0x2000, 0x23ff, MWA_RAM)
//MEM_END
//
//static void generic_sound_interrupt(void)
//{
//	// NMI is driven by the latch write above; nothing periodic needed.
//}

// ---------------------------------------------------------------------------
// Interrupts
//
// Most Z80 boards of this era take one VBLANK interrupt per frame, gated by a
// software-controlled enable latch. interrupt_enable_w maintains that gate;
// this callback just fires the interrupt each frame.
// ---------------------------------------------------------------------------
static void generic_interrupt(void)
{
	cpu_do_int_imm(0, INT_TYPE_INT);
	// Boards that use NMI for VBLANK instead:
	//cpu_do_int_imm(0, INT_TYPE_NMI);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
int init_generic(void)
{
	// Point the shared raster globals into the Z80 address space. The engine
	// draws from these, so they must match the memory map above.
	videoram       = &Machine->memory_region[CPU0][GENERIC_VIDEORAM_BASE];
	videoram_size  = GENERIC_VIDEORAM_SIZE;
	spriteram      = &Machine->memory_region[CPU0][GENERIC_SPRITERAM_BASE];
	spriteram_size = GENERIC_SPRITERAM_SIZE;

	generic_flipscreen = 0;

	if (generic_video_start() != 0)
		return 1;

	ay8910_sh_start(&generic_ay8910_cfg);

	LOG_INFO("generic: init complete");
	return 0;
}

// Called once per frame, after the CPUs have run and before the frame is
// composited.
void run_generic(void)
{
	generic_vh_screenrefresh();
	ay8910_sh_update();
}

void end_generic(void)
{
	ay8910_sh_stop();
}

// ---------------------------------------------------------------------------
// Inputs
//
// Four ports matching the memory map's ip_port_0_r .. ip_port_3_r. Active
// level matters: boards differ, and a whole game "not responding" is usually
// an inverted IP_ACTIVE_LOW / IP_ACTIVE_HIGH.
// ---------------------------------------------------------------------------
INPUT_PORTS_START(generic)
PORT_START("IN0")       // coins, start buttons, service
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START2)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN1")       // player 1
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW0")      // DIP bank 0
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x04, "20000")
PORT_DIPSETTING(0x08, "30000")
PORT_DIPSETTING(0x0c, "None")   // no STR_None token; DEF_STR only covers inptport.h's table
PORT_DIPNAME(0x30, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x10, "Normal")
PORT_DIPSETTING(0x20, "Hard")
PORT_DIPSETTING(0x30, "Hardest")
PORT_DIPNAME(0x40, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x40, DEF_STR(Cocktail))
PORT_DIPNAME(0x80, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x80, DEF_STR(On))

PORT_START("DSW1")      // DIP bank 1
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x00, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x03, DEF_STR(Free_Play))
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs
//
// PLACEHOLDER - these names and CRCs are invented and will fail to load.
// Copy the real ROM_LOAD lines out of the MAME driver.
//
// Region sizes must cover the highest offset loaded into them: REGION_CPU1 is
// 0x10000 for a Z80's full 64k map even when the ROMs stop well short.
// ---------------------------------------------------------------------------
ROM_START(generic)
ROM_REGION(0x10000, REGION_CPU1, 0)      // main Z80, 64k address space
ROM_LOAD("generic.1", 0x0000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))
ROM_LOAD("generic.2", 0x1000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))
ROM_LOAD("generic.3", 0x2000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))
ROM_LOAD("generic.4", 0x3000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))

// Second Z80's ROMs, if you enable the sound CPU:
//ROM_REGION(0x10000, REGION_CPU2, 0)
//ROM_LOAD("generic.s1", 0x0000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))

ROM_REGION(0x2000, REGION_GFX1, 0)       // characters + sprites, 2 bitplanes
ROM_LOAD("generic.5", 0x0000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))
ROM_LOAD("generic.6", 0x1000, 0x1000, CRC(00000000) SHA1(0000000000000000000000000000000000000000))

ROM_REGION(0x0020, REGION_PROMS, 0)      // colour PROM
ROM_LOAD("generic.clr", 0x0000, 0x0020, CRC(00000000) SHA1(0000000000000000000000000000000000000000))
ROM_END

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_generic, "generic", "Generic Z80 / AY-8910 Template")
AAE_DRIVER_ROM(rom_generic)
AAE_DRIVER_FUNCS(&init_generic, &run_generic, &end_generic)
AAE_DRIVER_INPUT(input_ports_generic)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: main Z80
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     GENERIC_MAIN_CLOCK,
		/*div*/      100,
		/*ipf*/      1,                     // interrupt passes per frame
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &generic_interrupt,
		/*r8*/       generic_readmem,
		/*w8*/       generic_writemem,
		/*pr*/       generic_readport,
		/*pw*/       generic_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: sound Z80 - swap this NONE entry for the block below to enable.
	//AAE_CPU_ENTRY(
	//	CPU_MZ80, GENERIC_MAIN_CLOCK, 100, 1, INT_TYPE_NONE, &generic_sound_interrupt,
	//	generic_sound_readmem, generic_sound_writemem,
	//	nullptr, nullptr, nullptr, nullptr
	//),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(GENERIC_FPS, DEFAULT_60HZ_VBLANK_DURATION,
	VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)

// Full screen, then the visible window (trims two tile rows top and bottom).
// Vertical games add ORIENTATION_ROTATE_90 to the line above instead of
// changing these numbers.
AAE_DRIVER_SCREEN(GENERIC_SCREEN_W, GENERIC_SCREEN_H, 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1)

// gfxdecode, total palette colours, colortable length, PROM converter
AAE_DRIVER_RASTER(generic_gfxdecodeinfo, 32, 8 * 4, generic_vh_convert_color_prom)

AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_generic)
