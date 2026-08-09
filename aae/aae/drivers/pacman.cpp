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

#include "pacman.h"
#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "timer.h"
#include "pacplus_decode.h"
#include "cpu_control.h"
#include "cpu_z80.h"

static UINT8 interrupt_enable_1 = 1;
static int flipscreen = 0;
static int pacintvect = 0xfa;
static int pacintenable = 1;
static int mspac_activate = 0;
//extern unsigned char* pengo_soundregs;
static int gfx_bank;
static int xoffsethack;

ART_START(pacman_art)
ART_LOAD("pacplus.zip", "pacman_bezel_upright.png", ART_TEX, 3)
ART_END

ART_START(mspacman_art)
ART_LOAD("mspacman.zip", "mspacman_bezel.png", ART_TEX, 3)
ART_END


/// // Video Settings
const rectangle visible_area =
{
	0 * 8, 36 * 8 - 1,
	0 * 8, 28 * 8 - 1
};

static struct rectangle spritevisiblearea =
{
	2 * 8, 34 * 8 - 1,
	0 * 8, 28 * 8 - 1
};

static struct namco_interface namco_interface =
{
	3072000 / 32,	/* sample rate */
	3,			/* number of voices */
	220,		/* playback volume */
	REGION_SOUND1	/* memory region */
};

/* Layouts match the original MAME pacman driver (landscape decode); the
   system-level ORIENTATION_ROTATE_90 in the video core rotates the final
   image, same as MAME's ROT90. */
static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },	/* bits are packed in groups of four */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	16 * 8	/* every char takes 16 bytes */
};
static struct GfxLayout spritelayout =
{
	16,16,	/* 16*16 sprites */
	64,	/* 64 sprites */
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
			24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
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

	for (i = 0; i < (int)Machine->drv->total_colors; i++)
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
	int offs;

	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 1; offs > 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int mx, my, sx, sy;

			dirtybuffer[offs] = 0;
			mx = offs % 32;
			my = offs / 32;

			if (my < 2)
			{
				if (mx < 2 || mx >= 30) continue; /* not visible */
				sx = my + 34;
				sy = mx - 2;
			}
			else if (my >= 30)
			{
				if (mx < 2 || mx >= 30) continue; /* not visible */
				sx = my - 30;
				sy = mx - 2;
			}
			else
			{
				sx = mx + 2;
				sy = my - 2;
			}

			if (flipscreen)
			{
				sx = 35 - sx;
				sy = 27 - sy;
			}

			drawgfx(tmpbitmap, Machine->gfx[gfx_bank * 2],
				videoram[offs],
				colorram[offs] & 0x1f,
				flipscreen, flipscreen,
				sx * 8, sy * 8,
				&visible_area, TRANSPARENCY_NONE, 0);
		}
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &visible_area, TRANSPARENCY_NONE, 0);

	/* Draw the sprites. Note that it is important to draw them exactly in this */
	/* order, to have the correct priorities. */
	for (offs = spriteram_size - 2; offs > 2 * 2; offs -= 2)
	{
		int sx, sy;

		sx = 272 - spriteram_2[offs + 1];
		sy = spriteram_2[offs] - 31;

		drawgfx(main_bitmap, Machine->gfx[gfx_bank * 2 + 1],
			spriteram[offs] >> 2,
			spriteram[offs + 1] & 0x1f,
			spriteram[offs] & 1, spriteram[offs] & 2,
			sx, sy,
			&spritevisiblearea, TRANSPARENCY_COLOR, 0);

		/* also plot the sprite with wraparound (tunnel in Crush Roller) */
		drawgfx(main_bitmap, Machine->gfx[gfx_bank * 2 + 1],
			spriteram[offs] >> 2,
			spriteram[offs + 1] & 0x1f,
			spriteram[offs] & 1, spriteram[offs] & 2,
			sx - 256, sy,
			&spritevisiblearea, TRANSPARENCY_COLOR, 0);
	}
	/* In the Pac Man based games (NOT Pengo) the first two sprites must be offset */
	/* one pixel to the left to get a more correct placement */
	for (offs = 2 * 2; offs >= 0; offs -= 2)
	{
		int sx, sy;

		sx = 272 - spriteram_2[offs + 1];
		sy = spriteram_2[offs] - 31;

		drawgfx(main_bitmap, Machine->gfx[gfx_bank * 2 + 1],
			spriteram[offs] >> 2,
			spriteram[offs + 1] & 0x1f,
			spriteram[offs] & 1, spriteram[offs] & 2,
			sx, sy + xoffsethack,
			&spritevisiblearea, TRANSPARENCY_COLOR, 0);

		/* also plot the sprite with wraparound (tunnel in Crush Roller) */
		drawgfx(main_bitmap, Machine->gfx[gfx_bank * 2 + 1],
			spriteram[offs] >> 2,
			spriteram[offs + 1] & 0x1f,
			spriteram[offs] & 2, spriteram[offs] & 1,
			sx - 256, sy + xoffsethack,
			&spritevisiblearea, TRANSPARENCY_COLOR, 0);
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
// Start-button lamps: 0x5004 drives the 1-player lamp, 0x5005 the 2-player one.
// The offset is range-relative, so it is already the 0/1 LED index.
static WRITE_HANDLER_NS(pacman_leds_w)
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
MEM_ADDR(0x5004, 0x5005, pacman_leds_w)			/* player 1 / player 2 start lamps */
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


///////////////////////////////////////////////////////////////////////////
// MULTIPAC (Clay Cowgill multigame hardware; MAME .55 driver by XzeriX)
//
// 32 x 16K code banks live at 0x10000+ in the CPU region, selected by a
// write to 0xa000. AAE has no cpu_setbank, so a switch memcpys the selected
// 16K into 0x0000-0x3fff (plus the 8K window at 0x8000 when enabled by
// RAM[0xa007]==0) and jumps the Z80 to 0x0000. Switches only happen on game
// selection, so the copy cost is irrelevant. GFX bank select at 0xa001
// drives the same gfx_bank the pengo refresh already indexes (gfx_bank*2).
///////////////////////////////////////////////////////////////////////////

static int multipac_bankaddress = 0x10000;

static void multipac_setbanks(void)
{
	unsigned char* RAM = memory_region(REGION_CPU1);
	memcpy(&RAM[0x0000], &RAM[multipac_bankaddress], 0x4000);
	if (RAM[0xa007] == 0)
		memcpy(&RAM[0x8000], &RAM[multipac_bankaddress + 0x4000], 0x2000);
}

WRITE_HANDLER(multipac_a000_w)
{
	if (data != multipac_bankaddress)
	{
		if (data < 0x20)	/* check for valid codes */
			multipac_bankaddress = 0x10000 + ((data & 0x10) / 0x10 + (data & 0x0f) * 2) * 0x4000;
		else
			multipac_bankaddress = 0x10000;
		multipac_setbanks();
		/* MAME: cpu_set_reg(PC, 0) -- the selected game starts at its bank origin */
		m_cpu_z80[CPU0]->SetPC(0x0000);
	}
}

WRITE_HANDLER(multipac_gfxbank_w)
{
	unsigned char* RAM = memory_region(REGION_CPU1);
	if (gfx_bank != (int)data)
	{
		int i;
		for (i = 0x4000; i < 0x4400; i++)
			RAM[i] = 0x40;
		memset(dirtybuffer, 1, videoram_size);
		gfx_bank = data;
	}
}

WRITE_HANDLER(multipac_color_w)
{
	/* colours are handled by the graphics banking, but here is an exception */
	unsigned char* RAM = memory_region(REGION_CPU1);
	if (data == 2)
		RAM[0xf000] = 0;
}

// Start1 + Start2 together = return to the Multipac menu (same convention as
// the Tempest Multigame reset adapter). This is the "menu button" the original
// MAME .55 mod attempted via a fake input port and couldn't finish ("I need
// a way to force gfx_bank to be zero and to disable interrupts, and turn
// off sound") -- a full cpu_reset gives us all of that.
static void multipac_menu_reset(void)
{
	int i;
	unsigned char* RAM = memory_region(REGION_CPU1);

	RAM[0xa000] = 0;	/* clear the bank latches like init does */
	RAM[0xa001] = 0;
	multipac_bankaddress = 0x10000;
	multipac_setbanks();

	if (gfx_bank != 0)
	{
		gfx_bank = 0;
		memset(dirtybuffer, 1, videoram_size);
	}

	/* silence the namco channels the interrupted game left playing */
	for (i = 0; i < 0x20; i++)
		namco_sound_w(i, 0);

	cpu_reset(CPU0);
}

void run_multi15()
{
	// IN1: START1=0x20, START2=0x40, active low -- both held means both bits
	// clear. Edge-triggered so holding the combo resets only once.
	static int prev_held = 0;
	int held = ((readinputportbytag("IN1") & 0x60) == 0);

	if (held && !prev_held)
		multipac_menu_reset();
	prev_held = held;

	run_pacman();
}

static struct GfxDecodeInfo multipac_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x00000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x01000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x02000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x03000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x04000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x05000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x06000, &charlayout,     4 * 32, 32 }, /* Pacman Plus */
	{ REGION_GFX1, 0x07000, &spritelayout,   4 * 32, 32 }, /* Pacman Plus */
	{ REGION_GFX1, 0x08000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x09000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x0a000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x0b000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x0c000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x0d000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x0e000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x0f000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x10000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x11000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x12000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x13000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x14000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x15000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x16000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x17000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x18000, &charlayout,        0, 32 },
	{ REGION_GFX1, 0x19000, &spritelayout,      0, 32 },
	{ REGION_GFX1, 0x1a000, &charlayout,     8 * 32, 32 }, /* Crush Roller */
	{ REGION_GFX1, 0x1b000, &spritelayout,   8 * 32, 32 }, /* Crush Roller */
	{ REGION_GFX1, 0x1c000, &charlayout,    12 * 32, 32 }, /* Liz Wiz */
	{ REGION_GFX1, 0x1d000, &spritelayout,  12 * 32, 32 }, /* Liz Wiz */
	{ REGION_GFX1, 0x1e000, &charlayout,        0, 32 }, /* Unused? */
	{ REGION_GFX1, 0x1f000, &spritelayout,      0, 32 }, /* Unused? */
	{ -1 } /* end of array */
};

void multipac_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	LOG_INFO("INIT: Multipac Color Prom init");

	/* MAME's ROM_COPY entries, done here because this runs BEFORE the driver
	   init function: assemble the four 16-color palette banks at the start of
	   the PROM region from pal_7f.bin (loaded at 0x1000), ahead of the CLUT
	   at 0x0040. color_prom is this same region (osd_video passes
	   memory_region(REGION_PROMS)). */
	{
		unsigned char* PROM = memory_region(REGION_PROMS);
		memcpy(&PROM[0x0000], &PROM[0x1000], 0x10);
		memcpy(&PROM[0x0010], &PROM[0x1020], 0x10);
		memcpy(&PROM[0x0020], &PROM[0x1040], 0x10);
		memcpy(&PROM[0x0030], &PROM[0x1060], 0x10);
	}

	for (i = 0; i < (int)Machine->drv->total_colors; i++)
	{
		int bit0, bit1, bit2;

		/* red component */
		bit0 = (*color_prom >> 0) & 0x01;
		bit1 = (*color_prom >> 1) & 0x01;
		bit2 = (*color_prom >> 2) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* green component */
		bit0 = (*color_prom >> 3) & 0x01;
		bit1 = (*color_prom >> 4) & 0x01;
		bit2 = (*color_prom >> 5) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* blue component */
		bit0 = 0;
		bit1 = (*color_prom >> 6) & 0x01;
		bit2 = (*color_prom >> 7) & 0x01;
		*(palette++) = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		color_prom++;
	}

	/* color_prom now points to the beginning of the lookup table */

	/* character lookup table; sprites use the same one */
	for (i = 0; i < TOTAL_COLORS(0); i++)
		COLOR(0, i) = *(color_prom++) & 0x0f;

	color_prom += 0x80;

	/* second bank (Pacman Plus palette) */
	for (i = 0; i < TOTAL_COLORS(6); i++)
	{
		if (*color_prom & 0x0f)
			COLOR(6, i) = (*color_prom & 0x0f) + 0x10;	/* second palette bank */
		else
			COLOR(6, i) = 0;	/* preserve transparency */
		color_prom++;
	}

	color_prom += 0x80;

	/* third bank (Crush Roller palette) */
	for (i = 0; i < TOTAL_COLORS(26); i++)
	{
		if (*color_prom) COLOR(26, i) = (*color_prom & 0x0f) + 0x00;
		else COLOR(26, i) = 0;	/* preserve transparency */
		color_prom++;
	}

	color_prom += 0x80;

	/* fourth bank (Liz Wiz palette) */
	for (i = 0; i < TOTAL_COLORS(0x1c); i++)
	{
		if (*color_prom) COLOR(0x1c, i) = (*color_prom & 0x0f) + 0x30;
		else COLOR(0x1c, i) = 0;	/* preserve transparency */
		color_prom++;
	}
}

MEM_READ(multipac_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)		/* current 16K bank (copied in place) */
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)
MEM_ADDR(0x5080, 0x50bf, ip_port_2_r)
MEM_ADDR(0x50c0, 0x50ff, ip_port_3_r)
MEM_ADDR(0x8000, 0x9fff, MRA_ROM)		/* Ms. Pac-Man / LizWiz bank window */
MEM_END

MEM_WRITE(multipac_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x43ff, m_videoram_w)
MEM_ADDR(0x4400, 0x47ff, m_colorram_w)
MEM_ADDR(0x5000, 0x5000, pacintenablew)
MEM_ADDR(0x5002, 0x5002, MWA_NOP)
MEM_ADDR(0x5003, 0x5003, pengo_flipscreen_w)
MEM_ADDR(0x5004, 0x5005, pacman_leds_w)
MEM_ADDR(0x5007, 0x5007, MWA_NOP)
MEM_ADDR(0x5040, 0x505f, namcosndw)
MEM_ADDR(0x8000, 0x9fff, MWA_ROM)
MEM_ADDR(0xa000, 0xa000, multipac_a000_w)
MEM_ADDR(0xa001, 0xa001, multipac_gfxbank_w)
MEM_ADDR(0xa002, 0xa002, multipac_color_w)
MEM_ADDR(0xc000, 0xc3ff, m_videoram_w)	/* mirror, HIGH SCORE / CREDITS */
MEM_ADDR(0xc400, 0xc7ef, m_colorram_w)
MEM_ADDR(0xffff, 0xffff, MWA_NOP)		/* Eyes writes here to simplify code */
MEM_END

int init_multi15()
{
	int i;
	unsigned char* RAM;

	LOG_INFO("INIT: Multipac Driver Init");
	init_z80(multipac_readmem, multipac_writemem, pacman_readport, pacman_writeport, CPU0);

	/* palette-bank PROM assembly lives in multipac_vh_convert_color_prom,
	   which runs before this init */

	RAM = memory_region(REGION_CPU1);

	/* clear bank-swap latches and sprite RAM */
	for (i = 0; i < 8; i++)
		RAM[0xa000 + i] = 0;
	for (i = 0; i < 8; i++)
	{
		RAM[0x5060 + i * 2] = 0;
		RAM[0x5061 + i * 2] = 0;
	}

	/* menu bank in, Z80 starts at its origin (ROM_COPY of bank 0 in MAME) */
	multipac_bankaddress = 0x10000;
	multipac_setbanks();

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

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
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	//Start Namco Sound interface
	namco_sh_start(&namco_interface);
    // Start Video Interface
	pacman_vh_start();

	return 0;
}

int init_pacplus()
{
	pacplus_decode();
	init_pacman();

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
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	//Start Namco Sound interface
	namco_sh_start(&namco_interface);
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

///////////////////////////////////////////////////////////////////////////
// CRUSH ROLLER / MAKE TRAX protection (ported from MAME .55 pacman.c)
//
// Kural's protection PAL returns fixed/PC-gated values on reads from the
// DSW1/DSW2 mirror windows. AAE has no install_mem_read_handler, so these
// are wired via a dedicated readmem table (maketrax_readmem) instead of the
// plain pacman_readmem used for the rest of the pacman-hardware sets.
///////////////////////////////////////////////////////////////////////////

READ_HANDLER(maketrax_special_port2_r)
{
	int pc = cpu_getpc();
	UINT8 data = ip_port_2_r(address, psMemRead);

	if ((pc == 0x1973) || (pc == 0x2389)) return data | 0x40;

	switch (address)
	{
	case 0x01:
	case 0x04:
		data |= 0x40; break;
	case 0x05:
		data |= 0xc0; break;
	default:
		data &= 0x3f; break;
	}

	return data;
}

READ_HANDLER(maketrax_special_port3_r)
{
	int pc = cpu_getpc();

	if (pc == 0x040e) return 0x20;
	if ((pc == 0x115e) || (pc == 0x3ae2)) return 0x00;

	switch (address)
	{
	case 0x00: return 0x1f;
	case 0x09: return 0x30;
	case 0x0c: return 0x00;
	default: return 0x20;
	}
}

MEM_READ(maketrax_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)
MEM_ADDR(0x5080, 0x50bf, maketrax_special_port2_r)
MEM_ADDR(0x50c0, 0x50ff, maketrax_special_port3_r)
MEM_ADDR(0x8000, 0xbfff, MRA_ROM)
MEM_END

// Patch the protection using a copy of the opcodes, so the game's power-on
// ROM checksum test will not fail. REGION_CPU1 is allocated at twice 64K: the
// low half is the ROM as loaded and stays pristine for data reads (that is
// what the checksum routine sums), the high half is the instruction-fetch
// image and carries the patches. Patching the live ROM instead makes all four
// program ROMs report bad in the boot self-test.
static void maketrax_rom_decode(void)
{
	unsigned char* rom = Machine->memory_region[CPU0];
	int diff = memory_region_length(REGION_CPU1) / 2;

	memcpy(rom + diff, rom, diff);
	memory_set_opcode_base(CPU0, rom + diff);

	rom[0x0415 + diff] = 0xc9;
	rom[0x1978 + diff] = 0x18;
	rom[0x238e + diff] = 0xc9;
	rom[0x3ae5 + diff] = 0xe6;
	rom[0x3ae7 + diff] = 0x00;
	rom[0x3ae8 + diff] = 0xc9;
	rom[0x3aed + diff] = 0x86;
	rom[0x3aee + diff] = 0xc0;
	rom[0x3aef + diff] = 0xb0;
}

int init_maketrax()
{
	LOG_INFO("INIT: Make Trax / Crush Roller Driver Init");
	init_z80(maketrax_readmem, pacman_writemem, pacman_readport, pacman_writeport, CPU0);

	maketrax_rom_decode();

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

///////////////////////////////////////////////////////////////////////////
// EYES / MR. TNT CPU + GFX decrypt (ported from MAME .55 pacman.c init_eyes)
///////////////////////////////////////////////////////////////////////////

static void eyes_decode(unsigned char* data)
{
	int j;
	unsigned char swapbuffer[8];

	for (j = 0; j < 8; j++)
	{
		swapbuffer[j] = data[(j >> 2) + (j & 2) + ((j & 1) << 2)];
	}

	for (j = 0; j < 8; j++)
	{
		unsigned char ch = swapbuffer[j];

		data[j] = (ch & 0x80) | ((ch & 0x10) << 2) |
			(ch & 0x20) | ((ch & 0x40) >> 2) | (ch & 0x0f);
	}
}

// ROM decrypt hook: runs from Step 3b, before vh_open decodes the gfx and
// frees the DISPOSE region. By init_eyes() time REGION_GFX1 is already gone.
static void eyes_rom_decrypt()
{
	int i;
	unsigned char* RAM;

	/* CPU ROMs: data lines D3 and D5 swapped */
	RAM = Machine->memory_region[CPU0];
	for (i = 0; i < 0x4000; i++)
	{
		RAM[i] = (RAM[i] & 0xc0) | ((RAM[i] & 0x08) << 2) |
			(RAM[i] & 0x10) | ((RAM[i] & 0x20) >> 2) | (RAM[i] & 0x07);
	}

	/* Graphics ROMs: data lines D4/D6 and address lines A0/A2 swapped */
	RAM = memory_region(REGION_GFX1);
	for (i = 0; i < memory_region_length(REGION_GFX1); i += 8)
		eyes_decode(&RAM[i]);
}

int init_eyes()
{
	LOG_INFO("INIT: Eyes / Mr. TNT Driver Init");
	init_z80(pacman_readmem, pacman_writemem, pacman_readport, pacman_writeport, CPU0);

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

///////////////////////////////////////////////////////////////////////////
// PONPOKO gfx bank de-swizzle (ported from MAME .55 pacman.c init_ponpoko).
// AAE keeps chars+sprites in one REGION_GFX1 region (0x0000 chars,
// 0x1000 sprites) instead of separate GFX1/GFX2 regions, so the two loops
// below are restricted to their respective halves of that region.
///////////////////////////////////////////////////////////////////////////

// ROM decrypt hook: runs from Step 3b, before vh_open decodes the gfx and
// frees the DISPOSE region (same reason as eyes_rom_decrypt above).
static void ponpoko_rom_decrypt()
{
	int i, j;
	unsigned char* RAM;
	unsigned char temp;

	RAM = memory_region(REGION_GFX1);

	/* Characters */
	for (i = 0; i < 0x1000; i += 0x10)
	{
		for (j = 0; j < 8; j++)
		{
			temp = RAM[i + j + 0x08];
			RAM[i + j + 0x08] = RAM[i + j + 0x00];
			RAM[i + j + 0x00] = temp;
		}
	}

	/* Sprites */
	for (i = 0x1000; i < 0x2000; i += 0x20)
	{
		for (j = 0; j < 8; j++)
		{
			temp = RAM[i + j + 0x18];
			RAM[i + j + 0x18] = RAM[i + j + 0x10];
			RAM[i + j + 0x10] = RAM[i + j + 0x08];
			RAM[i + j + 0x08] = RAM[i + j + 0x00];
			RAM[i + j + 0x00] = temp;
		}
	}
}

int init_ponpoko()
{
	LOG_INFO("INIT: Ponpoko Driver Init");
	init_z80(pacman_readmem, pacman_writemem, pacman_readport, pacman_writeport, CPU0);

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

///////////////////////////////////////////////////////////////////////////
// JUMP SHOT / SHOOT THE BULL PAL descramble (ported from MAME machine/jumpshot.cpp).
// Both games use the same decode (0.193: DRIVER_INIT_MEMBER(...,jumpshot)
// is shared by jumpshot, jumpshotp and shootbul).
///////////////////////////////////////////////////////////////////////////

static UINT8 jumpshot_decrypt(int addr, UINT8 e)
{
	static const UINT8 swap_xor_table[6][9] =
	{
		{ 7,6,5,4,3,2,1,0, 0x00 },
		{ 7,6,3,4,5,2,1,0, 0x20 },
		{ 5,0,4,3,7,1,2,6, 0xa4 },
		{ 5,0,4,3,7,1,2,6, 0x8c },
		{ 2,3,1,7,4,6,0,5, 0x6e },
		{ 2,3,4,7,1,6,0,5, 0x4e }
	};
	static const int picktable[32] =
	{
		0,2,4,4,4,2,0,2,2,0,2,4,4,2,0,2,
		5,3,5,1,5,3,5,3,1,5,1,5,5,3,5,3
	};
	int method;
	const UINT8* tbl;
	UINT8 result;
	int b;

	/* pick method from bits 0 2 5 7 9 of the address */
	method = picktable[
		(addr & 0x001) |
			((addr & 0x004) >> 1) |
			((addr & 0x020) >> 3) |
			((addr & 0x080) >> 4) |
			((addr & 0x200) >> 5)];

	/* switch method if bit 11 of the address is set */
	if ((addr & 0x800) == 0x800)
		method ^= 1;

	tbl = swap_xor_table[method];

	result = 0;
	for (b = 0; b < 8; b++)
		if (e & (1 << tbl[b])) result |= (1 << (7 - b));

	return result ^ tbl[8];
}

static void jumpshot_decode(void)
{
	int i;
	unsigned char* RAM = Machine->memory_region[CPU0];

	for (i = 0; i < 0x4000; i++)
		RAM[i] = jumpshot_decrypt(i, RAM[i]);
}

int init_jumpshot()
{
	LOG_INFO("INIT: Jump Shot / Shoot the Bull Driver Init");
	init_z80(pacman_readmem, pacman_writemem, pacman_readport, pacman_writeport, CPU0);

	jumpshot_decode();

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

///////////////////////////////////////////////////////////////////////////
// SUPER ABC (Two-Bit Score Pac-Man multigame kit)
//
// Real hardware banks a Z80 CPU socket adapter: an 8 x 64K ROM
// (superabc.u14) selected by the top nibble of a write to 0x5006. Each 64K
// bank supplies three windows: +0x0000 -> CPU 0x0000-0x3fff, +0x4000 ->
// CPU 0x8000-0x9fff, +0xa000 -> CPU 0xa000-0xbfff (see MAME's
// superabc_bank_w / the .55 mod's superabc_gfxbank_w). AAE has no
// cpu_setbank, so (like multipac) the u14 image is loaded high in the CPU1
// region and the selected bank is memcpy'd into the three live windows on
// every bank-select write. The low nibble of the same write also selects
// the character/sprite bank in superabc_gfxdecodeinfo below (gfx_bank is
// the shared index the pengo refresh already uses via gfx_bank*2).
///////////////////////////////////////////////////////////////////////////

static int superabc_bankaddress = 0x10000;	/* offset of bank 0 within REGION_CPU1 */

static void superabc_setbanks(void)
{
	unsigned char* RAM = memory_region(REGION_CPU1);
	memcpy(&RAM[0x0000], &RAM[superabc_bankaddress], 0x4000);
	memcpy(&RAM[0x8000], &RAM[superabc_bankaddress + 0x4000], 0x2000);
	memcpy(&RAM[0xa000], &RAM[superabc_bankaddress + 0xa000], 0x2000);
}

WRITE_HANDLER(superabc_gfxbank_w)
{
	int bank = (data / 0x10) & 7;
	int bankaddress = 0x10000 + bank * 0x10000;

	if (bankaddress != superabc_bankaddress)
	{
		superabc_bankaddress = bankaddress;
		superabc_setbanks();
	}

	if (gfx_bank != bank)
	{
		memset(dirtybuffer, 1, videoram_size);
		gfx_bank = bank;
	}
}

/* 8 char/sprite bank pairs decoded directly out of the raw 0x20000 char ROM
   (u1.rom / char5e5f.u1); offsets copied verbatim from the .55 mod's
   superabc_gfxdecodeinfo -- no gfx descrambling is required with this
   layout. */
static struct GfxDecodeInfo superabc_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x00000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x01000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x10000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x11000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x08000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x09000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x18000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x19000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x04000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x05000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x14000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x15000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x0c000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x0d000, &spritelayout, 0, 32 },
	{ REGION_GFX1, 0x1c000, &charlayout,   0, 32 },
	{ REGION_GFX1, 0x1d000, &spritelayout, 0, 32 },
	{ -1 } /* end of array */
};

MEM_READ(superabc_readmem)
MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
MEM_ADDR(0x5000, 0x503f, ip_port_0_r)
MEM_ADDR(0x5040, 0x507f, ip_port_1_r)
MEM_ADDR(0x5080, 0x50bf, ip_port_2_r)
MEM_ADDR(0x50c0, 0x50ff, ip_port_3_r)
MEM_ADDR(0x8000, 0x9fff, MRA_ROM)
MEM_ADDR(0xa000, 0xbfff, MRA_ROM)
MEM_END

MEM_WRITE(superabc_writemem)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x4000, 0x43ff, m_videoram_w)
MEM_ADDR(0x4400, 0x47ff, m_colorram_w)
MEM_ADDR(0x5000, 0x5000, pacintenablew)
MEM_ADDR(0x5002, 0x5002, MWA_NOP)
MEM_ADDR(0x5003, 0x5003, pengo_flipscreen_w)
MEM_ADDR(0x5004, 0x5005, pacman_leds_w)
MEM_ADDR(0x5006, 0x5006, superabc_gfxbank_w)
MEM_ADDR(0x5007, 0x5007, MWA_NOP)
MEM_ADDR(0x5040, 0x505f, namcosndw)
MEM_ADDR(0x8000, 0xbfff, MWA_ROM)
MEM_ADDR(0xc000, 0xc3ff, m_videoram_w)
MEM_ADDR(0xc400, 0xc7ef, m_colorram_w)
MEM_ADDR(0xffff, 0xffff, MWA_NOP)
MEM_END

int init_superabc()
{
	LOG_INFO("INIT: Super ABC Driver Init");
	init_z80(superabc_readmem, superabc_writemem, pacman_readport, pacman_writeport, CPU0);

	/* bank 0 in place at boot, matching real hardware's default bank1/2/3
	   pointers before the first write to 0x5006 */
	superabc_bankaddress = 0x10000;
	superabc_setbanks();

	videoram = &Machine->memory_region[0][0x4000];
	colorram = &Machine->memory_region[0][0x4400];
	videoram_size = 0x400;
	spriteram = &Machine->memory_region[0][0x4ff0];
	spriteram_size = 0x10;
	spriteram_2 = &Machine->memory_region[0][0x5060];
	spriteram_2_size = 0x10;

	namco_sh_start(&namco_interface);
	pacman_vh_start();

	return 0;
}

INPUT_PORTS_START(pacman)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, IP_JOY_NONE)
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
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, IP_JOY_NONE)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))
INPUT_PORTS_END

INPUT_PORTS_START(mspacman)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, IP_JOY_NONE)
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
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, IP_JOY_NONE)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x01, DEF_STR(On))
INPUT_PORTS_END

INPUT_PORTS_START(maketrax)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection */
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection */

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x08, "5")
PORT_DIPSETTING(0x0c, "6")
PORT_DIPNAME(0x10, 0x10, "First Pattern")
PORT_DIPSETTING(0x10, "Easy")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x20, 0x20, "Teleport Holes")
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection */

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(mbrush)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */

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
PORT_DIPSETTING(0x0c, "4")
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(paintrlr)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_DIPNAME(0x10, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x10, DEF_STR(Cocktail))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x08, "5")
PORT_DIPSETTING(0x0c, "6")
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)	/* Protection in Make Trax */

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(ponpoko)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

/* The 2nd player controls are used even in upright mode */
PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x01, "10000")
PORT_DIPSETTING(0x02, "30000")
PORT_DIPSETTING(0x03, "50000")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Unknown))
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x04, "1")
PORT_DIPSETTING(0x08, "2")
PORT_DIPSETTING(0x0c, "3")
PORT_DIPNAME(0x30, 0x20, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x10, "3")
PORT_DIPSETTING(0x20, "4")
PORT_DIPSETTING(0x30, "5")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Cabinet))
PORT_DIPSETTING(0x40, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW 2 */
PORT_DIPNAME(0x0f, 0x01, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, "A 3/1 B 3/1")
PORT_DIPSETTING(0x0e, "A 3/1 B 1/2")
PORT_DIPSETTING(0x0f, "A 3/1 B 1/4")
PORT_DIPSETTING(0x02, "A 2/1 B 2/1")
PORT_DIPSETTING(0x0d, "A 2/1 B 1/1")
PORT_DIPSETTING(0x07, "A 2/1 B 1/3")
PORT_DIPSETTING(0x0b, "A 2/1 B 1/5")
PORT_DIPSETTING(0x0c, "A 2/1 B 1/6")
PORT_DIPSETTING(0x01, "A 1/1 B 1/1")
PORT_DIPSETTING(0x06, "A 1/1 B 4/5")
PORT_DIPSETTING(0x05, "A 1/1 B 2/3")
PORT_DIPSETTING(0x0a, "A 1/1 B 1/3")
PORT_DIPSETTING(0x08, "A 1/1 B 1/5")
PORT_DIPSETTING(0x09, "A 1/1 B 1/6")
PORT_DIPSETTING(0x03, "A 1/2 B 1/2")
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
INPUT_PORTS_END

INPUT_PORTS_START(eyes)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x0c, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x30, "50000")
PORT_DIPSETTING(0x20, "75000")
PORT_DIPSETTING(0x10, "100000")
PORT_DIPSETTING(0x00, "125000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Cabinet))
PORT_DIPSETTING(0x40, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(mrtnt)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x0c, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x30, "75000")
PORT_DIPSETTING(0x20, "100000")
PORT_DIPSETTING(0x10, "125000")
PORT_DIPSETTING(0x00, "150000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Cabinet))
PORT_DIPSETTING(0x40, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(lizwiz)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x08, DEF_STR(Lives))
PORT_DIPSETTING(0x0c, "2")
PORT_DIPSETTING(0x08, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x30, "75000")
PORT_DIPSETTING(0x20, "100000")
PORT_DIPSETTING(0x10, "125000")
PORT_DIPSETTING(0x00, "150000")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, "Normal")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(jumpshot)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x03, 0x01, "Time")
PORT_DIPSETTING(0x02, "2 Minutes")
PORT_DIPSETTING(0x03, "3 Minutes")
PORT_DIPSETTING(0x01, "4 Minutes")
PORT_DIPNAME(0x04, 0x04, DEF_STR(Unknown))
PORT_DIPSETTING(0x04, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Unknown))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Free_Play))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x00, "2 Players Game")
PORT_DIPSETTING(0x20, "1 Credit")
PORT_DIPSETTING(0x00, "2 Credits")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(shootbul)
PORT_START("IN0")	/* IN0 */
PORT_ANALOG(0x0f, 0x0f, IPT_TRACKBALL_X, 50, 25, 0, 0)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN3)

PORT_START("IN1")	/* IN1 */
PORT_ANALOG(0x0f, 0x0f, IPT_TRACKBALL_Y | IPF_REVERSE, 50, 25, 0, 0)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_DIPNAME(0x80, 0x80, DEF_STR(Unknown))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")	/* DSW 1 */
PORT_DIPNAME(0x07, 0x07, "Time")
PORT_DIPSETTING(0x01, "Short")
PORT_DIPSETTING(0x07, "Average")
PORT_DIPSETTING(0x03, "Long")
PORT_DIPSETTING(0x05, "Longer")
PORT_DIPSETTING(0x06, "Longest")
PORT_DIPNAME(0x08, 0x08, "Title Page Sounds")
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x08, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Unknown))
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("DSW2")	/* DSW 2 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

INPUT_PORTS_START(superabc)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, IP_JOY_NONE)
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_SERVICE)

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
PORT_DIPNAME(0x01, 0x00, "DSW 2-1")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x01, "1")
PORT_DIPNAME(0x02, 0x00, "DSW 2-2")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x02, "1")
PORT_DIPNAME(0x04, 0x00, "DSW 2-3")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x04, "1")
PORT_DIPNAME(0x08, 0x00, "DSW 2-4")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x08, "1")
PORT_DIPNAME(0x10, 0x00, "DSW 2-5")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x10, "1")
PORT_DIPNAME(0x20, 0x00, "DSW 2-6")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x20, "1")
PORT_DIPNAME(0x40, 0x00, "DSW 2-7")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x40, "1")
PORT_DIPNAME(0x80, 0x00, "DSW 2-8")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x80, "1")

PORT_START("IN2")	/* FAKE */
/* This fake input port is used to get the status of the fire button */
/* and activate the speedup cheat if it is. */
PORT_BITX(0x01, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Speedup Cheat", OSD_KEY_LCONTROL, IP_JOY_NONE)
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

ROM_START(pacplus)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("pacplus.6e", 0x0000, 0x1000, CRC(d611ef68) SHA1(8531c54ca6b0de0ea4ccc34e0e801ba9847e75bc))
ROM_LOAD("pacplus.6f", 0x1000, 0x1000, CRC(c7207556) SHA1(8ba97215bdb75f0e70eb8d3223847efe4dc4fb48))
ROM_LOAD("pacplus.6h", 0x2000, 0x1000, CRC(ae379430) SHA1(4e8613d51a80cf106f883db79685e1e22541da45))
ROM_LOAD("pacplus.6j", 0x3000, 0x1000, CRC(5a6dff7b) SHA1(b956ae5d66683aab74b90469ad36b5bb361d677e))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("pacplus.5e", 0x0000, 0x1000, CRC(022c35da) SHA1(57d7d723c7b029e3415801f4ce83469ec97bb8a1))
ROM_LOAD("pacplus.5f", 0x1000, 0x1000, CRC(4de65cdd) SHA1(9c0699204484be819b77f0b212c792fe9e9fae5d))
//ROM_REGION(0x1000, REGION_GFX2, ROMREGION_DISPOSE)
//ROM_LOAD("pacplus.5f", 0x0000, 0x1000, CRC(4de65cdd) SHA1(9c0699204484be819b77f0b212c792fe9e9fae5d))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("pacplus.7f", 0x0000, 0x0020, CRC(063dd53a) SHA1(2e43b46ec3b101d1babab87cdaddfa944116ec06))
ROM_LOAD("pacplus.4a", 0x0020, 0x0100, CRC(e271a166) SHA1(cf006536215a7a1d488eebc1d8a2e2a8134ce1a6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(multi15)
ROM_REGION(0x90000, REGION_CPU1, 0)	/* menu + 32 x 16K game banks */
ROM_LOAD("multi15.bin", 0x10000, 0x80000, CRC(eb181a29))
/* bank 0 (the menu) is copied to 0x0000 by init_multi15 (MAME ROM_COPY) */

ROM_REGION(0x20000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("multipac.gfx", 0x0000, 0x20000, CRC(012fb9ec))

ROM_REGION(0x2420, REGION_PROMS, 0)
ROM_LOAD("multipac.7f", 0x1000, 0x0100, CRC(40a5c3d9))	/* 4 palette banks, assembled at 0x0000 by init */
ROM_LOAD("multipac.4a", 0x0040, 0x0400, CRC(562a66de))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END


ROM_START(puckman)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("pm1_prg1.6e", 0x0000, 0x0800, CRC(f36e88ab) SHA1(813cecf44bf5464b1aed64b36f5047e4c79ba176))
ROM_LOAD("pm1_prg2.6k", 0x0800, 0x0800, CRC(618bd9b3) SHA1(b9ca52b63a49ddece768378d331deebbe34fe177))
ROM_LOAD("pm1_prg3.6f", 0x1000, 0x0800, CRC(7d177853) SHA1(9b5ddaaa8b564654f97af193dbcc29f81f230a25))
ROM_LOAD("pm1_prg4.6m", 0x1800, 0x0800, CRC(d3e8914c) SHA1(c2f00e1773c6864435f29c8b7f44f2ef85d227d3))
ROM_LOAD("pm1_prg5.6h", 0x2000, 0x0800, CRC(6bf4f625) SHA1(afe72fdfec66c145b53ed865f98734686b26e921))
ROM_LOAD("pm1_prg6.6n", 0x2800, 0x0800, CRC(a948ce83) SHA1(08759833f7e0690b2ccae573c929e2a48e5bde7f))
ROM_LOAD("pm1_prg7.6j", 0x3000, 0x0800, CRC(b6289b26) SHA1(d249fa9cdde774d5fee7258147cd25fa3f4dc2b3))
ROM_LOAD("pm1_prg8.6p", 0x3800, 0x0800, CRC(17a88c13) SHA1(eb462de79f49b7aa8adb0cc6d31535b10550c0ce))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("pm1_chg1.5e", 0x0000, 0x0800, CRC(2066a0b7) SHA1(6d4ccc27d6be185589e08aa9f18702b679e49a4a))
ROM_LOAD("pm1_chg2.5h", 0x0800, 0x0800, CRC(3591b89d) SHA1(79bb456be6c39c1ccd7d077fbe181523131fb300))
ROM_LOAD("pm1_chg3.5f", 0x1000, 0x0800, CRC(9e39323a) SHA1(be933e691df4dbe7d12123913c3b7b7b585b7a35))
ROM_LOAD("pm1_chg4.5j", 0x1800, 0x0800, CRC(1b1d9096) SHA1(53771c573051db43e7185b1d188533056290a620))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("pm1-1.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("pm1-4.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("pm1-3.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("pm1-2.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(puckmod)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("namcopac.6e", 0x0000, 0x1000, CRC(fee263b3) SHA1(87117ba5082cd7a615b4ec7c02dd819003fbd669))
ROM_LOAD("namcopac.6f", 0x1000, 0x1000, CRC(39d1fc83) SHA1(326dbbf94c6fa2e96613dedb53702f8832b47d59))
ROM_LOAD("namcopac.6h", 0x2000, 0x1000, CRC(02083b03) SHA1(7e1945f6eb51f2e51806d0439f975f7a2889b9b8))
ROM_LOAD("npacmod.6j", 0x3000, 0x1000, CRC(7d98d5f5) SHA1(39939bcd6fb785d0d06fd29f0287158ab1267dfc))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("pacman.5e", 0x0000, 0x1000, CRC(0c944964) SHA1(06ef227747a440831c9a3a613b76693d52a2f0a9))
ROM_LOAD("pacman.5f", 0x1000, 0x1000, CRC(958fedf9) SHA1(4a937ac02216ea8c96477d4a15522070507fb599))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(pacmod)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("pacmanh.6e", 0x0000, 0x1000, CRC(3b2ec270) SHA1(48fc607ad8d86249948aa377c677ae44bb8ad3da))
ROM_LOAD("pacman.6f", 0x1000, 0x1000, CRC(1a6fb2d4) SHA1(674d3a7f00d8be5e38b1fdc208ebef5a92d38329))
ROM_LOAD("pacmanh.6h", 0x2000, 0x1000, CRC(18811780) SHA1(ab34acaa3dbcafe8b20c2197f36641e471984487))
ROM_LOAD("pacmanh.6j", 0x3000, 0x1000, CRC(5c96a733) SHA1(22ae15a6f088e7296f77c7487a350c4bd102f00e))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("pacmanh.5e", 0x0000, 0x1000, CRC(299fb17a) SHA1(ad97adc2122482a9018bacd137df9d8f409ddf85))
ROM_LOAD("pacman.5f", 0x1000, 0x1000, CRC(958fedf9) SHA1(4a937ac02216ea8c96477d4a15522070507fb599))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

// Ms. Pac-Man bootleg: NOT encrypted, plain 6-ROM load on stock pacman hardware/memory map.
ROM_START(mspacmab)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("boot1", 0x0000, 0x1000, CRC(d16b31b7) SHA1(bc2247ec946b639dd1f00bfc603fa157d0baaa97))
ROM_LOAD("boot2", 0x1000, 0x1000, CRC(0d32de5e) SHA1(13ea0c343de072508908be885e6a2a217bbb3047))
ROM_LOAD("boot3", 0x2000, 0x1000, CRC(1821ee0b) SHA1(5ea4d907dbb2690698db72c4e0b5be4d3e9a7786))
ROM_LOAD("boot4", 0x3000, 0x1000, CRC(165a9dd8) SHA1(3022a408118fa7420060e32a760aeef15b8a96cf))
ROM_LOAD("boot5", 0x8000, 0x1000, CRC(8c3e6de6) SHA1(fed6e9a2b210b07e7189a18574f6b8c4ec5bb49b))
ROM_LOAD("boot6", 0x9000, 0x1000, CRC(368cb165) SHA1(387010a0c76319a1eab61b54c9bcb5c66c4b67a1))

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

ROM_START(crush)
ROM_REGION(0x20000, REGION_CPU1, 0)	/* 64k for code + 64k for opcode copy to hack protection */
ROM_LOAD("crushkrl.6e", 0x0000, 0x1000, CRC(a8dd8f54) SHA1(4e3a973ea74a9e145c6997513b98fc80aa478442))
ROM_LOAD("crushkrl.6f", 0x1000, 0x1000, CRC(91387299) SHA1(3ad8c28e02c45667e32860953b157832445a82c8))
ROM_LOAD("crushkrl.6h", 0x2000, 0x1000, CRC(d4455f27) SHA1(53f8ffc28be664fa8a2d756b4c70045a3f041bea))
ROM_LOAD("crushkrl.6j", 0x3000, 0x1000, CRC(d59fc251) SHA1(024605e4485b0ac826217256e5356ed9a6c8ef34))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("maketrax.5e", 0x0000, 0x1000, CRC(91bad2da) SHA1(096197d0cb6d55bf72b5be045224f4bd6a9cfa1b))
ROM_LOAD("maketrax.5f", 0x1000, 0x1000, CRC(aea79f55) SHA1(279021e6771dfa5bd0b7c557aae44434286d91b7))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("2s140.4a", 0x0020, 0x0100, CRC(63efb927) SHA1(5c144a613fc4960a1dfd7ead89e7fee258a63171))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(crush2)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("tp1", 0x0000, 0x0800, CRC(f276592e) SHA1(68ebb7d9f70af868d99ec42c26bc55a54ba1f22c))
ROM_LOAD("tp5a", 0x0800, 0x0800, CRC(3d302abe) SHA1(8ca5cd82d099b55e20f785489158231a1d99a430))
ROM_LOAD("tp2", 0x1000, 0x0800, CRC(25f42e70) SHA1(66de8203c364fd90e8a2b5749c2e40665b2f5830))
ROM_LOAD("tp6", 0x1800, 0x0800, CRC(98279cbe) SHA1(84b5e64bdbc25afab9b6f53e1719640e21a6feba))
ROM_LOAD("tp3", 0x2000, 0x0800, CRC(8377b4cb) SHA1(f828a177f22db9093a00c31e39e16214ce0dc6de))
ROM_LOAD("tp7", 0x2800, 0x0800, CRC(d8e76c8c) SHA1(7c3d7eb07b9256130141f71eba722f7823fd4c32))
ROM_LOAD("tp4", 0x3000, 0x0800, CRC(90b28fa3) SHA1(ff58d2dfb016397daabe2996bc3a7b63d28a4cca))
ROM_LOAD("tp8", 0x3800, 0x0800, CRC(10854e1b) SHA1(b3b9066d9a43796185c00ae12f7bb2bbf42e3a07))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tpa", 0x0000, 0x0800, CRC(c7617198) SHA1(95b204af0345163f93811cc770ee0ca2851a39c1))
ROM_LOAD("tpc", 0x0800, 0x0800, CRC(e129d76a) SHA1(c9256795c6d0929ade1f24b372dadc2a2b88d897))
ROM_LOAD("tpb", 0x1000, 0x0800, CRC(d1899f05) SHA1(dce755511b6262b984a2bca329f454892e486a09))
ROM_LOAD("tpd", 0x1800, 0x0800, CRC(d35d1caf) SHA1(65dd7861e05651485626465dc97215fed58db551))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("2s140.4a", 0x0020, 0x0100, CRC(63efb927) SHA1(5c144a613fc4960a1dfd7ead89e7fee258a63171))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(mbrush)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("mbrush.6e", 0x0000, 0x1000, CRC(750fbff7) SHA1(986d20010d4fdd4bac916ac6b3a01bcd09d695ea))
ROM_LOAD("mbrush.6f", 0x1000, 0x1000, CRC(27eb4299) SHA1(af2d7fdedcea766045fc2f20ae383024d1c35731))
ROM_LOAD("mbrush.6h", 0x2000, 0x1000, CRC(d297108e) SHA1(a5bd11f26ba82b66a93d07e8cbc838ad9bd01413))
ROM_LOAD("mbrush.6j", 0x3000, 0x1000, CRC(6fd719d0) SHA1(3de00981264cef24dc2c6277192e071144da2a88))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tpa", 0x0000, 0x0800, CRC(c7617198) SHA1(95b204af0345163f93811cc770ee0ca2851a39c1))
ROM_LOAD("mbrush.5h", 0x0800, 0x0800, CRC(c15b6967) SHA1(d8f16e2d6af5bf0f610d1e23614c531f67490da9))
ROM_LOAD("mbrush.5f", 0x1000, 0x0800, CRC(d5bc5cb8) SHA1(269b82ae2b838c72ae06bff77412f22bb779ad2e))
ROM_LOAD("tpd", 0x1800, 0x0800, CRC(d35d1caf) SHA1(65dd7861e05651485626465dc97215fed58db551))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("2s140.4a", 0x0020, 0x0100, CRC(63efb927) SHA1(5c144a613fc4960a1dfd7ead89e7fee258a63171))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(paintrlr)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("paintrlr.1", 0x0000, 0x0800, CRC(556d20b5) SHA1(c0a74def85bca108fc56726d22bbea1fc051e1ff))
ROM_LOAD("paintrlr.5", 0x0800, 0x0800, CRC(4598a965) SHA1(866dbe7c0dbca10c5d5ec3efa3c79fb1ff1c5b56))
ROM_LOAD("paintrlr.2", 0x1000, 0x0800, CRC(2da29c81) SHA1(e77f84e2f3136a116b75b40869e7f59404b0dbab))
ROM_LOAD("paintrlr.6", 0x1800, 0x0800, CRC(1f561c54) SHA1(ef1159f2203ff6b5c17e3a79f32e8cafb12a49f7))
ROM_LOAD("paintrlr.3", 0x2000, 0x0800, CRC(e695b785) SHA1(bc627a1a03d2e701fa4051acee469a4516cfb5bf))
ROM_LOAD("paintrlr.7", 0x2800, 0x0800, CRC(00e6eec0) SHA1(e98850cf6e1762d08225a95f26a26766f8fa7303))
ROM_LOAD("paintrlr.4", 0x3000, 0x0800, CRC(0fd5884b) SHA1(fa9614b625b3d71a6e9d5f883da625ad88e3eb5e))
ROM_LOAD("paintrlr.8", 0x3800, 0x0800, CRC(4900114a) SHA1(47aee5bad136c19b203958b7ddac583d45018249))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tpa", 0x0000, 0x0800, CRC(c7617198) SHA1(95b204af0345163f93811cc770ee0ca2851a39c1))
ROM_LOAD("mbrush.5h", 0x0800, 0x0800, CRC(c15b6967) SHA1(d8f16e2d6af5bf0f610d1e23614c531f67490da9))
ROM_LOAD("mbrush.5f", 0x1000, 0x0800, CRC(d5bc5cb8) SHA1(269b82ae2b838c72ae06bff77412f22bb779ad2e))
ROM_LOAD("tpd", 0x1800, 0x0800, CRC(d35d1caf) SHA1(65dd7861e05651485626465dc97215fed58db551))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("2s140.4a", 0x0020, 0x0100, CRC(63efb927) SHA1(5c144a613fc4960a1dfd7ead89e7fee258a63171))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(maketrax)
ROM_REGION(0x20000, REGION_CPU1, 0)	/* 64k for code + 64k for opcode copy to hack protection */
ROM_LOAD("maketrax.6e", 0x0000, 0x1000, CRC(0150fb4a) SHA1(ba41582d5432670654479b4bf6d938d2168858af))
ROM_LOAD("maketrax.6f", 0x1000, 0x1000, CRC(77531691) SHA1(68a450bcc8d832368d0f1cb2815cb5c03451796e))
ROM_LOAD("maketrax.6h", 0x2000, 0x1000, CRC(a2cdc51e) SHA1(80d80235cda3ce19c1dbafacf3d47b1325ad4728))
ROM_LOAD("maketrax.6j", 0x3000, 0x1000, CRC(0b4b5e0a) SHA1(621aece612df612065f776696956ef3671421fac))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("maketrax.5e", 0x0000, 0x1000, CRC(91bad2da) SHA1(096197d0cb6d55bf72b5be045224f4bd6a9cfa1b))
ROM_LOAD("maketrax.5f", 0x1000, 0x1000, CRC(aea79f55) SHA1(279021e6771dfa5bd0b7c557aae44434286d91b7))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("2s140.4a", 0x0020, 0x0100, CRC(63efb927) SHA1(5c144a613fc4960a1dfd7ead89e7fee258a63171))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(eyes)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("d7", 0x0000, 0x1000, CRC(3b09ac89) SHA1(a8f1c918da74495bb73172f39364dada38ae4713))
ROM_LOAD("e7", 0x1000, 0x1000, CRC(97096855) SHA1(10d3b164bbbe5eee86e881a1434f0c114ee8adff))
ROM_LOAD("f7", 0x2000, 0x1000, CRC(731e294e) SHA1(96c0308c146dbd85e244c4530af9ae8df78c86de))
ROM_LOAD("h7", 0x3000, 0x1000, CRC(22f7a719) SHA1(eb000b606ecedd52bebbb232e661fb1ef205f8b0))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("d5", 0x0000, 0x1000, CRC(d6af0030) SHA1(652b779533e3f00e81cc102b78d367d503b06f33))
ROM_LOAD("e5", 0x1000, 0x1000, CRC(a42b5201) SHA1(2e5cede3b6039c7bd5230de27d02aaa3f35a7b64))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s129.4a", 0x0020, 0x0100, CRC(d8d78829) SHA1(19820d1651423210083a087fb70ebea73ad34951))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(eyes2)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("g38201.7d", 0x0000, 0x1000, CRC(2cda7185) SHA1(7ec3ee9bb90e6a1d83ad3aa12fd62184e07b1399))
ROM_LOAD("g38202.7e", 0x1000, 0x1000, CRC(b9fe4f59) SHA1(2d97dc1a0458b406ca0c50d6b8bd0dbe58d21464))
ROM_LOAD("g38203.7f", 0x2000, 0x1000, CRC(d618ba66) SHA1(76d93d8bc09bafac464ebfd002869e21535a365b))
ROM_LOAD("g38204.7h", 0x3000, 0x1000, CRC(cf038276) SHA1(bcf4e129a151e2245e630cf865ce6cb009b405a5))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("g38205.5d", 0x0000, 0x1000, CRC(03b1b4c7) SHA1(a90b2fbaee2888ee4f0bcdf80a069c8594ef5ea1))
ROM_LOAD("g38206.5e", 0x1000, 0x1000, CRC(a42b5201) SHA1(2e5cede3b6039c7bd5230de27d02aaa3f35a7b64))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s129.4a", 0x0020, 0x0100, CRC(d8d78829) SHA1(19820d1651423210083a087fb70ebea73ad34951))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(mrtnt)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("tnt.1", 0x0000, 0x1000, CRC(0e836586) SHA1(5037b7c618f05bc3d6a33694729ae575b9aa7dbb))
ROM_LOAD("tnt.2", 0x1000, 0x1000, CRC(779c4c5b) SHA1(5ecac4f5b64b306c73d8f57d5260b586789b3055))
ROM_LOAD("tnt.3", 0x2000, 0x1000, CRC(ad6fc688) SHA1(e5729e4e42a5b9b3a26de8a44b3a78b49c8b1d8e))
ROM_LOAD("tnt.4", 0x3000, 0x1000, CRC(d77557b3) SHA1(689746653b1e19fbcddd0d71db2b86d1019235aa))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("tnt.5", 0x0000, 0x1000, CRC(3038cc0e) SHA1(f8f5927ea4cbfda8fa7546abd766ba2e8b020004))
ROM_LOAD("tnt.6", 0x1000, 0x1000, CRC(97634d8b) SHA1(4c0fa4bc44bbb4b4614b5cc05e811c469c0e78e8))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(lizwiz)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("6e.cpu", 0x0000, 0x1000, CRC(32bc1990) SHA1(467c9d70e07f403b6b9118aebe4e6d0abb40a5c1))
ROM_LOAD("6f.cpu", 0x1000, 0x1000, CRC(ef24b414) SHA1(12fce48008c4f9387df0c84f3b0d7c5a1b35d898))
ROM_LOAD("6h.cpu", 0x2000, 0x1000, CRC(30bed83d) SHA1(8c2458f98320c6887580c71632b544da0a582ba2))
ROM_LOAD("6j.cpu", 0x3000, 0x1000, CRC(dd09baeb) SHA1(f91447ec1f06bf95106e6872d80dcb82e1d42ffb))
ROM_LOAD("wiza", 0x8000, 0x1000, CRC(f6dea3a6) SHA1(ec0b123fd2e6de6681ca14f35fda249b2c2ec44f))
ROM_LOAD("wizb", 0x9000, 0x1000, CRC(f27fb5a8) SHA1(3ea384a1064302709d97fc16b347d3c012e90ac7))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("5e.cpu", 0x0000, 0x1000, CRC(45059e73) SHA1(c960cd5720bfa21db73e1000fe8be7d5baf2a3a1))
ROM_LOAD("5f.cpu", 0x1000, 0x1000, CRC(d2469717) SHA1(194c8f816e5ff7614b3db4f355223667105738fa))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("7f.cpu", 0x0000, 0x0020, CRC(7549a947) SHA1(4f2c3e7d6c38f0b9a90317f91feb3f86c9a0d0a5))
ROM_LOAD("4a.cpu", 0x0020, 0x0100, CRC(5fdca536) SHA1(3a09b29374031aaa3722932aff974a467b3bb201))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(jumpshot)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("6e", 0x0000, 0x1000, CRC(f00def9a) SHA1(465a7f368e61a1e6614d6eab0fa2c6319920eaa5))
ROM_LOAD("6f", 0x1000, 0x1000, CRC(f70deae2) SHA1(a8a8369e865b62cb9ed66d3de2396c6a5fced549))
ROM_LOAD("6h", 0x2000, 0x1000, CRC(894d6f68) SHA1(8693ffc29587cdd1be0b42cede53f8f450a2c7fa))
ROM_LOAD("6j", 0x3000, 0x1000, CRC(f15a108a) SHA1(db5c8394f688c6f889cadddeeae4fbca63c29a4c))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("5e", 0x0000, 0x1000, CRC(d9fa90f5) SHA1(3c37fe077a77baa802230dddbc4bb2c05985d2bb))
ROM_LOAD("5f", 0x1000, 0x1000, CRC(2ec711c1) SHA1(fcc3169f48eb7d4af533ad0169701e4230ff5a1f))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("prom.7f", 0x0000, 0x0020, CRC(872b42f3) SHA1(bbcd392ba3d2a5715e92fa0f7a7cf1e7e6e655a2))
ROM_LOAD("prom.4a", 0x0020, 0x0100, CRC(0399f39f) SHA1(e98f08da4666cab44e01acb760a1bd2fc858bc0d))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(shootbul)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("sb6e.cpu", 0x0000, 0x1000, CRC(25daa5e9) SHA1(8257de5e0e62235d05d74b53e5fc716e85cb05b9))
ROM_LOAD("sb6f.cpu", 0x1000, 0x1000, CRC(92144044) SHA1(905a354a806da47ab40577171acdac7db635d102))
ROM_LOAD("sb6h.cpu", 0x2000, 0x1000, CRC(43b7f99d) SHA1(6372763fbbca3581376204c5e58ceedd3f47fc60))
ROM_LOAD("sb6j.cpu", 0x3000, 0x1000, CRC(bc4d3bbf) SHA1(2fa15b339166b9a5bf711b58a1705bc0b9e528e2))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("sb5e.cpu", 0x0000, 0x1000, CRC(07c6c5aa) SHA1(cbe99ece795f29fdeef374cbf9b1f45ff065e803))
ROM_LOAD("sb5f.cpu", 0x1000, 0x1000, CRC(eaec6837) SHA1(ff21b0fd5381afb1ba7f5920132006ee8e6d10eb))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("7f.rom", 0x0000, 0x0020, CRC(ec578b98) SHA1(196da49cc260f967ec5f01bc3c75b11077c85998))
ROM_LOAD("4a.rom", 0x0020, 0x0100, CRC(81a6b30f) SHA1(60c767fd536c325151a2b759fdbce4ba41e0c78f))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(ponpoko)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("ppokoj1.bin", 0x0000, 0x1000, CRC(ffa3c004) SHA1(d9e3186dcd4eb94d02bd24ad56030b248721537f))
ROM_LOAD("ppokoj2.bin", 0x1000, 0x1000, CRC(4a496866) SHA1(4b8bd13e58040c30ca032b54fb47d889677e8c6f))
ROM_LOAD("ppokoj3.bin", 0x2000, 0x1000, CRC(17da6ca3) SHA1(1a57767557c13fa3d08e4451fb9fb1f7219b26ef))
ROM_LOAD("ppokoj4.bin", 0x3000, 0x1000, CRC(9d39a565) SHA1(d4835ee97c9b3c63504d8b576a11f0a3a97057ec))
ROM_LOAD("ppoko5.bin", 0x8000, 0x1000, CRC(54ca3d7d) SHA1(b54299b00573fbd6d3278586df0c12c09235615d))
ROM_LOAD("ppoko6.bin", 0x9000, 0x1000, CRC(3055c7e0) SHA1(ab3fb9c8846effdcea0569d08a84c5fa19057a8f))
ROM_LOAD("ppoko7.bin", 0xa000, 0x1000, CRC(3cbe47ca) SHA1(577c79c016be26a9fc7895cef0f30bf3f0b15097))
ROM_LOAD("ppokoj8.bin", 0xb000, 0x1000, CRC(04b63fc6) SHA1(9b86ae34aaefa2813d29a4f7b24cee40eadcc6a1))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("ppoko9.bin", 0x0000, 0x1000, CRC(b73e1a06) SHA1(f1229e804eb15827b71f0e769a8c9e496c6d1de7))
ROM_LOAD("ppoko10.bin", 0x1000, 0x1000, CRC(62069b5d) SHA1(1b58ad1c2cc2d12f4e492fdd665b726d50c80364))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

ROM_START(ponpokov)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("ppoko1.bin", 0x0000, 0x1000, CRC(49077667) SHA1(3e760cd4dbe5913e58d786caf510237ff635c775))
ROM_LOAD("ppoko2.bin", 0x1000, 0x1000, CRC(5101781a) SHA1(a82fbd2418ac7866f9463092e9dd37fd7ba9b694))
ROM_LOAD("ppoko3.bin", 0x2000, 0x1000, CRC(d790ed22) SHA1(2d32f91f6993232db40b44b35bd2503d85e5c874))
ROM_LOAD("ppoko4.bin", 0x3000, 0x1000, CRC(4e449069) SHA1(d5e6e346f80e66eb0db530de9721d9b6f22e86ae))
ROM_LOAD("ppoko5.bin", 0x8000, 0x1000, CRC(54ca3d7d) SHA1(b54299b00573fbd6d3278586df0c12c09235615d))
ROM_LOAD("ppoko6.bin", 0x9000, 0x1000, CRC(3055c7e0) SHA1(ab3fb9c8846effdcea0569d08a84c5fa19057a8f))
ROM_LOAD("ppoko7.bin", 0xa000, 0x1000, CRC(3cbe47ca) SHA1(577c79c016be26a9fc7895cef0f30bf3f0b15097))
ROM_LOAD("ppoko8.bin", 0xb000, 0x1000, CRC(b39be27d) SHA1(c299d22d26da68bec8fc53c898523135ec4016fa))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("ppoko9.bin", 0x0000, 0x1000, CRC(b73e1a06) SHA1(f1229e804eb15827b71f0e769a8c9e496c6d1de7))
ROM_LOAD("ppoko10.bin", 0x1000, 0x1000, CRC(62069b5d) SHA1(1b58ad1c2cc2d12f4e492fdd665b726d50c80364))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(2fc650bd) SHA1(8d0268dee78e47c712202b0ec4f1f51109b1f2a5))
ROM_LOAD("82s126.4a", 0x0020, 0x0100, CRC(3eb3a8e4) SHA1(19097b5f60d1030f8b82d9f1d3a241f93e5c75d6))

ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound PROMs */
ROM_LOAD("82s126.1m", 0x0000, 0x0100, CRC(a9cc86bf) SHA1(bbcec0570aeceb582ff8238a4bc8546a23430081))
ROM_LOAD("82s126.3m", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END

// Super ABC (Two-Bit Score Pac-Man multigame kit). CRC/SHA1 from HBMAME
// (filenames+SHA1 unavailable in the .55 mod source); u14 is loaded at
// 0x10000 (not 0x0000, as in the real hardware/.55 layout) so the low
// 0x10000 of REGION_CPU1 is free to serve as the memcpy'd live bank
// windows -- see superabc_setbanks()/superabc_gfxbank_w above.
ROM_START(superabc)
ROM_REGION(0x90000, REGION_CPU1, 0)	/* 8 banks of 64k for code, offset by the live-bank window */
ROM_LOAD("superabc.u14", 0x10000, 0x80000, CRC(a560efe6) SHA1(c7d43cc3bb3b1b10d06403462276231bfc8542dd))

ROM_REGION(0x20000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("char5e5f.u1", 0x0000, 0x20000, CRC(45caace0) SHA1(f850bd09ec68b0263ac8b30ae38c3878c7978ace))

ROM_REGION(0x0120, REGION_PROMS, 0)
ROM_LOAD("82s123.7f", 0x0000, 0x0020, CRC(3a188666) SHA1(067386e477ce48bbde3cf71f744a78a42238d236))
ROM_LOAD("82s129.4a", 0x0020, 0x0100, CRC(4382c049) SHA1(5e535b1a6852260f38ae1e5cd57290a85cb6927f))

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
AAE_DRIVER_ART(pacman_art)

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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

// Ms. Pac-Man
AAE_DRIVER_BEGIN(drv_mspacman, "mspacman", "Ms. Pac-Man")
AAE_DRIVER_ROM(rom_mspacman)
AAE_DRIVER_FUNCS(&init_mspacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_pacman)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(mspacman_art)

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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")

AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// PacPlus
AAE_DRIVER_BEGIN(drv_pacplus, "pacplus", "Pacman Plus")
AAE_DRIVER_ROM(rom_pacplus)
AAE_DRIVER_FUNCS(&init_pacplus, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_pacman)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(pacman_art)

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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")

AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Multipac v1.5 (Clay Cowgill multigame)
AAE_DRIVER_BEGIN(drv_multi15, "multi15", "Multipac (v1.5)")
AAE_DRIVER_ROM(rom_multi15)
AAE_DRIVER_FUNCS(&init_multi15, &run_multi15, &end_pacman)
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
		/*r8*/       multipac_readmem,
		/*w8*/       multipac_writemem,
		/*pr*/       pacman_readport,
		/*pw*/       pacman_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(multipac_gfxdecodeinfo, 64, 4 * 128, multipac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// PuckMan (Japan set 1) - parent set
AAE_DRIVER_BEGIN(drv_puckman, "puckman", "PuckMan (Japan set 1)")
AAE_DRIVER_ROM(rom_puckman)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// PuckMan (harder?)
AAE_DRIVER_BEGIN(drv_puckmod, "puckmod", "PuckMan (harder?)")
AAE_DRIVER_ROM(rom_puckmod)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Pac-Man (Midway, harder)
AAE_DRIVER_BEGIN(drv_pacmod, "pacmod", "Pac-Man (Midway, harder)")
AAE_DRIVER_ROM(rom_pacmod)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Ms. Pac-Man (bootleg) - NOT encrypted, plain load on stock pacman hardware
AAE_DRIVER_BEGIN(drv_mspacmab, "mspacmab", "Ms. Pac-Man (bootleg)")
AAE_DRIVER_ROM(rom_mspacmab)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_mspacman)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Crush Roller (Kural Samno) - maketrax protection
AAE_DRIVER_BEGIN(drv_crush, "crush", "Crush Roller (Kural Samno)")
AAE_DRIVER_ROM(rom_crush)
AAE_DRIVER_FUNCS(&init_maketrax, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_maketrax)
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
		/*r8*/       maketrax_readmem,
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Crush Roller (Kural Esco - bootleg?)
AAE_DRIVER_BEGIN(drv_crush2, "crush2", "Crush Roller (Kural Esco - bootleg?)")
AAE_DRIVER_ROM(rom_crush2)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_maketrax)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Magic Brush (bootleg)
AAE_DRIVER_BEGIN(drv_mbrush, "mbrush", "Magic Brush")
AAE_DRIVER_ROM(rom_mbrush)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_mbrush)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Paint Roller (bootleg)
AAE_DRIVER_BEGIN(drv_paintrlr, "paintrlr", "Paint Roller")
AAE_DRIVER_ROM(rom_paintrlr)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_paintrlr)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Make Trax - maketrax protection, ROT270
AAE_DRIVER_BEGIN(drv_maketrax, "maketrax", "Make Trax")
AAE_DRIVER_ROM(rom_maketrax)
AAE_DRIVER_FUNCS(&init_maketrax, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_maketrax)
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
		/*r8*/       maketrax_readmem,
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Eyes (Digitrex Techstar)
AAE_DRIVER_BEGIN(drv_eyes, "eyes", "Eyes (Digitrex Techstar)")
AAE_DRIVER_ROM(rom_eyes)
AAE_DRIVER_FUNCS(&init_eyes, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_eyes)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&eyes_rom_decrypt)
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Eyes (Techstar Inc.)
AAE_DRIVER_BEGIN(drv_eyes2, "eyes2", "Eyes (Techstar Inc.)")
AAE_DRIVER_ROM(rom_eyes2)
AAE_DRIVER_FUNCS(&init_eyes, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_eyes)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&eyes_rom_decrypt)
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Mr. TNT
AAE_DRIVER_BEGIN(drv_mrtnt, "mrtnt", "Mr. TNT")
AAE_DRIVER_ROM(rom_mrtnt)
AAE_DRIVER_FUNCS(&init_eyes, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_mrtnt)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&eyes_rom_decrypt)
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Lizard Wizard
AAE_DRIVER_BEGIN(drv_lizwiz, "lizwiz", "Lizard Wizard")
AAE_DRIVER_ROM(rom_lizwiz)
AAE_DRIVER_FUNCS(&init_pacman, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_lizwiz)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Jump Shot
AAE_DRIVER_BEGIN(drv_jumpshot, "jumpshot", "Jump Shot")
AAE_DRIVER_ROM(rom_jumpshot)
AAE_DRIVER_FUNCS(&init_jumpshot, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_jumpshot)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Shoot the Bull
AAE_DRIVER_BEGIN(drv_shootbul, "shootbul", "Shoot the Bull")
AAE_DRIVER_ROM(rom_shootbul)
AAE_DRIVER_FUNCS(&init_jumpshot, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_shootbul)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Ponpoko - ORIENTATION_DEFAULT (ROT0)
AAE_DRIVER_BEGIN(drv_ponpoko, "ponpoko", "Ponpoko")
AAE_DRIVER_ROM(rom_ponpoko)
AAE_DRIVER_FUNCS(&init_ponpoko, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_ponpoko)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&ponpoko_rom_decrypt)
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Ponpoko (Venture Line) - ORIENTATION_DEFAULT (ROT0)
AAE_DRIVER_BEGIN(drv_ponpokov, "ponpokov", "Ponpoko (Venture Line)")
AAE_DRIVER_ROM(rom_ponpokov)
AAE_DRIVER_FUNCS(&init_ponpoko, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_ponpoko)
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

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(pacman_gfxdecodeinfo, 16, 4 * 32, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_ROM_DECRYPT(&ponpoko_rom_decrypt)
AAE_DRIVER_END()
///////////////////////////////////////////////////////////////////////
// Pacman Super ABC (Two-Bit Score multigame kit) - banked, own gfxdecode/CLUT
AAE_DRIVER_BEGIN(drv_superabc, "superabc", "Pacman Super ABC")
AAE_DRIVER_ROM(rom_superabc)
AAE_DRIVER_FUNCS(&init_superabc, &run_pacman, &end_pacman)
AAE_DRIVER_INPUT(input_ports_superabc)
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
		/*r8*/       superabc_readmem,
		/*w8*/       superabc_writemem,
		/*pr*/       pacman_readport,
		/*pw*/       pacman_writeport,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(superabc_gfxdecodeinfo, 16, 4 * 64, pacman_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()
//////////////////////////////////////////////////////////////////////////////////////////////////
AAE_REGISTER_DRIVER(drv_pacman)
AAE_REGISTER_DRIVER(drv_mspacman)
AAE_REGISTER_DRIVER(drv_pacplus)
AAE_REGISTER_DRIVER(drv_multi15)
AAE_REGISTER_DRIVER(drv_puckman)
//AAE_REGISTER_DRIVER(drv_puckmod)
//AAE_REGISTER_DRIVER(drv_pacmod)
//AAE_REGISTER_DRIVER(drv_mspacmab)
AAE_REGISTER_DRIVER(drv_crush)
//AAE_REGISTER_DRIVER(drv_crush2)
//AAE_REGISTER_DRIVER(drv_mbrush)
//AAE_REGISTER_DRIVER(drv_paintrlr)
AAE_REGISTER_DRIVER(drv_maketrax)
AAE_REGISTER_DRIVER(drv_eyes)
//AAE_REGISTER_DRIVER(drv_eyes2)
//AAE_REGISTER_DRIVER(drv_mrtnt)
//AAE_REGISTER_DRIVER(drv_lizwiz)
//AAE_REGISTER_DRIVER(drv_jumpshot)
//AAE_REGISTER_DRIVER(drv_shootbul)
AAE_REGISTER_DRIVER(drv_ponpoko)
//AAE_REGISTER_DRIVER(drv_ponpokov)
AAE_REGISTER_DRIVER(drv_superabc)