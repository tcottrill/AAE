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
// Gyruss (Konami, 1983).
//   CPU0  Z80   main      @ 3.072 MHz   (NMI/frame, gated by c180)
//   CPU1  M6809 sprites   @ 2.0   MHz   (konami1-encrypted, 256 scanline IRQs)
//   CPU2  Z80   audio     @ 3.579 MHz   (drives 5x AY-3-8910, IRQ from main)
//   CPU3  i8039 SFX       @ 8MHz/15     (DAC channel, IRQ from audio Z80)
// Raster video: 8x8 chars (REGION_GFX1) + 8x16 sprites (REGION_GFX2, 4bpp),
// color PROMs, char-over-sprite priority, per-scanline multiplexed sprites.
//==========================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "cpu_i8039.h"
#include "memory.h"
#include "ay8910.h"
#include "dac.h"
#include "old_mame_raster.h"
#include "timer.h"

#pragma warning(disable : 4838 4003)

// ---------------------------------------------------------------------------
// RAM
// ---------------------------------------------------------------------------
// AAE CPUs use flat memory: each CPU's RAM lives inside its memory_region.
// The framework globals videoram/colorram/spriteram and gyruss_sharedram are
// pointed into those regions in init_gyruss():
//   colorram  -> region[CPU0][0x8000]   videoram -> region[CPU0][0x8400]
//   work RAM     region[CPU0][0x9000..0x9fff]   (flat, no handler)
//   sharedram -> region[CPU0][0xa000]   (the 6809 reaches it at 0x6000)
//   spriteram -> region[CPU1][0x4040]   (6809 RAM 0x4000..0x47ff is flat)
//   audio RAM    region[CPU2][0x6000..0x63ff]   (flat)
static unsigned char* gyruss_sharedram = nullptr;

static int gyruss_soundlatch  = 0;
static int gyruss_soundlatch2 = 0;
static int main_irq_enable    = 0;
static int m6809_irq_enable   = 0;
static int flipscreen         = 0;
static int scanline           = 0;

#define GYRUSS_SPRITERAM_SIZE 0xc0
static unsigned char* sprite_mux_buffer = nullptr;

// ---------------------------------------------------------------------------
// konami1 opcode descramble (Gyruss sprite CPU). Decoded opcodes are written
// to the upper half of the CPU2 region; data reads keep the encrypted half.
// ---------------------------------------------------------------------------
static unsigned char konami1_decodebyte(unsigned char opcode, unsigned short address)
{
	unsigned char xormask = 0;
	if (address & 0x02) xormask |= 0x80; else xormask |= 0x20;
	if (address & 0x08) xormask |= 0x08; else xormask |= 0x02;
	return opcode ^ xormask;
}

// Runs after the 6809 core is constructed (init_game runs before the cores
// exist, so the opcode-base wiring must happen here).
static void gyruss_6809_post_init(int cpunum)
{
	// konami-1: only the opcode byte is scrambled. Decode the lower half into
	// the upper half of the (double-size) region and point the 6809's opcode
	// fetches there; data/operand reads keep the original lower half.
	unsigned char* rom = Machine->memory_region[CPU1];
	int diff = memory_region_length(CPU1) / 2;
	for (int A = 0; A < diff; ++A)
		rom[A + diff] = konami1_decodebyte(rom[A], (unsigned short)A);
	memory_set_opcode_base(CPU1, rom + diff);
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void gyruss_main_interrupt()      // CPU0, once/frame
{
	if (main_irq_enable) cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

void gyruss_6809_interrupt()      // CPU1, 256x/frame (scanline)
{
	scanline = 255 - cpu_getiloops();
	memcpy(sprite_mux_buffer + scanline * GYRUSS_SPRITERAM_SIZE,
		spriteram, GYRUSS_SPRITERAM_SIZE);
	if (scanline == 255 && m6809_irq_enable)
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
}

void gyruss_noop_interrupt() {}   // audio Z80 / i8039: IRQs are externally triggered

// ---------------------------------------------------------------------------
// Sound-latch + cross-CPU IRQ triggers
// ---------------------------------------------------------------------------
WRITE_HANDLER(gyruss_soundlatch_w)  { gyruss_soundlatch = data; }
READ_HANDLER(gyruss_soundlatch_r)   { return (UINT8)gyruss_soundlatch; }

WRITE_HANDLER(gyruss_sh_irqtrigger_w) { cpu_do_int_imm(CPU2, INT_TYPE_INT); }  // -> audio Z80

WRITE_HANDLER(gyruss_main_irq_enable_w)  { main_irq_enable  = data & 1; }
WRITE_HANDLER(gyruss_6809_irq_enable_w)  { m6809_irq_enable = data & 1; }

WRITE_HANDLER(gyruss_flipscreen_w)
{
	if (flipscreen != (data & 1)) {
		flipscreen = data & 1;
		if (dirtybuffer) memset(dirtybuffer, 1, videoram_size);
	}
}

READ_HANDLER(gyruss_scanline_r) { return (UINT8)scanline; }

// videoram_w/colorram_w are old-style (offset,data); wrap them for the map.
WRITE_HANDLER(gyruss_videoram_w) { videoram_w(address, data); }
WRITE_HANDLER(gyruss_colorram_w) { colorram_w(address, data); }

// shared RAM (main a000-a7ff, 6809 6000-67ff -> same buffer; both reached
// here as region-relative offset 0..0x7ff)
READ_HANDLER(gyruss_sharedram_r) 
{ 
	return gyruss_sharedram[address & 0x7ff]; 
}

WRITE_HANDLER(gyruss_sharedram_w) 
{
	gyruss_sharedram[address & 0x7ff] = data; 
}

// ---------------------------------------------------------------------------
// Z80 I/O-port handlers (audio CPU) and i8039 port handlers
// ---------------------------------------------------------------------------
void gyruss_soundlatch2_port_w(UINT16 port, UINT8 data, struct z80PortWrite*) { gyruss_soundlatch2 = data; }
void gyruss_i8039_irq_port_w  (UINT16 port, UINT8 data, struct z80PortWrite*) { cpu_do_int_imm(CPU3, INT_TYPE_INT); }

UINT16 gyruss_i8039_sndcmd_port_r(UINT16 port, struct z80PortRead*) { return (UINT16)gyruss_soundlatch2; }
void   gyruss_dac_port_w        (UINT16 port, UINT8 data, struct z80PortWrite*) { DAC_data_w(0, data); }
void   gyruss_port_nop_w        (UINT16 port, UINT8 data, struct z80PortWrite*) {}

// ---------------------------------------------------------------------------
// AY-3-8910 port-A timer (feeds music tempo) + RC filter writes (no-op here:
// AAE has no per-channel RC filter; sound plays unfiltered).
// ---------------------------------------------------------------------------
static int gyruss_timer_tab[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0a, 0x0b, 0x0a, 0x0d };

uint8_t gyruss_portA_r(void)
{
	static uint64_t last = 0;
	static int clk = 0;
	uint64_t now = (uint64_t)get_exact_cyclecount(get_active_cpu());
	clk = (clk + (int)(now - last)) % 10240;
	last = now;
	return (uint8_t)gyruss_timer_tab[clk / 1024];
}

void gyruss_filter0_w(uint8_t /*data*/) {}
void gyruss_filter1_w(uint8_t /*data*/) {}

static AY8910Config gyruss_ay8910_cfg =
{
	5,                 // num_chips
	14318180 / 8,      // 1.789772 MHz
	{ 40, 40, 70, 70, 70 },                                    // mixing_level
	{ nullptr, nullptr, gyruss_portA_r, nullptr, nullptr },    // port_a_read (chip 2 = timer)
	{ nullptr, nullptr, nullptr, nullptr, nullptr },           // port_b_read
	{ nullptr, nullptr, nullptr, nullptr, nullptr },           // port_a_write
	{ gyruss_filter0_w, gyruss_filter1_w, nullptr, nullptr, nullptr } // port_b_write (filters)
};

static struct DACinterface gyruss_dac_interface =
{
	1,
	{ 50 }
};

// ---------------------------------------------------------------------------
// GFX
// ---------------------------------------------------------------------------
static struct GfxLayout charlayout =
{
	8, 8, 512, 2,
	{ 4, 0 },
	{ 0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	16 * 8
};
static struct GfxLayout spritelayout =
{
	8, 16, 256, 4,
	{ 0x4000 * 8 + 4, 0x4000 * 8 + 0, 4, 0 },
	{ 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
	  32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
	64 * 8
};

struct GfxDecodeInfo gyruss_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &charlayout,      0, 16 },
	{ REGION_GFX2, 0x0000, &spritelayout, 16 * 4, 16 },   // upper half
	{ REGION_GFX2, 0x0010, &spritelayout, 16 * 4, 16 },   // lower half
	{ -1 }
};

void gyruss_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
	// 32-entry palette PROM
	for (i = 0; i < 32; i++) {
		int bit0, bit1, bit2;
		bit0 = (color_prom[i] >> 0) & 1; bit1 = (color_prom[i] >> 1) & 1; bit2 = (color_prom[i] >> 2) & 1;
		palette[3 * i + 0] = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		bit0 = (color_prom[i] >> 3) & 1; bit1 = (color_prom[i] >> 4) & 1; bit2 = (color_prom[i] >> 5) & 1;
		palette[3 * i + 1] = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		bit0 = 0; bit1 = (color_prom[i] >> 6) & 1; bit2 = (color_prom[i] >> 7) & 1;
		palette[3 * i + 2] = (unsigned char)(0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
	}
	const unsigned char* sprite_lut = color_prom + 0x20;   // 256 bytes
	const unsigned char* char_lut   = color_prom + 0x120;  // 256 bytes
	for (i = 0; i < 16 * 16; i++) colortable[16 * 4 + i] = sprite_lut[i] & 0x0f;          // sprites (start 64)
	for (i = 0; i < 16 * 4;  i++) colortable[i]          = (char_lut[i] & 0x0f) + 0x10;   // chars  (start 0)
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
static void gyruss_draw_sprites()
{
	
	struct rectangle clip = Machine->drv->visible_area;
	int offs, line;

	for (line = 0; line < 256; line++)
	{
		if (line >= Machine->drv->visible_area.min_y && line <= Machine->drv->visible_area.max_y)
		{
			unsigned char* sr = sprite_mux_buffer + line * GYRUSS_SPRITERAM_SIZE;
			clip.min_y = clip.max_y = line;

			for (offs = GYRUSS_SPRITERAM_SIZE - 4; offs >= 0; offs -= 4)
			{
				int sx = sr[offs];
				int sy = 241 - sr[offs + 3];
				if (sy > line - 16 && sy <= line)
				{
					drawgfx(main_bitmap, Machine->gfx[1 + (sr[offs + 1] & 1)],
						sr[offs + 1] / 2 + 4 * (sr[offs + 2] & 0x20),
						sr[offs + 2] & 0x0f,
						!(sr[offs + 2] & 0x40), sr[offs + 2] & 0x80,
						sx, sy,
						&clip, TRANSPARENCY_PEN, 0);
				}
			}
		}
	}
}

void gyruss_vh_screenrefresh()
{
	int offs;
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, flipx, flipy;
			dirtybuffer[offs] = 0;

			sx = offs % 32;
			sy = offs / 32;
			flipx = colorram[offs] & 0x40;
			flipy = colorram[offs] & 0x80;
			if (flipscreen) { sx = 31 - sx; sy = 31 - sy; flipx = !flipx; flipy = !flipy; }

			drawgfx(tmpbitmap, Machine->gfx[0],
				videoram[offs] + 8 * (colorram[offs] & 0x20),
				colorram[offs] & 0x0f,
				flipx, flipy,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);

	gyruss_draw_sprites();

	// characters with priority over sprites
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if ((colorram[offs] & 0x10) != 0)
		{
			int sx = offs % 32;
			int sy = offs / 32;
			int flipx = colorram[offs] & 0x40;
			int flipy = colorram[offs] & 0x80;
			if (flipscreen) { sx = 31 - sx; sy = 31 - sy; flipx = !flipx; flipy = !flipy; }

			drawgfx(main_bitmap, Machine->gfx[0],
				videoram[offs] + 8 * (colorram[offs] & 0x20),
				colorram[offs] & 0x0f,
				flipx, flipy,
				8 * sx, 8 * sy,
				&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
		}
	}
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------
// RAM is flat inside each CPU's memory_region; only I/O + dirty-tracked video
// writes need handlers. (See bosco/galaga for this idiom.)
MEM_READ(GyrussMainRead)
MEM_ADDR( 0x0000, 0x7fff, MRA_ROM )
MEM_ADDR(0xa000, 0xa7ff, gyruss_sharedram_r)
MEM_ADDR(0xc000, 0xc000, ip_port_4_r)   // DSW1
MEM_ADDR(0xc080, 0xc080, ip_port_0_r)   // IN0
MEM_ADDR(0xc0a0, 0xc0a0, ip_port_1_r)   // IN1
MEM_ADDR(0xc0c0, 0xc0c0, ip_port_2_r)   // IN2
MEM_ADDR(0xc0e0, 0xc0e0, ip_port_3_r)   // DSW0
MEM_ADDR(0xc100, 0xc100, ip_port_5_r)   // DSW2
MEM_END

MEM_WRITE(GyrussMainWrite)
MEM_ADDR(0x0000, 0x7fff, MWA_ROM)
MEM_ADDR(0x8000, 0x83ff, gyruss_colorram_w)
MEM_ADDR(0x8400, 0x87ff, gyruss_videoram_w)
MEM_ADDR(0xa000, 0xa7ff, gyruss_sharedram_w)
MEM_ADDR(0xc000, 0xc000, MWA_NOP)
MEM_ADDR(0xc080, 0xc080, gyruss_sh_irqtrigger_w)
MEM_ADDR(0xc100, 0xc100, gyruss_soundlatch_w)
MEM_ADDR(0xc180, 0xc180, gyruss_main_irq_enable_w)
MEM_ADDR(0xc185, 0xc185, gyruss_flipscreen_w)
MEM_END

MEM_READ(Gyruss6809Read)
MEM_ADDR(0x0000, 0x0000, gyruss_scanline_r)
MEM_ADDR(0x6000, 0x67ff, gyruss_sharedram_r)
// 0x4000-0x47ff RAM is flat; 0xe000-0xffff ROM data via MEM fallback
// (encrypted half), opcodes via the decrypted opcode base (post_init).
MEM_ADDR( 0xe000, 0xffff, MRA_ROM )
MEM_END

MEM_WRITE(Gyruss6809Write)
MEM_ADDR(0x2000, 0x2000, gyruss_6809_irq_enable_w)
MEM_ADDR(0x6000, 0x67ff, gyruss_sharedram_w)
MEM_ADDR(0xe000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(GyrussSoundRead)
MEM_ADDR(0x8000, 0x8000, gyruss_soundlatch_r)
MEM_END

MEM_WRITE(GyrussSoundWrite)
MEM_ADDR(0x0000, 0x5fff, MWA_ROM)
MEM_END

PORT_READ(GyrussSoundPortRead)
PORT_ADDR(0x01, 0x01, ay8910_0_data_port_r)
PORT_ADDR(0x05, 0x05, ay8910_1_data_port_r)
PORT_ADDR(0x09, 0x09, ay8910_2_data_port_r)
PORT_ADDR(0x0d, 0x0d, ay8910_3_data_port_r)
PORT_ADDR(0x11, 0x11, ay8910_4_data_port_r)
PORT_END

PORT_WRITE(GyrussSoundPortWrite)
PORT_ADDR(0x00, 0x00, ay8910_0_control_port_w)
PORT_ADDR(0x02, 0x02, ay8910_0_data_port_w)
PORT_ADDR(0x04, 0x04, ay8910_1_control_port_w)
PORT_ADDR(0x06, 0x06, ay8910_1_data_port_w)
PORT_ADDR(0x08, 0x08, ay8910_2_control_port_w)
PORT_ADDR(0x0a, 0x0a, ay8910_2_data_port_w)
PORT_ADDR(0x0c, 0x0c, ay8910_3_control_port_w)
PORT_ADDR(0x0e, 0x0e, ay8910_3_data_port_w)
PORT_ADDR(0x10, 0x10, ay8910_4_control_port_w)
PORT_ADDR(0x12, 0x12, ay8910_4_data_port_w)
PORT_ADDR(0x14, 0x14, gyruss_i8039_irq_port_w)
PORT_ADDR(0x18, 0x18, gyruss_soundlatch2_port_w)
PORT_END

MEM_READ(GyrussI8039Read)
// program ROM read from memory_region[CPU4] via MEM fallback
MEM_END

MEM_WRITE(GyrussI8039Write)
MEM_ADDR(0x0000, 0x0fff, MWA_ROM)
MEM_END

PORT_READ(GyrussI8039PortRead)
PORT_ADDR(0x00, 0xff, gyruss_i8039_sndcmd_port_r)
PORT_END

PORT_WRITE(GyrussI8039PortWrite)
PORT_ADDR(0x101, 0x101, gyruss_dac_port_w)   // I8039 P1 -> DAC
PORT_ADDR(0x102, 0x102, gyruss_port_nop_w)   // I8039 P2
PORT_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void run_gyruss()
{
	watchdog_reset_w(0, 0, 0);
	ay8910_sh_update();
	DAC_sh_update();
	gyruss_vh_screenrefresh();
}

int init_gyruss()
{
	LOG_INFO("Starting Gyruss Init");

	gyruss_soundlatch = gyruss_soundlatch2 = 0;
	main_irq_enable = m6809_irq_enable = 0;
	flipscreen = 0;
	scanline = 0;

	// Flat RAM lives inside each CPU's memory_region; point the framework
	// globals and the shared-RAM pointer into it (bosco/galaga idiom).
	colorram         = &Machine->memory_region[CPU0][0x8000];
	videoram         = &Machine->memory_region[CPU0][0x8400];
	videoram_size    = 0x400;
	gyruss_sharedram = &Machine->memory_region[CPU0][0xa000];
	spriteram        = &Machine->memory_region[CPU1][0x4040];
	spriteram_size   = GYRUSS_SPRITERAM_SIZE;

	if (!sprite_mux_buffer)
		sprite_mux_buffer = (unsigned char*)malloc(256 * GYRUSS_SPRITERAM_SIZE);
	if (sprite_mux_buffer)
		memset(sprite_mux_buffer, 0, 256 * GYRUSS_SPRITERAM_SIZE);

	ay8910_sh_start(&gyruss_ay8910_cfg);
	DAC_sh_start(&gyruss_dac_interface);

	generic_vh_start();

	LOG_INFO("End Gyruss Init");
	return 0;
}

void end_gyruss()
{
	ay8910_sh_stop();
	DAC_sh_stop();
	generic_vh_stop();
	if (sprite_mux_buffer) { free(sprite_mux_buffer); sprite_mux_buffer = nullptr; }
}

// ---------------------------------------------------------------------------
// Input ports
// ---------------------------------------------------------------------------
INPUT_PORTS_START(gyruss)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_SERVICE)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_2WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_2WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("DSW0")
PORT_DIPNAME(0xf0, 0xf0, DEF_STR(Coin_B))
PORT_DIPSETTING(0x80, DEF_STR(2C_1C))
PORT_DIPSETTING(0xf0, DEF_STR(1C_1C))
PORT_DIPSETTING(0xe0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0f, 0x0f, DEF_STR(Coin_A))
PORT_DIPSETTING(0x08, DEF_STR(2C_1C))
PORT_DIPSETTING(0x0f, DEF_STR(1C_1C))
PORT_DIPSETTING(0x0e, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))

PORT_START("DSW1")
PORT_DIPNAME(0x03, 0x03, DEF_STR(Lives))
PORT_DIPSETTING(0x03, "3")
PORT_DIPSETTING(0x02, "4")
PORT_DIPSETTING(0x01, "5")
PORT_DIPNAME(0x04, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x04, DEF_STR(Cocktail))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x08, "30000 60000")
PORT_DIPSETTING(0x00, "40000 70000")
PORT_DIPNAME(0x70, 0x70, DEF_STR(Difficulty))
PORT_DIPSETTING(0x70, "1 (Easiest)")
PORT_DIPSETTING(0x30, "5 (Average)")
PORT_DIPSETTING(0x00, "8 (Hardest)")
PORT_DIPNAME(0x80, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")
PORT_DIPNAME(0x01, 0x00, "Demo Music")
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
ROM_START(gyruss)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("gyrussk.1", 0x0000, 0x2000, CRC(c673b43d) SHA1(7c464fb154bac35dd6e2f547e157addeb8798194))
ROM_LOAD("gyrussk.2", 0x2000, 0x2000, CRC(a4ec03e4) SHA1(08c33ad7fcc2ad5e5787a1050284e3f8164f4618))
ROM_LOAD("gyrussk.3", 0x4000, 0x2000, CRC(27454a98) SHA1(030c7df225652ee20d5ef64d005eb011dc89a27d))

ROM_REGION(2 * 0x10000, REGION_CPU2, 0)   // 64k code + 64k decrypted opcodes
ROM_LOAD("gyrussk.9", 0xe000, 0x2000, CRC(822bf27e) SHA1(36d5bea2392a7d3476dd797dc05602705cfa23ef))

ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("gyrussk.1a", 0x0000, 0x2000, CRC(f4ae1c17) SHA1(ae568c96a31d910afe30d2b7eeb9ed1ed07290e3))
ROM_LOAD("gyrussk.2a", 0x2000, 0x2000, CRC(ba498115) SHA1(9cd1f42898cc590f39ba7cb3c975b0b3d3062eba))

ROM_REGION(0x1000, REGION_CPU4, 0)
ROM_LOAD("gyrussk.3a", 0x0000, 0x1000, CRC(3f9b5dea) SHA1(6e807da02c2885b18e8cc2199f12f6be9040bf75))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("gyrussk.4", 0x0000, 0x2000, CRC(27d8329b) SHA1(564ff945465a23d93a93137ad277298770dfa06a))

ROM_REGION(0x8000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("gyrussk.6", 0x0000, 0x2000, CRC(c949db10) SHA1(fcb8bcbd2bdd751fecb322a33c8a92fb6f07a7ab))
ROM_LOAD("gyrussk.5", 0x2000, 0x2000, CRC(4f22411a) SHA1(763bcd039f8c1838a0d7da7d4dadc14a26e25596))
ROM_LOAD("gyrussk.8", 0x4000, 0x2000, CRC(47cd1fbc) SHA1(8203c4ff0b1cd7b4dbc708e300bfeac1e7366e09))
ROM_LOAD("gyrussk.7", 0x6000, 0x2000, CRC(8e8d388c) SHA1(8f2928d71c02aba977d67575d6e34d69bda2b9d4))

ROM_REGION(0x0220, REGION_PROMS, 0)
ROM_LOAD("gyrussk.pr3", 0x0000, 0x0020, CRC(98782db3) SHA1(b891e43b25187faca8002919ccb44d744daa3594))
ROM_LOAD("gyrussk.pr1", 0x0020, 0x0100, CRC(7ed057de) SHA1(c04069ae1e2c62f9b3048844cd8cf5e1b03b7d3c))
ROM_LOAD("gyrussk.pr2", 0x0120, 0x0100, CRC(de823a81) SHA1(1af94b2a6a319a89b238a5076a2867f1cfd279b0))
ROM_END

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_gyruss, "gyruss", "Gyruss (Konami)")
AAE_DRIVER_ROM(rom_gyruss)
AAE_DRIVER_FUNCS(&init_gyruss, &run_gyruss, &end_gyruss)
AAE_DRIVER_INPUT(input_ports_gyruss)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Z80 main
	AAE_CPU_ENTRY(CPU_MZ80, 3072000, 100, 1, INT_TYPE_NMI, &gyruss_main_interrupt,
		GyrussMainRead, GyrussMainWrite, nullptr, nullptr, nullptr, nullptr),
	// CPU1: M6809 sprite CPU (konami1-encrypted; opcode base set in post_init)
	AAE_CPU_ENTRY_EX(CPU_M6809, 2000000, 400, 256, INT_TYPE_INT, &gyruss_6809_interrupt,
		Gyruss6809Read, Gyruss6809Write, nullptr, nullptr, nullptr, nullptr, &gyruss_6809_post_init),
	//AAE_CPU_NONE_ENTRY(),

	// CPU2: Z80 audio
	AAE_CPU_ENTRY(CPU_MZ80, 14318180 / 4, 100, 1, INT_TYPE_NONE, &gyruss_noop_interrupt,
		GyrussSoundRead, GyrussSoundWrite, GyrussSoundPortRead, GyrussSoundPortWrite, nullptr, nullptr),
	// CPU3: i8039 DAC
	AAE_CPU_ENTRY(CPU_8039, 8000000 / 15, 100, 1, INT_TYPE_NONE, &gyruss_noop_interrupt,
		GyrussI8039Read, GyrussI8039Write, GyrussI8039PortRead, GyrussI8039PortWrite, nullptr, nullptr)
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239)
AAE_DRIVER_RASTER(gyruss_gfxdecodeinfo, 32, 16 * 4 + 16 * 16, gyruss_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_gyruss)
