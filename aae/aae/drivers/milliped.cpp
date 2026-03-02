/****************************************************************************

Millipede driver by Ivan Mackintosh

Memory map for Millipede from the Atari schematics(SP - 217 1982)
----------------------------------------------------------------

Address  R / W  D7 D6 D5 D4 D3 D2 D1 D0   Function
--------------------------------------------------------------------------------------
0000 - 03FF       D  D  D  D  D  D  D  D   RAM
--------------------------------------------------------------------------------------
0400 - 0410  R    D  D  D  D  D  D  D  D   I / OSO(0400 - 040F POKEY 1)
0408       R    D  D  D  D  D  D  D  D   Option Switch 0 (0 = On) (Bank at
	--------------------------------------------------------------------------------------
	0800 - 0810  R    D  D  D  D  D  D  D  D   I / OS1(0800 - 080F POKEY 2)
	0808       R    D  D  D  D  D  D  D  D   Option Switch 1 (0 = On) (Bank at
		--------------------------------------------------------------------------------------
		1000 - 13BF       D  D  D  D  D  D  D  D   Playfield RAM(8x8 TILES, 32x30 SCREEN)
		13C0 - 13CF       D  D  D  D  D  D  D  D   Motion Object Picture
		13D0 - 13DF       D  D  D  D  D  D  D  D   Motion Object Vert.
		13E0 - 13EF       D  D  D  D  D  D  D  D   Motion Object Horiz.
		13F0 - 13FF             D  D  D  D  D  D   Motion Object Color
		--------------------------------------------------------------------------------------
		2000       R    D                        Horizontal Mini - Track Ball HORIZ DIR
		R       D                     VBLANK(1 = VBlank)
		R          D                  Player 1 Start
		R             D               Player 1 Fire
		R                D  D  D  D   Horizontal Mini - Track Ball HORIZ COUNT
		R                D  D  D  D   Options Switch 2 (Bottom 4 switches at P8)
		--------------------------------------------------------------------------------------
		2001       R    D                        Horizontal Mini - Track Ball VERT DIR
		R          D                  Player 2 Start
		R             D               Player 2 Fire
		R                D  D  D  D   Horizontal Mini - Track Ball VERT COUNT
		R                D  D  D  D   Options Switch 2 (Top 4 switches at P8)
		--------------------------------------------------------------------------------------
		2010       R    D  D  D                  R, C, L Coin Switches(0 = On)
		R             D               SLAM(0 = On)
		R                D  D  D  D   P1 Joystick Positions(0 = On)
		--------------------------------------------------------------------------------------
		2011       R    D                        Self - Test Switch(0 = On)
		R          D                  Cabinet Select(1 = Upright, 0 = Cocktail)
		R                D  D  D  D   P2 Joystick Positions(Undocumented)
		--------------------------------------------------------------------------------------
		2030       R    D  D  D  D  D  D  D  D   EA ROM Read Data
		--------------------------------------------------------------------------------------
		2480 - 248F  W    D  D  D  D  D  D  D  D   STAMP COLOR RAM
		2490 - 249F  W    D  D  D  D  D  D  D  D   MOTION OBJECT COLOR RAM
		--------------------------------------------------------------------------------------
		2501       W    D                        COIN CNTR L
		2502       W    D                        COIN CNTR R
		2503       W    D                        START LED 1
		2504       W    D                        START LED 2
		2505       W    D                        TRACKBALL ENABLE(TBEN)
		2506       W    D                        VIDEO ROTATE(VIDROT)
		2507       W    D                        CONTROL SELECT(CNTRLSEL) - (P1 = 1, P2 = 0 ? )
		--------------------------------------------------------------------------------------
		2600       W                             !IRQRES - IRQ Acknowledge
		2680       W                             !WATCHDOG - CLEAR WATCHDOG
		2700       W                D  D  D  D   !EAROMCON - earom control
		2780       W    D  D  D  D  D  D  D  D   !EAROMWR - earom write
		--------------------------------------------------------------------------------------
		3000 - 3FFF  R    D  D  D  D  D  D  D  D   ROM(NOT USED) (Schems listed 300 - 3fff typo)
		4000 - 7FFF  R    D  D  D  D  D  D  D  D   ROM(Schems listed 400 - 4fff typo)
		--------------------------------------------------------------------------------------

		Switch Settings for Millipede from the Atari schematics(TM - 217 1982)
		-------------------------------------------------------------------- -

		Switch Settings for Price Options / 8 - Toggle Switches on Millipede PCB(at B5)
		--------------------------------------------------------------------------------------
		8    7    6    5    4    3    2    1    Option
		--------------------------------------------------------------------------------------
		On   On                       Off  Off   Demonstration Mode
		On   Off  On                             For every 3 coins inserted, game logic
		adds 1 more coin
		On   Off  Off                            For every 5 coins inserted, game logic
		adds 1 more coin
		Off  On   On                             For every 4 coins inserted, game logic
		adds 2 more coin
		Off  On   Off                            For every 4 coins inserted, game logic
		adds 1 more coin
		Off  Off  On                             For every 2 coins inserted, game logic
		adds 1 more coin
		Off  Off  Off                            No Bonus Coins $
		--------------------------------------------------------------------------------------
		Off                       Left coin mech X 1 $
		On                        Left coin mech X 2
		--------------------------------------------------------------------------------------
		Off  Off             Right coin mech X 1 $
		Off  On              Right coin mech X 4
		On   Off             Right coin mech X 5
		On   On              Right coin mech X 6
		--------------------------------------------------------------------------------------
		On   On    2 coins for 1 credit
		On   Off   1 coin for 1 credit $
		Off  On    1 coin for 2 credits
		Off  Off   Free play
		--------------------------------------------------------------------------------------
		$ = Manufacturer's suggested settings

		Switch Settings for Play Options / 8 - Toggle Switches on Millipede PCB(at D5)
		--------------------------------------------------------------------------------------

		8    7    6    5    4    3    2    1    Option
		--------------------------------------------------------------------------------------
		Off                                      Select Mode $
		On                                       No Select Mode
		Off                                 Easy spider $
		On                                  Hard spider
		--------------------------------------------------------------------------------------
		Off  Off                       Bonus life every 12, 000 points
		Off  On                        Bonus life every 15, 000 points $
		On   Off                       Bonus life every 20, 000 points
		On   On                        No bonus life
		--------------------------------------------------------------------------------------
		Off  Off             2 lives per game
		Off  On              3 lives per game $
		On   Off             4 lives per game
		On   On              5 lives per game
		--------------------------------------------------------------------------------------
		Off        Easy beetle $
		On         Hard beetle
		--------------------------------------------------------------------------------------
		Off   Easy millipede head $
		On    Hard millipede head
		--------------------------------------------------------------------------------------
		$ = Manufacturer's suggested settings

		Switch settings for Special Options / 8 - Toggle Switches on Millipede PCB(at P8)
		--------------------------------------------------------------------------------------

		8    7    6    5    4    3    2    1    Option
		--------------------------------------------------------------------------------------
		On                                       1 coin counter
		Off                                      2 coin counters
		--------------------------------------------------------------------------------------
		On                                  1 credit minimum $
		Off                                 2 credit minimum
		--------------------------------------------------------------------------------------
		(Switches 5 and 6 unused)           Select Mode Starting Score
		On   On              0 points
		On   Off             0 and bonus life level
		Off  On              0, bonus life level, and 2x bonus life level $
		Off  Off             0, bonus life level, and 2x bonus life level,
		and 3x bonus life level
		--------------------------------------------------------------------------------------
		On   On    English $
		On   Off   German
		Off  On    French
		Off  Off   Spanish
		--------------------------------------------------------------------------------------
		$ = Manufacturer's suggested settings

		Notes: 15 Feb 2007 - MSH
		* Real Millipede boards do not have the Joystick circuit hooked up
		to the card edge.
		* The Joytick and T - Ball inputs are both swapped through LS157s by
		the CNTRLSEL signal at 0x2507.
		* How do we hookup TBEN signal at 0x2505 ?

		Changes : 15 Feb 2007 - MSH
		* Added corrected memory map and dip switch settings from Atari Manuals.
		* Added Cocktail mode(based on v0.36.1 driver from Scott Brasington)
		* Removed unused dip toggles, now set to IPT_UNKNOWN
		* Hooked up 2nd trackball
		* Map P2 Joy inputs to work correctly. (They don't work on a real board)

 ***************************************************************************/

#include "aae_mame_driver.h"
#include "warlord.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "earom.h"
#include "aae_pokey.h"
#include "timer.h"

 // ---------------------------------------------------------------------------
 // VBLANK latch: Millipede IN0 bit 6 is ACTIVE HIGH for VBLANK (per port def).
 // The game main loop polls this bit waiting for it to go HIGH before executing
 // per-frame logic (including sound table updates).  Getting this wrong means
 // the game never advances its sound state, producing silence or stuck tones.
 //
 // g_in0_vblank_bit holds the current state of bit 6 to merge into IN0 reads.
 // Call milliped_vblank_begin() when vblank starts  -> bit 6 HIGH (0x40)
 // Call milliped_vblank_end()   when vblank ends    -> bit 6 LOW  (0x00)
 // ---------------------------------------------------------------------------
static uint8_t g_in0_vblank_bit = 0x00;  /* starts outside vblank, bit 6 low */

static inline void milliped_vblank_begin() { g_in0_vblank_bit = 0x00; } /* bit 6 HIGH = in vblank (ACTIVE_HIGH) */
static inline void milliped_vblank_end() { g_in0_vblank_bit = 0x40; } /* bit 6 LOW  = not in vblank          */

#pragma warning( disable : 4838 4003 )

static int dsw_select;

unsigned char* milliped_paletteram;
int full_refresh = 0;

static struct rectangle spritevisiblearea =
{
	1 * 8, 31 * 8 - 1,
	0 * 8, 30 * 8 - 1
};

static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 256 * 8 * 8, 0 },	/* the two bitplanes are separated */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8	/* every char takes 8 consecutive bytes */
};
static struct GfxLayout spritelayout =
{
	8,16,	/* 16*8 sprites */
	128,	/* 64 sprites */
	2,	/* 2 bits per pixel */
	{ 128 * 16 * 8, 0 },	/* the two bitplanes are separated */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
	16 * 8	/* every sprite takes 16 consecutive bytes */
};

struct GfxDecodeInfo milliped_gfxdecodeinfo[] =
{
{ REGION_GFX1, 0, &charlayout,     0, 4 },	/* 4 color codes to support midframe */
/* palette changes in test mode */
{ REGION_GFX1, 0, &spritelayout, 4 * 4, 4 * 4 * 4 },
{ -1 } /* end of array */
};

static struct POKEYinterface milliped_pokey_interface =
{
	2,			/* 2 chips */
	1512000,
	240,	/* volume */
	8,
	NO_CLIP,
	/* The 8 pot handlers */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	/* allpot handler - Millipede reads DIP switches via POKEY ALLPOT register */
	{ input_port_4_r, input_port_5_r },
};

WRITE_HANDLER(milliped_irq_ack)
{
	/* Writing to 0x2600 acknowledges the 6502 IRQ from the VBLANK timer */
	m_cpu_6502[0]->m6502clearpendingint();
}

/* ---------------------------------------------------------------------------
   milliped_interrupt - fires at 60 Hz from the timer set in init_milliped.
   Latches the VBLANK bit HIGH so the game main loop can detect vblank start,
   then asserts the 6502 IRQ.  The game acks the IRQ by writing to 0x2600.
   run_milliped() clears the VBLANK latch at the end of each frame.
   --------------------------------------------------------------------------- */
void milliped_interrupt(int dummy)
{
	milliped_vblank_begin();              /* latch bit 6 HIGH for the game loop */
	cpu_do_int_imm(CPU0, INT_TYPE_INT);   /* assert the 6502 IRQ               */
}

WRITE_HANDLER(milliped_input_select_w)
{
	/* 0x2505 selects whether reads from IN0/IN1 return DIP switches or trackball */
	dsw_select = (data == 0);
}

/* ---------------------------------------------------------------------------
   milliped_IN0_r - reads 0x2000.
   When dsw_select is set, returns DIP switch data from port 0.
   Otherwise returns trackball delta in bits 0-3, with button/start in bits
   4-5 and the VBLANK latch in bit 6.

   NOTE: Do NOT add LOG_INFO here.  This handler is polled constantly by the
   game (thousands of times per second for the VBLANK-wait loop alone).
   Any per-read logging will destroy audio timing and flood the log file.
   --------------------------------------------------------------------------- */
READ_HANDLER(milliped_IN0_r)
{
	static int oldpos, sign;
	int newpos;

	if (dsw_select)
		return (readinputport(0) | sign);

	newpos = readinputport(6);
	if (newpos != oldpos)
	{
		sign = (newpos - oldpos) & 0x80;
		oldpos = newpos;
	}

	/* bits 4-6 from port 0 (fire, start1, and the VBLANK latch override).
	   bits 0-3 are the trackball delta counter.
	   bit  7 is the trackball sign computed above. */
	return ((readinputport(0) & 0x70) | (oldpos & 0x0f) | g_in0_vblank_bit | sign);
}

READ_HANDLER(milliped_IN1_r)
{
	static int oldpos, sign;
	int newpos;

	if (dsw_select)
		return (readinputport(1) | sign);

	newpos = readinputport(7);
	if (newpos != oldpos)
	{
		sign = (newpos - oldpos) & 0x80;
		oldpos = newpos;
	}

	return ((readinputport(1) & 0x70) | (oldpos & 0x0f) | sign);
}

/***************************************************************************

	Millipede doesn't have a color PROM, it uses RAM.
	The RAM seems to be conncted to the video output this way:

	bit 7 red
		  red
		  red
		  green
		  green
		  blue
		  blue
	bit 0 blue

***************************************************************************/

WRITE_HANDLER(milliped_paletteram_w)
{
	int bit0, bit1, bit2;
	int r, g, b;

	milliped_paletteram[address] = data;

	/* red component */
	bit0 = (~data >> 5) & 0x01;
	bit1 = (~data >> 6) & 0x01;
	bit2 = (~data >> 7) & 0x01;
	r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

	/* green component */
	bit0 = 0;
	bit1 = (~data >> 3) & 0x01;
	bit2 = (~data >> 4) & 0x01;
	g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

	/* blue component */
	bit0 = (~data >> 0) & 0x01;
	bit1 = (~data >> 1) & 0x01;
	bit2 = (~data >> 2) & 0x01;
	b = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

	palette_change_color(address, r, g, b);
}

void milliped_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

	/* the palette will be initialized by the game. We just set it to some */
	/* pre-cooked values so the startup copyright notice can be displayed. */
	for (i = 0; i < Machine->drv->total_colors; i++)
	{
		*(palette++) = ((i & 1) >> 0) * 0xff;
		*(palette++) = ((i & 2) >> 1) * 0xff;
		*(palette++) = ((i & 4) >> 2) * 0xff;
	}

	/* initialize the color table */
	for (i = 0; i < Machine->drv->color_table_len; i++)
		colortable[i] = i;
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.

  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void milliped_vh_screenrefresh()
{
	int offs;
	full_refresh = 0;

	const unsigned char* just_remapped = palette_recalc();
	if (just_remapped) {
		full_refresh = 1;  /* force a full redraw on palette change */
	}

	memset(dirtybuffer, 1, videoram_size);

	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int sx, sy;
		int bank;
		int color;

		dirtybuffer[offs] = 0;

		sx = offs % 32;
		sy = offs / 32;

		if (videoram[offs] & 0x40)
			bank = 1;
		else bank = 0;

		if (videoram[offs] & 0x80)
			color = 2;
		else color = 0;

		drawgfx(main_bitmap, Machine->gfx[0],
			0x40 + (videoram[offs] & 0x3f) + 0x80 * bank,
			bank + color,
			0, 0,
			8 * sx, 8 * sy,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	/* Draw the sprites */
	for (offs = 0; offs < 0x10; offs++)
	{
		int spritenum, color;
		int x, y;
		int sx, sy;

		x = spriteram[offs + 0x20];
		y = 240 - spriteram[offs + 0x10];

		spritenum = spriteram[offs] & 0x3f;
		if (spritenum & 1) spritenum = spritenum / 2 + 64;
		else spritenum = spritenum / 2;

		/* Millipede is unusual because the sprite color code specifies the */
		/* colors to use one by one, instead of a combination code. */
		/* bit 7-6 = palette bank (there are 4 groups of 4 colors) */
		/* bit 5-4 = color to use for pen 11 */
		/* bit 3-2 = color to use for pen 10 */
		/* bit 1-0 = color to use for pen 01 */
		/* pen 00 is transparent */
		color = spriteram[offs + 0x30];
		Machine->gfx[1]->colortable[3] =
			Machine->pens[16 + ((color >> 4) & 3) + 4 * ((color >> 6) & 3)];
		Machine->gfx[1]->colortable[2] =
			Machine->pens[16 + ((color >> 2) & 3) + 4 * ((color >> 6) & 3)];
		Machine->gfx[1]->colortable[1] =
			Machine->pens[16 + ((color >> 0) & 3) + 4 * ((color >> 6) & 3)];

		drawgfx(main_bitmap, Machine->gfx[1],
			spritenum,
			0,
			0, spriteram[offs] & 0x80,
			x, y,
			&spritevisiblearea, TRANSPARENCY_PEN, 0);

		/* mark tiles underneath as dirty */
		sx = x >> 3;
		sy = y >> 3;

		{
			int max_x = 1;
			int max_y = 2;
			int x2, y2;

			if (x & 0x07) max_x++;
			if (y & 0x0f) max_y++;

			for (y2 = sy; y2 < sy + max_y; y2++)
			{
				for (x2 = sx; x2 < sx + max_x; x2++)
				{
					if ((x2 < 32) && (y2 < 32) && (x2 >= 0) && (y2 >= 0))
						dirtybuffer[x2 + 32 * y2] = 1;
				}
			}
		}
	}
}

int milliped_vh_start(void)
{
	LOG_INFO("------- VIDEO RAM SIZE SET HERE --------------");
	/* For raster games, videoram and spriteram pointers must be set manually */
	videoram = &Machine->memory_region[0][0x1000];
	videoram_size = 0x400;

	spriteram = &Machine->memory_region[0][0x13c0];
	spriteram_size = 0x40;

	milliped_paletteram = &Machine->memory_region[0][0x2480];

	return generic_vh_start();
}

//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
// MAIN HANDLERS
//////////////////////////////////////////////////////////////

/* ---------------------------------------------------------------------------
   run_milliped - called once per frame by the main emulation loop.
   Clears the VBLANK latch (bit 6 goes LOW) so the game loop can detect the
   end of vblank on the next frame.  Then refreshes the screen and flushes
   the POKEY audio buffer for this frame.
   --------------------------------------------------------------------------- */
void run_milliped()
{
	//if (cpu_getcycles(CPU0) > 2500)
	//{
	milliped_vblank_end();          /* clear bit 6 - frame work period begins  */
	//m_cpu_6502[0]->m6502clearpendingint();
//}
//watchdog_reset_w(0, 0, 0);
	milliped_vh_screenrefresh();
	pokey_sh_update();              /* flush the POKEY buffer to the audio stream */
}

void end_milliped()
{
	generic_vh_stop();
	pokey_sh_stop();
}

WRITE_HANDLER(milliped_led_w)
{
	set_led_status(address, ~data >> 7);
}

/* called on reset */
void milliped_init_machine(void)
{
}

/* PORT HANDLERS */

MEM_READ(milliped_readmem)
//{0x0000, 0x03ff, MRA_RAM},
{0x0400, 0x040f, pokey_1_r},
{ 0x0800, 0x080f, pokey_2_r },
//{ 0x1000, 0x13ff, MRA_RAM },
{ 0x2000, 0x2000, milliped_IN0_r },
{ 0x2001, 0x2001, milliped_IN1_r },
{ 0x2010, 0x2010, ip_port_2_r },
{ 0x2011, 0x2011, ip_port_3_r },
{ 0x2030, 0x2030, EaromRead },
//{ 0x4000, 0x7fff, MRA_ROM },
//{ 0xf000, 0xffff, MRA_ROM },		/* for the reset / interrupt vectors */
MEM_END

MEM_WRITE(milliped_writemem)
//{0x0000, 0x03ff, MWA_RAM },
{0x0400, 0x040f, pokey_1_w},
{ 0x0800, 0x080f, pokey_2_w },
//{ 0x1000, 0x13ff, videoram_w, &videoram, &videoram_size },
//{ 0x13c0, 0x13ff, MWA_RAM, &spriteram },
{ 0x2480, 0x249f, milliped_paletteram_w },
//{ 0x2500, 0x2502, coin_counter_w },
{ 0x2503, 0x2504, milliped_led_w },
{ 0x2505, 0x2505, milliped_input_select_w },
{ 0x2506, 0x2507, MWA_NOP }, /* unused outputs */
{ 0x2600, 0x2600, milliped_irq_ack }, /* IRQ ack - game writes here after servicing the interrupt */
{ 0x2680, 0x2680, watchdog_reset_w },
{ 0x2700, 0x2700, EaromCtrl },
{ 0x2780, 0x27bf, EaromWrite },
{ 0x4000, 0x73ff, MWA_ROM },
MEM_END

int  init_milliped(void)
{
	init6502(milliped_readmem, milliped_writemem, 0x8000, CPU0);
	/* Ensure NTSC-like 262 scanlines per frame for 60 Hz scheduling */
	aae_set_lines_per_frame(262);
	pokey_sh_start(&milliped_pokey_interface);
	milliped_vh_start();

	/* Fire an IRQ at 60 Hz for VBLANK / game heartbeat.
	   The game acks the interrupt by writing to 0x2600 (milliped_irq_ack).
	   milliped_interrupt() also latches the VBLANK bit in IN0 so the game
	   main loop can synchronize with the display. */
	timer_set(TIME_IN_HZ(60), CPU0, milliped_interrupt);

	return 0;
}

INPUT_PORTS_START(milliped)
PORT_START("IN0")	/* IN0 $2000 */	/* see port 6 for x trackball */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "Spanish")
PORT_DIPNAME(0x0c, 0x04, "Bonus")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x04, "0 1x")
PORT_DIPSETTING(0x08, "0 1x 2x")
PORT_DIPSETTING(0x0c, "0 1x 2x 3x")
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_VBLANK)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* trackball sign bit */

PORT_START("IN1")	/* IN1 $2001 */	/* see port 7 for y trackball */
PORT_DIPNAME(0x01, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))
PORT_DIPNAME(0x02, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x02, DEF_STR(On))
PORT_DIPNAME(0x04, 0x00, "Credit Minimum")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x04, "2")
PORT_DIPNAME(0x08, 0x00, "Coin Counters")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x08, "2")
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* trackball sign bit */

PORT_START("IN2")	/* IN2 $2010 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN3")	/* IN3 $2011 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)

PORT_START("IN4")	/* 4 */ /* DSW1 $0408 */
PORT_DIPNAME(0x01, 0x00, "Millipede Head")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x01, "Hard")
PORT_DIPNAME(0x02, 0x00, "Beetle")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x02, "Hard")
PORT_DIPNAME(0x0c, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x08, "4")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x10, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "12000")
PORT_DIPSETTING(0x10, "15000")
PORT_DIPSETTING(0x20, "20000")
PORT_DIPSETTING(0x30, "None")
PORT_DIPNAME(0x40, 0x00, "Spider")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x40, "Hard")
PORT_DIPNAME(0x80, 0x00, "Starting Score Select")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("IN5")	/* 5 */ /* DSW2 $0808 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, "Left Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0xe0, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x20, "3 credits/2 coins")
PORT_DIPSETTING(0x40, "5 credits/4 coins")
PORT_DIPSETTING(0x60, "6 credits/4 coins")
PORT_DIPSETTING(0x80, "6 credits/5 coins")
PORT_DIPSETTING(0xa0, "4 credits/3 coins")
PORT_DIPSETTING(0xc0, "Demo mode")

PORT_START("IN6")	/* IN6: FAKE - used for trackball-x at $2000 */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_X | IPF_REVERSE, 50, 10, 0, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)

PORT_START("IN7")/* IN7: FAKE - used for trackball-y at $2001 */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_Y, 50, 10, 0, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)
INPUT_PORTS_END

ROM_START(milliped)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136013-104.mn1", 0x4000, 0x1000, CRC(40711675) SHA1(b595d6a0f5d3c611ade1b83a94c3b909d2124dc4))
ROM_LOAD("136013-103.l1", 0x5000, 0x1000, CRC(fb01baf2) SHA1(9c1d0bbc20bf25dd21761a311fd1ed80aa029241))
ROM_LOAD("136013-102.jk1", 0x6000, 0x1000, CRC(62e137e0) SHA1(9fe40db55ba1d20d4f11704f7f5df9ff75b87f30))
ROM_LOAD("136013-101.h1", 0x7000, 0x1000, CRC(46752c7d) SHA1(ab06b1fd80271849946f90757b3837b617394929))
ROM_RELOAD(0xf000, 0x1000)	/* for the reset and interrupt vectors */

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136013-107.r5", 0x0000, 0x0800, CRC(68c3437a) SHA1(4c7ea33d9501456ee8f5a642da7d6c972f2bb90d))
ROM_LOAD("136013-106.p5", 0x0800, 0x0800, CRC(f4468045) SHA1(602fcc7290f9f4eacb841c76665961ebf4307f80))

ROM_REGION(0x0100, REGION_PROMS, 0)
ROM_LOAD("136001-213.e7", 0x0000, 0x0100, CRC(6fa3093a) SHA1(2b7aeca74c1ae4156bf1878453a047330f96f0a8)) /* Sync PROM */
ROM_END

/* Millipede */
AAE_DRIVER_BEGIN(drv_milliped, "milliped", "Millipede - NO SOUND - HELP!")
AAE_DRIVER_ROM(rom_milliped)
AAE_DRIVER_FUNCS(&init_milliped, &run_milliped, &end_milliped)
AAE_DRIVER_INPUT(input_ports_milliped)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   0,
		/*r8*/       milliped_readmem,
		/*w8*/       milliped_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY | VIDEO_MODIFIES_PALETTE | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_SWAP_XY)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 239)
AAE_DRIVER_RASTER(milliped_gfxdecodeinfo, 32, 32, milliped_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_milliped)

/***************************************************************************

  Game driver(s)

***************************************************************************/