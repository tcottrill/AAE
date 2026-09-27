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
// williams.cpp - First-generation Williams hardware, from MAME 0.53's
// drivers/williams.c + machine/williams.c + vidhrdw/williams.c, with ROM
// names from MAME 0.286 so current romsets load unmodified.
//
//   defender, stargate, robotron, joust, bubbles, splat, sinistar
//
// Hardware notes that shaped this port:
//   * 6809 main CPU at 1 MHz, 6808 sound CPU at 3.579MHz/4. The sound board
//     is just the 6808, a 6821 PIA and an 8-bit DAC; Sinistar adds an
//     HC55516 CVSD driven by the sound PIA's CA2 (digit) and CB2 (clock).
//   * Three PIAs: widget (C804, player inputs), ROM board (C80C, coins +
//     6-bit sound command out + IRQ sources), sound ($0400 on the 6808).
//     All are instances of the mc6821 class; the IRQ wire-OR between the
//     ROM PIA's A and B halves happens inside the class because both wire
//     the same callback.
//   * Interrupts are video-counter driven: VA11 (scanline bit 5) into the
//     ROM PIA's CB1, COUNT240 (scanline >= 240) into CA1. MAME 0.53 does
//     this with timer_set scanline callbacks; here the CPU0 interrupt
//     callback runs 32x per frame (every 8 scanlines, the same cadence)
//     and derives both signals from aae_cpu_getscanline().
//   * Video RAM is 0x9800 bytes, COLUMN-major: x_pair = addr >> 8,
//     y = addr & 0xff, two 4bpp pixels per byte (high nibble left). VRAM is
//     copied to the bitmap in 8-line strips as the beam passes (from the
//     VA11 callback), finished off at end of frame - Joust (and others)
//     erase/redraw sprites racing the beam, so a single end-of-frame
//     snapshot drops sprites that are mid-redraw.
//   * 0000-8FFF is ROM banked over the video RAM (select at C900). Writes
//     always land in VRAM; reads (including opcode fetches - the 6809 core
//     reads through the handler table) switch on the bank flag.
//   * The SC1 blitter (CA00-CA07) XORs its width/height with 4; Splat's
//     SC2 fixed that (xor 0). Sinistar routes blit writes through a
//     clipping circuit (no writes at/above $7400 when enabled via C900
//     bit 2). Stargate and Defender have no blitter at all.
//   * Defender's C000-CFFF is a paged window (PAGE register at D000):
//     page 0 = I/O (palette, PIAs in swapped order, video counter, 256
//     bytes of CMOS at +400), other pages = banked ROM.
//   * CMOS (CC00-CFFF, 1K; Defender: C400, 256 bytes) is battery backed
//     via the generic NVRAM handler - high scores and bookkeeping live
//     there, saved whole as 0.53 does.
//============================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"
#include "mc6821.h"
#include "dac.h"
#include "hc55516.h"
#include "cpu_m6809.h"
#include "cpu_m6800.h"
#include "williams.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static unsigned char williams_videoram[0x9800];
static unsigned char blitterram[8];

static int vram_bank = 0;            // 0 = VRAM readable at 0000-97FF, 1 = ROM
static UINT16 sinistar_clip = 0xffff;
static int blitter_xor = 0;          // 4 on SC1 boards, 0 on splat's SC2
static int blitter_clip = 0;         // 1 = sinistar clipping circuit present
static int williams_d000_ram = 0;    // 1 = sinistar: D000-DFFF is RAM
static int williams_has_cvsd = 0;    // 1 = sinistar: HC55516 running
static int port_select = 0;          // widget PIA CB2 player mux
static int williams_render_line = 0; // next unrendered scanline (beam-chasing redraw)

static mc6821 pia_widget;            // C804-C807
static mc6821 pia_rom;               // C80C-C80F
static mc6821 pia_sound;             // sound board $0400

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

WRITE_HANDLER(williams_videoram_w)   // 0x0000-0x97ff; address == VRAM offset
{
	williams_videoram[address] = data;
}

READ_HANDLER(williams_vram_or_rom_r) // 0x0000-0x97ff
{
	if (vram_bank)
		return Machine->memory_region[CPU0][address];
	return williams_videoram[address];
}

WRITE_HANDLER(williams_vram_select_w) // 0xc900
{
	vram_bank = data & 0x01;
	// bit 1 is the cocktail flip; 0.53 tracks but never renders it.
	sinistar_clip = (data & 0x04) ? 0x7400 : 0xffff;
}

// Color registers, BBGGGRRR (MAME's paletteram_BBGGGRRR_w weighting).
WRITE_HANDLER(williams_palette_w)    // 0xc000-0xc00f
{
	Machine->memory_region[CPU0][0xc000 + address] = data;  // readable back
	int r = data & 7, g = (data >> 3) & 7, b = (data >> 6) & 3;
	r = (r << 5) | (r << 2) | (r >> 1);
	g = (g << 5) | (g << 2) | (g >> 1);
	b = (b << 6) | (b << 4) | (b << 2) | b;
	palette_change_color(address, (unsigned char)r, (unsigned char)g, (unsigned char)b);
}

READ_HANDLER(williams_video_counter_r) // 0xcb00: VA bits 2-7
{
	return (UINT8)(aae_cpu_getscanline() & 0xfc);
}

// Render rows y in [y0, y1) for all column pairs, chasing the beam so
// sprites mid-erase/redraw don't get caught by a stale snapshot.
static void williams_render_rows(int y0, int y1)
{
	if (y1 > 256) y1 = 256;
	if (y0 >= y1) return;
	for (int xp = 0; xp < 0x98; xp++)
	{
		const unsigned char* col = &williams_videoram[xp << 8];
		int x = xp * 2;
		for (int y = y0; y < y1; y++)
		{
			int pix = col[y];
			plot_pixel(main_bitmap, x, y, Machine->pens[pix >> 4]);
			plot_pixel(main_bitmap, x + 1, y, Machine->pens[pix & 0x0f]);
		}
	}
}

// Palette starts all black; the game programs the color registers itself.
static void williams_init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	memset(palette, 0, 16 * 3);
}

// ---------------------------------------------------------------------------
// Blitter (SC1/SC2 special chip)
// ---------------------------------------------------------------------------

// Blit source/dest are CPU0 bus addresses. SOURCE reads honor the VRAM/ROM
// bank (0.53 reads via cpu_readmem16 -> MRA_BANK1); DESTINATION reads always
// see the video RAM below 0x9800 (0.53 WILLIAMS_DEST_READ ignores the bank -
// honoring it here shreds every blit, since games blit with ROM banked in).
static UINT8 williams_blit_read(int a)
{
	a &= 0xffff;
	if (a < 0x9800)
		return vram_bank ? Machine->memory_region[CPU0][a] : williams_videoram[a];
	return Machine->memory_region[CPU0][a];
}

static UINT8 williams_blit_dest_read(int a)
{
	a &= 0xffff;
	if (a < 0x9800)
		return williams_videoram[a];
	return Machine->memory_region[CPU0][a];
}

static void williams_blit_write(int a, UINT8 v)
{
	a &= 0xffff;
	if (blitter_clip && a >= sinistar_clip) return;
	if (a < 0x9800)
		williams_videoram[a] = v;
	else if (a < 0xc000 || (a >= 0xcc00 && a <= 0xcfff))
		Machine->memory_region[CPU0][a] = v;
	else if (williams_d000_ram && a >= 0xd000 && a <= 0xdfff)
		Machine->memory_region[CPU0][a] = v;
	// anything else is I/O or ROM: the chip's writes go nowhere useful
}

// One per-pixel op covering 0.53's four BLIT_* macro variants: transparent
// mode adds per-nibble masking of zero source pixels, solid mode substitutes
// the mask register for the source data.
static void williams_blit_pixop(int dest, int srcdata, int keepmask, int solid,
	int transparent, int solid_mode)
{
	if (transparent)
	{
		if (!srcdata) return;
		int tempmask = keepmask;
		if (!(srcdata & 0xf0)) tempmask |= 0xf0;
		if (!(srcdata & 0x0f)) tempmask |= 0x0f;
		int pix = williams_blit_dest_read(dest);
		int src = solid_mode ? solid : srcdata;
		williams_blit_write(dest, (UINT8)((pix & tempmask) | (src & ~tempmask)));
	}
	else
	{
		int pix = williams_blit_dest_read(dest);
		int src = solid_mode ? solid : srcdata;
		williams_blit_write(dest, (UINT8)((pix & keepmask) | (src & ~keepmask)));
	}
}

static void williams_do_blit(int sstart, int dstart, int w, int h, int data)
{
	int sxadv = (data & 0x01) ? 0x100 : 1;
	int syadv = (data & 0x01) ? 1 : w;
	int dxadv = (data & 0x02) ? 0x100 : 1;
	int dyadv = (data & 0x02) ? 1 : w;

	int keepmask = 0x00;
	if (data & 0x80) keepmask |= 0xf0;
	if (data & 0x40) keepmask |= 0x0f;
	if (keepmask == 0xff)
		return;

	int solid = blitterram[1];
	const int transparent = data & 0x08;
	const int solid_mode = data & 0x10;

	if (!(data & 0x20))
	{
		for (int i = 0; i < h; i++)
		{
			int source = sstart & 0xffff;
			int dest = dstart & 0xffff;
			for (int j = w; j > 0; j--)
			{
				int srcdata = williams_blit_read(source);
				williams_blit_pixop(dest, srcdata, keepmask, solid, transparent, solid_mode);
				source = (source + sxadv) & 0xffff;
				dest = (dest + dxadv) & 0xffff;
			}
			sstart += syadv;
			dstart += dyadv;
		}
	}
	else
	{
		// shifted one pixel right: swap mask/solid nibble halves, then
		// stagger the source reads across byte boundaries
		keepmask = ((keepmask & 0xf0) >> 4) | ((keepmask & 0x0f) << 4);
		solid = ((solid & 0xf0) >> 4) | ((solid & 0x0f) << 4);

		for (int i = 0; i < h; i++)
		{
			int pixdata, srcdata, shiftedmask;
			int source = sstart & 0xffff;
			int dest = dstart & 0xffff;

			// left edge
			pixdata = williams_blit_read(source);
			srcdata = (pixdata >> 4) & 0x0f;
			shiftedmask = keepmask | 0xf0;
			williams_blit_pixop(dest, srcdata, shiftedmask, solid, transparent, solid_mode);
			source = (source + sxadv) & 0xffff;
			dest = (dest + dxadv) & 0xffff;

			for (int j = w - 1; j > 0; j--)
			{
				pixdata = (pixdata << 8) | williams_blit_read(source);
				srcdata = (pixdata >> 4) & 0xff;
				williams_blit_pixop(dest, srcdata, keepmask, solid, transparent, solid_mode);
				source = (source + sxadv) & 0xffff;
				dest = (dest + dxadv) & 0xffff;
			}

			// right edge
			srcdata = (pixdata << 4) & 0xf0;
			shiftedmask = keepmask | 0x0f;
			williams_blit_pixop(dest, srcdata, shiftedmask, solid, transparent, solid_mode);

			sstart += syadv;
			dstart += dyadv;
		}
	}
}

WRITE_HANDLER(williams_blitter_w)    // 0xca00-0xca07
{
	blitterram[address & 7] = data;
	if ((address & 7) != 0)
		return;                       // only writes to CA00 trigger the blit

	int sstart = (blitterram[2] << 8) + blitterram[3];
	int dstart = (blitterram[4] << 8) + blitterram[5];
	int w = blitterram[6] ^ blitter_xor;   // SC1 size bug: XOR with 4
	int h = blitterram[7] ^ blitter_xor;
	if (w == 0) w = 1;
	if (h == 0) h = 1;
	if (w == 255) w = 256;
	if (h == 255) h = 256;
	williams_do_blit(sstart, dstart, w, h, data);
}

// ---------------------------------------------------------------------------
// PIA glue: IRQ lines, inputs, sound command
// ---------------------------------------------------------------------------

static void williams_main_irq(int state) { m_cpu_6809[CPU0]->irq_line(state != 0); }
static void williams_snd_irq(int state) { m_cpu_6800[CPU1]->irq_line(state != 0); }

static uint8_t widget_in_a(void) { return (uint8_t)readinputport(0); }
static uint8_t widget_in_b(void) { return (uint8_t)readinputport(1); }
static uint8_t widget_in_a_muxed(void) { return (uint8_t)readinputport(port_select ? 3 : 0); }
static uint8_t widget_in_b_muxed(void) { return (uint8_t)readinputport(port_select ? 4 : 1); }
static uint8_t rom_in_a(void) { return (uint8_t)readinputport(2); }
static void widget_port_select(int state) { port_select = state; }

// Defender / Stargate joystick cheat: the fake port's left/right bits merge
// into port 0, swapping the reverse-button and right bits when the ship
// faces left (probed straight out of game RAM, as 0.53 does).
static uint8_t cheat_merged_in_a(int probe_addr)
{
	int keys = readinputport(0);
	int altkeys = readinputport(3);
	if (altkeys)
	{
		keys |= altkeys;
		if (Machine->memory_region[CPU0][probe_addr] == 0xfd)
		{
			if (keys & 0x02)      keys = (keys & 0xfd) | 0x40;
			else if (keys & 0x40) keys = (keys & 0xbf) | 0x02;
		}
	}
	return (uint8_t)keys;
}

static uint8_t stargate_in_a(void) { return cheat_merged_in_a(0x9c92); }
static uint8_t defender_in_a(void) { return cheat_merged_in_a(0xa0bb); }

// Sinistar 49-way joystick: 48 positions + center, encoded as two 4-bit
// thermometer codes. The fake analog ports deliver 0x00-0x6f (center 0x38),
// so >> 4 gives 0..6 with 3 = center.
static uint8_t sinistar_49way_in_a(void)
{
	int joy_x = (readinputport(3) >> 4) & 7;
	int joy_y = (readinputport(4) >> 4) & 7;
	if (joy_x > 6) joy_x = 6;
	if (joy_y > 6) joy_y = 6;
	int bits_x = (0x70 >> (7 - joy_x)) & 0x0f;
	int bits_y = (0x70 >> (7 - joy_y)) & 0x0f;
	return (uint8_t)((bits_x << 4) | bits_y);
}

// Sound command: ROM PIA port B (6 bits + two lines pulled high) straight
// into the sound PIA's port B, CB1 low only for the idle 0xff pattern.
// Direct writes, no timer sync - the 128-slice interleave covers it.
static void williams_snd_cmd_w(uint8_t data)
{
	// The top two bits are pulled high on the board, and the CB1 strobe is
	// compared AFTER that - the idle command is 0x3F, which reaches the sound
	// PIA as 0xFF and drops CB1. Comparing the raw byte instead would leave
	// CB1 stuck high, so the PIA would never see another edge and the sound
	// CPU would take exactly one interrupt per session.
	const uint8_t param = (uint8_t)(data | 0xc0);
	pia_sound.set_b(param);
	pia_sound.set_cb1((param == 0xff) ? 0 : 1);
}

static void williams_dac_out(uint8_t data) { DAC_data_w(0, data); }

static void sinistar_cvsd_digit(int state) { hc55516_digit_w(state); }
static void sinistar_cvsd_clock(int state) { hc55516_clock_w(state); }

// The interface builders assign members by name - the mc6821_interface has
// twelve slots and a positional mistake here is invisible until a game
// misbehaves.
static mc6821_interface widget_intf_plain(void)
{
	mc6821_interface i{};
	i.in_a = widget_in_a;
	i.in_b = widget_in_b;
	return i;
}

static mc6821_interface widget_intf_joust(void)  // port A muxed by CB2
{
	mc6821_interface i{};
	i.in_a = widget_in_a_muxed;
	i.in_b = widget_in_b;
	i.out_cb2 = widget_port_select;
	return i;
}

static mc6821_interface widget_intf_splat(void)  // ports A and B both muxed
{
	mc6821_interface i{};
	i.in_a = widget_in_a_muxed;
	i.in_b = widget_in_b_muxed;
	i.out_cb2 = widget_port_select;
	return i;
}

static mc6821_interface widget_intf_stargate(void)
{
	mc6821_interface i{};
	i.in_a = stargate_in_a;
	i.in_b = widget_in_b;
	return i;
}

static mc6821_interface widget_intf_defender(void)
{
	mc6821_interface i{};
	i.in_a = defender_in_a;
	i.in_b = widget_in_b;
	return i;
}

static mc6821_interface widget_intf_sinistar(void)
{
	mc6821_interface i{};
	i.in_a = sinistar_49way_in_a;
	i.in_b = widget_in_b;
	return i;
}

static mc6821_interface rom_pia_intf(void)
{
	mc6821_interface i{};
	i.in_a = rom_in_a;
	i.out_b = williams_snd_cmd_w;
	i.irq_a = williams_main_irq;
	i.irq_b = williams_main_irq;   // same pointer = wire-OR inside the class
	return i;
}

static mc6821_interface snd_pia_intf(void)
{
	mc6821_interface i{};
	i.out_a = williams_dac_out;
	i.irq_a = williams_snd_irq;
	i.irq_b = williams_snd_irq;
	return i;
}

static mc6821_interface snd_pia_intf_sinistar(void)
{
	mc6821_interface i = snd_pia_intf();
	i.out_ca2 = sinistar_cvsd_digit;
	i.out_cb2 = sinistar_cvsd_clock;
	return i;
}

// ---------------------------------------------------------------------------
// Interrupts: the CPU0 callback runs 32x per frame (every 8 scanlines) and
// feeds the video counter signals into the ROM PIA, which turns them into
// 6809 IRQs per its control-register programming.
// ---------------------------------------------------------------------------

void williams_va11_interrupt(void)
{
	int scanline = aae_cpu_getscanline();
	// Catch the bitmap up to the beam before servicing the PIA signals below
	// (the IRQ they may raise isn't taken until this callback returns anyway).
	if (scanline > williams_render_line)
	{
		williams_render_rows(williams_render_line, scanline);
		williams_render_line = scanline;
	}
	pia_rom.set_cb1((scanline & 0x20) ? 1 : 0);   // VA11
	pia_rom.set_ca1((scanline >= 240) ? 1 : 0);   // COUNT240
}

void williams_snd_dummy_interrupt(void)
{
	// the 6808's only interrupt source is the sound PIA
}

// ---------------------------------------------------------------------------
// PIA memory-map thunks (offset arrives range-relative = register index)
// ---------------------------------------------------------------------------

READ_HANDLER(williams_pia_widget_r) { return pia_widget.read(address); }
WRITE_HANDLER(williams_pia_widget_w) { pia_widget.write(address, data); }
READ_HANDLER(williams_pia_rom_r) { return pia_rom.read(address); }
WRITE_HANDLER(williams_pia_rom_w) { pia_rom.write(address, data); }
READ_HANDLER(williams_pia_sound_r) { return pia_sound.read(address); }
WRITE_HANDLER(williams_pia_sound_w) { pia_sound.write(address, data); }

// ---------------------------------------------------------------------------
// Defender: C000-CFFF paged window, PAGE register at D000
// ---------------------------------------------------------------------------

static const UINT32 defender_bank_list[8] =
{ 0x0c000, 0x10000, 0x11000, 0x12000, 0x0c000, 0x0c000, 0x0c000, 0x13000 };

static int defender_bank = 0;
static unsigned char defender_io_ram[0x1000];  // page-0 store; CMOS at +0x400

WRITE_HANDLER(defender_bank_select_w)          // 0xd000-0xdfff
{
	defender_bank = data & 7;
}

READ_HANDLER(defender_bank_r)                  // 0xc000-0xcfff
{
	UINT32 base = defender_bank_list[defender_bank];
	if (base < 0x10000)                        // page 0: I/O space
	{
		if (address >= 0x0c00 && address < 0x0c04) return pia_rom.read(address & 3);
		if (address >= 0x0c04 && address < 0x0c08) return pia_widget.read(address & 3);
		if (address == 0x800) return (UINT8)(aae_cpu_getscanline() & 0xfc);
		return defender_io_ram[address];
	}
	return Machine->memory_region[CPU0][base + address];   // banked ROM page
}

WRITE_HANDLER(defender_bank_w)                 // 0xc000-0xcfff
{
	UINT32 base = defender_bank_list[defender_bank];
	if (base >= 0x10000)
		return;                                // ROM page: writes ignored
	// 0.53 writes the byte through to the backing store before dispatching
	defender_io_ram[address] = data;
	if (address < 0x10) { williams_palette_w(address, data, nullptr); return; }
	if (address >= 0x0c00 && address < 0x0c04) { pia_rom.write(address & 3, data); return; }
	if (address >= 0x0c04 && address < 0x0c08) { pia_widget.write(address & 3, data); return; }
	if (address == 0x3fc) { watchdog_reset_w(0, data, nullptr); return; }
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------

MEM_READ(williams_readmem)
MEM_ADDR(0x0000, 0x97ff, williams_vram_or_rom_r)
MEM_ADDR(0x9800, 0xbfff, MRA_RAM)
MEM_ADDR(0xc804, 0xc807, williams_pia_widget_r)
MEM_ADDR(0xc80c, 0xc80f, williams_pia_rom_r)
MEM_ADDR(0xcb00, 0xcb00, williams_video_counter_r)
MEM_ADDR(0xcc00, 0xcfff, MRA_RAM)
MEM_ADDR(0xd000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(williams_writemem)
MEM_ADDR(0x0000, 0x97ff, williams_videoram_w)
MEM_ADDR(0x9800, 0xbfff, MWA_RAM)
MEM_ADDR(0xc000, 0xc00f, williams_palette_w)
MEM_ADDR(0xc804, 0xc807, williams_pia_widget_w)
MEM_ADDR(0xc80c, 0xc80f, williams_pia_rom_w)
MEM_ADDR(0xc900, 0xc900, williams_vram_select_w)
MEM_ADDR(0xca00, 0xca07, williams_blitter_w)
MEM_ADDR(0xcbff, 0xcbff, watchdog_reset_w)     // pets AAE's always-armed watchdog
MEM_ADDR(0xcc00, 0xcfff, MWA_RAM)
MEM_ADDR(0xd000, 0xffff, MWA_ROM)
MEM_END

// Sinistar: same board, but D000-DFFF is RAM instead of ROM.
MEM_READ(sinistar_readmem)
MEM_ADDR(0x0000, 0x97ff, williams_vram_or_rom_r)
MEM_ADDR(0x9800, 0xbfff, MRA_RAM)
MEM_ADDR(0xc804, 0xc807, williams_pia_widget_r)
MEM_ADDR(0xc80c, 0xc80f, williams_pia_rom_r)
MEM_ADDR(0xcb00, 0xcb00, williams_video_counter_r)
MEM_ADDR(0xcc00, 0xcfff, MRA_RAM)
MEM_ADDR(0xd000, 0xdfff, MRA_RAM)
MEM_ADDR(0xe000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(sinistar_writemem)
MEM_ADDR(0x0000, 0x97ff, williams_videoram_w)
MEM_ADDR(0x9800, 0xbfff, MWA_RAM)
MEM_ADDR(0xc000, 0xc00f, williams_palette_w)
MEM_ADDR(0xc804, 0xc807, williams_pia_widget_w)
MEM_ADDR(0xc80c, 0xc80f, williams_pia_rom_w)
MEM_ADDR(0xc900, 0xc900, williams_vram_select_w)
MEM_ADDR(0xca00, 0xca07, williams_blitter_w)
MEM_ADDR(0xcbff, 0xcbff, watchdog_reset_w)
MEM_ADDR(0xcc00, 0xcfff, MWA_RAM)
MEM_ADDR(0xd000, 0xdfff, MWA_RAM)
MEM_ADDR(0xe000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(defender_readmem)
MEM_ADDR(0x0000, 0x97ff, williams_vram_or_rom_r)
MEM_ADDR(0x9800, 0xbfff, MRA_RAM)
MEM_ADDR(0xc000, 0xcfff, defender_bank_r)
MEM_ADDR(0xd000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(defender_writemem)
MEM_ADDR(0x0000, 0x97ff, williams_videoram_w)
MEM_ADDR(0x9800, 0xbfff, MWA_RAM)
MEM_ADDR(0xc000, 0xcfff, defender_bank_w)
MEM_ADDR(0xd000, 0xdfff, defender_bank_select_w)
MEM_ADDR(0xe000, 0xffff, MWA_ROM)
MEM_END

// Sound board: 6810 RAM, PIA (Colony 7 mirrors it at 8400), ROM. The
// B000-FFFF ROM window also covers Sinistar's four speech ROMs.
MEM_READ(williams_sound_readmem)
MEM_ADDR(0x0000, 0x007f, MRA_RAM)
MEM_ADDR(0x0400, 0x0403, williams_pia_sound_r)
MEM_ADDR(0x8400, 0x8403, williams_pia_sound_r)
MEM_ADDR(0xb000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(williams_sound_writemem)
MEM_ADDR(0x0000, 0x007f, MWA_RAM)
MEM_ADDR(0x0400, 0x0403, williams_pia_sound_w)
MEM_ADDR(0x8400, 0x8403, williams_pia_sound_w)
MEM_ADDR(0xb000, 0xffff, MWA_ROM)
MEM_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static const struct DACinterface williams_dac_intf = { 1, { 100 } };

static int williams_common_init(const mc6821_interface& widget,
	const mc6821_interface& snd, int blit_xor, int blit_clip)
{
	aae_set_lines_per_frame(256);
	memset(williams_videoram, 0, sizeof(williams_videoram));
	memset(blitterram, 0, sizeof(blitterram));
	vram_bank = 0;
	port_select = 0;
	sinistar_clip = 0xffff;
	blitter_xor = blit_xor;
	blitter_clip = blit_clip;
	williams_d000_ram = 0;
	williams_has_cvsd = 0;
	defender_bank = 0;
	williams_render_line = 0;

	pia_widget.configure(widget);
	pia_rom.configure(rom_pia_intf());
	pia_sound.configure(snd);

	// CMOS lives in the region behind the MRA_RAM/MWA_RAM entries at CC00.
	nvram_set_region(&Machine->memory_region[CPU0][0xcc00], 0x400, 0x00);

	if (DAC_sh_start(&williams_dac_intf))
		return 1;

	LOG_INFO("williams: init complete");
	return 0;
}

int init_robotron(void) { return williams_common_init(widget_intf_plain(), snd_pia_intf(), 4, 0); }
int init_joust(void) { return williams_common_init(widget_intf_joust(), snd_pia_intf(), 4, 0); }
int init_bubbles(void) { return williams_common_init(widget_intf_plain(), snd_pia_intf(), 4, 0); }
int init_splat(void) { return williams_common_init(widget_intf_splat(), snd_pia_intf(), 0, 0); }
int init_stargate(void) { return williams_common_init(widget_intf_stargate(), snd_pia_intf(), 0, 0); }

int init_defender(void)
{
	int rc = williams_common_init(widget_intf_defender(), snd_pia_intf(), 0, 0);
	if (rc) return rc;
	memset(defender_io_ram, 0, sizeof(defender_io_ram));
	// Defender's CMOS is 256 bytes at C400 inside the paged I/O window.
	nvram_set_region(&defender_io_ram[0x400], 0x100, 0x00);
	return 0;
}

int init_sinistar(void)
{
	int rc = williams_common_init(widget_intf_sinistar(), snd_pia_intf_sinistar(), 4, 1);
	if (rc) return rc;
	williams_d000_ram = 1;
	williams_has_cvsd = 1;
	if (hc55516_sh_start(200))
		return 1;
	return 0;
}

void run_williams(void)
{
	palette_recalc();   // apply deferred color-register writes (milliped pattern)
	williams_render_rows(williams_render_line, 256);   // finish off the bottom strip
	williams_render_line = 0;
	DAC_sh_update();
	if (williams_has_cvsd)
		hc55516_sh_update();
}

void end_williams(void)
{
	DAC_sh_stop();
	if (williams_has_cvsd)
	{
		hc55516_sh_stop();
		williams_has_cvsd = 0;
	}
}

// ---------------------------------------------------------------------------
// Inputs (0.53 port order preserved - the mux/cheat readers index by number)
// ---------------------------------------------------------------------------

INPUT_PORTS_START(robotron)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_UP)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_DOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_LEFT)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_UP)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_DOWN)
PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_LEFT)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_RIGHT)
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
INPUT_PORTS_END

INPUT_PORTS_START(joust)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_START("IN1")
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_START("IN3")   /* muxed with IN0 via widget CB2 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_PLAYER1)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(stargate)
PORT_START("IN0")
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1, "Fire", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2, "Thrust", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x04, IP_ACTIVE_HIGH, IPT_BUTTON3, "Smart Bomb", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x08, IP_ACTIVE_HIGH, IPT_BUTTON6, "Hyperspace", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BITX(0x40, IP_ACTIVE_HIGH, IPT_BUTTON4, "Reverse", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_BUTTON5, "Inviso", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_START("IN3")   /* fake, merged by stargate_in_a() */
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_CHEAT)
INPUT_PORTS_END

INPUT_PORTS_START(bubbles)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_START("IN1")
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
INPUT_PORTS_END

INPUT_PORTS_START(splat)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_START("IN3")   /* muxed with IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_UP | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_DOWN | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_LEFT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICKLEFT_RIGHT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_UP | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_DOWN | IPF_8WAY | IPF_PLAYER1)
PORT_START("IN4")   /* muxed with IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_LEFT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICKRIGHT_RIGHT | IPF_8WAY | IPF_PLAYER1)
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)
INPUT_PORTS_END

INPUT_PORTS_START(defender)
PORT_START("IN0")
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1, "Fire", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2, "Thrust", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x04, IP_ACTIVE_HIGH, IPT_BUTTON3, "Smart Bomb", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x08, IP_ACTIVE_HIGH, IPT_BUTTON4, "Hyperspace", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BITX(0x40, IP_ACTIVE_HIGH, IPT_BUTTON6, "Reverse", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0xfe, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_START("IN3")   /* fake, merged by defender_in_a() */
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_CHEAT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_CHEAT)
INPUT_PORTS_END

INPUT_PORTS_START(sinistar)
PORT_START("IN0")   /* empty: real port A data comes from the 49-way reader */
PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START2)
PORT_START("IN2")
PORT_BITX(0x01, IP_ACTIVE_HIGH, 0, "Auto Up", OSD_KEY_F1, IP_JOY_NONE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, 0, "Advance", OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN3)
PORT_BITX(0x08, IP_ACTIVE_HIGH, 0, "High Score Reset", OSD_KEY_7, IP_JOY_NONE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_TILT)
PORT_START("IN3")   /* fake, consumed by sinistar_49way_in_a() */
PORT_ANALOG(0xff, 0x38, IPT_AD_STICK_X, 100, 10, 0x00, 0x6f)
PORT_START("IN4")   /* fake, consumed by sinistar_49way_in_a() */
PORT_ANALOG(0xff, 0x38, IPT_AD_STICK_Y | IPF_REVERSE, 100, 10, 0x00, 0x6f)
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs: 0.53 load addresses and regions, 0.286 file names and CRCs
// ---------------------------------------------------------------------------

ROM_START(defender)
ROM_REGION(0x14000, REGION_CPU1, 0)      /* 64k for code + 0x4000 banked */
ROM_LOAD("defend.1", 0x0d000, 0x0800, CRC(c3e52d7e) SHA1(a57f5278ffe44248fc73f9925d107f4024ad981a))
ROM_LOAD("defend.4", 0x0d800, 0x0800, CRC(9a72348b) SHA1(ed6ce796702ff32209ced3cb1ba3837dbafa526f))
ROM_LOAD("defend.2", 0x0e000, 0x1000, CRC(89b75984) SHA1(a9481478da38f99efb67f0ecf82d084e14b93b42))
ROM_LOAD("defend.3", 0x0f000, 0x1000, CRC(94f51e9b) SHA1(a24cfc55de56a72758c76fe2a55f1ec6c353b16f))
ROM_LOAD("defend.9", 0x10000, 0x0800, CRC(6870e8a5) SHA1(67ccc194b1753a18af0c85f5e603355549c4f727))
ROM_LOAD("defend.12", 0x10800, 0x0800, CRC(f1f88938) SHA1(26e48dfeefa0766837b1e762695b9532dbc8bc5e))
ROM_LOAD("defend.8", 0x11000, 0x0800, CRC(b649e306) SHA1(9d7bc3c89e5a53c575946f06702c722b864b1ff0))
ROM_LOAD("defend.11", 0x11800, 0x0800, CRC(9deaf6d9) SHA1(59b018ba0f3fe6eadfd387dc180ac281460358bc))
ROM_LOAD("defend.7", 0x12000, 0x0800, CRC(339e092e) SHA1(2f89951dbe55d80df43df8dcf497171f73e726d3))
ROM_LOAD("defend.10", 0x12800, 0x0800, CRC(a543b167) SHA1(9292b94b0d74e57e03aada4852ad1997c34122ff))
ROM_RELOAD(0x13800, 0x0800)
ROM_LOAD("defend.6", 0x13000, 0x0800, CRC(65f4efd1) SHA1(a960fd1559ed74b81deba434391e49fc6ec389ca))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_1.ic12", 0xf800, 0x0800, CRC(fefd5b48) SHA1(ceb0d18483f0691978c604db94417e6941ad7ff2))
ROM_END

ROM_START(stargate)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("stargate_rom_1-a_3002-1.e4", 0x0000, 0x1000, CRC(88824d18) SHA1(f003a5a9319c4eb8991fa2aae3f10c72d6b8e81a))
ROM_LOAD("stargate_rom_2-a_3002-2.c4", 0x1000, 0x1000, CRC(afc614c5) SHA1(087c6da93318e8dc922d3d22e0a2af7b9759701c))
ROM_LOAD("stargate_rom_3-a_3002-3.a4", 0x2000, 0x1000, CRC(15077a9d) SHA1(7badb4318b208f49d7fa65e915d0aa22a1e37915))
ROM_LOAD("stargate_rom_4-a_3002-4.e5", 0x3000, 0x1000, CRC(a8b4bf0f) SHA1(6b4d47c2899fe9f14f9dab5928499f12078c437d))
ROM_LOAD("stargate_rom_5-a_3002-5.c5", 0x4000, 0x1000, CRC(2d306074) SHA1(54f871983699113e31bb756d4ca885c26c2d66b4))
ROM_LOAD("stargate_rom_6-a_3002-6.a5", 0x5000, 0x1000, CRC(53598dde) SHA1(54b02d944caf95283c9b6f0160e75ea8c4ccc97b))
ROM_LOAD("stargate_rom_7-a_3002-7.e6", 0x6000, 0x1000, CRC(23606060) SHA1(a487ffcd4920d1056b87469735f7e1002f6a2e49))
ROM_LOAD("stargate_rom_8-a_3002-8.c6", 0x7000, 0x1000, CRC(4ec490c7) SHA1(8726ebaf048db9608dfe365bf434ed5ca9452db7))
ROM_LOAD("stargate_rom_9-a_3002-9.a6", 0x8000, 0x1000, CRC(88187b64) SHA1(efacc4a6d4b2af9a236c9d520de6d605c79cc5a8))
ROM_LOAD("stargate_rom_10-a_3002-10.a7", 0xd000, 0x1000, CRC(60b07ff7) SHA1(ba833f48ddfc1bd04ddb41b1d1c840d66ee7da30))
ROM_LOAD("stargate_rom_11-a_3002-11.c7", 0xe000, 0x1000, CRC(7d2c5daf) SHA1(6ca39f493eb8b370154ad46ef01976d352c929e1))
ROM_LOAD("stargate_rom_12-a_3002-12.e7", 0xf000, 0x1000, CRC(a0396670) SHA1(c46872550e0ca031453c6513f8f0448ecc9b5572))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_2_std_744.ic12", 0xf800, 0x0800, CRC(2fcf6c4d) SHA1(9c4334ac3ff15d94001b22fc367af40f9deb7d57))
ROM_END

ROM_START(robotron)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("2084_rom_1b_3005-13.e4", 0x0000, 0x1000, CRC(66c7d3ef) SHA1(f6d60e26c209c1df2cc01ac07ad5559daa1b7118))
ROM_LOAD("2084_rom_2b_3005-14.c4", 0x1000, 0x1000, CRC(5bc6c614) SHA1(4d6e82bc29f49100f7751ccfc6a9ff35695b84b3))
ROM_LOAD("2084_rom_3b_3005-15.a4", 0x2000, 0x1000, CRC(e99a82be) SHA1(06a8c8dd0b4726eb7f0bb0e89c8533931d75fc1c))
ROM_LOAD("2084_rom_4b_3005-16.e5", 0x3000, 0x1000, CRC(afb1c561) SHA1(aaf89c19fd8f4e8750717169eb1af476aef38a5e))
ROM_LOAD("2084_rom_5b_3005-17.c5", 0x4000, 0x1000, CRC(62691e77) SHA1(79b4680ce19bd28882ae823f0e7b293af17cbb91))
ROM_LOAD("2084_rom_6b_3005-18.a5", 0x5000, 0x1000, CRC(bd2c853d) SHA1(f76ec5432a7939b33a27be1c6855e2dbe6d9fdc8))
ROM_LOAD("2084_rom_7b_3005-19.e6", 0x6000, 0x1000, CRC(49ac400c) SHA1(06eae5138254723819a5e93cfd9e9f3285fcddf5))
ROM_LOAD("2084_rom_8b_3005-20.c6", 0x7000, 0x1000, CRC(3a96e88c) SHA1(7ae38a609ed9a6f62ca003cab719740ed7651b7c))
ROM_LOAD("2084_rom_9b_3005-21.a6", 0x8000, 0x1000, CRC(b124367b) SHA1(fd9d75b866f0ebbb723f84889337e6814496a103))
ROM_LOAD("2084_rom_10b_3005-22.a7", 0xd000, 0x1000, CRC(13797024) SHA1(d426a50e75dabe936de643c83a548da5e399331c))
ROM_LOAD("2084_rom_11b_3005-23.c7", 0xe000, 0x1000, CRC(7e3c1b87) SHA1(f8c6cbe3688f256f41a121255fc08f575f6a4b4f))
ROM_LOAD("2084_rom_12b_3005-24.e7", 0xf000, 0x1000, CRC(645d543e) SHA1(fad7cea868ebf17347c4bc5193d647bbd8f9517b))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_3_std_767.ic12", 0xf000, 0x1000, CRC(c56c1d28) SHA1(15afefef11bfc3ab78f61ab046701db78d160ec3))
ROM_END

ROM_START(joust)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("joust_rom_1b_3006-13.e4", 0x0000, 0x1000, CRC(fe41b2af) SHA1(0443e00ae2eb3e66cf805562ee04309487bb0ba4))
ROM_LOAD("joust_rom_2b_3006-14.c4", 0x1000, 0x1000, CRC(501c143c) SHA1(5fda266d43cbbf42eeae1a078b5209d9408ab99f))
ROM_LOAD("joust_rom_3b_3006-15.a4", 0x2000, 0x1000, CRC(43f7161d) SHA1(686da120aa4bd4a41f3d93e8c79ebb343977851a))
ROM_LOAD("joust_rom_4b_3006-16.e5", 0x3000, 0x1000, CRC(db5571b6) SHA1(cb1c3285344e2cfbe0a81ab9b51758c40da8a23f))
ROM_LOAD("joust_rom_5b_3006-17.c5", 0x4000, 0x1000, CRC(c686bb6b) SHA1(d9cac4c46820e1a451a145864bca7a35cfab7d37))
ROM_LOAD("joust_rom_6b_3006-18.a5", 0x5000, 0x1000, CRC(fac5f2cf) SHA1(febaa8cf5c3a0af901cd12d0b7909f6fec3beadd))
ROM_LOAD("joust_rom_7b_3006-19.e6", 0x6000, 0x1000, CRC(81418240) SHA1(5ad14aa65e71c3856dcdb04c99edda92e406a3e3))
ROM_LOAD("joust_rom_8b_3006-20.c6", 0x7000, 0x1000, CRC(ba5359ba) SHA1(f4ee13d5a95ed3e1050a3927a3a0ccf86ed7752d))
ROM_LOAD("joust_rom_9b_3006-21.a6", 0x8000, 0x1000, CRC(39643147) SHA1(d95d3b746133eac9dcc9ee05eabecb797023f1a5))
ROM_LOAD("joust_rom_10b_3006-22.a7", 0xd000, 0x1000, CRC(3f1c4f89) SHA1(90864a8ab944df45287bf0f68ad3a85194077a82))
ROM_LOAD("joust_rom_11b_3006-23.c7", 0xe000, 0x1000, CRC(ea48b359) SHA1(6d38003d56bebeb1f5b4d2287d587342847aa195))
ROM_LOAD("joust_rom_12b_3006-24.e7", 0xf000, 0x1000, CRC(c710717b) SHA1(7d01764e8251c60b3cab96f7dc6dcc1c624f9d12))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_4_std_780.ic12", 0xf000, 0x1000, CRC(f1835bdd) SHA1(af7c066d2949d36b87ea8c425ca7d12f82b5c653))
ROM_END

ROM_START(bubbles)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("bubbles_rom_1b_16-3012-40.4e", 0x0000, 0x1000, CRC(8234f55c) SHA1(4d60942320c03ae50b0b17267062a321cf49e240))
ROM_LOAD("bubbles_rom_2b_16-3012-41.4c", 0x1000, 0x1000, CRC(4a188d6a) SHA1(2788c4a21659799e59ab82bc8d1864a3abe3b6d7))
ROM_LOAD("bubbles_rom_3b_16-3012-42.4a", 0x2000, 0x1000, CRC(7728f07f) SHA1(2a2c6dd8c2196dcd5e71b38554a56ee03d2aa454))
ROM_LOAD("bubbles_rom_4b_16-3012-43.5e", 0x3000, 0x1000, CRC(040be7f9) SHA1(de4d212cd2967b2dcd7b2c09dea2c1b06ce4c5bd))
ROM_LOAD("bubbles_rom_5b_16-3012-44.5c", 0x4000, 0x1000, CRC(0b5f29e0) SHA1(ae52f8c69c8b821abb458288c8ee0bc6c28fe535))
ROM_LOAD("bubbles_rom_6b_16-3012-45.5a", 0x5000, 0x1000, CRC(4dd0450d) SHA1(d55aa8fb8f2974ce5ba7155b01bc3e3622f202af))
ROM_LOAD("bubbles_rom_7b_16-3012-46.6e", 0x6000, 0x1000, CRC(e0a26ec0) SHA1(2da6213df6c15735a8bbd6750cfb1a1b6232a6f5))
ROM_LOAD("bubbles_rom_8b_16-3012-47.6c", 0x7000, 0x1000, CRC(4fd23d8d) SHA1(9d71caa30bc3f4151789279d21651e5a4fe4a484))
ROM_LOAD("bubbles_rom_9b_16-3012-48.6a", 0x8000, 0x1000, CRC(b48559fb) SHA1(551a49a12353044dbbf28dba2bd860c2d00c50bd))
ROM_LOAD("bubbles_rom_10b_16-3012-49.a7", 0xd000, 0x1000, CRC(26e7869b) SHA1(db428e79fc325ae3c8cab460267c27cdbc35a3bd))
ROM_LOAD("bubbles_rom_11b_16-3012-50.c7", 0xe000, 0x1000, CRC(5a5b572f) SHA1(f0c3a330abf9c8cfb6007ee372409450d2a15a93))
ROM_LOAD("bubbles_rom_12b_16-3012-51.e7", 0xf000, 0x1000, CRC(ce22d2e2) SHA1(be4b9800c846660ce2b2ddd75ad872dcf174979a))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_5_std_771.ic12", 0xf000, 0x1000, CRC(689ce2aa) SHA1(b70d2553f731f9a20ddaf9af2f93b7e9c44d4d99))
ROM_END

ROM_START(splat)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("splat_rom_1b_16-3011-1.e4", 0x0000, 0x1000, CRC(1cf26e48) SHA1(6ba4de6cc7d1359ed450da7bae1000552373f873))
ROM_LOAD("splat_rom_2b_16-3011-2.c4", 0x1000, 0x1000, CRC(ac0d4276) SHA1(710aba98909d5d63c4b9b08579021f9c026b3111))
ROM_LOAD("splat_rom_3b_16-3011-3.a4", 0x2000, 0x1000, CRC(74873e59) SHA1(727c9da682fd10353f3969ef02e9f1826d8cb77a))
ROM_LOAD("splat_rom_4b_16-3011-4.e5", 0x3000, 0x1000, CRC(70a7064e) SHA1(7e6440585462b68b62d6d571d83635bf17149f1a))
ROM_LOAD("splat_rom_5b_16-3011-5.c5", 0x4000, 0x1000, CRC(c6895221) SHA1(6f88ba8ac72d9301760d6e2512549f70b5373c65))
ROM_LOAD("splat_rom_6b_16-3011-6.a5", 0x5000, 0x1000, CRC(ea4ab7fd) SHA1(288a361691a7f147ff3346627a10531d613ad017))
ROM_LOAD("splat_rom_7b_16-3011-7.e6", 0x6000, 0x1000, CRC(82fd8713) SHA1(c4d42b111a0357700ac2bf700117d75ffb3c5be5))
ROM_LOAD("splat_rom_8b_16-3011-8.c6", 0x7000, 0x1000, CRC(7dded1b4) SHA1(73df546dd60870f63a8c3deffea2b2d13149a48b))
ROM_LOAD("splat_rom_9b_16-3011-9.a6", 0x8000, 0x1000, CRC(71cbfe5a) SHA1(bf22bedeceffdccc340637098070b32e9c13cf68))
ROM_LOAD("splat_rom_10b_16-3011-10.a7", 0xd000, 0x1000, CRC(d1a1f632) SHA1(de4f5ba2b92c47757dfd2ca810bf8f87338223f7))
ROM_LOAD("splat_rom_11b_16-3011-11.c7", 0xe000, 0x1000, CRC(ca8cde95) SHA1(8e12f6d9eaf397646691ec5d02963b32973cb32e))
ROM_LOAD("splat_rom_12b_16-3011-12.e7", 0xf000, 0x1000, CRC(5bee3e60) SHA1(b4ee99fb6c353093faf1e088bab82fec66e785bc))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the sound CPU */
ROM_LOAD("video_sound_rom_13_std.ic12", 0xf000, 0x1000, CRC(a878d5f3) SHA1(f3347a354cb54ca228fe0971f0ae3bc778e2aecf))
ROM_END

ROM_START(sinistar)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for code */
ROM_LOAD("sinistar_rom_1-b_16-3004-53.1d", 0x0000, 0x1000, CRC(f6f3a22c) SHA1(026d8cab07734fa294a5645edbe65a904bcbc302))
ROM_LOAD("sinistar_rom_2-b_16-3004-54.1c", 0x1000, 0x1000, CRC(cab3185c) SHA1(423d1e3b0c07333ec582529bc4d0b7baf591820a))
ROM_LOAD("sinistar_rom_3-b_16-3004-55.1a", 0x2000, 0x1000, CRC(1ce1b3cc) SHA1(5bc03d7249529d827dc60c087e074ab3e4ea7361))
ROM_LOAD("sinistar_rom_4-b_16-3004-56.2d", 0x3000, 0x1000, CRC(6da632ba) SHA1(72c0c3d5a5ca87ca4d95fcedaf834206e4633950))
ROM_LOAD("sinistar_rom_5-b_16-3004-57.2c", 0x4000, 0x1000, CRC(b662e8fc) SHA1(828a89d2ea13d8a362dae708f86bff54cb231887))
ROM_LOAD("sinistar_rom_6-b_16-3004-58.2a", 0x5000, 0x1000, CRC(2306183d) SHA1(703e29e6446856615760a4897c0f5d79cc7bdfb2))
ROM_LOAD("sinistar_rom_7-b_16-3004-59.3d", 0x6000, 0x1000, CRC(e5dd918e) SHA1(bf4e2ada6a59d246218544d822ba5355da925924))
ROM_LOAD("sinistar_rom_8-b_16-3004-60.3c", 0x7000, 0x1000, CRC(4785a787) SHA1(8c7eca656b2c23b0da41a8c7ce51a2735cab85a4))
ROM_LOAD("sinistar_rom_9-b_16-3004-61.3a", 0x8000, 0x1000, CRC(50cb63ad) SHA1(96e28e4fef98fff2649741a266fa590e0313e3b0))
ROM_LOAD("sinistar_rom_10-b_16-3004-62.4c", 0xe000, 0x1000, CRC(3d670417) SHA1(81802622bee8dbea5c0f08019d87d941dcdbe292))
ROM_LOAD("sinistar_rom_11-b_16-3004-63.4a", 0xf000, 0x1000, CRC(3162bc50) SHA1(2f38e572ab9c731e38dfe9bad3cc8222a775c5ea))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* sound CPU + speech ROMs */
ROM_LOAD("3004_speech_ic7_r1_16-3004-52.ic7", 0xb000, 0x1000, CRC(e1019568) SHA1(442f4f3ccd2e1db2136d2ffb121ea442921f87ca))
ROM_LOAD("3004_speech_ic5_r1_16-3004-50.ic5", 0xc000, 0x1000, CRC(cf3b5ffd) SHA1(d5d51c550581c9d46ab331dd4fd32541a2ef598e))
ROM_LOAD("3004_speech_ic6_r1_16-3004-51.ic6", 0xd000, 0x1000, CRC(ff8d2645) SHA1(16fa2a602acbbc182dd96bab113ab18356f3daf0))
ROM_LOAD("3004_speech_ic4_r1_16-3004-49.ic4", 0xe000, 0x1000, CRC(4b56a626) SHA1(44430cd5c110ec751b0bfb8ae99b26d443350db1))
ROM_LOAD("video_sound_rom_9_std.808.ic12", 0xf000, 0x1000, CRC(b82f4ddb) SHA1(c70c7dd6e88897920d7709a260f27810f66aade1))
ROM_END

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------

#define WILLIAMS_CPUS(readmem, writemem)                                        \
AAE_DRIVER_CPUS(                                                                \
	AAE_CPU_ENTRY(                                                              \
		/*type*/     CPU_M6809,                                                 \
		/*freq*/     1000000,                                                   \
		/*div*/      128,                                                       \
		/*ipf*/      32,                                                        \
		/*int type*/ INT_TYPE_NONE,                                             \
		/*int cb*/   &williams_va11_interrupt,                                  \
		/*r8*/       readmem,                                                   \
		/*w8*/       writemem,                                                  \
		/*pr*/       nullptr,                                                   \
		/*pw*/       nullptr,                                                   \
		/*r16*/      nullptr,                                                   \
		/*w16*/      nullptr                                                    \
	),                                                                          \
	AAE_CPU_ENTRY(                                                              \
		CPU_M6808, 3579000 / 4, 128, 1, INT_TYPE_NONE,                          \
		&williams_snd_dummy_interrupt,                                          \
		williams_sound_readmem, williams_sound_writemem,                        \
		nullptr, nullptr, nullptr, nullptr                                      \
	),                                                                          \
	AAE_CPU_NONE_ENTRY(),                                                       \
	AAE_CPU_NONE_ENTRY()                                                        \
)

AAE_DRIVER_BEGIN(drv_robotron, "robotron", "Robotron: 2084 (Solid Blue label)")
AAE_DRIVER_ROM(rom_robotron)
AAE_DRIVER_FUNCS(&init_robotron, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_robotron)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(williams_readmem, williams_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_joust, "joust", "Joust (White/Green label)")
AAE_DRIVER_ROM(rom_joust)
AAE_DRIVER_FUNCS(&init_joust, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_joust)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(williams_readmem, williams_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_stargate, "stargate", "Stargate")
AAE_DRIVER_ROM(rom_stargate)
AAE_DRIVER_FUNCS(&init_stargate, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_stargate)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(williams_readmem, williams_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_bubbles, "bubbles", "Bubbles")
AAE_DRIVER_ROM(rom_bubbles)
AAE_DRIVER_FUNCS(&init_bubbles, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_bubbles)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(williams_readmem, williams_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_splat, "splat", "Splat!")
AAE_DRIVER_ROM(rom_splat)
AAE_DRIVER_FUNCS(&init_splat, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_splat)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(williams_readmem, williams_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_defender, "defender", "Defender (Red label)")
AAE_DRIVER_ROM(rom_defender)
AAE_DRIVER_FUNCS(&init_defender, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_defender)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(defender_readmem, defender_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_sinistar, "sinistar", "Sinistar (revision 3)")
AAE_DRIVER_ROM(rom_sinistar)
AAE_DRIVER_FUNCS(&init_sinistar, &run_williams, &end_williams)
AAE_DRIVER_INPUT(input_ports_sinistar)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
WILLIAMS_CPUS(sinistar_readmem, sinistar_writemem)
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ROT270)
AAE_DRIVER_SCREEN(304, 256, 6, 298 - 1, 7, 247 - 1)
AAE_DRIVER_RASTER(nullptr, 16, 0, williams_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_robotron)
AAE_REGISTER_DRIVER(drv_joust)
AAE_REGISTER_DRIVER(drv_stargate)
AAE_REGISTER_DRIVER(drv_bubbles)
AAE_REGISTER_DRIVER(drv_splat)
AAE_REGISTER_DRIVER(drv_defender)
AAE_REGISTER_DRIVER(drv_sinistar)
