/***************************************************************************

	  Modified from original schematics...

	  MISSILE COMMAND
	  ---------------
	  HEX		 R/W   D7 D6 D5 D4 D3 D2 D2 D0	function
	  ---------+-----+------------------------+------------------------
	  0000-01FF  R/W   D  D  D	D  D  D  D	D	512 bytes working ram

	  0200-05FF  R/W   D  D  D	D  D  D  D	D	3rd color bit region
												of screen ram.
												Each bit of every odd byte is the low color
												bit for the bottom scanlines
												The schematics say that its for the bottom
												32 scanlines, although the code only accesses
												$401-$5FF for the bottom 8 scanlines...
												Pretty wild, huh?

	  0600-063F  R/W   D  D  D	D  D  D  D	D	More working ram.

	  0640-3FFF  R/W   D  D  D	D  D  D  D	D	2-color bit region of
												screen ram.
												Writes to 4 bytes each to effectively
												address $1900-$ffff.

	  1900-FFFF  R/W   D  D 					2-color bit region of
												screen ram
													  Only accessed with
													   LDA ($ZZ,X) and
													   STA ($ZZ,X)
													  Those instructions take longer
													  than 5 cycles.

	  ---------+-----+------------------------+------------------------
	  4000-400F  R/W   D  D  D	D  D  D  D	D	POKEY ports.
	  -----------------------------------------------------------------
	  4008		 R	   D  D  D	D  D  D  D	D	Game Option switches
	  -----------------------------------------------------------------
	  4800		 R	   D						Right coin
	  4800		 R		  D 					Center coin
	  4800		 R			 D					Left coin
	  4800		 R				D				1 player start
	  4800		 R				   D			2 player start
	  4800		 R					  D 		2nd player left fire(cocktail)
	  4800		 R						 D		2nd player center fire	"
	  4800		 R							D	2nd player right fire	"
	  ---------+-----+------------------------+------------------------
	  4800		 R				   D  D  D	D	Horiz trackball displacement
															if ctrld=high.
	  4800		 R	   D  D  D	D				Vert trackball displacement
															if ctrld=high.
	  ---------+-----+------------------------+------------------------
	  4800		 W	   D						Unused ??
	  4800		 W		  D 					screen flip
	  4800		 W			 D					left coin counter
	  4800		 W				D				center coin counter
	  4800		 W				   D			right coin counter
	  4800		 W					  D 		2 player start LED.
	  4800		 W						 D		1 player start LED.
	  4800		 W							D	CTRLD, 0=read switches,
															1= read trackball.
	  ---------+-----+------------------------+------------------------
	  4900		 R	   D						VBLANK read
	  4900		 R		  D 					Self test switch input.
	  4900		 R			 D					SLAM switch input.
	  4900		 R				D				Horiz trackball direction input.
	  4900		 R				   D			Vert trackball direction input.
	  4900		 R					  D 		1st player left fire.
	  4900		 R						 D		1st player center fire.
	  4900		 R							D	1st player right fire.
	  ---------+-----+------------------------+------------------------
	  4A00		 R	   D  D  D	D  D  D  D	D	Pricing Option switches.
	  4B00-4B07  W				   D  D  D	D	Color RAM.
	  4C00		 W								Watchdog.
	  4D00		 W								Interrupt acknowledge.
	  ---------+-----+------------------------+------------------------
	  5000-7FFF  R	   D  D  D	D  D  D  D	D	Program.
	  ---------+-----+------------------------+------------------------

MISSILE COMMAND SWITCH SETTINGS (Atari, 1980)
---------------------------------------------

GAME OPTIONS:
(8-position switch at R8)

1	2	3	4	5	6	7	8	Meaning
-------------------------------------------------------------------------
Off Off 						Game starts with 7 cities
On	On							Game starts with 6 cities
On	Off 						Game starts with 5 cities
Off On							Game starts with 4 cities
		On						No bonus credit
		Off 					1 bonus credit for 4 successive coins
			On					Large trak-ball input
			Off 				Mini Trak-ball input
				On	Off Off 	Bonus city every  8000 pts
				On	On	On		Bonus city every 10000 pts
				Off On	On		Bonus city every 12000 pts
				On	Off On		Bonus city every 14000 pts
				Off Off On		Bonus city every 15000 pts
				On	On	Off 	Bonus city every 18000 pts
				Off On	Off 	Bonus city every 20000 pts
				Off Off Off 	No bonus cities
							On	Upright
							Off Cocktail

PRICING OPTIONS:
(8-position switch at R10)

1	2	3	4	5	6	7	8	Meaning
-------------------------------------------------------------------------
On	On							1 coin 1 play
Off On							Free play
On Off							2 coins 1 play
Off Off 						1 coin 2 plays
		On	On					Right coin mech * 1
		Off On					Right coin mech * 4
		On	Off 				Right coin mech * 5
		Off Off 				Right coin mech * 6
				On				Center coin mech * 1
				Off 			Center coin mech * 2
					On	On		English
					Off On		French
					On	Off 	German
					Off Off 	Spanish
							On	( Unused )
							Off ( Unused )

******************************************************************************************/

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

static struct POKEYinterface pokey_interface =
{
	1,	/* 1 chip */
	1250000,	/* 1.25 MHz??? */
	{ 240 },
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

void missile_vh_screenrefresh();
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

// Timing Code
//
// Missile Command hardware VBLANK timing (from schematics):
//   VTOTAL = 256 lines, VBLANK = lines 0-23 (first 24 lines)
//   Active display = lines 24-255
//   IRQs fire at V=0, 64, 128, 192 (4 per frame)
//
// VBLANK is at the START of the frame, not the end. The cpu_control
// system puts it at the end which is wrong for this game. Instead we
// calculate it based on elapsed CPU cycles within the current frame.
//
// The first 24/256 of the frame = first 9.375% of cycles = VBLANK HIGH
// The remaining 232/256 of the frame = VBLANK LOW (active display)

// How many 256ths of the frame are VBLANK (lines 0-23)
static constexpr int MISSILE_VTOTAL = 256;
static constexpr int MISSILE_VBLANK_LINES = 24;

static inline uint8_t read_IN1()
{
	// Read base port value but mask off the VBLANK bit (bit 7).
	// We manage VBLANK ourselves based on hardware scanline timing.
	uint8_t data = (uint8_t)(readinputport(1) & 0x7F);

	// Calculate how far through the frame we are using elapsed CPU0 cycles.
	// get_elapsed_ticks(0) returns cycles run so far this frame for CPU0.
	int elapsed = get_elapsed_ticks(0);
	int cpf = Machine->gamedrv->cpu[0].cpu_freq / Machine->gamedrv->fps;

	// VBLANK is HIGH for the first (VBLANK_LINES / VTOTAL) of the frame
	int vblank_end_cycle = (cpf * MISSILE_VBLANK_LINES) / MISSILE_VTOTAL;

	// IP_ACTIVE_HIGH: bit 7 = 1 during VBLANK
	if (elapsed < vblank_end_cycle)
		data |= 0x80;

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
	//LOG_INFO("Returning for Video_ram_r %x", missile_videoram[address] & 0xe0);
	return (missile_videoram[address] & 0xe0);
}

/********************************************************************************************/
/* This routine is called when the flipscreen bit changes. It forces a redraw of the entire bitmap. */
void missile_flip_screen(void)
{
	screen_flipped = 1;
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
	//palette_recalc();
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

void missile_clear_bottom_color6(void)
{
	/*
	  The bottom 32 visible lines map to video RAM rows 0xE0-0xFF.
	  Each row is 256 bytes (one per pixel column).
	  Address range: 0xE000 to 0xFFFF inclusive.
	*/
	const int start_addr = 0xE000;
	const int end_addr = 0xFFFF;

	for (int address = start_addr; address <= end_addr; ++address)
	{
		if (missile_videoram[address] & 0x20)
		{
			/* Clear the 3rd color bit (blue LSB) */
			missile_videoram[address] &= ~0x20;

			/* Re-plot so the scrbitmap matches the now-cleared RAM */
			missile_blit_w(address);
		}
	}
}

WRITE_HANDLER(missile_w)
{
	int pc, opcode;
	unsigned char* MEM = memory_region(REGION_CPU1);

	pc = cpu_getppc();
	opcode = MEM[pc];

	address += 0x640;

	/* 3 different ways to write to video ram - the third is caught by the core memory handler */
	if (opcode == 0x81)
	{
		/* STA ($00,X) -- write to video RAM */
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

	LOG_DEBUG("possible unmapped write, offset: %04x, data: %02x\n", address, data);
}

/********************************************************************************************/

READ_HANDLER(missile_r)
{
	int pc, opcode;
	uint8_t* MEM = memory_region(REGION_CPU1);

	pc = cpu_getppc();
	opcode = MEM[pc];

	address += 0x1900;

	if (opcode == 0xa1)
	{
		/* LDA ($00,X) -- return video RAM data (top 3 bits) */
		return (missile_video_r(address));
	}

	if (address >= 0x5000)
		return missile_video2ram[address - 0x5000];

	if (address == 0x4800)
		return (missile_IN0_r(0));
	if (address == 0x4900)
		return read_IN1();
	if (address == 0x4a00)
		return (readinputport(2));

	if ((address >= 0x4000) && (address <= 0x400f))
		return (pokey1_r(address & 0x0f));

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
	watchdog_reset_w(0, 0, 0);
	pokey_sh_update();
}

void end_missile()
{
	LOG_DEBUG("missile VH STOP CALLED)");
	generic_vh_stop();
}

int init_missile()
{
	LOG_INFO("missile init called");

	videoram_size = 0x10000;
	missile_video2ram = &Machine->memory_region[0][0x5000];

	pokey_sh_start(&pokey_interface);
	missile_vh_start();

	// VBLANK is handled by the cpu_control / inptport system automatically.
	// No custom timer setup needed.
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
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_X, 20, 10, 0, 0)

PORT_START("TRACK0_Y")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_Y | IPF_REVERSE, 20, 10, 0, 0)

PORT_START("TRACK1_X")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_X | IPF_REVERSE | IPF_COCKTAIL, 20, 10, 0, 0)

PORT_START("TRACK1_Y")	/* FAKE */
PORT_ANALOG(0x0f, 0x00, IPT_TRACKBALL_Y | IPF_REVERSE | IPF_COCKTAIL, 20, 10, 0, 0)
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
AAE_DRIVER_BEGIN(drv_missile, "missile", "Missile Command")
AAE_DRIVER_ROM(rom_missile)
AAE_DRIVER_FUNCS(&init_missile, &run_missile, &end_missile)
AAE_DRIVER_INPUT(input_ports_missile)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1000000,
		/*div*/      100,
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

AAE_DRIVER_VIDEO_CORE(60, 2500, VIDEO_TYPE_RASTER_COLOR | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_DEFAULT)
//AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 223)
AAE_DRIVER_SCREEN(256, 231, 0, 255, 0, 230)
AAE_DRIVER_RASTER(missile_gfxdecodeinfo, 8, 32 * 2, missile_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_missile)