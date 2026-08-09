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
#include "warlord.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "earom.h"
#include "aae_pokey.h"
#include "centiped_vid.h"
#include "timer.h"

extern unsigned char* centiped_charpalette, * centiped_spritepalette;
int centiped_flipscreen = 0;
static int powerup_counter = 20;
unsigned char centiped_paletteram[0x0f];

// ---------------------------------------------------------------------------
// VBLANK latch: Centipede IN0 bit 6 is ACTIVE HIGH for VBLANK (per port def).
// g_in0_vblank_bit holds the current state of bit 6 to merge into IN0 reads.
// ---------------------------------------------------------------------------
static uint8_t g_in0_vblank_bit = 0x00;

static inline void centiped_vblank_begin(int dum) { g_in0_vblank_bit = 0x00; }
static inline void centiped_vblank_end(int dum) { g_in0_vblank_bit = 0x40; }

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

struct GfxDecodeInfo centiped_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,   4, 4 },	/* 4 color codes to support midframe */
	/* palette changes in test mode */
{ REGION_GFX1, 0, &spritelayout, 0, 1 },
{ -1 } /* end of array */
};

static struct POKEYinterface centiped_pokey_interface =
{
	1,	/* 1 chip */
	12096000 / 8,	/* 1.512 MHz */
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
	{ 0 },
};

void centiped_interrupt()
{
	if (powerup_counter == 0)
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
	else
	{
		powerup_counter--;
		//return ignore_interrupt();
	}
}

//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
//MAIN HANDLERS
//////////////////////////////////////////////////////////////

WRITE_HANDLER(centiped_vh_charpalette_w)
{
	centiped_charpalette[address] = data;
	Machine->gfx[0]->colortable[address] = Machine->pens[15 - data];
}

// Trackball axes: a 4-bit count in bits 0-3 plus a direction bit in bit 7.
// The hardware latches direction only when a pulse arrives, so the sign bit
// holds its last value until the ball actually moves; the counter is read 240
// times per second and is free to wrap between reads.
READ_HANDLER(centiped_IN0_r)
{
	static int oldpos, sign;
	int newpos;

	newpos = readinputport(6);
	if (newpos != oldpos)
	{
		sign = (newpos - oldpos) & 0x80;
		oldpos = newpos;
	}

	return ((readinputport(0) & 0x30) | (oldpos & 0x0f) | g_in0_vblank_bit | sign);
}

READ_HANDLER(centiped_IN2_r)
{
	static int oldpos, sign;
	int newpos;

	newpos = readinputport(2);
	if (newpos != oldpos)
	{
		sign = (newpos - oldpos) & 0x80;
		oldpos = newpos;
	}

	return ((oldpos & 0x0f) | sign);
}

READ_HANDLER(centiped_IN1_r)
{
	return (UINT8)readinputportbytag("IN1");
}

WRITE_HANDLER(irq_ack)
{
	m_cpu_6502[0]->m6502clearpendingint();
}

WRITE_HANDLER(centiped_paletteram_w)
{
	
	int r, g, b;

	centiped_paletteram[address] = data;

	if ((~data & 0x08) == 0) /* luminance = 0 */
	{
		r = 0xc0 * ((~data >> 0) & 1);
		g = 0xc0 * ((~data >> 1) & 1);
		b = 0xc0 * ((~data >> 2) & 1);
	}
	else	/* luminance = 1 */
	{
		r = 0xff * ((~data >> 0) & 1);
		g = 0xff * ((~data >> 1) & 1);
		b = 0xff * ((~data >> 2) & 1);
	}

	osd_modify_pen(Machine->pens[address], r, g, b);
}

WRITE_HANDLER(centiped_led_w)
{
	set_led_status(address, ~data & 0x80);
}

WRITE_HANDLER(centiped_vh_flipscreen_w)
{
	if (centiped_flipscreen != (data & 0x80))
	{
		centiped_flipscreen = data & 0x80;
	}
}

void run_centiped()
{
	pokey_sh_update();
	centiped_vh_screenrefresh();
	centiped_vblank_end(0);       // clear vblank latch (bit 6 goes HIGH)
	timer_pulse(TIME_IN_CYCLES(1450, CPU0), CPU0, centiped_vblank_begin);
}

void end_centiped()
{
	generic_vh_stop();
	pokey_sh_stop();
}

int init_centiped(void)
{
	// Raster drivers set the videoram/spriteram pointers themselves.
	videoram = &Machine->memory_region[CPU0][0x0400];
	spriteram = &Machine->memory_region[CPU0][0x07c0];
	videoram_size = 0x3c0;
	spriteram_size = 0x40;

	centiped_spritepalette = &Machine->memory_region[CPU0][0x140c];
	centiped_charpalette = &Machine->memory_region[CPU0][0x1404];
	pokey_sh_start(&centiped_pokey_interface);
	aae_set_lines_per_frame(262);

	powerup_counter = 25;

	centiped_vh_start();

	return 0;
}

// Revision 4 only. This revision is 1-player-only: its ROM has no 2-player
// start check at all, so START2, the player-2 fire button and the cocktail
// controls are unwired, and DSW2 bits 2-4 carry a game timer in place of the
// coin multipliers. Revisions 1-3 use input_ports_centiped3 below.
INPUT_PORTS_START(centiped)
PORT_START("IN0")/* IN0 */
/* The lower 4 bits and bit 7 are for trackball x input. */
/* They are handled by fake input port 6 and a custom routine. */
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNUSED)	/* no cocktail wiring on rev 4 */
PORT_SERVICE(0x20, IP_ACTIVE_LOW)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_VBLANK)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)	/* no 2-player start on rev 4 */
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)	/* no player-2 fire on rev 4 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN2")	/* IN2 */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_Y, 50, 10, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)
/* The lower 4 bits are the input, and bit 7 is the direction. */
/* The state of bit 7 does not change if the trackball is not moved.*/

PORT_START("IN3")	/* IN3 */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNUSED)	/* cocktail joystick unwired on rev 4 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)

PORT_START("DSW1")	/* IN4 */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "Spanish")
PORT_DIPNAME(0x0c, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x08, "4")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x10, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x10, "12000")
PORT_DIPSETTING(0x20, "15000")
PORT_DIPSETTING(0x30, "20000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Easy")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x80, 0x00, "Credit Minimum")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x80, "2")

PORT_START("DSW2")	/* IN5 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x1c, 0x00, "Game Time")
PORT_DIPSETTING(0x00, "Untimed")
PORT_DIPSETTING(0x04, "1 Minute")
PORT_DIPSETTING(0x08, "2 Minutes")
PORT_DIPSETTING(0x0c, "3 Minutes")
PORT_DIPSETTING(0x10, "4 Minutes")
PORT_DIPSETTING(0x14, "5 Minutes")
PORT_DIPSETTING(0x18, "6 Minutes")
PORT_DIPSETTING(0x1c, "7 Minutes")
PORT_DIPNAME(0xe0, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x20, "3 credits/2 coins")
PORT_DIPSETTING(0x40, "5 credits/4 coins")
PORT_DIPSETTING(0x60, "6 credits/4 coins")
PORT_DIPSETTING(0x80, "6 credits/5 coins")
PORT_DIPSETTING(0xa0, "4 credits/3 coins")

PORT_START("IN6")	/* IN6, fake trackball input port. */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_X | IPF_REVERSE, 50, 10, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)
INPUT_PORTS_END

// Revisions 1-3. Revision 4 dropped the 2-player game entirely, so it gets its
// own port set above: there the 2-player start, the cocktail controls and the
// coin multipliers are all absent, and DSW2 bits 2-4 became the game timer.
INPUT_PORTS_START(centiped3)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* trackball data */
PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_SERVICE(0x20, IP_ACTIVE_LOW)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_VBLANK)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* trackball sign bit */

PORT_START("IN1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN2")	/* IN2 */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_Y, 50, 10, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)

PORT_START("IN3")	/* IN3 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)

PORT_START("DSW1")	/* IN4 */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "Spanish")
PORT_DIPNAME(0x0c, 0x04, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x04, "3")
PORT_DIPSETTING(0x08, "4")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x10, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x10, "12000")
PORT_DIPSETTING(0x20, "15000")
PORT_DIPSETTING(0x30, "20000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Easy")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x80, 0x00, "Credit Minimum")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x80, "2")

PORT_START("DSW2")	/* IN5 */
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

PORT_START("IN6")	/* IN6, fake trackball input port. */
PORT_ANALOGX(0xff, 0x00, IPT_TRACKBALL_X | IPF_REVERSE, 50, 10, 0, 0, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE)
INPUT_PORTS_END


///PORT HANDLERS
MEM_READ(centiped_readmem)
//MEM_ADDR(0x0000, 0x03ff, MRA_RAM)					/* work RAM */
//MEM_ADDR(0x0400, 0x07ff, MRA_RAM)					/* video + sprite RAM */
MEM_ADDR(0x0800, 0x0800, ip_port_4_r)				/* DSW1 */
MEM_ADDR(0x0801, 0x0801, ip_port_5_r)				/* DSW2 */
MEM_ADDR(0x0c00, 0x0c00, centiped_IN0_r)			/* IN0: trackball X, cabinet, service, VBLANK */
MEM_ADDR(0x0c01, 0x0c01, centiped_IN1_r)			/* IN1: starts, fire, coins */
MEM_ADDR(0x0c02, 0x0c02, centiped_IN2_r)			/* IN2: trackball Y */
MEM_ADDR(0x0c03, 0x0c03, ip_port_3_r)				/* IN3: joysticks */
MEM_ADDR(0x1000, 0x100f, pokey_1_r)					/* POKEY */
MEM_ADDR(0x1700, 0x173f, EaromRead)					/* EAROM */
//MEM_ADDR(0x2000, 0x3fff, MRA_ROM)					/* program ROM */
//MEM_ADDR(0xf800, 0xffff, MRA_ROM)					/* reset / interrupt vectors */
MEM_END

MEM_WRITE(centiped_writemem)
//MEM_ADDR(0x0000, 0x03ff, MWA_RAM)					/* work RAM */
//MEM_ADDR(0x0400, 0x07bf, videoram_w)				/* video RAM */
//MEM_ADDR(0x07c0, 0x07ff, MWA_RAM)					/* sprite RAM */
MEM_ADDR(0x1000, 0x100f, pokey_1_w)					/* POKEY */
//MEM_ADDR(0x1400, 0x140f, centiped_paletteram_w)	/* full palette RAM */
MEM_ADDR(0x1404, 0x1407, centiped_vh_charpalette_w)	/* character palette */
MEM_ADDR(0x1600, 0x163f, EaromWrite)				/* EAROM */
MEM_ADDR(0x1680, 0x1680, EaromCtrl)					/* EAROM control */
MEM_ADDR(0x1800, 0x1800, irq_ack)					/* IRQ acknowledge */
//MEM_ADDR(0x1c00, 0x1c02, MWA_NOP)					/* coin counters */
MEM_ADDR(0x1c03, 0x1c04, centiped_led_w)			/* start button lamps */
MEM_ADDR(0x1c07, 0x1c07, centiped_vh_flipscreen_w)	/* cocktail flip */
MEM_ADDR(0x2000, 0x2000, watchdog_reset_w)			/* watchdog */
MEM_ADDR(0x2000, 0x3fff, MWA_ROM)					/* program ROM */
MEM_END

/***************************************************************************

  Game driver(s)

***************************************************************************/
ROM_START(centiped)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136001-407.d1", 0x2000, 0x0800, CRC(c4d995eb) SHA1(d0b2f0461cfa35842045d40ffb65e777703b773e))
ROM_LOAD("136001-408.e1", 0x2800, 0x0800, CRC(bcdebe1b) SHA1(53f3bf88a79ce40661c0a9381928e55d8c61777a))
ROM_LOAD("136001-409.fh1", 0x3000, 0x0800, CRC(66d7b04a) SHA1(8fa758095b618085090491dfb5ea114cdc87f9df))
ROM_LOAD("136001-410.j1", 0x3800, 0x0800, CRC(33ce4640) SHA1(780c2eb320f64fad6b265c0dada961646ed30174))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136001-211.f7", 0x0000, 0x0800, CRC(880acfb9) SHA1(6c862352c329776f2f9974a0df9dbe41f9dbc361))
ROM_LOAD("136001-212.hj7", 0x0800, 0x0800, CRC(b1397029) SHA1(974c03d29aeca672fffa4dfc00a06be6a851aacb))

ROM_REGION(0x0100, REGION_PROMS, 0)
ROM_LOAD("136001-213.p4", 0x0000, 0x0100, CRC(6fa3093a) SHA1(2b7aeca74c1ae4156bf1878453a047330f96f0a8))
ROM_END

ROM_START(centiped3)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("136001-307.d1", 0x2000, 0x0800, CRC(5ab0d9de) SHA1(8ea6e3304202831aabaf31dbd0f970a7b3bfe421))
ROM_LOAD("136001-308.e1", 0x2800, 0x0800, CRC(4c07fd3e) SHA1(af4fdbf32c23b1864819d620a874e7f205da3cdb))
ROM_LOAD("136001-309.fh1", 0x3000, 0x0800, CRC(ff69b424) SHA1(689fa560d40a384dcbcad7c8095bc12e91875580))
ROM_LOAD("136001-310.j1", 0x3800, 0x0800, CRC(44e40fa4) SHA1(c557db83876afc8ab52047ab1a3c3bfef34d6351))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136001-211.f7", 0x0000, 0x0800, CRC(880acfb9) SHA1(6c862352c329776f2f9974a0df9dbe41f9dbc361))
ROM_LOAD("136001-212.hj7", 0x0800, 0x0800, CRC(b1397029) SHA1(974c03d29aeca672fffa4dfc00a06be6a851aacb))

ROM_REGION(0x0100, REGION_PROMS, 0)
ROM_LOAD("136001-213.p4", 0x0000, 0x0100, CRC(6fa3093a) SHA1(2b7aeca74c1ae4156bf1878453a047330f96f0a8))
ROM_END


// centipede
AAE_DRIVER_BEGIN(drv_centiped, "centiped", "Centipede V4 1 Player")
AAE_DRIVER_ROM(rom_centiped)
AAE_DRIVER_FUNCS(&init_centiped, &run_centiped, &end_centiped)
AAE_DRIVER_INPUT(input_ports_centiped)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &centiped_interrupt,
		/*r8*/       centiped_readmem,
		/*w8*/       centiped_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 1500, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY | VIDEO_MODIFIES_PALETTE | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 239)
AAE_DRIVER_RASTER(centiped_gfxdecodeinfo, 4 + 4 * 4, 4 * 4 + 4 * 4 * 4 * 4, centiped_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()


// centipede 3
AAE_DRIVER_BEGIN(drv_centiped3, "centiped3", "Centipede V3 2 Player")
AAE_DRIVER_ROM(rom_centiped3)
AAE_DRIVER_FUNCS(&init_centiped, &run_centiped, &end_centiped)
AAE_DRIVER_INPUT(input_ports_centiped3)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &centiped_interrupt,
		/*r8*/       centiped_readmem,
		/*w8*/       centiped_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 1500, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY | VIDEO_MODIFIES_PALETTE | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 239)
AAE_DRIVER_RASTER(centiped_gfxdecodeinfo, 4 + 4 * 4, 4 * 4 + 4 * 4 * 4 * 4, centiped_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()


AAE_REGISTER_DRIVER(drv_centiped)
AAE_REGISTER_DRIVER(drv_centiped3)