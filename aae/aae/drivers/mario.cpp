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
// Mario Bros. (Nintendo, 1983).  Original MAME driver by Mirko Buffoni.
//   CPU0  Z80    main   @ 3.072 MHz  (NMI/frame, gated by 7e84)
//   CPU1  i8039  sound  @ 730 kHz    (tunes + software DAC; EXT IRQ = death)
// Discrete ice/coin/skid effects and the run samples are WAVs (mario.zip).
// Raster video: 512 8x8 chars (banked, REGION_GFX1) 2bpp + 256 16x16
// sprites (REGION_GFX2) 3bpp, vertical scroll register, palette bank.
//
// Ported from MAME 0.57 drivers/mario.c, vidhrdw/mario.c, sndhrdw/mario.c.
// ROM sets use the MAME 0.286 file names / CRCs (US revision F, Japan
// revision C), so a current mario.zip / marioj.zip loads directly.
//==========================================================================

#include "aae_mame_driver.h"
#include "mixer.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "cpu_i8039.h"
#include "memory.h"
#include "dac.h"
#include "old_mame_raster.h"
#include "sound_latch.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int mario_intenable = 0;
static int gfx_bank = 0;
static int palette_bank = 0;

// Vertical scroll register (0x7d00) lives in flat CPU0 RAM; pointer set in init.
static unsigned char* mario_scrolly = 0;

// ---- Sound CPU plumbing (the i8039 reads these; the main Z80 writes them) ----
// p[1] initial 0xf0 matches MAME 0.57 (crab/turtle/fly bits idle high nibble).
static int p[8] = { 0, 0xf0, 0, 0, 0, 0, 0, 0 };
static int t[2] = { 0, 0 };

#define ACTIVEHIGH_PORT_BIT(P,A,D)   ((P & (~(1 << A))) | ((D & 1) << A))

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void mario_interrupt()        // CPU0, once/frame, gated by the 7e84 enable latch
{
	if (mario_intenable) cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

void mario_noop_interrupt() {}   // i8039: EXT IRQ is triggered from the main CPU

// ---------------------------------------------------------------------------
// Main-CPU write handlers
// ---------------------------------------------------------------------------
WRITE_HANDLER(mario_videoram_w) { videoram_w(address, data); }

WRITE_HANDLER(mario_interrupt_enable_w) { mario_intenable = data & 1; }

WRITE_HANDLER(mario_gfxbank_w)       // 7e80: character bank select
{
	if (gfx_bank != (data & 1)) {
		gfx_bank = data & 1;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

WRITE_HANDLER(mario_palettebank_w)   // 7e83: sprite/char palette bank
{
	if (palette_bank != (data & 1)) {
		palette_bank = data & 1;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

// ---- Sound: main-CPU side ----
WRITE_HANDLER(mario_sh_tuneselect_w) { soundlatch_set(0, data); }   // 7e00

WRITE_HANDLER(mario_sh1_w)           // 7c00: Mario run sample
{
	static int last;
	if (last != data) {
		last = data;
		if (data && sample_playing(0) == 0) sample_start(0, 3, 0);
	}
}

WRITE_HANDLER(mario_sh2_w)           // 7c80: Luigi run sample
{
	static int last;
	if (last != data) {
		last = data;
		if (data && sample_playing(1) == 0) sample_start(1, 4, 0);
	}
}

// 7f00-7f07 sound triggers. address is the offset within the range:
//   0 death (i8039 EXT IRQ)   1 get-coin (T0)      2 ice sample
//   3 crab (P1.0)             4 turtle (P1.1)      5 fly (P1.2)
//   6 coin sample             7 skid sample
WRITE_HANDLER(mario_sh_trigger_w)
{
	static int state[8];

	switch (address)
	{
	case 0:
		// MAME holds the 8039 IRQ line ASSERT/CLEAR (level), and the sound
		// program both vectors on it AND polls the pin with JNI to sequence
		// the death tune and the respawn jingle. Drive the core's actual
		// INT pin level - a one-shot cpu_do_int_imm pulse loses the JNI
		// side and silences both effects.
		if (m_cpu_i8039[CPU1]) m_cpu_i8039[CPU1]->set_int_line(data ? 1 : 0);
		state[0] = data;
		return;
	case 1: t[0] = data; return;
	case 3: p[1] = ACTIVEHIGH_PORT_BIT(p[1], 0, data); return;
	case 4: p[1] = ACTIVEHIGH_PORT_BIT(p[1], 1, data); return;
	case 5: p[1] = ACTIVEHIGH_PORT_BIT(p[1], 2, data); return;
	}

	// Sampled effects (2/6/7) - don't retrigger while the line is held.
	if (state[address] == data) return;
	state[address] = data;
	if (data)
	{
		switch (address)
		{
		case 2: sample_start(2, 0, 0); break;   // ice
		case 6: sample_start(2, 1, 0); break;   // coin
		case 7: sample_start(2, 2, 0); break;   // skid
		}
	}
}

// ---------------------------------------------------------------------------
// Sound-CPU (i8039) port handlers. MAME-compat port numbers:
//   P1=0x101  P2=0x102  T0=0x110  T1=0x111 ; MOVX @Rr hits ports 0x00..0xff.
// ---------------------------------------------------------------------------
static UINT16 mario_sh_getp1(UINT16, struct z80PortRead*) { return (UINT16)p[1]; }
static UINT16 mario_sh_getp2(UINT16, struct z80PortRead*) { return (UINT16)p[2]; }
static UINT16 mario_sh_gett0(UINT16, struct z80PortRead*) { return (UINT16)t[0]; }
static UINT16 mario_sh_gett1(UINT16, struct z80PortRead*) { return (UINT16)t[1]; }

// MOVX read: the latched tune select (MAME 0.57 returns the latch for the
// whole external bus; the tune ROM doubles as the program ROM we execute).
static UINT16 mario_sh_gettune(UINT16, struct z80PortRead*)
{
	return (UINT16)soundlatch_get(0);
}

static void mario_sh_putsound(UINT16, UINT8 data, struct z80PortWrite*)
{
	DAC_data_w(0, data);
}
static void mario_sh_putp1(UINT16, UINT8 data, struct z80PortWrite*) { p[1] = data; }
static void mario_sh_putp2(UINT16, UINT8 data, struct z80PortWrite*) { p[2] = data; }

// ---------------------------------------------------------------------------
// GFX
// ---------------------------------------------------------------------------
static struct GfxLayout mario_charlayout =
{
	8, 8,           // 8x8 characters
	512,            // 512 characters (two banks)
	2,              // 2 bits per pixel
	{ 512 * 8 * 8, 0 },                       // the bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8           // every char takes 8 consecutive bytes
};

static struct GfxLayout mario_spritelayout =
{
	16, 16,         // 16x16 sprites
	256,            // 256 sprites
	3,              // 3 bits per pixel
	{ 2 * 256 * 16 * 16, 256 * 16 * 16, 0 }, // the bitplanes are separated
	{ 0, 1, 2, 3, 4, 5, 6, 7,                // the two halves of the sprite are separated
		256 * 16 * 8 + 0, 256 * 16 * 8 + 1, 256 * 16 * 8 + 2, 256 * 16 * 8 + 3,
		256 * 16 * 8 + 4, 256 * 16 * 8 + 5, 256 * 16 * 8 + 6, 256 * 16 * 8 + 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
		8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
	16 * 8          // every sprite takes 16 consecutive bytes
};

struct GfxDecodeInfo mario_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &mario_charlayout,      0, 16 },
	{ REGION_GFX2, 0x0000, &mario_spritelayout, 16 * 4, 32 },
	{ -1 }
};

// Mario Bros. has a 512x8 palette PROM; bytes 0-255 contain an inverted
// palette (like Donkey Kong), bytes 256-511 a non-inverted one for standard
// monitors. We use the inverted (Nintendo monitor) half:
//   bit 7 -- 220 ohm -- inverter -- RED       bit 4..2 -- GREEN
//   bit 1..0 -- BLUE
void mario_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	for (i = 0; i < 256; i++)
	{
		int bit0, bit1, bit2;
		// red
		bit0 = (*color_prom >> 5) & 1;
		bit1 = (*color_prom >> 6) & 1;
		bit2 = (*color_prom >> 7) & 1;
		*(palette++) = 255 - (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		// green
		bit0 = (*color_prom >> 2) & 1;
		bit1 = (*color_prom >> 3) & 1;
		bit2 = (*color_prom >> 4) & 1;
		*(palette++) = 255 - (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		// blue
		bit0 = (*color_prom >> 0) & 1;
		bit1 = (*color_prom >> 1) & 1;
		*(palette++) = 255 - (0x55 * bit0 + 0xaa * bit1);

		color_prom++;
	}

	// Characters share the sprite palette but only use colors 64-127 and
	// 192-255 (the two palette banks).
	for (i = 0; i < 8; i++)
	{
		COLOR(0, 4 * i)     = (unsigned char)(8 * i + 64);
		COLOR(0, 4 * i + 1) = (unsigned char)(8 * i + 1 + 64);
		COLOR(0, 4 * i + 2) = (unsigned char)(8 * i + 2 + 64);
		COLOR(0, 4 * i + 3) = (unsigned char)(8 * i + 3 + 64);
	}
	for (i = 0; i < 8; i++)
	{
		COLOR(0, 4 * i + 8 * 4)     = (unsigned char)(8 * i + 192);
		COLOR(0, 4 * i + 8 * 4 + 1) = (unsigned char)(8 * i + 1 + 192);
		COLOR(0, 4 * i + 8 * 4 + 2) = (unsigned char)(8 * i + 2 + 192);
		COLOR(0, 4 * i + 8 * 4 + 3) = (unsigned char)(8 * i + 3 + 192);
	}

	// Sprites map straight through.
	for (i = 0; i < 32 * 8; i++)
		COLOR(1, i) = (unsigned char)i;
#undef COLOR
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
int mario_vh_start(void)
{
	gfx_bank = 0;
	palette_bank = 0;
	videoram_size = 0x400;
	return generic_vh_start();
}

void mario_vh_screenrefresh()
{
	int offs;

	// Redraw dirty characters into the scroll buffer (no clip: scrolled-in
	// rows come from outside the visible area).
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy;
			dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;

			drawgfx(tmpbitmap, Machine->gfx[0],
				videoram[offs] + 256 * gfx_bank,
				(videoram[offs] >> 5) + 8 * palette_bank,
				0, 0,
				8 * sx, 8 * sy,
				0, TRANSPARENCY_NONE, 0);
		}
	}

	// Copy with vertical (POW) scroll.
	{
		int scrolly = -*mario_scrolly - 17;
		copyscrollbitmap(main_bitmap, tmpbitmap, 0, 0, 1, &scrolly,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	// Sprites.
	for (offs = 0; offs < spriteram_size; offs += 4)
	{
		if (spriteram[offs])
		{
			drawgfx(main_bitmap, Machine->gfx[1],
				spriteram[offs + 2],
				(spriteram[offs + 1] & 0x0f) + 16 * palette_bank,
				spriteram[offs + 1] & 0x80, spriteram[offs + 1] & 0x40,
				spriteram[offs + 3] - 8, 240 - spriteram[offs] + 8,
				&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
		}
	}
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------
// Flat RAM (0x6000-0x6fff, sprite/scroll/video regs backing store) lives in
// the CPU0 region; only ROM protection, dirty-tracked video writes, and I/O
// get handlers.
MEM_READ(MarioMainRead)
MEM_ADDR(0x0000, 0x5fff, MRA_ROM)
MEM_ADDR(0x7c00, 0x7c00, ip_port_0_r)   // IN0
MEM_ADDR(0x7c80, 0x7c80, ip_port_1_r)   // IN1
MEM_ADDR(0x7f80, 0x7f80, ip_port_2_r)   // DSW
MEM_ADDR(0xf000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(MarioMainWrite)
MEM_ADDR(0x0000, 0x5fff, MWA_ROM)
MEM_ADDR(0x7400, 0x77ff, mario_videoram_w)
MEM_ADDR(0x7c00, 0x7c00, mario_sh1_w)          // Mario run sample
MEM_ADDR(0x7c80, 0x7c80, mario_sh2_w)          // Luigi run sample
MEM_ADDR(0x7e00, 0x7e00, mario_sh_tuneselect_w)
MEM_ADDR(0x7e80, 0x7e80, mario_gfxbank_w)
MEM_ADDR(0x7e83, 0x7e83, mario_palettebank_w)
MEM_ADDR(0x7e84, 0x7e84, mario_interrupt_enable_w)
MEM_ADDR(0x7f00, 0x7f07, mario_sh_trigger_w)   // death/coin/ice/crab/turtle/fly/skid
MEM_ADDR(0xf000, 0xffff, MWA_ROM)
MEM_END

// Z80 I/O: port 0 is the Z80 DMA (sprite copy on the real board; the 0.57
// driver draws from 0x6900 directly and NOPs the port). The mz80 core walks
// the port table without a null check, so an explicit NOP map is required.
static UINT16 mario_port_nop_r(UINT16, struct z80PortRead*) { return 0; }
static void mario_port_nop_w(UINT16, UINT8, struct z80PortWrite*) {}

PORT_READ(MarioMainPortRead)
PORT_ADDR(0x00, 0xff, mario_port_nop_r)
PORT_END

PORT_WRITE(MarioMainPortWrite)
PORT_ADDR(0x00, 0xff, mario_port_nop_w)   // Z80 DMA - not emulated
PORT_END

// i8039 sound CPU: program ROM (REGION_CPU2 0x000-0xfff) via MEM fallback.
MEM_READ(MarioSoundRead)
MEM_END

MEM_WRITE(MarioSoundWrite)
MEM_ADDR(0x0000, 0x0fff, MWA_ROM)
MEM_END

PORT_READ(MarioSoundPortRead)
PORT_ADDR(0x00, 0xff, mario_sh_gettune)   // MOVX @Rr -> tune select latch
PORT_ADDR(0x101, 0x101, mario_sh_getp1)
PORT_ADDR(0x102, 0x102, mario_sh_getp2)
PORT_ADDR(0x110, 0x110, mario_sh_gett0)
PORT_ADDR(0x111, 0x111, mario_sh_gett1)
PORT_END

PORT_WRITE(MarioSoundPortWrite)
PORT_ADDR(0x00, 0xff, mario_sh_putsound)  // MOVX @Rr -> DAC
PORT_ADDR(0x101, 0x101, mario_sh_putp1)
PORT_ADDR(0x102, 0x102, mario_sh_putp2)
PORT_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static struct DACinterface mario_dac_interface =
{
	1,
	{ 100 }
};

static const char* mario_samples[] =
{
	"mario.zip",
	"ice.wav",        // 0: 0x7f02 ice appears
	"coin.wav",       // 1: 0x7f06 coin appears
	"skid.wav",       // 2: 0x7f07 skid
	"run.wav",        // 3: 0x7c00 Mario run
	"luigirun.wav",   // 4: 0x7c80 Luigi run
	0
};

int init_mario()
{
	LOG_INFO("Starting Mario Bros Init");

	mario_intenable = 0;
	gfx_bank = 0;
	palette_bank = 0;
	p[0] = 0; p[1] = 0xf0;
	for (int i = 2; i < 8; i++) p[i] = 0;
	t[0] = t[1] = 0;

	// Flat RAM lives in the CPU0 region; point the framework globals at it.
	videoram = &Machine->memory_region[CPU0][0x7400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[CPU0][0x6900];
	spriteram_size = 0x180;
	mario_scrolly = &Machine->memory_region[CPU0][0x7d00];

	DAC_sh_start(&mario_dac_interface);
	mario_vh_start();

	LOG_INFO("End Mario Bros Init");
	return 0;
}

void run_mario()
{
	watchdog_reset_w(0, 0, 0);
	mario_vh_screenrefresh();
	DAC_sh_update();
}

void end_mario()
{
	DAC_sh_stop();
	generic_vh_stop();
}

// ---------------------------------------------------------------------------
// Input ports (bits are NOT inverted -> IP_ACTIVE_HIGH)
// ---------------------------------------------------------------------------
INPUT_PORTS_START(mario)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)
PORT_BITX(0x80, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, DEF_STR(1C_2C))
PORT_DIPSETTING(0x0c, DEF_STR(1C_3C))
PORT_DIPNAME(0x30, 0x00, DEF_STR(Bonus_Life))   // revision F: repeating bonus
PORT_DIPSETTING(0x00, "20k 40k 20k+")
PORT_DIPSETTING(0x10, "30k 50k 20k+")
PORT_DIPSETTING(0x20, "40k 60k 20k+")
PORT_DIPSETTING(0x30, "None")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x40, "Medium")
PORT_DIPSETTING(0x80, "Hard")
PORT_DIPSETTING(0xc0, "Hardest")
INPUT_PORTS_END

// Japan set: COIN1/COIN2 swapped, 3-bit coinage, 2-player credit option.
INPUT_PORTS_START(marioj)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)
PORT_BITX(0x80, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_COIN2)   // doesn't work in game, but does in service mode
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPNAME(0x1c, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x08, DEF_STR(3C_1C))
PORT_DIPSETTING(0x10, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x18, DEF_STR(1C_2C))
PORT_DIPSETTING(0x04, DEF_STR(1C_3C))
PORT_DIPSETTING(0x0c, DEF_STR(1C_4C))
PORT_DIPSETTING(0x14, DEF_STR(1C_5C))
PORT_DIPSETTING(0x1c, DEF_STR(1C_6C))
PORT_DIPNAME(0x20, 0x20, "2 Players Game")
PORT_DIPSETTING(0x00, "1 Credit")
PORT_DIPSETTING(0x20, "2 Credits")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "20000")
PORT_DIPSETTING(0x40, "30000")
PORT_DIPSETTING(0x80, "40000")
PORT_DIPSETTING(0xc0, "None")
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs (MAME 0.286 names / CRCs / SHA1s)
// ---------------------------------------------------------------------------

ROM_START(mario)   // US revision F (MAME 0.286 "mariof" contents, local mario.zip)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("tma1-c-7f_f.7f", 0x0000, 0x2000, CRC(c0c6e014) SHA1(36a04f9ca1c2a583477cb8a6f2ef94e044e08296))
ROM_LOAD("tma1-c-7e_f.7e", 0x2000, 0x2000, CRC(94fb60d6) SHA1(e74d74aa27f87a164bdd453ab0076efeeb7d4ea3))
ROM_LOAD("tma1-c-7d_f.7d", 0x4000, 0x2000, CRC(dcceb6c1) SHA1(b19804e69ce2c98cf276c6055c3a250316b96b45))
ROM_LOAD("tma1-c-7c_f.7c", 0xf000, 0x1000, CRC(4a63d96b) SHA1(b09060b2c84ab77cc540a27b8f932cb60ec8d442))

ROM_REGION(0x1000, REGION_CPU2, 0) /* sound (external i8039 tune ROM) */
ROM_LOAD("tma1-c-6k_e.6k", 0x0000, 0x1000, CRC(06b9ff85) SHA1(111a29bcb9cda0d935675fa26eca6b099a88427f))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tma1-v-3f.3f", 0x0000, 0x1000, CRC(28b0c42c) SHA1(46749568aff88a28c3b6a1ac423abd1b90742a4d))
ROM_LOAD("tma1-v-3j.3j", 0x1000, 0x1000, CRC(0c8cc04d) SHA1(15fae47d701dc1ef15c943cee6aa991776ecffdf))

ROM_REGION(0x6000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("tma1-v-7m.7m", 0x0000, 0x1000, CRC(22b7372e) SHA1(4a1c1e239cb6d483e76f50d7a3b941025963c6a3))
ROM_LOAD("tma1-v-7n.7n", 0x1000, 0x1000, CRC(4f3a1f47) SHA1(0747d693b9482f6dd28b0bc484fd1d3e29d35654))
ROM_LOAD("tma1-v-7p.7p", 0x2000, 0x1000, CRC(56be6ccd) SHA1(15a6e16c189d45f72761ebcbe9db5001bdecd659))
ROM_LOAD("tma1-v-7s.7s", 0x3000, 0x1000, CRC(56f1d613) SHA1(9af6844dbaa3615433d0595e9e85e72493e31a54))
ROM_LOAD("tma1-v-7t.7t", 0x4000, 0x1000, CRC(641f0008) SHA1(589fe108c7c11278fd897f2ded8f0498bc149cfd))
ROM_LOAD("tma1-v-7u.7u", 0x5000, 0x1000, CRC(7baf5309) SHA1(d9194ff7b89a18273d37b47228fc7fb7e2a0ed1f))

ROM_REGION(0x0200, REGION_PROMS, 0)
ROM_LOAD("tma1-c-4p_1.4p", 0x0000, 0x0200, CRC(8187d286) SHA1(8a6d8e622599f1aacaeb10f7b1a39a23c8a840a0)) // BPROM was a MB7124E read as 82S147
ROM_END

ROM_START(marioj)  // Japan revision C
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("tma1-c-a1.7f", 0x0000, 0x2000, CRC(b64b6330) SHA1(f7084251ac325bbfa3fb804da16a50622e1fd213))
ROM_LOAD("tma1-c-a2.7e", 0x2000, 0x2000, CRC(290c4977) SHA1(5af266be0ddc883c6548c90e4a9084024a1e91a0))
ROM_LOAD("tma1-c-a1.7d", 0x4000, 0x2000, CRC(f8575f31) SHA1(710d0e72fcfce700ed2a22fb9c7c392cc76b250b))
ROM_LOAD("tma1-c-a2.7c", 0xf000, 0x1000, CRC(a3c11e9e) SHA1(d0612b0f8c2ea4e798f551922a04a324f4ed5f3d))

ROM_REGION(0x1000, REGION_CPU2, 0) /* sound (external i8039 tune ROM) */
ROM_LOAD("tma1c-a.6k", 0x0000, 0x1000, CRC(06b9ff85) SHA1(111a29bcb9cda0d935675fa26eca6b099a88427f))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tma1-v-a.3f", 0x0000, 0x1000, CRC(adf49ee0) SHA1(11fc2cd197bfe3ecb6af55c3c7a326c94988d2bd))
ROM_LOAD("tma1-v-a.3j", 0x1000, 0x1000, CRC(a5318f2d) SHA1(e42f5e51804195c64a56addb18b7ad12c57bb09a))

ROM_REGION(0x6000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("tma1-v-a.7m", 0x0000, 0x1000, CRC(186762f8) SHA1(711fdd37392656bdd5027e020d51d083ccd7c407))
ROM_LOAD("tma1-v-a.7n", 0x1000, 0x1000, CRC(e0e08bba) SHA1(315eba2c10d426c9c0bb4e36987bf8ebed7df9a0))
ROM_LOAD("tma1-v-a.7p", 0x2000, 0x1000, CRC(7b27c8c1) SHA1(3fb2613ce19e353fbcc77b6817927794fb35810f))
ROM_LOAD("tma1-v-a.7s", 0x3000, 0x1000, CRC(912ba80a) SHA1(351fb5b160216eb10e281815d05a7165ca0e5909))
ROM_LOAD("tma1-v-a.7t", 0x4000, 0x1000, CRC(5cbb92a5) SHA1(a78a378e6d3060143dc456e9c33a5068da648331))
ROM_LOAD("tma1-v-a.7u", 0x5000, 0x1000, CRC(13afb9ed) SHA1(b29dcd91cf5e639ee50b734afc7a3afce79634df))

ROM_REGION(0x0200, REGION_PROMS, 0)
ROM_LOAD("tma1-c-4p.4p", 0x0000, 0x0200, CRC(afc9bd41) SHA1(90b739c4c7f24a88b6ac5ca29b06c032906a2801))
ROM_END

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_mario, "mario", "Mario Bros. (US, revision F)")
AAE_DRIVER_ROM(rom_mario)
AAE_DRIVER_FUNCS(&init_mario, &run_mario, &end_mario)
AAE_DRIVER_INPUT(input_ports_mario)
AAE_DRIVER_SAMPLES(mario_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_NMI, &mario_interrupt,
		MarioMainRead, MarioMainWrite, MarioMainPortRead, MarioMainPortWrite, nullptr, nullptr),
	// CPU1: i8039 sound (tunes via DAC)
	AAE_CPU_ENTRY(CPU_8039, 730000, 100, 1, INT_TYPE_NONE, &mario_noop_interrupt,
		MarioSoundRead, MarioSoundWrite, MarioSoundPortRead, MarioSoundPortWrite, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_180)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239)
AAE_DRIVER_RASTER(mario_gfxdecodeinfo, 256, 16 * 4 + 32 * 8, mario_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_mario)

AAE_DRIVER_BEGIN(drv_marioj, "marioj", "Mario Bros. (Japan, revision C)")
AAE_DRIVER_ROM(rom_marioj)
AAE_DRIVER_FUNCS(&init_mario, &run_mario, &end_mario)
AAE_DRIVER_INPUT(input_ports_marioj)
AAE_DRIVER_SAMPLES(mario_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_NMI, &mario_interrupt,
		MarioMainRead, MarioMainWrite, MarioMainPortRead, MarioMainPortWrite, nullptr, nullptr),
	// CPU1: i8039 sound (tunes via DAC)
	AAE_CPU_ENTRY(CPU_8039, 730000, 100, 1, INT_TYPE_NONE, &mario_noop_interrupt,
		MarioSoundRead, MarioSoundWrite, MarioSoundPortRead, MarioSoundPortWrite, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_180)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239)
AAE_DRIVER_RASTER(mario_gfxdecodeinfo, 256, 16 * 4 + 32 * 8, mario_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_CLONE_OF("mario")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_marioj)
