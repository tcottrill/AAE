#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "missile.h"
#include "aae_pokey.h"
#include "earom.h"
#include "timer.h"

extern void osd_modify_pen(int pen, unsigned char red, unsigned char green, unsigned char blue);
// Video Section Start ---------------------------

#pragma warning( disable : 4838 4003 )

static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	1,	/* 1 bit per pixel */
	{ 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },	/* pretty straightforward layout */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8	/* every char takes 8 consecutive bytes */
};

struct GfxDecodeInfo missile_gfxdecodeinfo[] =
{
	{ REGION_CPU1, 0xe800, &charlayout, 0, 32 },	/* the game dynamically modifies this */
	{ -1 }	/* end of array */
};

static struct POKEYinterface missile_pokey_interface =
{
	1,	/* 1 chip */
	1250000,	/* 1.25 MHz??? */
	{ 100 },
	6,
	USE_CLIP,
	/* The 8 pot handlers */
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	/* The allpot handler */
	{ input_port_3_r },
};

//extern unsigned char* missile_video2ram;
void missile_init_machine(void);
int  missile_vh_start(void);
void missile_vh_stop(void);

// Machine

static int ctrld;
static int h_pos, v_pos;
extern int missile_flipscreen;
int  missile_video_r(int address);
void missile_flip_screen(void);

// Video
unsigned char* missile_videoram;
unsigned char* missile_video2ram;
int missile_flipscreen;
static int screen_flipped;


// ----------------------------------------------------------------------------
// Missile Command IRQ scheduling (4x per frame at 32V phase)
//
// Hardware: D flip-flop latched by SYNC (opcode fetch), D fed from a second FF
// clocked by 16V or /16V (depending on flip), sampling 32V. The /IRQ line is
// active low when 32V=0. We pulse the CPU IRQ 4x per frame at the start of
// each 32-line "low" window.
//
// Not flipped:  V = 0, 64, 128, 192
// Flipped:      V = 16, 80, 144, 208
// ----------------------------------------------------------------------------

static int t_irq_align = -1;
static int t_irq_rep = -1;

static void missile_irq_fire(int)
{
	// Pulse the 6502 IRQ now; acknowledge via $4D00 clears it on your core.
	cpu_do_int_imm(/*CPU*/0, INT_TYPE_INT);
}

static void missile_stop_irq_timers()
{
	if (t_irq_rep >= 0) { timer_remove(t_irq_rep);  t_irq_rep = -1; }
	if (t_irq_align >= 0) { timer_remove(t_irq_align); t_irq_align = -1; }
}

static void missile_irq_align_cb(int)
{
	// Fire immediately at the alignment scanline, then repeat every 64 lines.
	missile_irq_fire(0);

	const int cpl = aae_cpu_getscanlinecycles(); // CPU0 cycles per scanline
	if (t_irq_rep >= 0) timer_remove(t_irq_rep);
	t_irq_rep = timer_pulse(TIME_IN_CYCLES(64 * cpl, /*CPU*/0), /*CPU*/0, missile_irq_fire);
}

static void missile_start_irq_timers()
{
	const int cpl = aae_cpu_getscanlinecycles(); // CPU0 cycles per scanline

	// When flipped, the 16V clock phase inverts, shifting by 16 lines.
	const int base_line = (missile_flipscreen ? 16 : 0); // 0 or 16
	const int align = (base_line % 256) * cpl;

	missile_stop_irq_timers();
	// One-shot to the first "active" scanline in this phase, then a 64-line pulse.
	t_irq_align = timer_set(TIME_IN_CYCLES(align, /*CPU*/0), /*CPU*/0, missile_irq_align_cb);
}


// Timing Code
// --- VBLANK state read by IN1 (bit 7 = active high) ---
static volatile int g_vblank_state = 0;

// Timers: one-shot aligners + steady repeats
static int t_vb_on_align = -1, t_vb_off_align = -1;
static int t_vb_on_rep = -1, t_vb_off_rep = -1;

// Optional: fire a frame IRQ/NMI at vblank edge (pick one or comment out)
static inline void missile_frame_pulse() {
	//cpu_do_int_imm(0, INT_TYPE_INT);   // or INT_TYPE_NMI if your board wants NMI
}

// VBLANK ON (rising): set bit and (optionally) pulse frame interrupt
static void vblank_on_cb(int) {
	g_vblank_state = 1;
	// Most Atari rasters prefer the pulse on RISE; move this to off_cb if needed:
	missile_frame_pulse();

	// Arm repeating ON each frame (60 Hz)
	if (t_vb_on_rep < 0) t_vb_on_rep = timer_set(TIME_IN_HZ(60), /*CPU0*/0, vblank_on_cb);
}

// VBLANK OFF (falling): clear bit
static void vblank_off_cb(int) {
	g_vblank_state = 0;

	// Arm repeating OFF each frame (60 Hz)
	if (t_vb_off_rep < 0) t_vb_off_rep = timer_set(TIME_IN_HZ(60), /*CPU0*/0, vblank_off_cb);
}

static void missile_start_vblank_timers() {
	// NTSC-ish: default lines/frame is 262 in your core; use helpers to phase-align
	const int cpl = aae_cpu_getscanlinecycles();          // cycles per scanline (CPU0)
	const int total_lines = 262;                          // keep default
	const int vbon_line = total_lines - 50;             // ~20 lines of vblank
	const int on_cycles = vbon_line * cpl;              // start of vblank within this frame
	const int off_cycles = 0;                            // start-of-frame

	g_vblank_state = 0;

	// One-shot alignment inside THIS frame, then callbacks arm per-frame repeats
	t_vb_on_align = timer_pulse(TIME_IN_CYCLES(on_cycles, 0), /*CPU0*/0, vblank_on_cb);
	t_vb_off_align = timer_pulse(TIME_IN_CYCLES(off_cycles, 0), /*CPU0*/0, vblank_off_cb);
}

static inline uint8_t read_IN1()
{
	uint8_t data = input_port_1_r(0);

	// PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_VBLANK) -> set when in vblank
	if (g_vblank_state) data |= 0x80;
	else			    data &= ~0x80;

	return data;
}

void missile_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	// 0..7: simple RGB cube corners + white
	static const unsigned char base[8][3] = {
		{ 0x00, 0x00, 0x00 }, // 0 black
		{ 0xFF, 0x00, 0x00 }, // 1 red
		{ 0x00, 0xFF, 0x00 }, // 2 green
		{ 0xFF, 0xFF, 0x00 }, // 3 yellow
		{ 0x00, 0x00, 0xFF }, // 4 blue
		{ 0xFF, 0x00, 0xFF }, // 5 magenta
		{ 0x00, 0xFF, 0xFF }, // 6 cyan
		{ 0xFF, 0xFF, 0xFF }  // 7 white
	};

	// Seed the palette pens (3 bytes per entry)
	for (int i = 0; i < 8; ++i) {
		palette[3 * i + 0] = base[i][0];
		palette[3 * i + 1] = base[i][1];
		palette[3 * i + 2] = base[i][2];
	}

	// 1bpp chars => 2 pens per color code: [background, foreground]
	// Provide 32 "color codes"; foreground just cycles 0..7
	for (int code = 0; code < 32; ++code) {
		colortable[code * 2 + 0] = 0;            // background: black
		colortable[code * 2 + 1] = (unsigned char)(code & 7); // foreground: cycle colors
	}
}

/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int missile_vh_start(void)
{
	/* force video ram to be $0000-$FFFF even though only $1900-$FFFF is used */
	if ((missile_videoram = (unsigned char*)malloc(256 * 256)) == 0)
		return 1;

	memset(missile_videoram, 0, 256 * 256);
	missile_flipscreen = 0;
	return generic_vh_start();
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void missile_vh_stop(void)
{
	free(missile_videoram);
}

/********************************************************************************************/
int missile_video_r(int address)
{
	return (missile_videoram[address] & 0xe0);
}

/********************************************************************************************/
/* This routine is called when the flipscreen bit changes. It forces a redraw of the entire bitmap. */
void missile_flip_screen(void)
{
	screen_flipped = 1;
	missile_start_irq_timers();
}

/********************************************************************************************/
void missile_blit_w(int address)
{
	int x, y;
	int bottom;
	int color = 0;

	/* The top 25 lines ($0000 -> $18ff) aren't used or drawn */
	y = (address >> 8) - 25;
	x = address & 0xff;
	
	if (y < 231 - 32)
		bottom = 1;
	else
		bottom = 0;

	/* cocktail mode */
	if (missile_flipscreen)
	{
		y = Machine->scrbitmap->height - 1 - y;
	}

	color = (missile_videoram[address] >> 5);

	if (bottom) color &= 0x06;

	plot_pixel(Machine->scrbitmap, x, y, Machine->pens[color]);
	// Machine->scrbitmap->line[y][x] = Machine->pens[color];
	//main_bitmap->line[y][x] = color;
}

/********************************************************************************************/
WRITE_HANDLER(missile_video_w)
{
	/* $0640 - $4fff */
	int wbyte, wbit;
	unsigned char* RAM = memory_region(REGION_CPU1);

	if (address < 0xf800)
	{
		missile_videoram[address] = data;
		missile_blit_w(address);
	}
	else
	{
		missile_videoram[address] = (missile_videoram[address] & 0x20) | data;
		missile_blit_w(address);
		wbyte = ((address - 0xf800) >> 2) & 0xfffe;
		wbit = (address - 0xf800) % 8;
		if (data & 0x20)
			RAM[0x401 + wbyte] |= (1 << wbit);
		else
			RAM[0x401 + wbyte] &= ((1 << wbit) ^ 0xff);
	}
}

WRITE_HANDLER(missile_video2_w)
{
	/* $5000 - $ffff */
	address += 0x5000;
	missile_video_w(address, data, 0);
}

/********************************************************************************************/
WRITE_HANDLER(missile_video_mult_w)
{
	/*
		$1900 - $3fff

		2-bit color writes in 4-byte blocks.
		The 2 color bits are in x000x000.

		Note that the address range is smaller because 1 byte covers 4 screen pixels.
	*/

	data = (data & 0x80) + ((data & 8) << 3);
	address = address << 2;

	/* If this is the bottom 8 lines of the screen, set the 3rd color bit */
	if (address >= 0xf800) data |= 0x20;

	missile_videoram[address] = data;
	missile_videoram[address + 1] = data;
	missile_videoram[address + 2] = data;
	missile_videoram[address + 3] = data;

	missile_blit_w(address);
	missile_blit_w(address + 1);
	missile_blit_w(address + 2);
	missile_blit_w(address + 3);
}

/********************************************************************************************/
WRITE_HANDLER(missile_video_3rd_bit_w)
{
	int i;
	unsigned char* RAM = memory_region(REGION_CPU1);

	address += 0x400;
	/* This is needed to make the scrolling text work properly */
	RAM[address] = data;

	address = ((address - 0x401) << 2) + 0xf800;
	for (i = 0; i < 8; i++)
	{
		if (data & (1 << i))
			missile_videoram[address + i] |= 0x20;
		else
			missile_videoram[address + i] &= 0xc0;
		 missile_blit_w(address + i);
	}
}

/********************************************************************************************/
void missile_vh_screenrefresh()
{
	int address;
	
	//if (palette_recalc() || full_refresh || screen_flipped)
	//{
	for (address = 0x1900; address <= 0xffff; address++)
		missile_blit_w(address);

	screen_flipped = 0;
	//}
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/

/********************************************************************************************/
int missile_IN0_r(int port)
{
	if (ctrld)	/* trackball */
	{
		if (!missile_flipscreen)
			return ((readinputport(5) << 4) & 0xf0) | (readinputport(4) & 0x0f);
		else
			return ((readinputport(7) << 4) & 0xf0) | (readinputport(6) & 0x0f);
	}
	else	/* buttons */
		return (readinputport(0));
}

/********************************************************************************************/
void missile_init_machine(void)
{
	h_pos = v_pos = 0;
}

/********************************************************************************************/
WRITE_HANDLER(missile_w)
{
	int pc, opcode;
	uint8_t* MEM = Machine->memory_region[CPU0];

	pc = cpu_getppc();
	opcode = MEM[pc];// &0xFF;   //cpu_readop(pc);
	//LOG_INFO("OPcode %x Prev: %x, Post: %x",MEM[pc-1], opcode, MEM[pc+1]);
	address += 0x640;

	/* 3 different ways to write to video ram - the third is caught by the core memory handler */
	if (opcode == 0x81)
	{
		/* 	STA ($00,X) */
		missile_video_w(address, data, 0);
		return;
	}
	if (address <= 0x3fff)
	{
		missile_video_mult_w(address, data, 0);
		return;
	}

	/* $4c00 - watchdog */
	if (address == 0x4c00)
	{
		watchdog_reset_w(address, data, 0);
		return;
	}

	/* $4800 - various IO */
	if (address == 0x4800)
	{
		if (missile_flipscreen != (!(data & 0x40)))
			missile_flip_screen();
		missile_flipscreen = !(data & 0x40);
		//coin_counter_w(0, data & 0x20);
		//coin_counter_w(1, data & 0x10);
		//coin_counter_w(2, data & 0x08);
		//osd_led_w(0, ~data >> 1);
		//osd_led_w(1, ~data >> 2);
		ctrld = data & 1;
		MEM[address] = data;
		return;
	}

	/* $4d00 - IRQ acknowledge */
	if (address == 0x4d00)
	{
		m_cpu_6502[CPU0]->m6502clearpendingint();
		return;
	}

	/* $4000 - $400f - Pokey */
	if (address >= 0x4000 && address <= 0x400f)
	{
		pokey1_w(address, data);
		return;
	}

	/* $4b00 - $4b07 - color RAM */
	if (address >= 0x4b00 && address <= 0x4b07)
	{
		const int pen = address - 0x4b00;

		const int r = 0xFF * ((~data >> 3) & 1);
		const int g = 0xFF * ((~data >> 2) & 1);
		const int b = 0xFF * ((~data >> 1) & 1);

		// Update the runtime palette pen (Machine->pens is identity, but use it properly)
		osd_modify_pen(Machine->pens[pen], (unsigned char)r, (unsigned char)g, (unsigned char)b);
		MEM[address] = (data & 0x0E) >> 1;
		return;
	}

	if (errorlog) fprintf(errorlog, "possible unmapped write, offset: %04x, data: %02x\n", address, data);
}

/********************************************************************************************/

READ_HANDLER(missile_r)
{
	int pc, opcode;
	uint8_t* MEM = Machine->memory_region[CPU0];

	pc = cpu_getppc();
	opcode = MEM[pc] & 0xFF; //cpu_readop(pc);

	address += 0x1900;

	if (opcode == 0x81)
	{
		/* 	LDA ($00,X)  */
		return (missile_video_r(address));
	}

	if (address >= 0x5000)
		return missile_video2ram[address - 0x5000];

	if (address == 0x4800)
		return (missile_IN0_r(0));
	if (address == 0x4900)
		return read_IN1();//(readinputport(1));
	if (address == 0x4a00)
		return (readinputport(2));

	if ((address >= 0x4000) && (address <= 0x400f))
		return (pokey1_r(address & 0x0f));

	LOG_ERROR("possible unmapped read, offset: %04x\n", address);
	return MEM[address];
}

// Video Section End ----------------------------

void missile_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

MEM_READ(missile_readmem)
MEM_ADDR(0x0000, 0x18ff, MRA_RAM)
MEM_ADDR(0x1900, 0xfff9, missile_r)     /* shared region */
MEM_ADDR(0xfffa, 0xffff, MRA_ROM)
MEM_END

MEM_WRITE(missile_writemem)
MEM_ADDR(0x0000, 0x03ff, MWA_RAM)
MEM_ADDR(0x0400, 0x05ff, missile_video_3rd_bit_w)
MEM_ADDR(0x0600, 0x063f, MWA_RAM)
MEM_ADDR(0x0640, 0x4fff, missile_w)     /* shared region */
MEM_ADDR(0x5000, 0xffff, missile_video2_w) // , &missile_video2ram
MEM_END

void run_missile()
{
	LOG_ERROR("RUN MISSILE");
	watchdog_reset_w(0, 0, 0);
	pokey_sh_update();
	missile_vh_screenrefresh();
}

void end_missile()
{
	LOG_DEBUG("missile VH STOP CALLED)");
	generic_vh_stop();
	missile_stop_irq_timers();
}

int init_missile()
{
	LOG_INFO("missile init called");

	//init6502(missile_readmem, missile_writemem, 0xffff, CPU0);
	videoram_size = 0x10000;
	missile_video2ram = &Machine->memory_region[0][0x5000];
	//videoram = (unsigned char*)calloc(0x400, 1);

	//missile_video2ram = (unsigned char*)calloc(0xB000, 1);
	pokey_sh_start(&missile_pokey_interface);
	missile_vh_start();
	//generic_vh_start();
	missile_start_vblank_timers();
	missile_start_irq_timers();
	return 0;
}

INPUT_PORTS_START(missile)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON3)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x18, 0x00, IPT_UNUSED) /* trackball input, handled in machine/missile.c */
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_TILT)
PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_VBLANK)

PORT_START("IN2")	/* IN2 */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_2C))
PORT_DIPSETTING(0x02, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, "Center Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0x60, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x20, "French")
PORT_DIPSETTING(0x40, "German")
PORT_DIPSETTING(0x60, "Spanish")
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("IN3")	/* IN3 */
PORT_DIPNAME(0x03, 0x00, "Cities")
PORT_DIPSETTING(0x02, "4")
PORT_DIPSETTING(0x01, "5")
PORT_DIPSETTING(0x03, "6")
PORT_DIPSETTING(0x00, "7")
PORT_DIPNAME(0x04, 0x04, "Bonus Credit for 4 Coins")
PORT_DIPSETTING(0x04, DEF_STR(No))
PORT_DIPSETTING(0x00, DEF_STR(Yes))
PORT_DIPNAME(0x08, 0x00, "Trackball Size")
PORT_DIPSETTING(0x00, "Large")
PORT_DIPSETTING(0x08, "Mini")
PORT_DIPNAME(0x70, 0x70, "Bonus City")
PORT_DIPSETTING(0x10, "8000")
PORT_DIPSETTING(0x70, "10000")
PORT_DIPSETTING(0x60, "12000")
PORT_DIPSETTING(0x50, "14000")
PORT_DIPSETTING(0x40, "15000")
PORT_DIPSETTING(0x30, "18000")
PORT_DIPSETTING(0x20, "20000")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x80, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x80, DEF_STR(Cocktail))

PORT_START("TRACK0_X")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_X, 20, 10, 0, 0, 0)

PORT_START("TRACK0_Y")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_Y | IPF_REVERSE, 20, 10, 0, 0, 0)

PORT_START("TRACK1_X")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_X | IPF_REVERSE | IPF_COCKTAIL, 20, 10, 0, 0, 0)

PORT_START("TRACK1_Y")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_Y | IPF_REVERSE | IPF_COCKTAIL, 20, 10, 0, 0, 0)
INPUT_PORTS_END

ROM_START(missile)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("035820-02.h1", 0x5000, 0x0800, CRC(7a62ce6a) SHA1(9a39978138dc28fdefe193bfae1b226391e471db))
ROM_LOAD("035821-02.jk1", 0x5800, 0x0800, CRC(df3bd57f) SHA1(0916925d3c94d766d33f0e4badf6b0add835d748))
ROM_LOAD("035822-03e.kl1", 0x6000, 0x0800, CRC(1a2f599a) SHA1(2deb1219223032a9c83114e4e8b2fc11a570754c))
ROM_LOAD("035823-02.ln1", 0x6800, 0x0800, CRC(82e552bb) SHA1(d0f22894f779c74ceef644c9f03d840d9545efea))
ROM_LOAD("035824-02.np1", 0x7000, 0x0800, CRC(606e42e0) SHA1(9718f84a73c66b4e8ef7805a7ab638a7380624e1))
ROM_LOAD("035825-02.r1", 0x7800, 0x0800, CRC(f752eaeb) SHA1(0339a6ce6744d2091cc7e07675e509b202b0f380))
ROM_RELOAD(0xF800, 0x0800)

ROM_REGION(0x0020, REGION_PROMS, 0)
ROM_LOAD("035826-01.l6", 0x0000, 0x0020, CRC(86a22140) SHA1(2beebf7855e29849ada1823eae031fc98220bc43))
ROM_END

// Missile Command
AAE_DRIVER_BEGIN(drv_missile, "missile", "Missile Command (BROKEN)")
AAE_DRIVER_ROM(rom_missile)
AAE_DRIVER_FUNCS(&init_missile, &run_missile, &end_missile)
AAE_DRIVER_INPUT(input_ports_missile)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1250000,
		/*div*/      400,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &missile_interrupt,
		/*r8*/       missile_readmem,
		/*w8*/       missile_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_FLIP_Y)
//AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 223)
AAE_DRIVER_SCREEN(256, 231, 0, 255, 0, 230)//256, 256, 0, 255, 0, 255)
AAE_DRIVER_RASTER(missile_gfxdecodeinfo, 8, 32 * 2, missile_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_missile)