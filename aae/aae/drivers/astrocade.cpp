/***************************************************************************

  astrocade.cpp

  AAE driver for the Bally/Midway Astrocade arcade hardware family.
  Pairs with sndhrdwr/astrocade_sound.cpp (custom I/O sound chip) and
  vidhrdwr/astrocde_video.cpp (magic RAM / pattern board bitmap video).

  Original MAME driver by Nicola Salmoria, Mike Coates, Frank Palazzolo.
  Ported to AAE from MAME 0.57 (video) / 0.90 (sound, ROM sets).

  Games (add more below as they come online):
    - Space Zap        (Midway, 1980)   [working]
    - Sea Wolf II, Extra Bases, Wizard of Wor, Gorf, Robby Roto,
      Professor PacMan  -- TODO (WOW/Gorf also need the Votrax speech
      sample player and the star field, both already scaffolded).

  Common hardware:
    CPU:    Z80 @ 1,789,773 Hz
    Video:  320x204 bitmapped, 2bpp packed (80 bytes/line), "magic RAM"
            write processor + pattern board blitter, 8 scanline-latched
            color registers out of a fixed 256-entry YUV palette.
            Scanline interrupt (Z80 IM2, vector on port 0x0d).
    Sound:  Astrocade custom I/O chips (ports 0x10-0x18, 0x50-0x58)

  Space Zap memory map:
    0000-3fff  ROM, writes pass through the magic RAM processor
    4000-7fff  video RAM (bitmap; same 16KB the magic RAM writes into)
    8000-cfff  ROM (8000-bfff used)
    d000-dfff  static RAM

  I/O ports: see the port maps below.

***************************************************************************/

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "astrocde_video.h"
#include "astrocade_sound.h"

#include "opengl_renderer.h"	/* g_scanline_override */

/*--------------------------------------------------------------------------
  Sound: two Astrocade chips at the Z80 clock
--------------------------------------------------------------------------*/
static struct astrocade_interface astrocade_2chip_interface =
{
	2,			/* Number of chips */
	1789773,	/* Clock speed */
	{ 255, 255 }	/* Volume */
};

/*--------------------------------------------------------------------------
  Input port read handlers (Z80 port map)
--------------------------------------------------------------------------*/
PORT_READ_HANDLER(spacezap_IN0_r) { (void)port; (void)pPR; return (UINT16)input_port_0_r(0); }
PORT_READ_HANDLER(spacezap_IN1_r) { (void)port; (void)pPR; return (UINT16)input_port_1_r(0); }
PORT_READ_HANDLER(spacezap_IN2_r) { (void)port; (void)pPR; return (UINT16)input_port_2_r(0); }
PORT_READ_HANDLER(spacezap_DSW_r) { (void)port; (void)pPR; return (UINT16)input_port_3_r(0); }

/*--------------------------------------------------------------------------
  Memory maps
--------------------------------------------------------------------------*/
MEM_READ(spacezap_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x4000, 0x7fff, MRA_RAM)
MEM_ADDR(0x8000, 0xcfff, MRA_ROM)
MEM_ADDR(0xd000, 0xdfff, MRA_RAM)
MEM_END

MEM_WRITE(spacezap_writemem)
MEM_ADDR(0x0000, 0x3fff, wow_magicram_w)
MEM_ADDR(0x4000, 0x7fff, wow_videoram_w)
MEM_ADDR(0x8000, 0xcfff, MWA_ROM)
MEM_ADDR(0xd000, 0xdfff, MWA_RAM)
MEM_END

/*--------------------------------------------------------------------------
  Port maps
--------------------------------------------------------------------------*/
PORT_READ(spacezap_readport)
PORT_ADDR(0x08, 0x08, wow_intercept_r)
PORT_ADDR(0x0e, 0x0e, wow_video_retrace_r)
PORT_ADDR(0x10, 0x10, spacezap_IN0_r)
PORT_ADDR(0x11, 0x11, spacezap_IN1_r)
PORT_ADDR(0x12, 0x12, spacezap_IN2_r)
PORT_ADDR(0x13, 0x13, spacezap_DSW_r)
PORT_END

PORT_WRITE(spacezap_writeport)
PORT_ADDR(0x00, 0x07, astrocde_colour_register_w)
PORT_ADDR(0x08, 0x08, astrocde_mode_w)
PORT_ADDR(0x09, 0x09, astrocde_colour_split_w)
PORT_ADDR(0x0a, 0x0a, astrocde_vertical_blank_w)
PORT_ADDR(0x0b, 0x0b, astrocde_colour_block_w)
PORT_ADDR(0x0c, 0x0c, astrocde_magic_control_w)
PORT_ADDR(0x0d, 0x0d, interrupt_vector_w)
PORT_ADDR(0x0e, 0x0e, astrocde_interrupt_enable_w)
PORT_ADDR(0x0f, 0x0f, astrocde_interrupt_w)
PORT_ADDR(0x10, 0x18, astrocade_sound1_w)
PORT_ADDR(0x19, 0x19, astrocde_magic_expand_color_w)
PORT_ADDR(0x50, 0x58, astrocade_sound2_w)
PORT_ADDR(0x78, 0x7e, astrocde_pattern_board_w)
PORT_END

/*--------------------------------------------------------------------------
  Interrupt: per-scanline tick (div=256 / ipf=256), the video core fires
  the Z80 INT when the programmed scanline is reached.
--------------------------------------------------------------------------*/
static void spacezap_interrupt(void)
{
	astrocde_scanline_interrupt();
}

/*--------------------------------------------------------------------------
  Game lifecycle
--------------------------------------------------------------------------*/
int init_spacezap(void)
{
	if (astrocde_vh_start())
		return 1;

	astrocade_sh_start(&astrocade_2chip_interface);

	/* Default is the production cabinet: B&W monitor + color gel overlay. */
	astrocde_set_bw_monitor(readinputport(4) & 0x01);

	return 0;
}

void run_spacezap(void)
{
	/* Monitor dip, warlords-style: B&W = grayscale pens + the color gel
	   overlay artwork; Color = full palette, overlay/artwork off. */
	static bool user_overlay = config.overlay;
	static bool user_artwork = config.artwork;
	bool bw = (readinputport(4) & 0x01) != 0;

	astrocde_set_bw_monitor(bw);
	if (bw)
	{
		config.overlay = user_overlay;
		config.artwork = user_artwork;
		g_scanline_override = -1;  // force off
	}
	else
	{
		config.overlay = false;
		config.artwork = false;
		g_scanline_override = 1;   // force on (punch through RASTER_BW block)
	}

	/* All rendering happens per scanline in spacezap_interrupt; only the
	   sound stream needs a per-frame push. */
	watchdog_reset_w(0, 0, 0);
	astrocade_sh_update();
}

void end_spacezap(void)
{
	astrocade_sh_stop();
	astrocde_vh_stop();
}

/***************************************************************************
  Input port definitions (from MAME 0.57)
***************************************************************************/

INPUT_PORTS_START(spacezap)
	PORT_START("IN0")	/* port 0x10 */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_TILT)
	PORT_SERVICE(0x08, IP_ACTIVE_LOW)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START("IN1")	/* port 0x11: unused */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START("IN2")	/* port 0x12 */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START("DSW")	/* port 0x13 */
	PORT_DIPNAME(0x01, 0x01, DEF_STR(Coin_A))
	PORT_DIPSETTING(0x00, DEF_STR(2C_1C))
	PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
	PORT_DIPNAME(0x06, 0x06, DEF_STR(Coin_B))
	PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
	PORT_DIPSETTING(0x06, DEF_STR(1C_1C))
	PORT_DIPSETTING(0x02, DEF_STR(1C_3C))
	PORT_DIPSETTING(0x00, DEF_STR(1C_5C))
	PORT_DIPNAME(0x08, 0x00, DEF_STR(Unknown))
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x08, DEF_STR(On))
	PORT_DIPNAME(0x10, 0x00, DEF_STR(Unknown))
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x10, DEF_STR(On))
	PORT_DIPNAME(0x20, 0x00, DEF_STR(Unknown))
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x20, DEF_STR(On))
	PORT_DIPNAME(0x40, 0x00, DEF_STR(Unknown))
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x40, DEF_STR(On))
	PORT_DIPNAME(0x80, 0x00, DEF_STR(Unknown))
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x80, DEF_STR(On))

	PORT_START("MONITOR")	/* fake port: not read by the game, only by run_spacezap */
	PORT_DIPNAME(0x01, 0x01, "Monitor")
	PORT_DIPSETTING(0x01, "Black & White")	/* production cabinet: B&W + color gel overlay */
	PORT_DIPSETTING(0x00, "Color")
INPUT_PORTS_END

/***************************************************************************
  ROM definitions (CRCs/SHA1s from MAME 0.90)
***************************************************************************/

ROM_START(spacezap)
	ROM_REGION(0x10000, REGION_CPU1, 0)
	ROM_LOAD("0662.01", 0x0000, 0x1000, CRC(a92de312) SHA1(784ac67c75c7c101f97ebfd39b2b3f7bf7fa470a))
	ROM_LOAD("0663.xx", 0x1000, 0x1000, CRC(4836ebf1) SHA1(ad0e8c34a209c827c1336f0250cc61fee667fb03))
	ROM_LOAD("0664.xx", 0x2000, 0x1000, CRC(d8193a80) SHA1(72151e773562da62acd2c1d9638711711cbc13a3))
	ROM_LOAD("0665.xx", 0x3000, 0x1000, CRC(3784228d) SHA1(5aabd720a106158a892368c4920d9cd0f5235e34))
ROM_END

/***************************************************************************
  Driver registration

  Z80 @ 1,789,773 Hz, 60 fps.
  div = ipf = 256: the interrupt callback is the per-scanline tick
  (matches MAME's wow_interrupt,256).
***************************************************************************/

AAE_DRIVER_BEGIN(drv_spacezap, "spacezap", "Space Zap")
AAE_DRIVER_ROM(rom_spacezap)
AAE_DRIVER_FUNCS(&init_spacezap, &run_spacezap, &end_spacezap)
AAE_DRIVER_INPUT(input_ports_spacezap)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     1789773,      /* 1.789 MHz */
		/*div*/      256,
		/*ipf*/      256,          /* one interrupt pass per scanline */
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &spacezap_interrupt,
		/*r8*/       spacezap_readmem,
		/*w8*/       spacezap_writemem,
		/*pr*/       spacezap_readport,
		/*pw*/       spacezap_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)	/* B&W + overlay, like warlords */
AAE_DRIVER_SCREEN(320, 204, 0, 320 - 1, 0, 204 - 1)
AAE_DRIVER_RASTER(nullptr, 256, 0, astrocde_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")	/* B&W monitor + color gel overlay + bezel (Mr. Do artwork) */
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_spacezap)
