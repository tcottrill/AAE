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
// qix.cpp - Taito Qix hardware, from MAME 0.90's drivers/qix.c +
// machine/qix.c + vidhrdw/qix.c, with ROM names from MAME 0.286 so current
// romsets load unmodified.
//
//   qix, qixa, qixb, qixo, qix2
//
// driver by John Butler, Ed Mueller, Aaron Giles (MAME TEAM)
//
// Hardware notes that shaped this port:
//   * Three CPUs. A 6809 "data" CPU at 1.25MHz (20MHz osc /4 /4) runs the
//     game and talks to the sound board; a second 6809 "video" CPU at the
//     same clock owns the frame buffer; a 6802 at 0.92MHz (7.3728MHz /2 /4)
//     drives an 8-bit DAC. The schematics also show a TMS5220 that is never
//     accessed - u27 is the only sound code there is.
//   * The two 6809s share 1K of dual-port RAM at $8000 and interrupt each
//     other with FIRQs: $8C00 asserts the OTHER cpu's FIRQ, $8C01 acks your
//     own. Both reads and writes of those addresses do it, which is why
//     every one of the four gets a handler pair below.
//   * Six 6821 PIAs, all instances of the mc6821 class:
//       0 ($9400, data cpu)  player 1 inputs / coin door
//       1 ($9800, data cpu)  spare
//       2 ($9C00, data cpu)  player 2 inputs, port B = coin lockout/counter
//       3 ($9000, data cpu)  sound command out, CB1 = VSYNC in,
//                            CB2 = INV (cocktail flip) out,
//                            IRQA/IRQB = /DINT -> data cpu IRQ
//       4 ($4000, sound cpu) sound command in, port B = DAC,
//                            IRQA/IRQB = /SINT -> sound cpu IRQ
//       5 ($2000, sound cpu) the TMS5220 PIA - wired, never touched
//     The data CPU has NO periodic interrupt of its own: its IRQ is entirely
//     PIA 3's doing, driven by the VSYNC edge into CB1. That is why the CPU0
//     interrupt callback here only moves CB1 - the mc6821 decides whether
//     that becomes a 6809 IRQ, per how the game programmed CRB.
//   * The frame buffer is 256x256x8bpp (64K) but the video CPU only sees
//     32K at a time at $0000-$7FFF; bit 7 of the address latch at $9402
//     picks the half. $9400 is a second, indirect port into the same 64K
//     using $9402/$9403 as a full 16-bit address.
//   * Color RAM at $9000-$93FF is four pages of 256 entries, RRGGBBII, with
//     the live page selected by the low 2 bits of $8800. See the palette
//     section for why AAE handles the paging differently from 0.90.
//   * CMOS at $8400-$87FF on the video CPU is battery backed - high scores,
//     coinage, difficulty and the language byte all live there.
//============================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"
#include "mc6821.h"
#include "dac.h"
#include "cpu_m6809.h"
#include "cpu_m6800.h"
#include "qix.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static unsigned char qix_videoram[256 * 256];
static unsigned char qix_sharedram[0x400];      // dual-port, both 6809s
static unsigned char qix_paletteram[0x400];     // four pages of 256
static unsigned char qix_videoaddress[2];       // $9402 high byte, $9403 low
static unsigned char qix_palettebank;           // low 2 bits of $8800
static unsigned char qix_cocktail_flip;         // PIA 3 CB2 (INV)
static unsigned char qix_coinctrl;              // PIA 2 port B

static mc6821 pia0, pia1, pia2, pia3, pia4, pia5;

// ---------------------------------------------------------------------------
// Palette
//
// 0.90 declares 1024 pens and lets the bank select a 256-pen window at draw
// time (`&Machine->pens[qix_palettebank * 256]`). AAE caps the palette at
// MAX_PENS = 256, so that is not available here. Instead the driver keeps all
// four pages in qix_paletteram and re-pushes the selected page into pens
// 0-255 whenever the bank changes - which is what MAME 0.36 did before the
// 1024-pen rewrite. Same picture, one indirection earlier.
//
// A consequence: a bank switch part-way down the screen recolors the whole
// frame rather than just the lines below it. 0.90 hides that with
// force_partial_update(); AAE renders once per frame and has no equivalent.
// Qix switches banks during vertical blank, so this does not show in practice.
// ---------------------------------------------------------------------------

// This conversion table should be about right. It gives a reasonable gray
// scale in the test screen, and the red, green and blue squares in the same
// screen are barely visible, as the manual requires.
static const unsigned char qix_intensity_table[16] =
{
	0x00,	/* value = 0, intensity = 0 */
	0x12,	/* value = 0, intensity = 1 */
	0x24,	/* value = 0, intensity = 2 */
	0x49,	/* value = 0, intensity = 3 */
	0x12,	/* value = 1, intensity = 0 */
	0x24,	/* value = 1, intensity = 1 */
	0x49,	/* value = 1, intensity = 2 */
	0x92,	/* value = 1, intensity = 3 */
	0x5b,	/* value = 2, intensity = 0 */
	0x6d,	/* value = 2, intensity = 1 */
	0x92,	/* value = 2, intensity = 2 */
	0xdb,	/* value = 2, intensity = 3 */
	0x7f,	/* value = 3, intensity = 0 */
	0x91,	/* value = 3, intensity = 1 */
	0xb6,	/* value = 3, intensity = 2 */
	0xff	/* value = 3, intensity = 3 */
};

// Push one color RAM entry (0-0x3ff) at the pen it maps to, if its page is
// the live one. Qix uses 64 colors (2 bits each RGB) x 4 intensities.
static void qix_set_pen(int offset)
{
	if ((offset >> 8) != qix_palettebank)
		return;

	const int data = qix_paletteram[offset];
	const int intensity = (data >> 0) & 0x03;

	int bits = (data >> 6) & 0x03;
	const int red = qix_intensity_table[(bits << 2) | intensity];
	bits = (data >> 4) & 0x03;
	const int green = qix_intensity_table[(bits << 2) | intensity];
	bits = (data >> 2) & 0x03;
	const int blue = qix_intensity_table[(bits << 2) | intensity];

	palette_change_color(offset & 0xff, (unsigned char)red,
		(unsigned char)green, (unsigned char)blue);
}

READ_HANDLER(qix_paletteram_r)       // 0x9000-0x93ff, video cpu
{
	return qix_paletteram[address];
}

WRITE_HANDLER(qix_paletteram_w)      // 0x9000-0x93ff, video cpu
{
	qix_paletteram[address] = data;
	qix_set_pen(address);
}

WRITE_HANDLER(qix_palettebank_w)     // 0x8800, video cpu
{
	if (qix_palettebank != (data & 3))
	{
		qix_palettebank = data & 3;
		for (int i = 0; i < 256; i++)
			qix_set_pen((qix_palettebank << 8) | i);
	}

	// LEDs are in the upper 6 bits.
}

// Palette starts black; the game programs color RAM itself.
static void qix_init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	memset(palette, 0, 256 * 3);
}

// ---------------------------------------------------------------------------
// Video RAM
//
// 0.90 keeps videoram as the sole truth and blits it to the bitmap in
// VIDEO_UPDATE. AAE never clears main_bitmap between frames and its pens are
// an identity map that palette_recalc() recolors in place, so plotting at
// write time is both cheaper and always correct: the bitmap holds pen
// indices, and a later color change repaints them for free. Nothing else can
// modify videoram, so no per-frame blit is needed at all.
//
// vram_mask (0.90's Slither write-blend register) is not carried over - it is
// 0xff for every Qix set, and Slither is not one of them.
// ---------------------------------------------------------------------------

static inline void qix_plot(int offset, unsigned char data)
{
	qix_videoram[offset] = data;
	plot_pixel(main_bitmap, offset & 0xff, offset >> 8, Machine->pens[data]);
}

READ_HANDLER(qix_videoram_r)         // 0x0000-0x7fff, video cpu
{
	// add in the upper bit of the address latch
	return qix_videoram[address + ((qix_videoaddress[0] & 0x80) << 8)];
}

WRITE_HANDLER(qix_videoram_w)        // 0x0000-0x7fff, video cpu
{
	// add in the upper bit of the address latch
	qix_plot((int)address + ((qix_videoaddress[0] & 0x80) << 8), data);
}

READ_HANDLER(qix_addresslatch_r)     // 0x9400, video cpu
{
	return qix_videoram[(qix_videoaddress[0] << 8) | qix_videoaddress[1]];
}

WRITE_HANDLER(qix_addresslatch_w)    // 0x9400, video cpu
{
	qix_plot((qix_videoaddress[0] << 8) | qix_videoaddress[1], data);
}

WRITE_HANDLER(qix_videoaddress_w)    // 0x9402-0x9403, video cpu
{
	qix_videoaddress[address & 1] = data;
}

// The scan line counter the video ROM polls; several routines spin here
// waiting for it to wrap to zero.
READ_HANDLER(qix_scanline_r)         // 0x9800, video cpu
{
	const int scanline = aae_cpu_getscanline();
	return (UINT8)((scanline <= 0xff) ? scanline : 0);
}

// ---------------------------------------------------------------------------
// Shared RAM
//
// AAE's MRA_RAM/MWA_RAM index memory_region[active_cpu], so the dual-port
// window cannot be plain RAM the way it is in MAME - each CPU would get its
// own private copy. Explicit handlers over one array instead.
// ---------------------------------------------------------------------------

READ_HANDLER(qix_sharedram_r)
{
	return qix_sharedram[address];
}

WRITE_HANDLER(qix_sharedram_w)
{
	qix_sharedram[address] = data;
}

// ---------------------------------------------------------------------------
// Cross-CPU FIRQ
//
// AAE's 6809 firq_line() latches a request that the core lowers when the
// interrupt is taken, so an assert behaves as a pulse and the explicit ack is
// a no-op in the common case. Keeping both halves anyway: the ROMs write the
// ack unconditionally, and an unmapped write there would be a silent lie.
// ---------------------------------------------------------------------------

WRITE_HANDLER(qix_data_firq_w) { m_cpu_6809[CPU0]->firq_line(true); }
WRITE_HANDLER(qix_data_firq_ack_w) { m_cpu_6809[CPU0]->firq_line(false); }
READ_HANDLER(qix_data_firq_r) { m_cpu_6809[CPU0]->firq_line(true);  return 0xff; }
READ_HANDLER(qix_data_firq_ack_r) { m_cpu_6809[CPU0]->firq_line(false); return 0xff; }

WRITE_HANDLER(qix_video_firq_w) { m_cpu_6809[CPU1]->firq_line(true); }
WRITE_HANDLER(qix_video_firq_ack_w) { m_cpu_6809[CPU1]->firq_line(false); }
READ_HANDLER(qix_video_firq_r) { m_cpu_6809[CPU1]->firq_line(true);  return 0xff; }
READ_HANDLER(qix_video_firq_ack_r) { m_cpu_6809[CPU1]->firq_line(false); return 0xff; }

// ---------------------------------------------------------------------------
// PIA peripheral wiring
// ---------------------------------------------------------------------------

static uint8_t qix_in_port0(void) { return (uint8_t)readinputport(0); }
static uint8_t qix_in_port1(void) { return (uint8_t)readinputport(1); }
static uint8_t qix_in_port2(void) { return (uint8_t)readinputport(2); }
static uint8_t qix_in_port3(void) { return (uint8_t)readinputport(3); }
static uint8_t qix_in_port4(void) { return (uint8_t)readinputport(4); }

// /DINT - PIA 3's interrupt output is the data CPU's only IRQ source.
static void qix_pia_dint(int state) { m_cpu_6809[CPU0]->irq_line(state != 0); }

// /SINT - PIA 4's interrupt output is the sound CPU's only IRQ source.
static void qix_pia_sint(int state) { m_cpu_6800[CPU2]->irq_line(state != 0); }

// Data CPU -> sound CPU. 0.90 defers this through timer_set(TIME_NOW) so the
// 6802 cannot miss a command while the data CPU runs ahead. AAE has no
// scheduler-synchronize primitive; the fine CPU interleave set in the driver
// definition (256 slices/frame, vs 0.90's default) serves the same purpose.
static void qix_pia_3_porta_out(uint8_t data) { pia4.set_a(data); }
static void qix_pia_3_ca2_out(int state) { pia4.set_ca1(state); }

// Sound CPU -> data CPU.
static void qix_pia_4_porta_out(uint8_t data) { pia3.set_a(data); }
static void qix_pia_4_ca2_out(int state) { pia3.set_ca1(state); }

static void qix_dac_w(uint8_t data) { DAC_data_w(0, data); }

// INV, the cocktail flip line. Tracked as 0.90 does; only Slither's trackball
// readers ever consume it, so for Qix it is recorded and nothing more.
static void qix_inv_flag_w(int state) { qix_cocktail_flip = (unsigned char)state; }

// Coin lockout (bit 2, inverted) and coin counter (bit 1). AAE has no
// bookkeeping counters, so this records the byte and stops there.
static void qix_coinctl_w(uint8_t data) { qix_coinctrl = data; }

static mc6821_interface qix_pia_0_intf(void)
{
	mc6821_interface i{};
	i.in_a = qix_in_port0;
	i.in_b = qix_in_port1;          // coin door
	return i;
}

static mc6821_interface qix_pia_1_intf(void)
{
	mc6821_interface i{};
	i.in_a = qix_in_port2;
	i.in_b = qix_in_port3;
	return i;
}

static mc6821_interface qix_pia_2_intf(void)
{
	mc6821_interface i{};
	i.in_a = qix_in_port4;
	i.out_b = qix_coinctl_w;
	return i;
}

static mc6821_interface qix_pia_3_intf(void)
{
	mc6821_interface i{};
	i.out_a = qix_pia_3_porta_out;
	i.out_ca2 = qix_pia_3_ca2_out;
	i.out_cb2 = qix_inv_flag_w;
	i.irq_a = qix_pia_dint;
	i.irq_b = qix_pia_dint;
	return i;
}

// PIA 4's port A input needs no callback: the data CPU pushes the command in
// with set_a(), which is exactly what 0.90's pia_4_porta_r reads back.
static mc6821_interface qix_pia_4_intf(void)
{
	mc6821_interface i{};
	i.out_a = qix_pia_4_porta_out;
	i.out_b = qix_dac_w;
	i.out_ca2 = qix_pia_4_ca2_out;
	i.irq_a = qix_pia_sint;
	i.irq_b = qix_pia_sint;
	return i;
}

// The TMS5220 PIA. 0.90 wires it identically to PIA 4 but with its interrupt
// outputs left unconnected; since no Qix ROM ever addresses $2000 none of it
// runs. Carried over as-is rather than second-guessing the schematic.
static mc6821_interface qix_pia_5_intf(void)
{
	mc6821_interface i{};
	i.out_a = qix_pia_4_porta_out;
	i.out_b = qix_dac_w;
	i.out_ca2 = qix_pia_4_ca2_out;
	return i;
}

// ---------------------------------------------------------------------------
// Interrupts
//
// VSYNC feeds PIA 3's CB1. 0.90 raises it in a VBLANK handler and drops it
// from a scanline-0 timer; here the CPU0 callback runs 64x per frame (every
// four scanlines) and derives the level from the scanline counter, so the
// mc6821 still sees one clean edge per frame in each direction.
// ---------------------------------------------------------------------------

void qix_vblank_interrupt(void)
{
	// Visible area ends at line 247; 248-255 is vertical blank.
	pia3.set_cb1((aae_cpu_getscanline() >= 248) ? 1 : 0);
}

void qix_video_dummy_interrupt(void)
{
	// the video 6809's only interrupt source is the data CPU's FIRQ
}

void qix_sound_dummy_interrupt(void)
{
	// the 6802's only interrupt source is PIA 4
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------

MEM_READ(qix_readmem_data)
MEM_ADDR(0x8000, 0x83ff, qix_sharedram_r)
MEM_ADDR(0x8400, 0x87ff, MRA_RAM)
MEM_ADDR(0x8800, 0x8800, MRA_NOP)              // ACIA
MEM_ADDR(0x8c00, 0x8c00, qix_video_firq_r)
MEM_ADDR(0x8c01, 0x8c01, qix_data_firq_ack_r)
MEM_ADDR(0x9000, 0x93ff, pia_3_r)
MEM_ADDR(0x9400, 0x97ff, pia_0_r)
MEM_ADDR(0x9800, 0x9bff, pia_1_r)
MEM_ADDR(0x9c00, 0x9fff, pia_2_r)
MEM_ADDR(0xa000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(qix_writemem_data)
MEM_ADDR(0x8000, 0x83ff, qix_sharedram_w)
MEM_ADDR(0x8400, 0x87ff, MWA_RAM)
MEM_ADDR(0x8c00, 0x8c00, qix_video_firq_w)
MEM_ADDR(0x8c01, 0x8c01, qix_data_firq_ack_w)
MEM_ADDR(0x9000, 0x93ff, pia_3_w)
// 0.90 routes this through qix_pia_0_w, a scheduler-synchronize wrapper that
// exists so the 68705 coin MCU cannot miss a command. No Qix set has that
// MCU, so the write goes straight to the PIA.
MEM_ADDR(0x9400, 0x97ff, pia_0_w)
MEM_ADDR(0x9800, 0x9bff, pia_1_w)
MEM_ADDR(0x9c00, 0x9fff, pia_2_w)
MEM_ADDR(0xa000, 0xffff, MWA_ROM)
MEM_END

MEM_READ(qix_readmem_video)
MEM_ADDR(0x0000, 0x7fff, qix_videoram_r)
MEM_ADDR(0x8000, 0x83ff, qix_sharedram_r)
MEM_ADDR(0x8400, 0x87ff, MRA_RAM)              // CMOS
MEM_ADDR(0x8c00, 0x8c00, qix_data_firq_r)
MEM_ADDR(0x8c01, 0x8c01, qix_video_firq_ack_r)
MEM_ADDR(0x9000, 0x93ff, qix_paletteram_r)
MEM_ADDR(0x9400, 0x9400, qix_addresslatch_r)
MEM_ADDR(0x9800, 0x9800, qix_scanline_r)
MEM_ADDR(0xa000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(qix_writemem_video)
MEM_ADDR(0x0000, 0x7fff, qix_videoram_w)
MEM_ADDR(0x8000, 0x83ff, qix_sharedram_w)
MEM_ADDR(0x8400, 0x87ff, MWA_RAM)              // CMOS - battery backed
MEM_ADDR(0x8800, 0x8800, qix_palettebank_w)
MEM_ADDR(0x8c00, 0x8c00, qix_data_firq_w)
MEM_ADDR(0x8c01, 0x8c01, qix_video_firq_ack_w)
MEM_ADDR(0x9000, 0x93ff, qix_paletteram_w)
MEM_ADDR(0x9400, 0x9400, qix_addresslatch_w)
MEM_ADDR(0x9402, 0x9403, qix_videoaddress_w)
MEM_ADDR(0x9c00, 0x9fff, MWA_RAM)              // CRT controller
MEM_ADDR(0xa000, 0xffff, MWA_ROM)
MEM_END

// Sound board: 128 bytes of on-chip 6802 RAM, the two PIAs, and u27 at the
// top of the D000-FFFF window.
MEM_READ(qix_readmem_sound)
MEM_ADDR(0x0000, 0x007f, MRA_RAM)
MEM_ADDR(0x2000, 0x2003, pia_5_r)
MEM_ADDR(0x4000, 0x4003, pia_4_r)
MEM_ADDR(0xd000, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(qix_writemem_sound)
MEM_ADDR(0x0000, 0x007f, MWA_RAM)
MEM_ADDR(0x2000, 0x2003, pia_5_w)
MEM_ADDR(0x4000, 0x4003, pia_4_w)
MEM_ADDR(0xd000, 0xffff, MWA_ROM)
MEM_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static const struct DACinterface qix_dac_intf = { 1, { 100 } };

int init_qix(void)
{
	aae_set_lines_per_frame(256);

	memset(qix_videoram, 0, sizeof(qix_videoram));
	memset(qix_sharedram, 0, sizeof(qix_sharedram));
	memset(qix_paletteram, 0, sizeof(qix_paletteram));
	qix_videoaddress[0] = qix_videoaddress[1] = 0;
	qix_palettebank = 0;
	qix_cocktail_flip = 0;
	qix_coinctrl = 0;

	pia_detach_all();
	pia0.configure(qix_pia_0_intf());  pia_attach(0, &pia0);
	pia1.configure(qix_pia_1_intf());  pia_attach(1, &pia1);
	pia2.configure(qix_pia_2_intf());  pia_attach(2, &pia2);
	pia3.configure(qix_pia_3_intf());  pia_attach(3, &pia3);
	pia4.configure(qix_pia_4_intf());  pia_attach(4, &pia4);
	pia5.configure(qix_pia_5_intf());  pia_attach(5, &pia5);
	pia_reset_all();

	// CMOS is the RAM behind the MRA_RAM/MWA_RAM rows at 8400 on the VIDEO
	// cpu, so it lives in that cpu's region - memory_region[CPU1].
	nvram_set_region(&Machine->memory_region[CPU1][0x8400], 0x400, 0x00);

	if (DAC_sh_start(&qix_dac_intf))
		return 1;

	LOG_INFO("qix: init complete");
	return 0;
}

void run_qix(void)
{
	// Commit deferred color-register writes. No bitmap work: pixels were
	// plotted as the video cpu wrote them and AAE's pens are an identity map
	// that this call recolors in place.
	palette_recalc();
	DAC_sh_update();

	// Qix has no watchdog - nothing in its memory map pets one - but AAE's is
	// always armed and resets every CPU after ~half a second. Pet it here, as
	// aztarac and astrocade do.
	watchdog_reset_w(0, 0, 0);
}

void end_qix(void)
{
	DAC_sh_stop();
	pia_detach_all();
}

// ---------------------------------------------------------------------------
// Inputs (0.90 port order preserved - the PIA interfaces index by number)
// ---------------------------------------------------------------------------

#define QIX_COIN_PORT \
PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_SERVICE, "Test Advance",   OSD_KEY_F1, IP_JOY_NONE) \
PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_SERVICE, "Test Next line", OSD_KEY_F2, IP_JOY_NONE) \
PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, "Test Slew Up",   OSD_KEY_F5, IP_JOY_NONE) \
PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_SERVICE, "Test Slew Down", OSD_KEY_F6, IP_JOY_NONE) \
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN1) \
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN2) \
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN3) \
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_TILT)

INPUT_PORTS_START(qix)
PORT_START("IN0")                                   /* PIA 0 port A - player 1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)          /* slow draw */
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)          /* fast draw */

PORT_START("IN1")                                   /* PIA 0 port B - coin door */
QIX_COIN_PORT

PORT_START("IN2")                                   /* PIA 1 port A - spare */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN3")                                   /* PIA 1 port B - spare */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN4")                                   /* PIA 2 port A - player 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x60, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROM definitions (MAME 0.286 names, CRCs and SHA1s)
// ---------------------------------------------------------------------------

ROM_START(qix)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for the data CPU */
ROM_LOAD("qq12_rev2.u12", 0xc000, 0x0800, CRC(aad35508) SHA1(5fa72e00b4373de21e27a86b49a44a9769f769f4))
ROM_LOAD("qq13_rev2.u13", 0xc800, 0x0800, CRC(46c13504) SHA1(19c084c38b75f14bf5094b317afeecaca6870f7a))
ROM_LOAD("qq14_rev2.u14", 0xd000, 0x0800, CRC(5115e896) SHA1(8359a1700fff7a38e8ea4f92a4f18bc628cf1cb1))
ROM_LOAD("qq15_rev2.u15", 0xd800, 0x0800, CRC(ccd52a1b) SHA1(86d134cd769ef12820638b96a4ffedd8b15dffd2))
ROM_LOAD("qq16_rev2.u16", 0xe000, 0x0800, CRC(cd1c36ee) SHA1(b379b1fe3109947a12c9683cd0c2400c2ee845b3))
ROM_LOAD("qq17_rev2.u17", 0xe800, 0x0800, CRC(1acb682d) SHA1(a2c60964e8d838d09662f8a670c6da41ba850df9))
ROM_LOAD("qq18_rev2.u18", 0xf000, 0x0800, CRC(de77728b) SHA1(8e183bb27858aad9a996e4a2e5a95f0145d1f5b4))
ROM_LOAD("qq19_rev2.u19", 0xf800, 0x0800, CRC(c0994776) SHA1(9452a98c78a038679c4e58f4a9983adb28ea5e78))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the video CPU */
ROM_LOAD("qq4_rev2.u4", 0xc800, 0x0800, CRC(5b906a09) SHA1(84a2e817d6718e0276fcea702811a91bc054a670))
ROM_LOAD("qq5_rev2.u5", 0xd000, 0x0800, CRC(254a3587) SHA1(66045c71cc1d04d4e03c728e578f570fbf7c650d))
ROM_LOAD("qq6_rev2.u6", 0xd800, 0x0800, CRC(ace30389) SHA1(50c6275d13cfbca7750d5a3e725faedba7574e04))
ROM_LOAD("qq7_rev2.u7", 0xe000, 0x0800, CRC(8ebcfa7c) SHA1(21ccf5e74424ab5470473d1059ee6a43d144f685))
ROM_LOAD("qq8_rev2.u8", 0xe800, 0x0800, CRC(b8a3c8f9) SHA1(32ba771913ef44b1133ecfaedaae7f96dcc84343))
ROM_LOAD("qq9_rev2.u9", 0xf000, 0x0800, CRC(26cbcd55) SHA1(2e55e222f850548cd1d461ab5337e98dd817b567))
ROM_LOAD("qq10_rev2.u10", 0xf800, 0x0800, CRC(568be942) SHA1(8b6a01d983d355a64372fa76af810ab53e09d5df))
ROM_REGION(0x10000, REGION_CPU3, 0)      /* 64k for the sound CPU */
ROM_LOAD("qq27.u27", 0xf800, 0x0800, CRC(f3782bd0) SHA1(bfc6d29f9668e02857453e96c005c81568ae931d))
ROM_END

// original taito board 08-00003-001
ROM_START(qixa)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for the data CPU */
ROM_LOAD("u12_", 0xc000, 0x0800, CRC(5adc046d) SHA1(aafd3ae7139a83fe64fc69c771a57fe7ee271469))
ROM_LOAD("u13_", 0xc800, 0x0800, CRC(e63283c6) SHA1(efb6e5c835c0511be024f664cb8d2fcfe8f842f1))
ROM_LOAD("u14_", 0xd000, 0x0800, CRC(a2fd4c28) SHA1(7da8bd1164d8d1a16f0841345ab6d73ae4fde97e))
ROM_LOAD("u15_", 0xd800, 0x0800, CRC(0e5e17d6) SHA1(fd6afe1ef87158868c266a0d39dd894c1f656a1f))
ROM_LOAD("u16_", 0xe000, 0x0800, CRC(6acfa3b8) SHA1(4759c72626cf287b068a1f14eae9fdb210b6ecfa))
ROM_LOAD("u17_", 0xe800, 0x0800, CRC(bc091f21) SHA1(f3db149a794640e0826def688ab19a26750d9f1e))
ROM_LOAD("u18_", 0xf000, 0x0800, CRC(610b19ce) SHA1(6d3d6012a4d0cd3ea82f4ab07582fa262feaaf97))
ROM_LOAD("u19_", 0xf800, 0x0800, CRC(11f957f4) SHA1(5700603b2d50eef6ab32316f59d70782a8ef4a6d))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the video CPU */
ROM_LOAD("u3_", 0xc000, 0x0800, CRC(79cf997c) SHA1(853cb88d371cd2d47caf60baa795caef9461815c))
ROM_LOAD("u4_", 0xc800, 0x0800, CRC(e5ee74cd) SHA1(10fd95385d0d8667f739fe43a9fb26d2780840f9))
ROM_LOAD("u5_", 0xd000, 0x0800, CRC(4e939d87) SHA1(ca45f212bd419666716685931f18d2990444bb1b))
ROM_LOAD("u6_", 0xd800, 0x0800, CRC(b42ca7d8) SHA1(09907f7c2c5165ce5cdf270c33caafc9e5db534e))
ROM_LOAD("u7_", 0xe000, 0x0800, CRC(d6733019) SHA1(89e9e63c91e044fe1c6ce883e3ec18eec0cb39d3))
ROM_LOAD("u8_", 0xe800, 0x0800, CRC(8cab8fb2) SHA1(4ae58119d24ac70bea2cd4871a21a59ef20f7351))
ROM_LOAD("u9_", 0xf000, 0x0800, CRC(98fbac76) SHA1(5a9b5bdf930dc494cd50059f88256d7e561b4e09))
ROM_LOAD("u10_", 0xf800, 0x0800, CRC(b40f084a) SHA1(16811b8f24b955a72f2a0950e9688cbd0e26afa4))
ROM_REGION(0x10000, REGION_CPU3, 0)      /* 64k for the sound CPU */
ROM_LOAD("qq27.u27", 0xf800, 0x0800, CRC(f3782bd0) SHA1(bfc6d29f9668e02857453e96c005c81568ae931d))
ROM_END

// same set as qixa, but with larger roms
ROM_START(qixb)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for the data CPU */
ROM_LOAD("lk14.bin", 0xc000, 0x1000, CRC(6d164986) SHA1(c805abe1a441e10080ceca8ba547835bafb61bcc))
ROM_LOAD("lk15.bin", 0xd000, 0x1000, CRC(16c6ce0f) SHA1(b8091d2db476d2acb4b3f0789e1f155336be9b39))
ROM_LOAD("lk16.bin", 0xe000, 0x1000, CRC(698b1f9c) SHA1(7e7637ca5985f072e821e16f8b65aedb87df136b))
ROM_LOAD("lk17.bin", 0xf000, 0x1000, CRC(7e3adde6) SHA1(dfe66317f87e10919f1ea4b4d565703e73039821))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the video CPU */
ROM_LOAD("lk10.bin", 0xc000, 0x1000, CRC(7eac67d0) SHA1(ca5938422aaa1e380af0afa505876d4682ac69b9))
ROM_LOAD("lk11.bin", 0xd000, 0x1000, CRC(90ccbb6a) SHA1(b65592384597dc2aafc02f49b6b6f477c9112580))
ROM_LOAD("lk12.bin", 0xe000, 0x1000, CRC(be9b9f7d) SHA1(e681bdb9aa8b8c31af1c14e23d0f420577d6db63))
ROM_LOAD("lk13.bin", 0xf000, 0x1000, CRC(51c9853b) SHA1(29a5221f2af866d2ee73110409ecddc2c96404fd))
ROM_REGION(0x10000, REGION_CPU3, 0)      /* 64k for the sound CPU */
ROM_LOAD("qq27.u27", 0xf800, 0x0800, CRC(f3782bd0) SHA1(bfc6d29f9668e02857453e96c005c81568ae931d))
ROM_END

// oldest set / prototype? spells 'deutch' and will not let the language be changed
ROM_START(qixo)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for the data CPU */
ROM_LOAD("qu12", 0xc000, 0x0800, CRC(1c55b44d) SHA1(6385e5e484e24cf396c14de86344170639c3cc65))
ROM_LOAD("qu13", 0xc800, 0x0800, CRC(20279e8c) SHA1(722da239636de3fe40318768ddbe687b19afcdb6))
ROM_LOAD("qu14", 0xd000, 0x0800, CRC(bafe3ce3) SHA1(648a54545a1b545c82c0ace5eb1ce17af5ea7391))
/* d800-dfff empty */
ROM_LOAD("qu16", 0xe000, 0x0800, CRC(db560753) SHA1(4acbe17f1e555f45606ddec197c5ab691ff46d39))
ROM_LOAD("qu17", 0xe800, 0x0800, CRC(8c7aeed8) SHA1(b7da2b0f34f72f9853cdf6ce55e604b09fcf4728))
ROM_LOAD("qu18", 0xf000, 0x0800, CRC(353be980) SHA1(a50e02fcc69771a13b238aa0e8dc3c56b01a58d5))
ROM_LOAD("qu19", 0xf800, 0x0800, CRC(f46a69ca) SHA1(dacb53c0318445da3fbb86f9a45914c5b7a4c4a1))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the video CPU */
ROM_LOAD("qu3", 0xc000, 0x0800, CRC(8b4c0ef0) SHA1(6d18d1052f342e3b3313f2174b20f2a179e2c6bd))
ROM_LOAD("qu4", 0xc800, 0x0800, CRC(66a5c260) SHA1(8cce71bcd3a432650f0d0c94f3a2151ba8154220))
ROM_LOAD("qu5", 0xd000, 0x0800, CRC(70160ea3) SHA1(a411130c5c669a181564369a8921b26e0f0b5450))
/* d800-dfff empty */
ROM_LOAD("qu7", 0xe000, 0x0800, CRC(d6733019) SHA1(89e9e63c91e044fe1c6ce883e3ec18eec0cb39d3))
ROM_LOAD("qu8", 0xe800, 0x0800, CRC(66870dcc) SHA1(9f926390f5ce86d7c1bf55b75dbfb34119425c46))
ROM_LOAD("qu9", 0xf000, 0x0800, CRC(c99bf94d) SHA1(7b6fa6e1cf0f131909d44694c261b1cc2de65003))
ROM_LOAD("qu10", 0xf800, 0x0800, CRC(88b45037) SHA1(e2e5fefe377def3f784026b921527898af8b83a9))
ROM_REGION(0x10000, REGION_CPU3, 0)      /* 64k for the sound CPU */
ROM_LOAD("qu27.u27", 0xf800, 0x0800, CRC(f3782bd0) SHA1(bfc6d29f9668e02857453e96c005c81568ae931d))
ROM_END

ROM_START(qix2)
ROM_REGION(0x10000, REGION_CPU1, 0)      /* 64k for the data CPU */
ROM_LOAD("u12.rmb", 0xc000, 0x0800, CRC(484280fd) SHA1(a60c1a278e519721294b2486dc817d248d19c3be))
ROM_LOAD("u13.rmb", 0xc800, 0x0800, CRC(3d089fcb) SHA1(f4f31134c9c15160d2d15cb41296dfec6f2dfe37))
ROM_LOAD("u14.rmb", 0xd000, 0x0800, CRC(362123a9) SHA1(3e2a853f6960f2d5fdcdef8dec8ccf5aad449548))
ROM_LOAD("u15.rmb", 0xd800, 0x0800, CRC(60f3913d) SHA1(a97b658fe2c58b00c2749072828b2e0032894915))
ROM_LOAD("u16.rmb", 0xe000, 0x0800, CRC(cc139e34) SHA1(0ed3e7179b0cbaa31fa91e1ed862b86f5032919a))
ROM_LOAD("u17.rmb", 0xe800, 0x0800, CRC(cf31dc49) SHA1(71c089d827ab61ba69e5e95b7e53220763786df9))
ROM_LOAD("u18.rmb", 0xf000, 0x0800, CRC(1f91ed7a) SHA1(85bb5370a244719663a4f859f66860613aa2b86e))
ROM_LOAD("u19.rmb", 0xf800, 0x0800, CRC(68e8d5a6) SHA1(d09252c393be2fdaf3b9b9f477c79f721d15943f))
ROM_REGION(0x10000, REGION_CPU2, 0)      /* 64k for the video CPU */
ROM_LOAD("u3.rmb", 0xc000, 0x0800, CRC(19cebaca) SHA1(7d7e79ab0920952cf7618567c9c65397535b6d4f))
ROM_LOAD("u4.rmb", 0xc800, 0x0800, CRC(6cfb4185) SHA1(6545dece8eaeb716877aa6e7b24c21f6e5991451))
ROM_LOAD("u5.rmb", 0xd000, 0x0800, CRC(948f53f3) SHA1(db6eddec8ba41335316d80b6f97e932bf91139af))
ROM_LOAD("u6.rmb", 0xd800, 0x0800, CRC(8630120e) SHA1(14a020fd1bff4acbb034883e33130adda85884e5))
ROM_LOAD("u7.rmb", 0xe000, 0x0800, CRC(bad037c9) SHA1(17218c31895b1547b71d2d9d2b6a93d2e5d73bdd))
ROM_LOAD("u8.rmb", 0xe800, 0x0800, CRC(3159bc00) SHA1(479a69bfe5af48d5ce63978265ce59f79c25749f))
ROM_LOAD("u9.rmb", 0xf000, 0x0800, CRC(e80e9b1d) SHA1(66ef22a26df3f766ae813213473b9ac4b35b01f6))
ROM_LOAD("u10.rmb", 0xf800, 0x0800, CRC(9a55d360) SHA1(fc5f8c853dcc573f6b36dbdd63e5d1edba88bce1))
ROM_REGION(0x10000, REGION_CPU3, 0)      /* 64k for the sound CPU */
ROM_LOAD("u27", 0xf800, 0x0800, CRC(f3782bd0) SHA1(bfc6d29f9668e02857453e96c005c81568ae931d))
ROM_END

// ---------------------------------------------------------------------------
// Drivers
//
// 256 slices per frame across all three CPUs. 0.90 leans on
// timer_set(TIME_NOW) to synchronize the data CPU with the 6802 before a
// sound command lands; AAE has no such primitive, so the interleave has to be
// fine enough that the 6802 services PIA 4 promptly. At 1.25MHz/60Hz that is
// roughly 81 cycles of the data CPU between switches.
// ---------------------------------------------------------------------------

#define QIX_CPUS()                                                              \
AAE_DRIVER_CPUS(                                                                \
	AAE_CPU_ENTRY(                                                              \
		/*type*/     CPU_M6809,          /* data CPU  */                        \
		/*freq*/     20000000 / 4 / 4,   /* 1.25 MHz  */                        \
		/*div*/      256,                                                       \
		/*ipf*/      64,                 /* VSYNC sampling, every 4 lines */    \
		/*int type*/ INT_TYPE_NONE,                                             \
		/*int cb*/   &qix_vblank_interrupt,                                     \
		/*r8*/       qix_readmem_data,                                          \
		/*w8*/       qix_writemem_data,                                         \
		/*pr*/       nullptr,                                                   \
		/*pw*/       nullptr,                                                   \
		/*r16*/      nullptr,                                                   \
		/*w16*/      nullptr                                                    \
	),                                                                          \
	AAE_CPU_ENTRY(                                                              \
		CPU_M6809, 20000000 / 4 / 4, 256, 1, INT_TYPE_NONE,                     \
		&qix_video_dummy_interrupt,                                             \
		qix_readmem_video, qix_writemem_video,                                  \
		nullptr, nullptr, nullptr, nullptr                                      \
	),                                                                          \
	AAE_CPU_ENTRY(                                                              \
		CPU_M6802, 7372800 / 2 / 4, 256, 1, INT_TYPE_NONE,                      \
		&qix_sound_dummy_interrupt,                                             \
		qix_readmem_sound, qix_writemem_sound,                                  \
		nullptr, nullptr, nullptr, nullptr                                      \
	),                                                                          \
	AAE_CPU_NONE_ENTRY()                                                        \
)

#define QIX_VIDEO()                                                             \
AAE_DRIVER_VIDEO_CORE(60, DEFAULT_REAL_60HZ_VBLANK_DURATION,                    \
	VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_ROTATE_270)    \
AAE_DRIVER_SCREEN(256, 256, 0, 255, 8, 247)                                     \
AAE_DRIVER_RASTER(nullptr, 256, 0, qix_init_palette)

AAE_DRIVER_BEGIN(drv_qix, "qix", "Qix (Rev 2)")
AAE_DRIVER_ROM(rom_qix)
AAE_DRIVER_FUNCS(&init_qix, &run_qix, &end_qix)
AAE_DRIVER_INPUT(input_ports_qix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
QIX_CPUS()
QIX_VIDEO()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_qixa, "qixa", "Qix (set 2, smaller roms)")
AAE_DRIVER_ROM(rom_qixa)
AAE_DRIVER_FUNCS(&init_qix, &run_qix, &end_qix)
AAE_DRIVER_INPUT(input_ports_qix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
QIX_CPUS()
QIX_VIDEO()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("qix")
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_qixb, "qixb", "Qix (set 2, larger roms)")
AAE_DRIVER_ROM(rom_qixb)
AAE_DRIVER_FUNCS(&init_qix, &run_qix, &end_qix)
AAE_DRIVER_INPUT(input_ports_qix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
QIX_CPUS()
QIX_VIDEO()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("qix")
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_qixo, "qixo", "Qix (set 3, earlier)")
AAE_DRIVER_ROM(rom_qixo)
AAE_DRIVER_FUNCS(&init_qix, &run_qix, &end_qix)
AAE_DRIVER_INPUT(input_ports_qix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
QIX_CPUS()
QIX_VIDEO()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("qix")
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_qix2, "qix2", "Qix II (Tournament)")
AAE_DRIVER_ROM(rom_qix2)
AAE_DRIVER_FUNCS(&init_qix, &run_qix, &end_qix)
AAE_DRIVER_INPUT(input_ports_qix)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
QIX_CPUS()
QIX_VIDEO()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("qix")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_qix)
AAE_REGISTER_DRIVER(drv_qixa)
AAE_REGISTER_DRIVER(drv_qixb)
AAE_REGISTER_DRIVER(drv_qixo)
AAE_REGISTER_DRIVER(drv_qix2)
