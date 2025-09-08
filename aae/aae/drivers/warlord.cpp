#include "aae_mame_driver.h"
#include "warlord.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "earom.h"
#include "aae_pokey.h"

#pragma warning( disable : 4838 4003 )

const rectangle visible_area =
{
 0,
 255,
 0,
 255
};

static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	64,	    /* 64  characters */
	2,	    /* 2 bits per pixel */
	{ 128 * 8 * 8, 0 },	/* bitplane separation */
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 8	/* every char takes 8 consecutive bytes */
};

struct GfxDecodeInfo warlords_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &charlayout,   0,   8 },
	{ REGION_GFX1, 0x0200, &charlayout,   8 * 4, 8 },
	{ -1 } /* end of array */
};

static struct POKEYinterface warlords_pokey_interface =
{
	1,	/* 1 chip */
	1512000,	/* 1.5 MHz??? */
	{ 128 },
	POKEY_DEFAULT_GAIN,
	NO_CLIP,
	/* The 8 pot handlers */
	{ input_port_4_r },
	{ input_port_5_r },
	{ input_port_6_r },
	{ input_port_7_r },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	/* The allpot handler */
	{ 0 }
};

void warlords_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

void warlords_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i, j;
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	for (i = 0; i < Machine->drv->total_colors; i++)
	{
		int r, g, b;

		r = ((*color_prom >> 2) & 0x01) * 0xff;
		g = ((*color_prom >> 1) & 0x01) * 0xff;
		b = ((*color_prom >> 0) & 0x01) * 0xff;

		/* Colors 0x40-0x7f are converted to grey scale as it's used on the
		   upright version that had an overlay */
		if (i >= Machine->drv->total_colors / 2)
		{
			int grey;

			/* Use the standard ratios: r = 30%, g = 59%, b = 11% */
			grey = ((r != 0) * 0x4d) + ((g != 0) * 0x96) + ((b != 0) * 0x1c);

			r = g = b = grey;
		}

		*(palette++) = r;
		*(palette++) = g;
		*(palette++) = b;

		color_prom++;
	}

	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 4; j++)
		{
			COLOR(0, i * 4 + j) = i * 16 + j;
			COLOR(1, i * 4 + j) = i * 16 + j * 4;
		}
	}
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void warlords_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	int offs, upright_mode, palette;
	
	//	if (palette_recalc())
		//	memset(dirtybuffer, 1, videoram_size);

		/* Cocktail mode uses colors 0-3, upright 4-7 */

	upright_mode = input_port_0_r(0) & 0x80;
	palette = (upright_mode ? 4 : 0);
	
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		//if (dirtybuffer[offs])
	//	{
		int sx, sy, color, flipx, flipy;

		//dirtybuffer[offs] = 0;

		sy = (offs / 32);
		sx = (offs % 32);

		flipx = !(videoram[offs] & 0x40);
		flipy = videoram[offs] & 0x80;

		if (upright_mode)
		{
			sx = 31 - sx;
			flipx = !flipx;
		}

		// The four quadrants have different colors
		color = ((sy & 0x10) >> 3) | ((sx & 0x10) >> 4) | palette;

		drawgfx(tmpbitmap, Machine->gfx[0],
			videoram[offs] & 0x3f,
			color,
			flipx, flipy,
			8 * sx, 8 * sy,
			&Machine->gamedrv->visible_area, TRANSPARENCY_NONE, 0);
		//}
	}

	/* copy the temporary bitmap to the screen */
	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &visible_area, TRANSPARENCY_NONE, 0);

	/* Draw the sprites */
	for (offs = 0; offs < 0x10; offs++)
	{
		int sx, sy, flipx, flipy, spritenum, color;

		sx = spriteram[offs + 0x20];
		sy = 248 - spriteram[offs + 0x10];

		flipx = !(spriteram[offs] & 0x40);
		flipy = spriteram[offs] & 0x80;

		if (upright_mode)
		{
			sx = 248 - sx;
			flipx = !flipx;
		}

		spritenum = (spriteram[offs] & 0x3f);

		/* The four quadrants have different colors. This is not 100% accurate,
		   because right on the middle the sprite could actually have two or more
		   different color, but this is not noticable, as the color that
		   changes between the quadrants is mostly used on the paddle sprites */
		color = ((sy & 0x80) >> 6) | ((sx & 0x80) >> 7) | palette;

		drawgfx(main_bitmap, Machine->gfx[1],
			spritenum, color,
			flipx, flipy,
			sx, sy,
			&visible_area, TRANSPARENCY_PEN, 0);
	}
}

int warlords_vh_start(void)
{
	videoram = &Machine->memory_region[0][0x0400];
	videoram_size = 0x3c0;
	spriteram = &Machine->memory_region[0][0x07c0];
	spriteram_size = 0x40;

	return generic_vh_start();
}

//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
//MAIN HANDLERS
//////////////////////////////////////////////////////////////

WRITE_HANDLER(warlord_led_w)
{
	set_led_status(address, ~data >> 7);
}

WRITE_HANDLER(irq_ack)
{
}

//called on reset
void warlords_init_machine(void)
{
}

void run_warlords()
{
	warlords_vh_screenrefresh();
	pokey_sh_update();
}

void end_warlords()
{
	pokey_sh_stop();
}

INPUT_PORTS_START(warlords)
PORT_START("IN0")
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_DIPNAME(0x10, 0x00, "Diag Step")  /* Not referenced */
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x10, DEF_STR(On))
PORT_SERVICE(0x20, IP_ACTIVE_LOW)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_VBLANK)
PORT_DIPNAME(0x80, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER4)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("DSW1")
PORT_DIPNAME(0x03, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x01, "French")
PORT_DIPSETTING(0x02, "Spanish")
PORT_DIPSETTING(0x03, "German")
PORT_DIPNAME(0x04, 0x00, "Music")
PORT_DIPSETTING(0x00, "End of game")
PORT_DIPSETTING(0x04, "High score only")
PORT_BIT(0xc8, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_DIPNAME(0x30, 0x00, "Credits")
PORT_DIPSETTING(0x00, "1p/2p = 1 credit")
PORT_DIPSETTING(0x10, "1p = 1, 2p = 2")
PORT_DIPSETTING(0x20, "1p/2p = 2 credits")

PORT_START("DSW2")	/* IN3 */
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

/* IN4-7 fake to control player paddles */
PORT_START("IN4")
PORT_ANALOG(0xff, 0x80, IPT_PADDLE | IPF_PLAYER1, 50, 10, 0x1d, 0, 0xcb, 0xbf)//191?
//PORT_ANALOGX(0xff, 0x80, IPT_PADDLE | IPF_PLAYER1, 50, 10, 0x1d, 0xcb, OSD_KEY_LEFT, OSD_KEY_RIGHT, OSD_JOY_LEFT, OSD_JOY_RIGHT, 1)

PORT_START("IN5")
PORT_ANALOG(0xff, 0x80, IPT_PADDLE | IPF_PLAYER2, 50, 10, 0, 0x1d, 0xcb)

PORT_START("IN6")
PORT_ANALOG(0xff, 0x80, IPT_PADDLE | IPF_PLAYER3, 50, 10, 0, 0x1d, 0xcb)

PORT_START("IN7")
PORT_ANALOG(0xff, 0x80, IPT_PADDLE | IPF_PLAYER4, 50, 10, 0, 0x1d, 0xcb)
INPUT_PORTS_END

///PORT HANDLERS

MEM_READ(warlords_readmem)
MEM_ADDR(0x0000, 0x07ff, MRA_RAM)
MEM_ADDR(0x0800, 0x0800, ip_port_2_r)	/* DSW1 */
MEM_ADDR(0x0801, 0x0801, ip_port_3_r)	/* DSW2 */
MEM_ADDR(0x0c00, 0x0c00, ip_port_0_r)	/* IN0 */
MEM_ADDR(0x0c01, 0x0c01, ip_port_1_r)	/* IN1 */
MEM_ADDR(0x1000, 0x100f, pokey_1_r)  	/* Read the 4 paddle values & the random # gen */
MEM_ADDR(0x5000, 0x7fff, MRA_ROM)
MEM_ADDR(0xf800, 0xffff, MRA_ROM)		/* for the reset / interrupt vectors */
MEM_END

MEM_WRITE(warlords_writemem)
MEM_ADDR(0x0000, 0x03ff, MWA_RAM)
//MEM_ADDR(0x0400, 0x07bf, videoram_w, &videoram, &videoram_size),
//MEM_ADDR(0x07c0, 0x07ff, MWA_RAM, &spriteram),
MEM_ADDR(0x1000, 0x100f, pokey_1_w)
MEM_ADDR(0x1800, 0x1800, MWA_NOP)        /* IRQ Acknowledge */
MEM_ADDR(0x1c00, 0x1c02, MWA_NOP)
MEM_ADDR(0x1c03, 0x1c06, warlord_led_w)	 /* 4 start lights */
MEM_ADDR(0x4000, 0x4000, watchdog_reset_w)
MEM_END

int init_warlords()
{
	//init6502(warlords_readmem, warlords_writemem, 0xffff, CPU0);

	pokey_sh_start(&warlords_pokey_interface);
	warlords_vh_start();
	return 0;
}

/***************************************************************************

  Game driver(s)

***************************************************************************/
ROM_START(warlords)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("037154-01.m1", 0x5000, 0x0800, CRC(18006c87) SHA1(6b4aab1b1710819d29f4bbc29269eb9c915626c0))
ROM_LOAD("037153-01.k1", 0x5800, 0x0800, CRC(67758f4c) SHA1(b65ca677b54de7a8202838207d9a7bb0aed3e0f2))
ROM_LOAD("037158-01.j1", 0x6000, 0x0800, CRC(1f043a86) SHA1(b1e271c0979d62202ae86c4b6860fb67bbef6400))
ROM_LOAD("037157-01.h1", 0x6800, 0x0800, CRC(1a639100) SHA1(41ec333aee7192f8aeef49e5257f201f4db01cff))
ROM_LOAD("037156-01.e1", 0x7000, 0x0800, CRC(534f34b4) SHA1(1680982ded17350c2ae10bb47f7eb8908bb10db2))
ROM_LOAD("037155-01.d1", 0x7800, 0x0800, CRC(23b94210) SHA1(d74c1ca90caf15942805043b4ebe4ee077799da0))
ROM_RELOAD(0xf800, 0x0800)	/* for the reset and interrupt vectors */

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("037159-01.e6", 0x0000, 0x0800, CRC(ff979a08) SHA1(422053473e41e3e1f71eb28e40eedc78f22326b3))

ROM_REGION(0x0100, REGION_PROMS, 0)
/* Only the first 0x80 bytes are used by the hardware. A7 is grounded. */
/* Bytes 0x00-0x3f are used fore the color cocktail version. */
/* Bytes 0x40-0x7f are for the upright version of the cabinet with a */
/* mirror and painted background. */
ROM_LOAD("037235-01.n7", 0x0000, 0x0100, CRC(a2c5c277) SHA1(f04de9fb6ee9619b4a4aae10c92b16b3123046cf))
//ROM_LOAD("037161-01.m6", 0x0100, 0x0100, CRC(4cd24c85), SHA1(00f4876279255f3a2d136a9d916b388812cbd1fc)) // Sync PROM

ROM_END

// Warlords
AAE_DRIVER_BEGIN(drv_warlords, "warlords", "Warlords")
AAE_DRIVER_ROM(rom_warlords)
AAE_DRIVER_FUNCS(&init_warlords, &run_warlords, &end_warlords)
AAE_DRIVER_INPUT(input_ports_warlords)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6502,
		/*freq*/     756000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &warlords_interrupt,
		/*r8*/       warlords_readmem,
		/*w8*/       warlords_writemem,
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
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 255)
AAE_DRIVER_RASTER(warlords_gfxdecodeinfo, 128, 8 * 4 + 8 * 4, warlords_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_warlords)

/***************************************************************************
Warlords Driver by Lee Taylor and John Clegg

			  Warlords Memory map and Dip Switches
			  ------------------------------------

 Address  R/W  D7 D6 D5 D4 D3 D2 D1 D0   Function
--------------------------------------------------------------------------------------
0000-03FF       D  D  D  D  D  D  D  D   RAM
--------------------------------------------------------------------------------------
0400-07BF       D  D  D  D  D  D  D  D   Screen RAM (8x8 TILES, 32x32 SCREEN)
07C0-07CF       D  D  D  D  D  D  D  D   Motion Object Picture
07D0-07DF       D  D  D  D  D  D  D  D   Motion Object Vert.
07E0-07EF       D  D  D  D  D  D  D  D   Motion Object Horiz.
--------------------------------------------------------------------------------------
0800       R    D  D  D  D  D  D  D  D   Option Switch 1 (0 = On) (DSW 1)
0801       R    D  D  D  D  D  D  D  D   Option Switch 2 (0 = On) (DSW 2)
--------------------------------------------------------------------------------------
0C00       R    D                        Cocktail Cabinet  (0 = Cocktail)
		   R       D                     VBLANK  (1 = VBlank)
		   R          D                  SELF TEST
		   R             D               DIAG STEP (Unused)
0C01       R    D  D  D                  R,C,L Coin Switches (0 = On)
		   R             D               Slam (0 = On)
		   R                D            Player 4 Start Switch (0 = On)
		   R                   D         Player 3 Start Switch (0 = On)
		   R                      D      Player 2 Start Switch (0 = On)
		   R                         D   Player 1 Start Switch (0 = On)
--------------------------------------------------------------------------------------
1000-100F  W   D  D  D  D  D  D  D  D    Pokey
--------------------------------------------------------------------------------------
1800       W                             IRQ Acknowledge
--------------------------------------------------------------------------------------
1C00-1C02  W    D  D  D  D  D  D  D  D   Coin Counters
--------------------------------------------------------------------------------------
1C03-1C06  W    D  D  D  D  D  D  D  D   LEDs
--------------------------------------------------------------------------------------
4000       W                             Watchdog
--------------------------------------------------------------------------------------
5000-7FFF  R                             Program ROM
--------------------------------------------------------------------------------------

Game Option Settings - J2 (DSW1)
=========================

8   7   6   5   4   3   2   1       Option
------------------------------------------
						On  On      English
						On  Off     French
						Off On      Spanish
						Off Off     German
					On              Music at end of each game
					Off             Music at end of game for new highscore
		On  On                      1 or 2 player game costs 1 credit
		On  Off                     1 player game=1 credit, 2 player=2 credits
		Off Off                     1 or 2 player game costs 2 credits
		Off On                      Not used
-------------------------------------------

Game Price Settings - M2 (DSW2)
========================

8   7   6   5   4   3   2   1       Option
------------------------------------------
						On  On      Free play
						On  Off     1 coin for 2 credits
						Off On      1 coin for 1 credit
						Off Off     2 coins for 1 credit
				On  On              Right coin mech x 1
				On  Off             Right coin mech x 4
				Off On              Right coin mech x 5
				Off Off             Right coin mech x 6
			On                      Left coin mech x 1
			Off                     Left coin mech x 2
On  On  On                          No bonus coins
On  On  Off                         For every 2 coins, add 1 coin
On  Off On                          For every 4 coins, add 1 coin
On  Off Off                         For every 4 coins, add 2 coins
Off On  On                          For every 5 coins, add 1 coin
------------------------------------------

***************************************************************************/