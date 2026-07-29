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
// Donkey Kong (Nintendo, 1981).
//   CPU0  Z80    main   @ 3.072 MHz   (NMI/frame, gated by 7d84)
//   CPU1  i8035  sound  @ 6MHz/15     (background music + "dead" tune via a
//                                      software-enveloped DAC; EXT IRQ from main)
// Discrete walk/jump/boom effects are WAV samples (effect00/01/02.wav).
// Raster video: 8x8 chars (REGION_GFX1) + 16x16 sprites (REGION_GFX2), 2bpp,
// per-column color-code PROM, palette + sprite bank.
//
// Ported from dkong_temp/dkong_driver.c, dkong.video.c and dkong_audio.c.
//==========================================================================

#include "aae_mame_driver.h"
#include "mixer.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "cpu_i8039.h"
#include "memory.h"
#include "dac.h"
#include "old_mame_raster.h"
#include <math.h>

#pragma warning(disable : 4838 4003 4244)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int dkong_intenable = 0;
static int flipscreen = 0;
static int palette_bank = 0;
static int gfx_bank = 0;   // always 0 for plain dkong (Jr/3 bank the chars)

// Per-column character color-code PROM (dkong.5f). Copied out of the PROM
// region so it survives independently of region lifetime.
static unsigned char color_codes[256] = { 0 };

// ---- Sound CPU plumbing (the i8035 reads these; the main Z80 writes them) ----
static int dkong_soundlatch = 0;          // background tune select
static int page = 0;          // i8035 P2 bank/decay/status latch
static int mcustatus = 0;          // status bit fed back to main IN2.6
static int p[8] = { 255,255,255,255,255,255,255,255 };
static int t[2] = { 1, 1 };

// DAC envelope (the i8035 software-shapes its DAC output)
static double envelope = 0.0, tt = 0.0;
static int    decay = 0;
#define TSTEP 0.001

#define ACTIVELOW_PORT_BIT(P,A,D)   ((P & (~(1 << A))) | ((D ^ 1) << A))

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void dkong_interrupt()       // CPU0, once/frame, gated by the 7d84 enable latch
{
	if (dkong_intenable) cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

void dkong_noop_interrupt() {}   // i8035: IRQ is externally triggered from main

// ---------------------------------------------------------------------------
// Main-CPU write handlers
// ---------------------------------------------------------------------------
WRITE_HANDLER(dkong_videoram_w) { videoram_w(address, data); }

// Driver-private, deliberately NOT the exported interrupt_enable_w from
// cpu_control.h (defined at cpu_control.cpp:402). It used to reuse that name,
// which meant the extern declaration was in scope and then a static of the
// same name was defined - accepted by MSVC, rejected by g++. Renamed rather
// than made public: two external definitions would be a link-time duplicate.
WRITE_HANDLER(dkong_interrupt_enable_w) { dkong_intenable = data & 1; }

WRITE_HANDLER(dkong_flipscreen_w)
{
	if (flipscreen != (~data & 1)) {
		flipscreen = ~data & 1;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

WRITE_HANDLER(dkong_palettebank_w)   // 7d86/7d87: two-bit palette bank
{
	int newbank = palette_bank;
	if (data & 1) newbank |= (1 << address);
	else          newbank &= ~(1 << address);

	if (palette_bank != newbank) {
		palette_bank = newbank;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

// ---- Sound: main-CPU side ----
WRITE_HANDLER(dkong_sh_tuneselect) { dkong_soundlatch = data ^ 0x0f; }

WRITE_HANDLER(dkong_sh1_w)           // 7d00/01/02: walk / jump / boom samples
{
	static int state[8];
	if (state[address] != data) {
		if (data) sample_start(address, address, 0);
		state[address] = data;
	}
}

WRITE_HANDLER(dkong_sh_sound3) { p[2] = ACTIVELOW_PORT_BIT(p[2], 5, data); } // jumping
WRITE_HANDLER(dkong_sh_sound4) { t[1] = ~data & 1; }                         // gorilla falling
WRITE_HANDLER(dkong_sh_sound5) { t[0] = ~data & 1; }                         // jump select

WRITE_HANDLER(dkong_sh_w)            // 7d80: "dead" tune -> i8035 external IRQ (rising edge)
{
	// The game holds 0x7d80 asserted for the whole death sequence (it re-writes
	// the latch every frame). Fire the 8035 EXT interrupt only on the 0->nonzero
	// edge: a held strobe otherwise re-arms a pending IRQ during the (long)
	// death-jingle ISR, which re-fires after the ISR's RETR and plays the jingle
	// twice. (MAME's MCS-48 core coalesces this; ours latches the stale request.)
	static int last = 0;
	if (data && !last) cpu_do_int_imm(CPU1, INT_TYPE_INT);
	last = data;
}

// IN2 read merges the i8035 status bit into bit 6.
READ_HANDLER(dkong_in2_r) { return (UINT8)(readinputport(2) | (mcustatus << 6)); }

// ---------------------------------------------------------------------------
// Sound-CPU (i8035) port handlers. MAME-compat port numbers:
//   P1=0x101  P2=0x102  T0=0x110  T1=0x111 ; MOVX @Rr hits ports 0x00..0xff.
// ---------------------------------------------------------------------------
static UINT16 dkong_sh_getp1(UINT16, struct z80PortRead*) { return (UINT16)p[1]; }
static UINT16 dkong_sh_getp2(UINT16, struct z80PortRead*) { return (UINT16)p[2]; }
static UINT16 dkong_sh_gett0(UINT16, struct z80PortRead*) { return (UINT16)t[0]; }
static UINT16 dkong_sh_gett1(UINT16, struct z80PortRead*) { return (UINT16)t[1]; }

// MOVX read: either the latched tune select (page bank 0x40, offset 0x20) or a
// byte of the compressed sample ROM (REGION_CPU2 0x800.., banked by page&7).
static UINT16 dkong_sh_gettune(UINT16 port, struct z80PortRead*)
{
	unsigned char* SND = Machine->memory_region[CPU1];
	if ((page & 0x40) && (port == 0x20))
		return (UINT16)dkong_soundlatch;
	return (UINT16)SND[0x800 + (page & 7) * 256 + (port & 0xff)];
}

// P1 write -> enveloped DAC output.
static void dkong_sh_putp1(UINT16, UINT8 data, struct z80PortWrite*)
{
	envelope = exp(-tt);
	DAC_data_w(0, (int)(data * envelope));
	if (decay) tt += TSTEP; else tt = 0.0;
}

// P2 write -> external decay (b7), sample-ROM bank/enable (b6,b2-0), status (b4).
static void dkong_sh_putp2(UINT16, UINT8 data, struct z80PortWrite*)
{
	decay = !(data & 0x80);
	page = (data & 0x47);
	mcustatus = ((~data & 0x10) >> 4);
}

// ---------------------------------------------------------------------------
// GFX
// ---------------------------------------------------------------------------
static struct GfxLayout dkong_charlayout =
{
	8, 8,           // 8x8 characters
	256,            // 256 characters
	2,              // 2 bits per pixel
	{ 256 * 8 * 8, 0 },                       // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8           // every char takes 8 consecutive bytes
};
static struct GfxLayout dkong_spritelayout =
{
	16, 16,         // 16x16 sprites
	128,            // 128 sprites
	2,              // 2 bits per pixel
	{ 128 * 16 * 16, 0 },                     // the two bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7,                 // the two halves of the sprite are separated
		64 * 16 * 16 + 0, 64 * 16 * 16 + 1, 64 * 16 * 16 + 2, 64 * 16 * 16 + 3,
		64 * 16 * 16 + 4, 64 * 16 * 16 + 5, 64 * 16 * 16 + 6, 64 * 16 * 16 + 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
		8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
	16 * 8          // every sprite takes 16 consecutive bytes
};

struct GfxDecodeInfo dkong_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &dkong_charlayout,   0, 64 },
	{ REGION_GFX2, 0x0000, &dkong_spritelayout, 0, 64 },
	{ -1 }
};

// Two 256x4 palette PROMs + one 256x4 per-column color-code PROM. The palette
// PROMs feed inverted RGB through the resistor network below.
void dkong_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
	for (i = 0; i < 256; i++)
	{
		int bit0, bit1, bit2;
		// red
		bit0 = (color_prom[256] >> 1) & 1;
		bit1 = (color_prom[256] >> 2) & 1;
		bit2 = (color_prom[256] >> 3) & 1;
		*(palette++) = 255 - (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		// green
		bit0 = (color_prom[0] >> 2) & 1;
		bit1 = (color_prom[0] >> 3) & 1;
		bit2 = (color_prom[256] >> 0) & 1;
		*(palette++) = 255 - (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		// blue
		bit0 = (color_prom[0] >> 0) & 1;
		bit1 = (color_prom[0] >> 1) & 1;
		*(palette++) = 255 - (0x55 * bit0 + 0xaa * bit1);

		color_prom++;
	}

	// color_prom now points past both palette PROMs, at the per-column codes.
	color_prom += 256;
	memcpy(color_codes, color_prom, 256);

	// DK derives gfx colors at draw time from color_codes, so the gfx-pen
	// lookup table is a straight identity map.
	for (i = 0; i < 256; i++) colortable[i] = (unsigned char)i;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
int dkong_vh_start(void)
{
	gfx_bank = 0;
	palette_bank = 0;
	videoram_size = 0x400;
	return generic_vh_start();
}

static void dkong_draw_tiles()
{
	int offs;
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, charcode, color;
			dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;

			charcode = videoram[offs] + 256 * gfx_bank;
			color = (color_codes[offs % 32 + 32 * (offs / 32 / 4)] & 0x0f) + 0x10 * palette_bank;

			if (flipscreen) { sx = 31 - sx; sy = 31 - sy; }

			drawgfx(tmpbitmap, Machine->gfx[0],
				charcode, color,
				flipscreen, flipscreen,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}

static void dkong_draw_sprites()
{
	int offs;
	for (offs = 0; offs < spriteram_size; offs += 4)
	{
		if (spriteram[offs])
		{
			int x = spriteram[offs + 3] - 8;
			int y = 240 - spriteram[offs] + 7;

			if (flipscreen)
			{
				x = 240 - x;
				y = 240 - y;
				drawgfx(main_bitmap, Machine->gfx[1],
					(spriteram[offs + 1] & 0x7f) + 2 * (spriteram[offs + 2] & 0x40),
					(spriteram[offs + 2] & 0x0f) + 16 * palette_bank,
					!(spriteram[offs + 2] & 0x80), !(spriteram[offs + 1] & 0x80),
					x, y,
					&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
			}
			else
			{
				drawgfx(main_bitmap, Machine->gfx[1],
					(spriteram[offs + 1] & 0x7f) + 2 * (spriteram[offs + 2] & 0x40),
					(spriteram[offs + 2] & 0x0f) + 16 * palette_bank,
					(spriteram[offs + 2] & 0x80), (spriteram[offs + 1] & 0x80),
					x, y,
					&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
			}
		}
	}
}

void dkong_vh_screenrefresh()
{
	dkong_draw_tiles();
	dkong_draw_sprites();
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------
// Flat RAM lives inside each CPU's memory_region and needs no handler; only
// ROM protection, dirty-tracked video writes, and I/O get handlers.
MEM_READ(DkongMainRead)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x7c00, 0x7c00, ip_port_0_r)   // IN0
MEM_ADDR(0x7c80, 0x7c80, ip_port_1_r)   // IN1
MEM_ADDR(0x7d00, 0x7d00, dkong_in2_r)   // IN2 (+ sound-cpu status)
MEM_ADDR(0x7d80, 0x7d80, ip_port_3_r)   // DSW1
MEM_END

MEM_WRITE(DkongMainWrite)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x7400, 0x77ff, dkong_videoram_w)
MEM_ADDR(0x7c00, 0x7c00, dkong_sh_tuneselect)
MEM_ADDR(0x7d00, 0x7d02, dkong_sh1_w)     // walk/jump/boom sample triggers
MEM_ADDR(0x7d03, 0x7d03, dkong_sh_sound3)
MEM_ADDR(0x7d04, 0x7d04, dkong_sh_sound4)
MEM_ADDR(0x7d05, 0x7d05, dkong_sh_sound5)
MEM_ADDR(0x7d80, 0x7d80, dkong_sh_w)
MEM_ADDR(0x7d82, 0x7d82, dkong_flipscreen_w)
MEM_ADDR(0x7d84, 0x7d84, dkong_interrupt_enable_w)
MEM_ADDR(0x7d86, 0x7d87, dkong_palettebank_w)
MEM_END

// i8035 sound CPU: program ROM (REGION_CPU2 0x000-0x7ff) via MEM fallback.
MEM_READ(DkongSoundRead)
MEM_END

MEM_WRITE(DkongSoundWrite)
MEM_ADDR(0x0000, 0x0fff, MWA_ROM)
MEM_END

PORT_READ(DkongSoundPortRead)
PORT_ADDR(0x00, 0xff, dkong_sh_gettune)   // MOVX @Rr -> tune/sample ROM
PORT_ADDR(0x101, 0x101, dkong_sh_getp1)
PORT_ADDR(0x102, 0x102, dkong_sh_getp2)
PORT_ADDR(0x110, 0x110, dkong_sh_gett0)
PORT_ADDR(0x111, 0x111, dkong_sh_gett1)
PORT_END

PORT_WRITE(DkongSoundPortWrite)
PORT_ADDR(0x101, 0x101, dkong_sh_putp1)     // P1 -> DAC
PORT_ADDR(0x102, 0x102, dkong_sh_putp2)     // P2 -> bank/decay/status
PORT_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static struct DACinterface dkong_dac_interface =
{
	1,
	{ 55 }
};

static const char* dkong_samples[] =
{
	"dkong.zip",
	"effect00.wav",   // walk
	"effect01.wav",   // jump
	"effect02.wav",   // boom (gorilla stomp)
	0
};

int init_dkong()
{
	LOG_INFO("Starting Donkey Kong Init");

	dkong_intenable = 0;
	flipscreen = 0;
	palette_bank = 0;
	gfx_bank = 0;
	dkong_soundlatch = 0;
	page = 0;
	mcustatus = 0;
	for (int i = 0; i < 8; i++) p[i] = 255;
	t[0] = t[1] = 1;
	envelope = 0.0; tt = 0.0; decay = 0;

	// Flat RAM lives in the CPU0 region; point the framework globals at it.
	videoram = &Machine->memory_region[CPU0][0x7400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[CPU0][0x6900];
	spriteram_size = 0x180;

	DAC_sh_start(&dkong_dac_interface);
	dkong_vh_start();

	LOG_INFO("End Donkey Kong Init");
	return 0;
}

void run_dkong()
{
	watchdog_reset_w(0, 0, 0);
	dkong_vh_screenrefresh();
	DAC_sh_update();
}

void end_dkong()
{
	DAC_sh_stop();
	generic_vh_stop();
}

// ---------------------------------------------------------------------------
// Input ports (bits are NOT inverted -> IP_ACTIVE_HIGH)
// ---------------------------------------------------------------------------
INPUT_PORTS_START(dkong)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0xe0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0xe0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN2")
//PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_SERVICE)
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x30, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)   // status from sound cpu (merged in dkong_in2_r)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_COIN1)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "7000")
PORT_DIPSETTING(0x04, "10000")
PORT_DIPSETTING(0x08, "15000")
PORT_DIPSETTING(0x0c, "20000")
PORT_DIPNAME(0x70, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x70, DEF_STR(5C_1C))
PORT_DIPSETTING(0x50, DEF_STR(4C_1C))
PORT_DIPSETTING(0x30, DEF_STR(3C_1C))
PORT_DIPSETTING(0x10, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x20, DEF_STR(1C_2C))
PORT_DIPSETTING(0x40, DEF_STR(1C_3C))
PORT_DIPSETTING(0x60, DEF_STR(1C_4C))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------

ROM_START(dkong)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("c_5et_g.bin", 0x0000, 0x1000, CRC(ba70b88b) SHA1(d76ebecfea1af098d843ee7e578e480cd658ac1a))
ROM_LOAD("c_5ct_g.bin", 0x1000, 0x1000, CRC(5ec461ec) SHA1(acb11a8fbdbb3ab46068385fe465f681e3c824bd))
ROM_LOAD("c_5bt_g.bin", 0x2000, 0x1000, CRC(1c97d324) SHA1(c7966261f3a1d3296927e0b6ee1c58039fc53c1f))
ROM_LOAD("c_5at_g.bin", 0x3000, 0x1000, CRC(b9005ac0) SHA1(3fe3599f6fa7c496f782053ddf7bacb453d197c4))

ROM_REGION(0x1000, REGION_CPU2, 0) /* sound */
ROM_LOAD("s_3i_b.bin", 0x0000, 0x0800, CRC(45a4ed06) SHA1(144d24464c1f9f01894eb12f846952290e6e32ef))
ROM_LOAD("s_3j_b.bin", 0x0800, 0x0800, CRC(4743fe92) SHA1(6c82b57637c0212a580591397e6a5a1718f19fd2))

ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("v_5h_b.bin", 0x0000, 0x0800, CRC(12c8c95d) SHA1(a57ff5a231c45252a63b354137c920a1379b70a3))
ROM_LOAD("v_3pt.bin", 0x0800, 0x0800, CRC(15e9c5e9) SHA1(976eb1e18c74018193a35aa86cff482ebfc5cc4e))

ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("l_4m_b.bin", 0x0000, 0x0800, CRC(59f8054d) SHA1(793dba9bf5a5fe76328acdfb90815c243d2a65f1))
ROM_LOAD("l_4n_b.bin", 0x0800, 0x0800, CRC(672e4714) SHA1(92e5d379f4838ac1fa44d448ce7d142dae42102f))
ROM_LOAD("l_4r_b.bin", 0x1000, 0x0800, CRC(feaa59ee) SHA1(ecf95db5a20098804fc8bd59232c66e2e0ed3db4))
ROM_LOAD("l_4s_b.bin", 0x1800, 0x0800, CRC(20f2ef7e) SHA1(3bc482a38bf579033f50082748ee95205b0f673d))

ROM_REGION(0x0300, REGION_PROMS, 0)
ROM_LOAD("c-2k.bpr", 0x0000, 0x0100, CRC(e273ede5) SHA1(b50ec9e1837c00c20fb2a4369ec7dd0358321127))
ROM_LOAD("c-2j.bpr", 0x0100, 0x0100, CRC(d6412358) SHA1(f9c872da2fe8e800574ae3bf483fb3ccacc92eb3))
ROM_LOAD("v-5e.bpr", 0x0200, 0x0100, CRC(b869b8f5) SHA1(c2bdccbf2654b64ea55cd589fd21323a9178a660))
ROM_END

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_dkong, "dkong", "Donkey Kong (US)")
AAE_DRIVER_ROM(rom_dkong)
AAE_DRIVER_FUNCS(&init_dkong, &run_dkong, &end_dkong)
AAE_DRIVER_INPUT(input_ports_dkong)
AAE_DRIVER_SAMPLES(dkong_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_NMI, &dkong_interrupt,
		DkongMainRead, DkongMainWrite, nullptr, nullptr, nullptr, nullptr),
	// CPU1: i8035 sound (DAC)
	AAE_CPU_ENTRY(CPU_8035, 6000000 / 15, 100, 1, INT_TYPE_NONE, &dkong_noop_interrupt,
		DkongSoundRead, DkongSoundWrite, DkongSoundPortRead, DkongSoundPortWrite, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239)
AAE_DRIVER_RASTER(dkong_gfxdecodeinfo, 256, 256, dkong_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_dkong)

// ===========================================================================
// Donkey Kong Junior (Nintendo, 1982)
//
// Same base board as Donkey Kong (Z80 main + i8035 + raster), reusing dkong's
// read map, 8035 sound ports/DAC, video, palette PROM conversion and input
// ports. Differences:
//   - Effects are WAV samples (jump/land/roar/climb/death/drop/snapjaw); the
//     i8035 still plays the background music via the DAC. The tune select is
//     NOT inverted here (dkong uses ^0x0f).
//   - 512 characters with a gfx-bank bit at 0x7c80.
//   - Death/drop are samples, so the dkong 0x7d80 EXT-IRQ path is unused.
// Sound handlers ported from dkong_temp/dkong_audio.c; map from dkong_driver.c.
// ===========================================================================

static int dkongjr_walk = 0;   // 0 = climbing, 1 = walking (selects climb sample)

WRITE_HANDLER(dkongjr_sh_tuneselect) { dkong_soundlatch = data; }   // no ^0x0f

WRITE_HANDLER(dkongjr_gfxbank_w)     // 0x7c80: character bank select
{
	if (gfx_bank != (data & 1)) {
		gfx_bank = data & 1;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

WRITE_HANDLER(dkongjr_sh_test6) { p[2] = ACTIVELOW_PORT_BIT(p[2], 6, data); }

// Sample-driven effects. Channel / sample-number pairs match the MAME layout
// (and the brace quirks where sample_start runs on any edge are preserved).
WRITE_HANDLER(dkongjr_sh_death_w)
{
	static int death = 0;
	if (death != data) {
		if (data) sample_stop(7);
		sample_start(6, 4, 0);
		death = data;
	}
}

WRITE_HANDLER(dkongjr_sh_drop_w)
{
	static int drop = 0;
	if (drop != data) {
		if (data) sample_start(7, 5, 0);
		drop = data;
	}
}

WRITE_HANDLER(dkongjr_sh_roar_w)
{
	static int roar = 0;
	if (roar != data) {
		if (data) sample_start(7, 2, 0);
		roar = data;
	}
}

WRITE_HANDLER(dkongjr_sh_jump_w)
{
	static int jump = 0;
	if (jump != data) {
		if (data) sample_start(6, 0, 0);
		jump = data;
	}
}

WRITE_HANDLER(dkongjr_sh_land_w)
{
	static int land = 0;
	if (land != data) {
		if (data) sample_stop(7);
		sample_start(4, 1, 0);
		land = data;
	}
}

WRITE_HANDLER(dkongjr_sh_climb_w)
{
	static int climb = 0;
	if (climb != data) {
		if (data && dkongjr_walk == 0)      sample_start(3, 3, 0);
		else if (data && dkongjr_walk == 1) sample_start(3, 6, 0);
		climb = data;
	}
}

WRITE_HANDLER(dkongjr_sh_snapjaw_w)
{
	static int snapjaw = 0;
	if (snapjaw != data) {
		if (data) sample_stop(7);
		sample_start(4, 7, 0);
		snapjaw = data;
	}
}

WRITE_HANDLER(dkongjr_sh_walk_w) { if (dkongjr_walk != data) dkongjr_walk = data; }

// 512 8x8 chars (banked); sprites are identical to dkong.
static struct GfxLayout dkongjr_charlayout =
{
	8, 8,
	512,
	2,
	{ 512 * 8 * 8, 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8
};

struct GfxDecodeInfo dkongjr_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &dkongjr_charlayout, 0, 64 },
	{ REGION_GFX2, 0x0000, &dkong_spritelayout, 0, 64 },
	{ -1 }
};

// Write map. Reads reuse DkongMainRead (0x4000-0x5fff ROM falls through to the
// region). Code spans 0x0000-0x5fff on Jr, so ROM-protect that whole range.
MEM_WRITE(DkongjrMainWrite)
MEM_ADDR(0x0000, 0x5fff, MWA_ROM)
MEM_ADDR(0x7400, 0x77ff, dkong_videoram_w)
MEM_ADDR(0x7c00, 0x7c00, dkongjr_sh_tuneselect)
MEM_ADDR(0x7c80, 0x7c80, dkongjr_gfxbank_w)
MEM_ADDR(0x7c81, 0x7c81, dkongjr_sh_test6)
MEM_ADDR(0x7d00, 0x7d00, dkongjr_sh_climb_w)
MEM_ADDR(0x7d01, 0x7d01, dkongjr_sh_jump_w)
MEM_ADDR(0x7d02, 0x7d02, dkongjr_sh_land_w)
MEM_ADDR(0x7d03, 0x7d03, dkongjr_sh_roar_w)
MEM_ADDR(0x7d04, 0x7d04, dkong_sh_sound4)
MEM_ADDR(0x7d05, 0x7d05, dkong_sh_sound5)
MEM_ADDR(0x7d06, 0x7d06, dkongjr_sh_snapjaw_w)
MEM_ADDR(0x7d07, 0x7d07, dkongjr_sh_walk_w)
MEM_ADDR(0x7d80, 0x7d80, dkongjr_sh_death_w)
MEM_ADDR(0x7d81, 0x7d81, dkongjr_sh_drop_w)
MEM_ADDR(0x7d82, 0x7d82, dkong_flipscreen_w)
MEM_ADDR(0x7d84, 0x7d84, dkong_interrupt_enable_w)
MEM_ADDR(0x7d86, 0x7d87, dkong_palettebank_w)
MEM_END

static const char* dkongjr_samples[] =
{
	"dkongjr.zip",
	"jump.wav",     // 0
	"land.wav",     // 1
	"roar.wav",     // 2
	"climb.wav",    // 3
	"death.wav",    // 4
	"drop.wav",     // 5
	"walk.wav",     // 6
	"snapjaw.wav",  // 7
	0
};

int init_dkongjr()
{
	LOG_INFO("Starting Donkey Kong Junior Init");

	dkong_intenable = 0;
	flipscreen = 0;
	palette_bank = 0;
	gfx_bank = 0;
	dkong_soundlatch = 0;
	page = 0;
	mcustatus = 0;
	dkongjr_walk = 0;
	for (int i = 0; i < 8; i++) p[i] = 255;
	t[0] = t[1] = 1;
	envelope = 0.0; tt = 0.0; decay = 0;

	videoram = &Machine->memory_region[CPU0][0x7400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[CPU0][0x6900];
	spriteram_size = 0x180;

	DAC_sh_start(&dkong_dac_interface);
	dkong_vh_start();

	LOG_INFO("End Donkey Kong Junior Init");
	return 0;
}

// NOTE: classic MAME ROM names as placeholders - update to the latest set.
// The main program ROMs use ROM_CONTINUE (interleaved load) like MAME.
ROM_START(dkongjro)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("dkj.5b", 0x0000, 0x1000, CRC(dea28158))
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("dkj.5c", 0x2000, 0x0800, CRC(6fb5faf6))
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("dkj.5e", 0x4000, 0x0800, CRC(d042b6a8))
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)

ROM_REGION(0x1000, REGION_CPU2, 0) /* sound */
ROM_LOAD("dkj.3h", 0x0000, 0x1000, CRC(715da5f8))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("dkj.3n", 0x0000, 0x1000, CRC(8d51aca9))
ROM_LOAD("dkj.3p", 0x1000, 0x1000, CRC(4ef64ba5))

ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("dkj.7c", 0x0000, 0x0800, CRC(dc7f4164))
ROM_LOAD("dkj.7d", 0x0800, 0x0800, CRC(0ce7dcf6))
ROM_LOAD("dkj.7e", 0x1000, 0x0800, CRC(24d1ff17))
ROM_LOAD("dkj.7f", 0x1800, 0x0800, CRC(0f8c083f))

ROM_REGION(0x0300, REGION_PROMS, 0)
ROM_LOAD("dkjrprom.2e", 0x0000, 0x0100, CRC(463dc7ad))  // palette low 4 bits (inverted)
ROM_LOAD("dkjrprom.2f", 0x0100, 0x0100, CRC(47ba0042))  // palette high 4 bits (inverted)
ROM_LOAD("dkjrprom.2n", 0x0200, 0x0100, CRC(dbf185bf))  // per-column character color codes
ROM_END

ROM_START(dkongjr)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("djr1-c_5b_f-2.5b", 0x0000, 0x1000, CRC(dea28158) SHA1(08baf84ae6f9b40a2c743fe1d8c158c74a40e95a))
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("djr1-c_5c_f-2.5c", 0x2000, 0x0800, CRC(6fb5faf6) SHA1(ce1cfde71a9e2a8b5896a6301d386f72869a1d2e))
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("djr1-c_5e_f-2.5e", 0x4000, 0x0800, CRC(d042b6a8) SHA1(57ac237d273496b44220b4437118115ef11dbd9f))
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)
//empty socket on position 5A on pcb 0x8000, 0x1000

ROM_REGION(0x1000, REGION_CPU2, 0) /* sound */
ROM_LOAD("djr1-c_3h.3h", 0x0000, 0x1000, CRC(715da5f8) SHA1(f708c3fd374da65cbd9fe2e191152f5d865414a0))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("djr1-v.3n", 0x0000, 0x1000, CRC(8d51aca9) SHA1(64887564b079d98e98aafa53835e398f34fe4e3f))
ROM_LOAD("djr1-v.3p", 0x1000, 0x1000, CRC(4ef64ba5) SHA1(41a7a4005087951f57f62c9751d62a8c495e6bb3))

ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("djr1-v_7c.7c", 0x0000, 0x0800, CRC(dc7f4164) SHA1(07a6242e95b5c3b8dfdcd4b4950f463dba16dd77))
ROM_LOAD("djr1-v_7d.7d", 0x0800, 0x0800, CRC(0ce7dcf6) SHA1(0654b77526c49f0dfa077ac4f1f69cf5cb2e2f64))
ROM_LOAD("djr1-v_7e.7e", 0x1000, 0x0800, CRC(24d1ff17) SHA1(696854bf3dc5447d33b4815db357e6ce3834d867))
ROM_LOAD("djr1-v_7f.7f", 0x1800, 0x0800, CRC(0f8c083f) SHA1(0b688ae9da296b2447fffa5e135fd6a56ec3e790))

ROM_REGION(0x0300, REGION_PROMS, 0)
ROM_LOAD("djr1-c-2e.2e", 0x0000, 0x0100, CRC(463dc7ad) SHA1(b2c9f22facc8885be2d953b056eb8dcddd4f34cb))   /* palette low 4 bits (inverted) */
ROM_LOAD("djr1-c-2f.2f", 0x0100, 0x0100, CRC(47ba0042) SHA1(dbec3f4b8013628c5b8f83162e5f8b1f82f6ee5f))   /* palette high 4 bits (inverted) */
ROM_LOAD("djr1-v-2n.2n", 0x0200, 0x0100, CRC(dbf185bf) SHA1(2697a991a4afdf079dd0b7e732f71c7618f43b70))   /* character color codes on a per-column basis */
ROM_END

ROM_START(dkong3)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("dk3c.7b", 0x0000, 0x2000, CRC(38d5f38e) SHA1(5a6bb0e5070211515e3d56bd7d4c2d1655ac1621))
ROM_LOAD("dk3c.7c", 0x2000, 0x2000, CRC(c9134379) SHA1(ecddb3694b93cb3dc98c3b1aeeee928e27529aba))
ROM_LOAD("dk3c.7d", 0x4000, 0x2000, CRC(d22e2921) SHA1(59a4a1a36aaca19ee0a7255d832df9d042ba34fb))
ROM_LOAD("dk3c.7e", 0x8000, 0x2000, CRC(615f14b7) SHA1(145674073e95d97c9131b6f2b03303eadb57ca78))

ROM_REGION(0x10000, REGION_CPU2, 0)  /* sound #1 */
ROM_LOAD("dk3c.5l", 0x8000, 0x2000, CRC(7ff88885) SHA1(d530581778aab260e21f04c38e57ba34edea7c64))

ROM_REGION(0x10000, REGION_CPU3, 0)  /* sound #2 */
ROM_LOAD("dk3c.6h", 0x8000, 0x2000, CRC(36d7200c) SHA1(7965fcb9bc1c0fdcae8a8e79df9c7b7439c506d8))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("dk3v.3n", 0x0000, 0x1000, CRC(415a99c7) SHA1(e0855b03bb1dc0d8ae46da9fe33ca30ecf6a2e96))
ROM_LOAD("dk3v.3p", 0x1000, 0x1000, CRC(25744ea0) SHA1(4866e43e80b010ccf2c8cc94c232786521f9e26e))

ROM_REGION(0x4000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("dk3v.7c", 0x0000, 0x1000, CRC(8ffa1737) SHA1(fa5896124227d412fbdf83f129ddffa32cf2053b))
ROM_LOAD("dk3v.7d", 0x1000, 0x1000, CRC(9ac84686) SHA1(a089376b9c23094490703152ad98ed27f519402d))
ROM_LOAD("dk3v.7e", 0x2000, 0x1000, CRC(0c0af3fb) SHA1(03e0c3f51bc3c20f95cb02f76f2d80188d5dbe36))
ROM_LOAD("dk3v.7f", 0x3000, 0x1000, CRC(55c58662) SHA1(7f3d5a1b386cc37d466e42392ffefc928666a8dc))

ROM_REGION(0x0500, REGION_PROMS, 0)
ROM_LOAD("dkc1-c.1d", 0x0000, 0x0200, CRC(df54befc) SHA1(7912dbf0a0c8ef68f4ae0f95e55ab164da80e4a1)) /* palette red & green component */
ROM_LOAD("dkc1-c.1c", 0x0200, 0x0200, CRC(66a77f40) SHA1(c408d65990f0edd78c4590c447426f383fcd2d88)) /* palette blue component */
ROM_LOAD("dkc1-v.2n", 0x0400, 0x0100, CRC(50e33434) SHA1(b63da9bed9dc4c7da78e4c26d4ba14b65f2b7e72)) /* character color codes on a per-column basis */

ROM_REGION(0x0020, REGION_USER1, 0)
/* address decode prom 18s030 - this has inverted outputs. The dump does not reflect this. */
ROM_LOAD("dkc1-v.5e", 0x0000, 0x0020, CRC(d3e2eaf8) SHA1(87bb298137c26570dafb4ac495c87e82441e70e5))
ROM_END

AAE_DRIVER_BEGIN(drv_dkongjr, "dkongjr", "Donkey Kong Junior (US)")
AAE_DRIVER_ROM(rom_dkongjr)
AAE_DRIVER_FUNCS(&init_dkongjr, &run_dkong, &end_dkong)
AAE_DRIVER_INPUT(input_ports_dkong)
AAE_DRIVER_SAMPLES(dkongjr_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_NMI, &dkong_interrupt,
		DkongMainRead, DkongjrMainWrite, nullptr, nullptr, nullptr, nullptr),
	// CPU1: i8035 sound (background music via DAC)
	AAE_CPU_ENTRY(CPU_8035, 6000000 / 15, 100, 1, INT_TYPE_NONE, &dkong_noop_interrupt,
		DkongSoundRead, DkongSoundWrite, DkongSoundPortRead, DkongSoundPortWrite, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239)
AAE_DRIVER_RASTER(dkongjr_gfxdecodeinfo, 256, 256, dkong_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_dkongjr)