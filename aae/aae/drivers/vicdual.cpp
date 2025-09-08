#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "vicdual.h"

ART_START(depthch_art)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_END

//Sound Section Start ---------------------------
const char* depthch_samples[] =
{
	"depthch.zip",
	"longex.wav",
	"shortex.wav",
	"spray.wav",
	"sonar.wav",
	"sonarena.wav",
	0
};

/* output port 0x01 definitions - sound effect drive outputs */
#define OUT_PORT_1_LONGEXPL     0x01
#define OUT_PORT_1_SHRTEXPL     0x02
#define OUT_PORT_1_SPRAY        0x04
#define OUT_PORT_1_SONAR        0x08


#define PLAY(id,loop)           sample_start( id, id, loop )
#define STOP(id)                sample_stop( id )


/* sample sound IDs - must match sample file name table above */
enum
{
	SND_LONGEXPL = 0,
	SND_SHRTEXPL,
	SND_SPRAY,
	SND_SONAR,
	SND_SONARENA
};


PORT_WRITE_HANDLER( depthch_sh_port1_w)
{
	static int port1State = 0;
	int bitsChanged;
	int bitsGoneHigh;
	int bitsGoneLow;


	bitsChanged = port1State ^ data;
	bitsGoneHigh = bitsChanged & data;
	bitsGoneLow = bitsChanged & ~data;

	port1State = data;

	if (bitsGoneHigh & OUT_PORT_1_LONGEXPL)
	{
		PLAY(SND_LONGEXPL, 0);
	}

	if (bitsGoneHigh & OUT_PORT_1_SHRTEXPL)
	{
		PLAY(SND_SHRTEXPL, 0);
	}

	if (bitsGoneHigh & OUT_PORT_1_SPRAY)
	{
		PLAY(SND_SPRAY, 0);
	}

	if (bitsGoneHigh & OUT_PORT_1_SONAR)
	{
		PLAY(SND_SONAR, 1);
	}
	if (bitsGoneLow & OUT_PORT_1_SONAR)
	{
		STOP(SND_SONAR);
	}
}

//Sound Section End -- ---------------------------

// Video Section Start ---------------------------
const rectangle visible_area =
{
 0,
 255,
 0,
 255
};

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

struct GfxDecodeInfo vicdual_gfxdecodeinfo[] =
{
	{ 0, 0xe800, &charlayout, 0, 32 },	/* the game dynamically modifies this */
	{ -1 }	/* end of array */
};

static unsigned char* vicdual_ram;
unsigned char* vicdual_characterram;
static unsigned char dirtycharacter[1024];

static int palette_bank = 0;

void vicdual_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
	/* for b&w games we'll use the Head On PROM */
	static unsigned char bw_color_prom[] =
	{
		/* for b/w games, let's use the Head On PROM */
		0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,
		0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,
		0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,
		0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1
	};

	color_prom = bw_color_prom;

	for (i = 0; i < Machine->drv->total_colors / 2; i++)
	{
		int bit;

		/* background red component */
		bit = (*color_prom >> 3) & 0x01;
		*(palette++) = 0xff * bit;
		/* background green component */
		bit = (*color_prom >> 1) & 0x01;
		*(palette++) = 0xff * bit;
		/* background blue component */
		bit = (*color_prom >> 2) & 0x01;
		*(palette++) = 0xff * bit;

		/* foreground red component */
		bit = (*color_prom >> 7) & 0x01;
		*(palette++) = 0xff * bit;
		/* foreground green component */
		bit = (*color_prom >> 5) & 0x01;
		*(palette++) = 0xff * bit;
		/* foreground blue component */
		bit = (*color_prom >> 6) & 0x01;
		*(palette++) = 0xff * bit;

		color_prom++;
	}

	palette_bank = 0;
}

WRITE_HANDLER(vicdual_characterram_w)
{
	if (vicdual_characterram[address] != data)
	{
		dirtycharacter[(address / 8) & 0xff] = 1;

		vicdual_characterram[address] = data;
	}
}

READ_HANDLER(vicdual_characterram_r)
{
	return vicdual_characterram[address];
}

PORT_WRITE_HANDLER(vicdual_palette_bank_w)
{
	if (palette_bank != (data & 3))
	{
		palette_bank = data & 3;
		memset(dirtybuffer, 1, videoram_size);
	}
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/

void vicdual_vh_screenrefresh(void)
{
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			int offs = ((y >> 3) << 5) | (x >> 3);       // tile index
			uint8_t char_code = videoram[offs & 0x3FF];  // 0..0x3FF safe
			int row = y & 7;
			uint8_t video_data = vicdual_characterram[((char_code << 3) | row) & 0x7FF];

			// MSB-first bit within the 8-pixel cell
			int bit = 7 - (x & 7);
			int pen = (video_data >> bit) & 1;

			// 1) swap x/y when calling plot_pixel to counter the current backend
		    // 2) for B/W: use pen directly (0=bg, 1=fg)
			plot_pixel(main_bitmap, x, y, Machine->pens[pen]);
		}
	}
}


READ_HANDLER(vicdual_videoram_r)
{
	return videoram_r(address);
}

WRITE_HANDLER(vicdual_videoram_w)
{
	videoram_w(address, data);
}

// Video Section End ----------------------------


WRITE_HANDLER(vicdual_ram_w)
{
	vicdual_ram[address] = data;
}

READ_HANDLER(vicdual_ram_r)
{
	return vicdual_ram[address];
}

void vicdual_interrupt()
{}

PORT_READ_HANDLER(vicdual_ip_port_0_r)
{
	return  input_port_0_r(0);
}

PORT_READ_HANDLER(vicdual_ip_port_1_r)
{
	// --- 1) Compute current scanline based on exact cycles (CPU 0 drives video) ---
	const int scan = aae_cpu_getscanline();   // 0..LINES_PER_FRAME-1
	const int bit64V = (scan >> 6) & 1;              // toggle every 64 lines

	// --- 2) Gate a full redraw on the *rising* edge of VBLANK ---
	// VBLANK region start; 240 works well for 256 visible lines
	const int vblank = (scan >= 256); // (256 seems to be smoother)

	// Edge detector + reentrancy guard (static = per-process, safe across calls)
	static int last_vblank = 0;
	static int in_refresh = 0;

	if (vblank && !last_vblank && !in_refresh) {
		in_refresh = 1;                 // guard against re-entry
		vicdual_vh_screenrefresh();    // or vicdual_vh_screenrefresh()
		in_refresh = 0;
	}
	last_vblank = vblank;

	// --- 3) Return the port value with bit 0 = 64V (like original MAME) ---
	return (input_port_1_r(0) & 0xFE) | bit64V;
}

MEM_READ(vicdual_readmem)
MEM_ADDR(0x0000, 0x7fff, MRA_ROM)
MEM_ADDR(0x8000, 0x83ff, vicdual_videoram_r)
MEM_ADDR(0x8400, 0x87ff, vicdual_ram_r)
MEM_ADDR(0x8800, 0x8fff, vicdual_characterram_r)
MEM_ADDR(0x9000, 0x93ff, vicdual_videoram_r)
MEM_ADDR(0x9400, 0x97ff, vicdual_ram_r)
MEM_ADDR(0x9800, 0x9fff, vicdual_characterram_r)
MEM_ADDR(0xa000, 0xa3ff, vicdual_videoram_r)
MEM_ADDR(0xa400, 0xa7ff, vicdual_ram_r)
MEM_ADDR(0xa800, 0xafff, vicdual_characterram_r)
MEM_ADDR(0xb000, 0xb3ff, vicdual_videoram_r)
MEM_ADDR(0xb400, 0xb7ff, vicdual_ram_r)
MEM_ADDR(0xb800, 0xbfff, vicdual_characterram_r)
MEM_ADDR(0xc000, 0xc3ff, vicdual_videoram_r)
MEM_ADDR(0xc400, 0xc7ff, vicdual_ram_r)
MEM_ADDR(0xc800, 0xcfff, vicdual_characterram_r)
MEM_ADDR(0xd000, 0xd3ff, vicdual_videoram_r)
MEM_ADDR(0xd400, 0xd7ff, vicdual_ram_r)
MEM_ADDR(0xd800, 0xdfff, vicdual_characterram_r)
MEM_ADDR(0xe000, 0xe3ff, vicdual_videoram_r)
MEM_ADDR(0xe400, 0xe7ff, vicdual_ram_r)
MEM_ADDR(0xe800, 0xefff, vicdual_characterram_r)
MEM_ADDR(0xf000, 0xf3ff, vicdual_videoram_r)
MEM_ADDR(0xf400, 0xf7ff, vicdual_ram_r)
MEM_ADDR(0xf800, 0xffff, vicdual_characterram_r)
MEM_END

MEM_WRITE(vicdual_writemem)
MEM_ADDR(0x0000, 0x7fff, MWA_ROM)
MEM_ADDR(0x8000, 0x83ff, vicdual_videoram_w)
MEM_ADDR(0x8400, 0x87ff, vicdual_ram_w)           // &vicdual_ram
MEM_ADDR(0x8800, 0x8fff, vicdual_characterram_w)  // &vicdual_characterram
MEM_ADDR(0x9000, 0x93ff, vicdual_videoram_w)
MEM_ADDR(0x9400, 0x97ff, vicdual_ram_w)           // &vicdual_ram
MEM_ADDR(0x9800, 0x9fff, vicdual_characterram_w)  // &vicdual_characterram
MEM_ADDR(0xa000, 0xa3ff, vicdual_videoram_w)
MEM_ADDR(0xa400, 0xa7ff, vicdual_ram_w)
MEM_ADDR(0xa800, 0xafff, vicdual_characterram_w)
MEM_ADDR(0xb000, 0xb3ff, vicdual_videoram_w)
MEM_ADDR(0xb400, 0xb7ff, vicdual_ram_w)
MEM_ADDR(0xb800, 0xbfff, vicdual_characterram_w)
MEM_ADDR(0xc000, 0xc3ff, vicdual_videoram_w)
MEM_ADDR(0xc400, 0xc7ff, vicdual_ram_w)
MEM_ADDR(0xc800, 0xcfff, vicdual_characterram_w)
MEM_ADDR(0xd000, 0xd3ff, vicdual_videoram_w)
MEM_ADDR(0xd400, 0xd7ff, vicdual_ram_w)
MEM_ADDR(0xd800, 0xdfff, vicdual_characterram_w)
MEM_ADDR(0xe000, 0xe3ff, vicdual_videoram_w)
MEM_ADDR(0xe400, 0xe7ff, vicdual_ram_w)
MEM_ADDR(0xe800, 0xefff, vicdual_characterram_w)
MEM_ADDR(0xf000, 0xf3ff, vicdual_videoram_w)
MEM_ADDR(0xf400, 0xf7ff, vicdual_ram_w)
MEM_ADDR(0xf800, 0xffff, vicdual_characterram_w)
MEM_END


PORT_READ(vicdual_readport)
PORT_ADDR(0x01, 0x01, vicdual_ip_port_0_r)
PORT_ADDR(0x08, 0x08, vicdual_ip_port_1_r)
PORT_END

PORT_WRITE(vicdual_writeport)
PORT_ADDR(0x04, 0x04, depthch_sh_port1_w)
PORT_ADDR(0x40, 0x40, vicdual_palette_bank_w)
PORT_END

void run_vicdual()
{	
	watchdog_reset_w(0, 0, 0);
}

void end_vicdual()
{
	LOG_DEBUG("VICDUAL VH STOP CALLED)");
	generic_vh_stop();
}

int init_vicdual()
{
	LOG_INFO("vicdual init called");
	//init8080(vicdual_readmem, vicdual_writemem, vicdual_readport, vicdual_writeport, CPU0);
	//init_z80(vicdual_readmem, vicdual_writemem, vicdual_readport, vicdual_writeport, CPU0);
	videoram_size = 0x400;
	
	videoram = (unsigned char*)calloc(0x400, 1);
	vicdual_ram = (unsigned char*)calloc(0x400, 1);  // small work RAM
	vicdual_characterram = (unsigned char*)calloc(0x800, 1);  // 256 chars * 8 bytes

	 // Mirror the 0x0000-0x3FFF ROMs to 0x4000-0x7FFF
	unsigned char* RAM = Machine->memory_region[CPU0];
	memcpy(&RAM[0x4000], &RAM[0x0000], 0x4000);
	
	generic_vh_start();
	//cpu_reset(0);
	return 0;
}


INPUT_PORTS_START(depthch)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_DIPNAME(0x30, 0x30, "Coinage")
PORT_DIPSETTING(0x00, "4 Coins/1 Credit")
PORT_DIPSETTING(0x10, "3 Coins/1 Credit")
PORT_DIPSETTING(0x20, "2 Coins/1 Credit")
PORT_DIPSETTING(0x30, "1 Coin/1 Credit")
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN)	/* probably unused */

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_VBLANK)
PORT_BIT(0x7e, IP_ACTIVE_LOW, IPT_UNKNOWN)	/* probably unused */
//PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_COIN1 | IPF_IMPULSE | IPF_RESETCPU, IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT, 30)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1 | IPF_RESETCPU, 30)
INPUT_PORTS_END

ROM_START(depthch)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("50a", 0x0000, 0x0400, CRC(56c5ffed) SHA1(f1e6cc322da93615d59850b3225a50f06fe58259))
ROM_LOAD("51a", 0x0400, 0x0400, CRC(695eb81f) SHA1(f2491b8b9ce2dbb6d2606dcfaeb8658671f25400))
ROM_LOAD("52", 0x0800, 0x0400, CRC(aed0ba1b) SHA1(cb7473e6b3c192953ae1832ab444545ddd85babb))
ROM_LOAD("53", 0x0c00, 0x0400, CRC(2ccbd2d0) SHA1(76d8459bbad709666ce0c0be51f1d09e091983a2))
ROM_LOAD("54a", 0x1000, 0x0400, CRC(1b7f6a43) SHA1(08d7864378b012a735eac4968f4dd86e36dc9d8d))
ROM_LOAD("55a", 0x1400, 0x0400, CRC(9fc2eb41) SHA1(95a1684da346709908cd66bec06acfaeead596cf))

//ROM_REGION(0x0040, REGION_USER1, 0)	/* misc PROMs, but no color so don't use REGION_PROMS! */
//ROM_LOAD("316-0043.u87", 0x0000, 0x0020, CRC(e60a7960) SHA1(b8b8716e859c57c35310efc4594262afedb84823))	/* control PROM */
//ROM_LOAD("316-0042.u88", 0x0020, 0x0020, CRC(a1506b9d) SHA1(037c3db2ea40eca459e8acba9d1506dd28d72d10))	/* sequence PROM */
ROM_END

// Depth Charge
AAE_DRIVER_BEGIN(drv_depthch, "depthch", "Depth Charge")
AAE_DRIVER_ROM(rom_depthch)
AAE_DRIVER_FUNCS(&init_vicdual, &run_vicdual, &end_vicdual)
AAE_DRIVER_INPUT(input_ports_depthch)
AAE_DRIVER_SAMPLES(depthch_samples)
AAE_DRIVER_ART(depthch_art)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     1933560,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &vicdual_interrupt,
		/*r8*/       vicdual_readmem,
		/*w8*/       vicdual_writemem,
		/*pr*/       vicdual_readport,
		/*pw*/       vicdual_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_BW | VIDEO_SUPPORTS_DIRTY | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 255)
AAE_DRIVER_RASTER(vicdual_gfxdecodeinfo, 64, 64, vicdual_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_depthch)