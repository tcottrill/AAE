#include "pacman.h"

#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "timer.h"

static UINT8 interrupt_enable_1 = 1;
static int flipscreen = 0;
static int pacintvect = 0xfa;
static int pacintenable = 1;
static int mspac_activate = 0;
extern unsigned char* pengo_soundregs;
static int gfx_bank;
static int xoffsethack;

/// // Video Settings
const rectangle visible_area =
{
 0,
 224,
 0,
 288
};

static struct rectangle spritevisiblearea =
{
	0, 28 * 8 - 1,
	2 * 8, 34 * 8 - 1
};

static struct namco_interface pacman_namco_interface =
{
	3072000 / 32,	// sample rate
	3,			// number of voices
	32,			// gain adjustment
	245			// playback volume
};

static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 }, /* characters are rotated 90 degrees */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },	/* bits are packed in groups of four */
	16 * 8	/* every char takes 16 bytes */
};
static struct GfxLayout spritelayout =
{
	16,16,	/* 16*16 sprites */
	64,	/* 64 sprites */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
			7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
			24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3 },
	64 * 8	/* every sprite takes 64 bytes */
};

struct GfxDecodeInfo pacman_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x1000, &spritelayout, 0, 32 },
	{ -1 } /* end of array */
};

void pacman_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	LOG_INFO("INIT: Pacman Color Prom init");

	for (i = 0; i < Machine->drv->total_colors; i++)
	{
		int bit0, bit1, bit2;

		// red component
		bit0 = (*color_prom >> 0) & 0x01;
		bit1 = (*color_prom >> 1) & 0x01;
		bit2 = (*color_prom >> 2) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		// green component
		bit0 = (*color_prom >> 3) & 0x01;
		bit1 = (*color_prom >> 4) & 0x01;
		bit2 = (*color_prom >> 5) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		// blue component
		bit0 = 0;
		bit1 = (*color_prom >> 6) & 0x01;
		bit2 = (*color_prom >> 7) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		color_prom++;
	}

	// color_prom now points to the beginning of the lookup table
	// skip over the 0x10 empty entries in the color prom.
	color_prom += 0x10;
	// character lookup table
	// sprites use the same color lookup table as characters
	for (i = 0; i < TOTAL_COLORS(0); i++)
		COLOR(0, i) = *(color_prom++);
}

/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void pengo_vh_screenrefresh()// struct osd_bitmap* bitmap)
{
	int i, offs;
	//LOG_INFO("1--------------------------------");
	{
		/* for every character in the Video RAM, check if it has been modified */
		/* since last time and update it accordingly. */
		for (offs = 0; offs < 0x400; offs++)
		{
			if (dirtybuffer[offs])
			{
			int sx, sy, mx, my;

			dirtybuffer[offs] = 0;

			mx = offs / 32;
			my = offs % 32;

			if (mx <= 1)
			{
				sx = 29 - my;
				sy = mx + 34;
			}
			else if (mx >= 30)
			{
				sx = 29 - my;
				sy = mx - 30;
			}
			else
			{
				sx = 29 - mx;
				sy = my + 2;
			}

			drawgfx(tmpbitmap, Machine->gfx[0], videoram[offs], colorram[offs] & 0x1f, 0, 0, 8 * sx, 8 * sy, &visible_area, TRANSPARENCY_NONE, 0);
			}
		}
		// copy the character mapped graphics
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &visible_area, TRANSPARENCY_NONE, 0);

		for (int i = 6; i > 2; i--)
		{
			drawgfx(main_bitmap, Machine->gfx[1],
				spriteram[2 * i] >> 2, spriteram[2 * i + 1],
				spriteram[2 * i] & 2, spriteram[2 * i] & 1,
				239 - spriteram_2[2 * i], 272 - spriteram_2[2 * i + 1],
				&spritevisiblearea, TRANSPARENCY_COLOR, 0);
		}

		// the first two sprites must be offset one pixel to the left
		for (i = 2; i > 0; i--)
		{
			drawgfx(main_bitmap, Machine->gfx[1],
				spriteram[2 * i] >> 2, spriteram[2 * i + 1],
				spriteram[2 * i] & 2, spriteram[2 * i] & 1,
				238 - spriteram_2[2 * i], 272 - spriteram_2[2 * i + 1],
				&spritevisiblearea, TRANSPARENCY_COLOR, 0);
		}
	}

}

int pacman_vh_start(void)
{
	LOG_INFO("-----------------------!!!!!!!!!!!!!!!!!!!PACMAN VH START CALLED--------------------------------");

	gfx_bank = 0;
	/* In the Pac Man based games (NOT Pengo) the first two sprites must be offset */
	/* one pixel to the left to get a more correct placement */
	xoffsethack = 1;
	videoram_size = 0x400;
	return generic_vh_start();
}

void pacman_interrupt()
{
	//LOG_INFO("Pacman Interrupt called");

	if (pacintenable)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
		pacintenable = 0;
	}
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void pacman_vh_stop(void)
{
	//osd_free_bitmap(tmpbitmap1);
}


//////////////////////////////////////////////////////////////
//MAIN pacman HANDLERS
//////////////////////////////////////////////////////////////
static WRITE_HANDLER_NS(pacman_leds_w) // REVIEW THIS, This is important.
{
	set_led_status(address, data & 1);
}

WRITE_HANDLER(m_colorram_w)
{
	colorram_w(address, data);
}

WRITE_HANDLER(m_videoram_w)
{
	videoram_w(address, data);
}

READ_HANDLER(MEM_BANK_READ) 
{
	unsigned char* RAM = Machine->memory_region[CPU0];

	if (mspac_activate)
	{     
		return  Machine->memory_region[CPU0][(address + psMemRead->lowAddr) + 0x10000];
	}

	else {
	
		return Machine->memory_region[CPU0][(address + psMemRead->lowAddr)];
	}
}

WRITE_HANDLER(MEM_BANK_WRITE)
{
	unsigned char* RAM = Machine->memory_region[CPU0];

	if (mspac_activate)
	{
		Machine->memory_region[CPU0][(address + psMemWrite->lowAddr) + 0x10000] = data;
	}

	else  Machine->memory_region[CPU0][(address + psMemWrite->lowAddr)] = data;
}

WRITE_HANDLER(pengo_flipscreen_w)
{
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		memset(dirtybuffer, 1, videoram_size);
	}
}

WRITE_HANDLER(mspacman_activate_rom)
{
	if (data == 1) mspac_activate =  1;
}

WRITE_HANDLER(pacintenablew)
{
	pacintenable = data & 1;
	//if (pacintenable) { LOG_INFO("pac int enable %x", pacintenable); }
}

WRITE_HANDLER(namcosndw)
{
	namco_sound_w(address & 0x1f, data);
}


void pacmanint()
{
	if (interrupt_enable_1)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
		//LOG_INFO("pacman interrupt CPU0 called");
	}
}


void run_pacman()
{
	watchdog_reset_w(0, 0, 0);
	pengo_vh_screenrefresh();
	namco_sh_update();
}
/////////////////// MS PACMAN DECODE FUNCTIONS - PLEASE MOVE ////////////////////////////////////

static unsigned char decryptd(unsigned char e)
{
	unsigned char d;

	d = (e & 0x80) >> 3;
	d |= (e & 0x40) >> 3;
	d |= (e & 0x20);
	d |= (e & 0x10) << 2;
	d |= (e & 0x08) >> 1;
	d |= (e & 0x04) >> 1;
	d |= (e & 0x02) >> 1;
	d |= (e & 0x01) << 7;

	return d;
}

static unsigned int decrypta1(unsigned int e)
{
	unsigned int d;

	d = (e & 0x800);
	d |= (e & 0x400) >> 7;
	d |= (e & 0x200) >> 2;
	d |= (e & 0x100) << 1;
	d |= (e & 0x80) << 3;
	d |= (e & 0x40) << 2;
	d |= (e & 0x20) << 1;
	d |= (e & 0x10) << 1;
	d |= (e & 0x08) << 1;
	d |= (e & 0x04);
	d |= (e & 0x02);
	d |= (e & 0x01);

	return d;
}

static unsigned int decrypta2(unsigned int e)
{
	unsigned int d;
	d = (e & 0x800);
	d |= (e & 0x400) >> 2;
	d |= (e & 0x200) >> 2;
	d |= (e & 0x100) >> 3;
	d |= (e & 0x80) << 2;
	d |= (e & 0x40) << 4;
	d |= (e & 0x20) << 1;
	d |= (e & 0x10) >> 1;
	d |= (e & 0x08) << 1;
	d |= (e & 0x04);
	d |= (e & 0x02);
	d |= (e & 0x01);

	return d;
}

void mspacman_decode(void)
{
	int i;
	
	/* CPU ROMs */

	unsigned char* RAM = Machine->memory_region[CPU0];
	for (i = 0; i < 0x1000; i++)
	{
		RAM[0x10000 + i] = RAM[0x0000 + i];
		RAM[0x11000 + i] = RAM[0x1000 + i];
		RAM[0x12000 + i] = RAM[0x2000 + i];
		RAM[0x1a000 + i] = RAM[0x2000 + i];  /*not needed but it's there*/
		RAM[0x1b000 + i] = RAM[0x3000 + i];  /*not needed but it's there*/
	}

	for (i = 0; i < 0x1000; i++)
	{
		RAM[decrypta1(i) + 0x13000] = decryptd(RAM[0xb000 + i]);	/*u7*/
		RAM[decrypta1(i) + 0x19000] = decryptd(RAM[0x9000 + i]);	/*u6*/
	}

	for (i = 0; i < 0x800; i++)
	{
		RAM[decrypta2(i) + 0x18000] = decryptd(RAM[0x8000 + i]);  	/*u5*/
		RAM[0x18800 + i] = RAM[0x19800 + i];
	}

	for (i = 0; i < 8; i++)
	{
		RAM[0x10410 + i] = RAM[0x18008 + i];
		RAM[0x108E0 + i] = RAM[0x181D8 + i];
		RAM[0x10A30 + i] = RAM[0x18118 + i];
		RAM[0x10BD0 + i] = RAM[0x180D8 + i];
		RAM[0x10C20 + i] = RAM[0x18120 + i];
		RAM[0x10E58 + i] = RAM[0x18168 + i];
		RAM[0x10EA8 + i] = RAM[0x18198 + i];

		RAM[0x11000 + i] = RAM[0x18020 + i];
		RAM[0x11008 + i] = RAM[0x18010 + i];
		RAM[0x11288 + i] = RAM[0x18098 + i];
		RAM[0x11348 + i] = RAM[0x18048 + i];
		RAM[0x11688 + i] = RAM[0x18088 + i];
		RAM[0x116B0 + i] = RAM[0x18188 + i];
		RAM[0x116D8 + i] = RAM[0x180C8 + i];
		RAM[0x116F8 + i] = RAM[0x181C8 + i];
		RAM[0x119A8 + i] = RAM[0x180A8 + i];
		RAM[0x119B8 + i] = RAM[0x181A8 + i];

		RAM[0x12060 + i] = RAM[0x18148 + i];
		RAM[0x12108 + i] = RAM[0x18018 + i];
		RAM[0x121A0 + i] = RAM[0x181A0 + i];
		RAM[0x12298 + i] = RAM[0x180A0 + i];
		RAM[0x123E0 + i] = RAM[0x180E8 + i];
		RAM[0x12418 + i] = RAM[0x18000 + i];
		RAM[0x12448 + i] = RAM[0x18058 + i];
		RAM[0x12470 + i] = RAM[0x18140 + i];
		RAM[0x12488 + i] = RAM[0x18080 + i];
		RAM[0x124B0 + i] = RAM[0x18180 + i];
		RAM[0x124D8 + i] = RAM[0x180C0 + i];
		RAM[0x124F8 + i] = RAM[0x181C0 + i];
		RAM[0x12748 + i] = RAM[0x18050 + i];
		RAM[0x12780 + i] = RAM[0x18090 + i];
		RAM[0x127B8 + i] = RAM[0x18190 + i];
		RAM[0x12800 + i] = RAM[0x18028 + i];
		RAM[0x12B20 + i] = RAM[0x18100 + i];
		RAM[0x12B30 + i] = RAM[0x18110 + i];
		RAM[0x12BF0 + i] = RAM[0x181D0 + i];
		RAM[0x12CC0 + i] = RAM[0x180D0 + i];
		RAM[0x12CD8 + i] = RAM[0x180E0 + i];
		RAM[0x12CF0 + i] = RAM[0x181E0 + i];
		RAM[0x12D60 + i] = RAM[0x18160 + i];
	}
}

/////////////////// END - MS PACMAN DECODE FUNCTIONS ///////////////////////////////////

////////////////////////////////////////////////////

MEM_READ(pacman_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)
MEM_ADDR(0x5080, 0x50bf, ip_port_2_r)
MEM_ADDR(0x8000, 0xbfff, MRA_ROM)
MEM_END

MEM_WRITE(pacman_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x43ff, m_videoram_w)
MEM_ADDR(0x4400, 0x47ff, m_colorram_w)
MEM_ADDR(0x5000, 0x5000, pacintenablew)
MEM_ADDR(0x5002, 0x5002, MWA_ROM)
MEM_ADDR(0x5003, 0x5003, pengo_flipscreen_w)
MEM_ADDR(0x5006, 0x5006, mspacman_activate_rom)	/* Not actually, just handy */
MEM_ADDR(0x5004, 0x5007, MWA_ROM)
MEM_ADDR(0x5040, 0x505f, namcosndw)
//MEM_ADDR(0x50c0, 0x50c0, ROM)
MEM_ADDR(0x8000, 0xbfff, MWA_ROM)
MEM_ADDR(0xc000, 0xc3ff, m_videoram_w)
MEM_ADDR(0xc400, 0xc7ef, m_colorram_w)
MEM_END

PORT_READ(pacman_readport)
PORT_END

PORT_WRITE(pacman_writeport)
PORT_ADDR(0x00, 0x00, interrupt_vector_w)
PORT_END

MEM_READ(mspacman_readmem)
MEM_ADDR(0x0000, 0x3fff, MEM_BANK_READ)
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)
MEM_ADDR(0x5080, 0x50bf, ip_port_2_r)
MEM_ADDR(0x50c0, 0x50ff, ip_port_3_r)
MEM_ADDR(0x8000, 0xbfff, MEM_BANK_READ)
MEM_END

MEM_WRITE(mspacman_writemem)
MEM_ADDR(0x0000, 0x3fff, MEM_BANK_WRITE)
MEM_ADDR(0x4000, 0x43ff, m_videoram_w)
MEM_ADDR(0x4400, 0x47ff, m_colorram_w)
MEM_ADDR(0x5000, 0x5000, pacintenablew)
MEM_ADDR(0x5002, 0x5002, MWA_NOP)
MEM_ADDR(0x5003, 0x5003, pengo_flipscreen_w)
MEM_ADDR(0x5004, 0x5005, pacman_leds_w )
MEM_ADDR(0x5006, 0x5006, mspacman_activate_rom)	/* Not actually, just handy */
MEM_ADDR(0x5007, 0x5007, MWA_NOP)
MEM_ADDR(0x5040, 0x505f, namcosndw)
MEM_ADDR(0x50c0, 0x50c0, watchdog_reset_w)
MEM_ADDR(0x8000, 0xbfff, MEM_BANK_WRITE)
MEM_ADDR(0xc000, 0xc3ff, m_videoram_w)
MEM_ADDR(0xc400, 0xc7ef, m_colorram_w)
MEM_ADDR(0xffff, 0xffff, MWA_NOP)
MEM_END


int init_pacman()
{
	//Init CPU's
	init_z80(pacman_readmem, pacman_writemem, pacman_readport, pacman_writeport, CPU0);

	LOG_INFO("pacman Init called");
	//FOR RASTER, VIDEORAM POINTER, SPRITERAM POINTER NEED TO BE SET MANUALLY
	LOG_INFO("INIT: Pacman Driver Init");
	videoram = &Machine->memory_region[0][0x4000]; 
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x0f;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x0f;

	//Start Namco Sound interface
	namco_sh_start(&pacman_namco_interface);
    // Start Video Interface
	pacman_vh_start();

	return 0;
}

int init_mspacman()
{
	LOG_INFO("INIT: MsPacman Driver Init");
	//Init CPU's
	//init_z80(mspacman_readmem, mspacman_writemem, pacman_readport, pacman_writeport, CPU0);
	mspacman_decode();
	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x0f;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x0f;

	//Start Namco Sound interface
	namco_sh_start(&pacman_namco_interface);
	// Start Video Interface
	pacman_vh_start();
	
	mspac_activate = 0;
	
	return 0;
}

void end_pacman()
{
	LOG_DEBUG("PACMAN VH STOP CALLED)");
	generic_vh_stop();
}



INPUT_PORTS_START(pacman)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, OSD_KEY_F2)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x04, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x10, "15000")
PORT_DIPSETTING(0x20, "20000")
PORT_DIPSETTING(0x30, "None")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Normal")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x80, 0x80, "Ghost Names")
PORT_DIPSETTING(0x80, "Normal")
PORT_DIPSETTING(0x00, "Alternate")

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN2")	/* FAKE */
/* This fake input port is used to get the status of the fire button */
/* and activate the speedup cheat if it is. */
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))
INPUT_PORTS_END

INPUT_PORTS_START(mspacman)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x04, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x10, "15000")
PORT_DIPSETTING(0x20, "20000")
PORT_DIPSETTING(0x30, "None")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Normal")
PORT_DIPSETTING(0x00, "Hard")
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("IN2")	/* FAKE */
/* This fake input port is used to get the status of the fire button */
/* and activate the speedup cheat if it is. */
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, OSD_JOY_FIRE1)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))
INPUT_PORTS_END


ROM_START(pacman)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("pacman.6e", 0x0000, 0x1000, CRC(c1e6ab10) SHA1(e87e059c5be45753f7e9f33dff851f16d6751181))
ROM_LOAD("pacman.6f", 0x1000, 0x1000, CRC(1a6fb2d4) SHA1(674d3a7f00d8be5e38b1fdc208ebef5a92d38329))
ROM_LOAD("pacman.6h", 0x2000, 0x1000, CRC(bcdd1beb) SHA1(8e47e8c2c4d6117d174cdac150392042d3e0a881))
ROM_LOAD("pacman.6j", 0x3000, 0x1000, CRC(817d94e3) SHA1(d4a70d56bb01d27d094d73db8667ffb00ca69cb9))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("pacman.5e", 0x0000, 0x1000, CRC(0c944964) SHA1(06ef227747a440831c9a3a613b76693d52a2f0a9))
ROM_LOAD("pacman.5f", 0x1000, 0x1000, CRC(958fedf9) SHA1(4a937ac02216ea8c96477d4a15522070507fb599))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5)) //Half the rom is empty
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(aa)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("aa.1", 0x0000, 0x1000, CRC(7b73ff28) SHA1(3b05c9ecaa418291b9b3501fbfd4a1e48be7281e))
ROM_LOAD("aa.2", 0x1000, 0x1000, CRC(848ca2fa) SHA1(d11e874a0bd0dcf88ed0781d7dc7b7d98b4ac1e8))
ROM_LOAD("aa.3", 0x2000, 0x1000, CRC(b3d3ff37) SHA1(0df28470eb70f4a84f24c2a86b4b9d338b9b6a76))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("aa.5e", 0x0000, 0x1000, CRC(e69596af) SHA1(b53ef6fce2d9fa1163f722a1a6be56085bde415c))
ROM_LOAD("aa.5f", 0x1000, 0x1000, CRC(c26ecd63) SHA1(40d618b171c7ea164384c2ded098520c77941cbc))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5)) //Half the rom is empty
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END


ROM_START(mspacman)
ROM_REGION(0x20000, REGION_CPU1, 0)
ROM_LOAD("pacman.6e", 0x0000, 0x1000, CRC(c1e6ab10) SHA1(e87e059c5be45753f7e9f33dff851f16d6751181))
ROM_LOAD("pacman.6f", 0x1000, 0x1000, CRC(1a6fb2d4) SHA1(674d3a7f00d8be5e38b1fdc208ebef5a92d38329))
ROM_LOAD("pacman.6h", 0x2000, 0x1000, CRC(bcdd1beb) SHA1(8e47e8c2c4d6117d174cdac150392042d3e0a881))
ROM_LOAD("pacman.6j", 0x3000, 0x1000, CRC(817d94e3) SHA1(d4a70d56bb01d27d094d73db8667ffb00ca69cb9))
ROM_LOAD("u5", 0x8000, 0x0800, CRC(f45fbbcd) SHA1(b26cc1c8ee18e9b1daa97956d2159b954703a0ec))
ROM_LOAD("u6", 0x9000, 0x1000, CRC(a90e7000) SHA1(e4df96f1db753533f7d770aa62ae1973349ea4cf))
ROM_LOAD("u7", 0xb000, 0x1000, CRC(c82cd714) SHA1(1d8ac7ad03db2dc4c8c18ade466e12032673f874))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("5e", 0x0000, 0x1000, CRC(5c281d01) SHA1(5e8b472b615f12efca3fe792410c23619f067845))
ROM_LOAD("5f", 0x1000, 0x1000, CRC(615af909) SHA1(fd6a1dde780b39aea76bf1c4befa5882573c2ef4))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END


// Pacman
AAE_DRIVER_BEGIN(drv_pacman, "pacman", "Pacman")
AAE_DRIVER_ROM(rom_pacman)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_pacman)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3072000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &pacman_interrupt,
		/*r8*/       pacman_readmem,
		/*w8*/       pacman_writemem,
		/*pr*/       pacman_readport,
		/*pw*/       pacman_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 224 - 1, 0, 288 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Ms. Pac-Man
AAE_DRIVER_BEGIN(drv_mspacman, "mspacman", "Ms. Pac-Man")
AAE_DRIVER_ROM(rom_mspacman)
AAE_DRIVER_FUNCS(&init_mspacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_pacman)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3072000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &pacman_interrupt,
		/*r8*/       mspacman_readmem,     // per init_mspacman()
		/*w8*/       mspacman_writemem,
		/*pr*/       pacman_readport,
		/*pw*/       pacman_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 224 - 1, 0, 288 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()
AAE_REGISTER_DRIVER(drv_pacman)
AAE_REGISTER_DRIVER(drv_mspacman)