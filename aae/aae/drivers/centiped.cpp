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

#pragma warning( disable : 4838 4003 )

constexpr auto IN0_VBLANK = (1 << 6);
static int vblank = 0;
extern unsigned char* centiped_charpalette, * centiped_spritepalette;
int centiped_flipscreen = 0;
static int powerup_counter = 20;
unsigned char centiped_paletteram[0x0f];
int j;
UINT8 vsync;

static int oldpos[4];
static UINT8 sign[4];
static UINT8 dsw_select;

// ---------------------------------------------------------------------------
// VBLANK latch: Centipede IN0 bit 6 is ACTIVE HIGH for VBLANK (per port def).
// g_in0_vblank_bit holds the current state of bit 6 to merge into IN0 reads.
// ---------------------------------------------------------------------------
static uint8_t g_in0_vblank_bit = 0x00;

static inline void centiped_vblank_begin(int dum) { g_in0_vblank_bit = 0x00; }
static inline void centiped_vblank_end(int dum) { g_in0_vblank_bit = 0x40; }

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

static void setcolor(int pen, int data)
{
	int r, g, b;

	r = 0xff * ((~data >> 0) & 1);
	g = 0xff * ((~data >> 1) & 1);
	b = 0xff * ((~data >> 2) & 1);

	if (~data & 0x08) /* alternate = 1 */
	{
		/* when blue component is not 0, decrease it. When blue component is 0, */
		/* decrease green component. */
		if (b) b = 0xc0;
		else if (g) g = 0xc0;
	}

	osd_modify_pen(pen, r, g, b);
}

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

/*
 * This wrapper routine is necessary because Centipede requires a direction bit
 * to be set or cleared. The direction bit is held until the mouse is moved
 * again.
 *
 * There is a 4-bit counter, and two inputs from the trackball: DIR and CLOCK.
 * CLOCK makes the counter move in the direction of DIR. Since DIR is latched
 * only when a CLOCK arrives, the DIR bit in the input port doesn't change
 * until the trackball actually moves.
 *
 * There is also a CLR input to the counter which could be used by the game to
 * clear the counter, but Centipede doesn't use it (though it would be a good
 * idea to support it anyway).
 *
 * The counter is read 240 times per second. There is no provision whatsoever
 * to prevent the counter from wrapping around between reads.
 */

int read_trackball(int idx, int switch_port)
{
	int newpos;

	/* adjust idx if we're cocktail flipped */
	if (centiped_flipscreen)
		idx += 2;

	/* if we're to read the dipswitches behind the trackball data, do it now */
	if (dsw_select)
		return (readinputport(switch_port) & 0x7f) | sign[idx];

	/* get the new position and adjust the result */
	newpos = readinputport(6 + idx);
	if (newpos != oldpos[idx])
	{
		sign[idx] = (newpos - oldpos[idx]) & 0x80;
		oldpos[idx] = newpos;
	}

	/* blend with the bits from the switch port */
	return (readinputport(switch_port) & 0x70) | (oldpos[idx] & 0x0f) | sign[idx];
}

WRITE_HANDLER(centiped_vh_charpalette_w)
{
	centiped_charpalette[address] = data;
	Machine->gfx[0]->colortable[address] = Machine->pens[15 - data];
}

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
	//	return read_trackball(1, 2);
}

READ_HANDLER(centiped_IN1_r)
{
	UINT8 data;
	data = readinputportbytag("IN1");
	return (UINT8)data;
}

READ_HANDLER(centiped_IN4_r)
{
	UINT8 data;
	data = readinputportbytag("DSW1");
	return (UINT8)data;
}
READ_HANDLER(centiped_IN5_r)
{
	UINT8 data;
	data = readinputportbytag("DSW2");
	return (UINT8)data;
}

WRITE_HANDLER(irq_ack)
{
	m_cpu_6502[0]->m6502clearpendingint();
}

WRITE_HANDLER(centiped_paletteram_w)
{
	/*
	centiped_paletteram[address] = data;

	wrlog("Address here %d", address);

	// the char palette will be effectively updated by the next interrupt handler

	if (address >= 12 && address < 16)	//sprites palette
	{
		int start = Machine->drv->gfxdecodeinfo[1].color_codes_start;

		setcolor(start + (address - 12), data);
	}
	*/

	int r, g, b;

	//wrlog("Address here is %d");
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
	//set_led_status(offset, ~data & 0x80);
}

WRITE_HANDLER(centiped_vh_flipscreen_w)
{
	if (centiped_flipscreen != (data & 0x80))
	{
		centiped_flipscreen = data & 0x80;
	}
}

//called on reset
void centiped_init_machine(void)
{
	LOG_INFO("------- WHY IS THIS NOT BEING SET? --------------");
	powerup_counter = 50;
}

void run_centiped()
{
	pokey_sh_update();
//	watchdog_reset_w(0, 0, 0);
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
	LOG_INFO("------- VIDEO RAM SIZE SET HERE --------------");
	//FOR RASTER, VIDEORAM POINTER, SPRITERAM POINTER NEED TO BE SET MANUALLY
	videoram = &Machine->memory_region[CPU0][0x0400];
	spriteram = &Machine->memory_region[CPU0][0x07c0];
	videoram_size = 0x3c0;
	//colorram = &Machine->memory_region[Machine->drv->cpu[0].memory_region][0x440];

	spriteram_size = 0x40;

	centiped_spritepalette = &Machine->memory_region[CPU0][0x140c];
	centiped_charpalette = &Machine->memory_region[CPU0][0x1404];
	pokey_sh_start(&centiped_pokey_interface);
	aae_set_lines_per_frame(262);

	dsw_select = 0;
	/* CPU ROMs */
	powerup_counter = 25;

	centiped_vh_start();

	return 0;
}

INPUT_PORTS_START(centiped)
PORT_START("IN0")/* IN0 */
/* The lower 4 bits and bit 7 are for trackball x input. */
/* They are handled by fake input port 6 and a custom routine. */
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_SERVICE(0x20, IP_ACTIVE_LOW)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_VBLANK)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

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
/* The lower 4 bits are the input, and bit 7 is the direction. */
/* The state of bit 7 does not change if the trackball is not moved.*/

PORT_START("IN3")	/* IN3 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)

PORT_START("DSW1")	/* IN4 */
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "German")
PORT_DIPSETTING(0x02, "French")
PORT_DIPSETTING(0x03, "English")
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
//{ 0x0000, 0x03ff, MRA_RAM },
//{ 0x0400, 0x07ff, MRA_RAM },
{
	0x0800, 0x0800, ip_port_4_r
},	/* DSW1 */
{ 0x0801, 0x0801, ip_port_5_r },	/* DSW2 */
{ 0x0c00, 0x0c00, centiped_IN0_r },	/* IN0 */
{ 0x0c01, 0x0c01, ip_port_1_r },	/* IN1 */
{ 0x0c02, 0x0c02, centiped_IN2_r },	/* IN2 */	/* JB 971220 */
{ 0x0c03, 0x0c03, ip_port_3_r },	/* IN3 */
{ 0x1000, 0x100f, pokey_1_r },
{ 0x1700, 0x173f, EaromRead },
//{ 0x2000, 0x3fff, MRA_ROM },
//{ 0xf800, 0xffff, MRA_ROM },	/* for the reset / interrupt vectors */
MEM_END

MEM_WRITE(centiped_writemem)
//{ 0x0000, 0x03ff, MWA_RAM },
//{ 0x0400, 0x07bf, videoram_w },
//{ 0x07c0, 0x07ff, MWA_RAM, &spriteram },
{
	0x1000, 0x100f, pokey_1_w
},
//{ 0x1400, 0x140f, centiped_paletteram_w },
{ 0x1404, 0x1407,centiped_vh_charpalette_w },
{ 0x1600, 0x163f, EaromWrite },
{ 0x1680, 0x1680, EaromCtrl },
{ 0x1800, 0x1800, irq_ack },	/* IRQ acknowldege */
//{ 0x1c00, 0x1c02, MWA_NOP },
{ 0x1c03, 0x1c04, centiped_led_w },
{ 0x1c07, 0x1c07, centiped_vh_flipscreen_w },
{ 0x2000, 0x2000, watchdog_reset_w },
{ 0x2000, 0x3fff, MWA_ROM },
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

// centipede
AAE_DRIVER_BEGIN(drv_centiped, "centiped", "Centipede")
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

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY | VIDEO_MODIFIES_PALETTE | VIDEO_UPDATE_AFTER_VBLANK, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 239)
AAE_DRIVER_RASTER(centiped_gfxdecodeinfo, 4 + 4 * 4, 4 * 4 + 4 * 4 * 4 * 4, centiped_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(atari_vg_earom_handler)
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_centiped)