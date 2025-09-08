#include "galaga.h"

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "namco_stars.h"
#include "namco.h"
#include "timer.h"


static const char* galaga_samples[] =
{
	"galaga.zip",
	"bang.wav",
	"init.wav",
	0       /* end of array */
};

static UINT8 interrupt_enable_1 = 1;
static UINT8 interrupt_enable_2 = 0;
static UINT8 interrupt_enable_3 = 0;
int galaga_irq_clock = 0;
static int nmi_timer = -1;
static int flipscreen = 0;
static int j = 0;
static int k = 0;
static int l = 0;

void galaga_nmi_generate(int param);

static struct namco_interface namco_interface =
{
	3072000 / 32,	// sample rate
	3,			// number of voices
	32,			// gain adjustment
	245			// playback volume
};

/// // Video Settings
const rectangle visible_area =
{
 0,
 224,
 0,
 288
};


static struct GfxLayout charlayout =
{
	8,8,           /* 8*8 characters */
	128,           /* 128 characters */
	2,             /* 2 bits per pixel */
	{ 0, 4 },       /* the two bitplanes for 4 pixels are packed into one byte */
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },   /* characters are rotated 90 degrees */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },   /* bits are packed in groups of four */
	16 * 8           /* every char takes 16 bytes */
};

static struct GfxLayout spritelayout =
{
	16,16,          /* 16*16 sprites */
	128,            /* 128 sprites */
	2,              /* 2 bits per pixel */
	{ 0, 4 },       /* the two bitplanes for 4 pixels are packed into one byte */
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
			7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
			24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8    /* every sprite takes 64 bytes */
};


struct GfxDecodeInfo galaga_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,        0, 32 },
	{ REGION_GFX2, 0, &spritelayout, 32 * 4, 32 },
	{ -1 }
};

void galaga_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])

	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;


		bit0 = (color_prom[31 - i] >> 0) & 0x01;
		bit1 = (color_prom[31 - i] >> 1) & 0x01;
		bit2 = (color_prom[31 - i] >> 2) & 0x01;
		palette[3 * i] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (color_prom[31 - i] >> 3) & 0x01;
		bit1 = (color_prom[31 - i] >> 4) & 0x01;
		bit2 = (color_prom[31 - i] >> 5) & 0x01;
		palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = 0;
		bit1 = (color_prom[31 - i] >> 6) & 0x01;
		bit2 = (color_prom[31 - i] >> 7) & 0x01;
		palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	}

	color_prom += 32;

	/* characters */
	for (i = 0; i < TOTAL_COLORS(0); i++)
		COLOR(0, i) = 15 - (*(color_prom++) & 0x0f);

	color_prom += 128;

	/* sprites */
	for (i = 0; i < TOTAL_COLORS(1); i++)
	{
		if (i % 4 == 0) COLOR(1, i) = 0;	/* preserve transparency */
		else COLOR(1, i) = 15 - ((*color_prom & 0x0f)) + 0x10;

		color_prom++;
	}

	color_prom += 128;

	/* now the stars */
	for (i = 32; i < 32 + 64; i++)
	{
		int bits;
		int map[4] = { 0x00, 0x88, 0xcc, 0xff };

		bits = ((i - 32) >> 0) & 0x03;
		palette[3 * i] = map[bits];
		bits = ((i - 32) >> 2) & 0x03;
		palette[3 * i + 1] = map[bits];
		bits = ((i - 32) >> 4) & 0x03;
		palette[3 * i + 2] = map[bits];
	}
}


static void draw_stars(struct osd_bitmap* bitmap, const struct rectangle* cliprect)
{
	/* draw the stars */

	/* $a005 controls the stars ON/OFF */
	if (galaga_starcontrol[5] == 1)
	{
		//int bpen;
		int star_cntr;
		int set_a, set_b;
		//bpen = Machine->pens[0x1f];

		/* two sets of stars controlled by these bits */

		set_a = galaga_starcontrol[3];
		set_b = galaga_starcontrol[4] | 0x2;

		for (star_cntr = 0; star_cntr < MAX_STARS; star_cntr++)
		{
			int x, y;

			if ((set_a == star_seed_tab[star_cntr].set) || (set_b == star_seed_tab[star_cntr].set))
			{
				y = (star_seed_tab[star_cntr].x + stars_scrollx) % 256 + 16;
				x = (112 + star_seed_tab[star_cntr].y + stars_scrolly) % 256;
				/* 112 is a tweak to get alignment about perfect */

				if (x >= 0 && x <= 224)
				{
					if (read_pixel(bitmap, x, y) == 0) {
						plot_pixel(bitmap, x, y, STARS_COLOR_BASE + star_seed_tab[star_cntr].col);
					}
				}
			}
		}
	}
}



/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void galaga_vh_screenrefresh()//struct osd_bitmap* bitmap)
{
	//LOG_INFO("Video Update started");

	int offs = 0;
	int sx, sy, mx, my;

	// for every character in the Video RAM, check if it has been modified 
	// since last time and update it accordingly./

	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		// Even if Galaga's screen is 28x36, the memory layout is 32x32. We therefore 
		// have to convert the memory coordinates into screen coordinates. 
		// Note that 32*32 = 1024, while 28*36 = 1008: therefore 16 bytes of Video RAM 
		// don't map to a screen position. We don't check that here, however: range 
		// checking is performed by drawgfx(). 

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

		drawgfx(tmpbitmap, Machine->gfx[0], videoram[offs], colorram[offs] & 0x3f, 0, 0, 8 * sx, 8 * sy, &visible_area, TRANSPARENCY_NONE, 0);
	}

	osd_clearbitmap(main_bitmap);

	// Draw the sprites. 

	for (offs = 0; offs < spriteram_size; offs += 2)
	{
		if ((spriteram_3[offs + 1] & 2) == 0)
		{
			int code, color, flipx, flipy, sx, sy, sfa, sfb;

			code = spriteram[offs];
			color = spriteram[offs + 1];
			flipx = spriteram_3[offs] & 2;
			flipy = spriteram_3[offs] & 1;
			sx = spriteram_2[offs] - 16;
			sy = spriteram_2[offs + 1] - 40 + 0x100 * (spriteram_3[offs + 1] & 1);
			sfa = 0;
			sfb = 16;

			if (flipscreen)
			{
				flipx = !flipx;
				flipy = !flipy;
				sfa = 16;
				sfb = 0;
			}
			//LOG_INFO("3");

			if ((spriteram_3[offs] & 0x0c) == 0x0c)		// double width, double height 
			{
				drawgfx(main_bitmap, Machine->gfx[1], code + 2, color, flipx, flipy, sx + sfa, sy + sfa, &visible_area, TRANSPARENCY_COLOR, 0);
				drawgfx(main_bitmap, Machine->gfx[1], code, color, flipx, flipy, sx + sfb, sy + sfa, &visible_area, TRANSPARENCY_COLOR, 0);
				drawgfx(main_bitmap, Machine->gfx[1], code + 3, color, flipx, flipy, sx + sfa, sy + sfb, &visible_area, TRANSPARENCY_COLOR, 0);
				drawgfx(main_bitmap, Machine->gfx[1], code + 1, color, flipx, flipy, sx + sfb, sy + sfb, &visible_area, TRANSPARENCY_COLOR, 0);

			}
			else if (spriteram_3[offs] & 8)	// double width 
			{
				drawgfx(main_bitmap, Machine->gfx[1], code + 2, color, flipx, flipy, sx + sfa, sy, &Machine->drv->visible_area, TRANSPARENCY_COLOR, 0);
				drawgfx(main_bitmap, Machine->gfx[1], code, color, flipx, flipy, sx + sfb, sy, &Machine->drv->visible_area, TRANSPARENCY_COLOR, 0);
			}
			else if (spriteram_3[offs] & 4)	// double height 
			{

				drawgfx(main_bitmap, Machine->gfx[1], code, color, flipx, flipy, sx, sy + sfa, &Machine->drv->visible_area, TRANSPARENCY_COLOR, 0);
				drawgfx(main_bitmap, Machine->gfx[1], code + 1, color, flipx, flipy, sx, sy + sfb, &Machine->drv->visible_area, TRANSPARENCY_COLOR, 0);
			}
			else { drawgfx(main_bitmap, Machine->gfx[1], code, color, flipx, flipy, sx, sy, &visible_area, TRANSPARENCY_COLOR, 0); } // normal TRANSPARENCY_THROUGH

		}
	}
	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &visible_area, TRANSPARENCY_COLOR, 0);
	draw_stars(main_bitmap, &Machine->drv->visible_area);
}

int galaga_vh_start(void)
{
	videoram_size = 0x400;
	LOG_INFO("TEMP BITMAP 1 CREATED");
	//if ((tmpbitmap1 = osd_create_bitmap(32 * 8, 32 * 8)) == 0)	return 1;
	//if ((tmpbitmap1 = osd_create_bitmap(Machine->drv->screen_width, Machine->drv->screen_height)) == 0)
		//return 1;

	return generic_vh_start();
}


/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void galaga_vh_stop(void)
{
	//osd_free_bitmap(tmpbitmap1);
}


/***************************************************************************

Emulate the custom IO chip.

***************************************************************************/
static int customio_command;
static int mode, credits;
static int coinpercred, credpercoin;
static unsigned char customio[16];


READ_HANDLER(galaga_customio_r)
{
	return customio_command;
}

WRITE_HANDLER(galaga_customio_data_w)
{
	int offset = address & 0x0f;
	customio[offset] = data;

	switch (customio_command)
	{
	case 0xa8:
	{
		if (offset == 3 && data == 0x2)	/* total hack */
		{
			sample_start(0, 1, 0);
		}
		if (offset == 3 && data == 0x20)
		{
			sample_start(0, 0, 0);
		}
		break;
	}
	case 0xe1:
		if (offset == 7)
		{
			coinpercred = customio[1];
			credpercoin = customio[2];
		}
		break;
	}
}

READ_HANDLER(galaga_customio_data_r)
{
	int offset = address & 0x0f;

	switch (customio_command)
	{
	case 0x71:	/* read input */
	case 0xb1:	/* only issued after 0xe1 (go into credit mode) */
		if (offset == 0)
		{
			if (mode)	/* switch mode */
			{
				/* bit 7 is the service switch */
				return readinputport(4);
			}
			else	/* credits mode: return number of credits in BCD format */
			{
				int in;
				static int coininserted;


				in = readinputport(4);

				/* check if the user inserted a coin */
				if (coinpercred > 0)
				{
					if ((in & 0x70) != 0x70 && credits < 99)
					{
						coininserted++;
						if (coininserted >= coinpercred)
							if (coininserted >= coinpercred)
							{
								credits += credpercoin;
								coininserted = 0;
							}
					}
				}
				else credits = 2;


				/* check for 1 player start button */
				if ((in & 0x04) == 0)
					if (credits >= 1) credits--;

				/* check for 2 players start button */
				if ((in & 0x08) == 0)
					if (credits >= 2) credits -= 2;

				return (credits / 10) * 16 + credits % 10;
			}
		}
		else if (offset == 1)
			return readinputport(2);	/* player 1 input */
		else if (offset == 2)
			return readinputport(3);	/* player 2 input */
		break;
	}
	return -1;
}

WRITE_HANDLER(galaga_customio_w)
{
	//if (errorlog && data != 0x10 && data != 0x71) fprintf(errorlog, "%04x: custom IO command %02x\n", cpu_getpc(), data);

	customio_command = data;

	switch (data)
	{
	case 0x10:
		if (nmi_timer > -1)
		{
			timer_remove(nmi_timer);
			nmi_timer = -1;
		}
		return;

	case 0xa1:	/* go into switch mode */
		mode = 1;
		break;

	case 0xe1:	/* go into credit mode */
		credits = 0;	/* this is a good time to reset the credits counter */
		mode = 0;
		break;
	}
	//if (nmi_timer == -1){
	nmi_timer = timer_set(TIME_IN_USEC(50), CPU0, 0, galaga_nmi_generate);
	//}
}




//////////////////////////////////////////////////////////////
//MAIN GALAGA HANDLERS
//////////////////////////////////////////////////////////////

void galaga_interrupt(int galaga)
{
}


void galaga_vh_interrupt(void)
{
	/* this function is called by galaga_interrupt_1() */
	int s0, s1, s2;
	static const int speeds[8] = { -1, -2, -3, 0, 3, 2, 1, 0 };


	s0 = galaga_starcontrol[0];
	s1 = galaga_starcontrol[1];
	s2 = galaga_starcontrol[2];

	stars_scrollx += speeds[s0 + s1 * 2 + s2 * 4];
}


void galagaint()
{

	if (interrupt_enable_1)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
		//LOG_INFO("Galaga interrupt CPU0 called");
	}
	galaga_vh_interrupt();	/* update the background stars position */
}

void galagaint2()
{
	if (interrupt_enable_2)
	{
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
		//LOG_INFO("Galaga interrupt CPU1 called?");
	}
}

void galagaint3()
{
	//LOG_INFO("Iterrupt 3 status here is %d", inten3);
	if (interrupt_enable_3)
	{
		cpu_do_int_imm(CPU2, INT_TYPE_NMI);
		//LOG_INFO("Galaga interrupt CPU2 called?");
	}
}


WRITE_HANDLER(galaga_starcontrol_w)
{
	galaga_starcontrol[address & 0xf] = data & 1;
}

WRITE_HANDLER(galaga_flipscreen_w)
{
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		//memset(dirtybuffer,1,videoram_size);
	}
}

WRITE_HANDLER(NO_WRITE)
{
}

WRITE_HANDLER(galaga_sound_w)
{
	namco_sound_w(address & 0x1f, data);
}

WRITE_HANDLER(galaga_interrupt_enable_1_w)
{
	interrupt_enable_1 = data & 1;
}

WRITE_HANDLER(galaga_interrupt_enable_2_w)
{
	interrupt_enable_2 = data & 1;
	//LOG_INFO("Data write to int en cpu 2 %x ", data & 1);
}

WRITE_HANDLER(galaga_interrupt_enable_3_w)
{
	interrupt_enable_3 = !(data & 1); //LOG_INFO("Interrupt 3 status change to :%d", interrupt_enable_3);
}

WRITE_HANDLER(galagahaltw)
{
	static int reset23;

	data &= 1;
	if (data && !reset23)
	{
		cpu_needs_reset(1);
		cpu_needs_reset(2);
		cpu_enable(CPU1, 1);
		cpu_enable(CPU2, 1);

		LOG_INFO("CPUS ENABLED");
		//timer_reset(j, 0);
		//timer_reset(k, 0);
		//timer_reset(l, 0);
	}
	else if (!data)
	{
		cpu_enable(CPU1, 0);
		cpu_enable(CPU2, 0);
		LOG_INFO("CPUS ENABLED without reset");
	}

	reset23 = data;
}


void galaga_nmi_generate(int param)
{
	cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

READ_HANDLER(galaga_dsw_r)
{
	int bit0, bit1;
	int offset = address;// -0x6800;

	bit0 = (input_port_0_r(0) >> offset) & 1;
	bit1 = (input_port_1_r(0) >> offset) & 1;

	return bit0 | (bit1 << 1);
}


WRITE_HANDLER(galagasharew)
{
	Machine->memory_region[CPU0][address + 0x8000] = data;
}

READ_HANDLER(galagasharer)
{
	return Machine->memory_region[CPU0][address + 0x8000];
}


void run_galaga()
{
	watchdog_reset_w(0, 0, 0);
	galaga_vh_screenrefresh();
	namco_sh_update();
}


////////////////////////////////////////////////////


PORT_READ(GalagaPortRead)
PORT_END
PORT_WRITE(GalagaPortWrite)
PORT_END

MEM_READ(GalagaCPU1_Read)
MEM_ADDR(0x6800, 0x6807, galaga_dsw_r)
MEM_ADDR(0x7000, 0x700f, galaga_customio_data_r)
MEM_ADDR(0x7100, 0x7100, galaga_customio_r)
MEM_END

MEM_WRITE(GalagaCPU1_Write)
MEM_ADDR(0x0000, 0x3fff, NO_WRITE)
MEM_ADDR(0x6820, 0x6820, galaga_interrupt_enable_1_w)
MEM_ADDR(0x6822, 0x6822, galaga_interrupt_enable_3_w)
MEM_ADDR(0x6823, 0x6823, galagahaltw)
MEM_ADDR(0x6830, 0x6830, NO_WRITE)
MEM_ADDR(0x7000, 0x700f, galaga_customio_data_w)
MEM_ADDR(0x7100, 0x7100, galaga_customio_w)
MEM_ADDR(0xa000, 0xa005, galaga_starcontrol_w)
MEM_ADDR(0xa007, 0xa007, galaga_flipscreen_w)
MEM_END

MEM_READ(GalagaCPU2_Read)
MEM_ADDR(0x6800, 0x6807, galaga_dsw_r)
MEM_ADDR(0x8000, 0x89fff, galagasharer)
MEM_END

MEM_WRITE(GalagaCPU2_Write)
MEM_ADDR(0x0000, 0x1fff, NO_WRITE)
MEM_ADDR(0x6821, 0x6821, galaga_interrupt_enable_2_w)
MEM_ADDR(0x8000, 0x89fff, galagasharew)

MEM_END

MEM_READ(GalagaCPU3_Read)
MEM_ADDR(0x6800, 0x6807, galaga_dsw_r)
MEM_ADDR(0x8000, 0x9fff, galagasharer)

MEM_END

MEM_WRITE(GalagaCPU3_Write)
MEM_ADDR(0x0000, 0x1fff, NO_WRITE)
MEM_ADDR(0x6800, 0x681f, galaga_sound_w)
MEM_ADDR(0x6822, 0x6822, galaga_interrupt_enable_3_w)
MEM_ADDR(0x8000, 0x9fff, galagasharew)

MEM_END



int init_galaga()
{
		
	//Init CPU's
	//init_z80(GalagaCPU1_Read, GalagaCPU1_Write,GalagaPortRead, GalagaPortWrite, CPU0);
	//init_z80(GalagaCPU2_Read, GalagaCPU2_Write, GalagaPortRead, GalagaPortWrite, CPU1);
	//init_z80(GalagaCPU3_Read, GalagaCPU3_Write, GalagaPortRead, GalagaPortWrite, CPU2);
	LOG_INFO("Galaga Init called");
	//FOR RASTER, VIDEORAM POINTER, SPRITERAM POINTER NEED TO BE SET MANUALLY
	videoram = &Machine->memory_region[CPU0][0x8000];
	colorram = &Machine->memory_region[CPU0][0x8400];
	spriteram = &Machine->memory_region[CPU0][0x8b80];
	spriteram_size = 0x7f;
	spriteram_2 = &Machine->memory_region[CPU0][0x9380];
	spriteram_2_size = 0x7f;
	spriteram_3 = &Machine->memory_region[CPU0][0x9b80];
	spriteram_3_size = 0x7f;
	//Special CPU Interrupt Timers set manually.
//	j = timer_set(TIME_IN_CYCLES((3125000 / 60), CPU0), CPU0,   galagaint);
//	k = timer_set(TIME_IN_CYCLES((3125000 / 60 ), CPU1), CPU1,   galagaint2);
//	l = timer_set(TIME_IN_CYCLES((3125000 / 60 / 2) , CPU2), CPU2, galagaint3);



	//Start with CPU's 2 and 3 off.
	cpu_enable(1, 0);
	cpu_enable(2, 0);

	galaga_vh_start();
	//Start Namco Sound interface
	namco_sh_start(&namco_interface);

	return 0;
}

void end_galaga()
{

}



INPUT_PORTS_START(galaga)
PORT_START("DSW0")  // DSW0 
PORT_DIPNAME(0x07, 0x07, DEF_STR(Coinage))
PORT_DIPSETTING(0x04, DEF_STR(4C_1C))
PORT_DIPSETTING(0x02, DEF_STR(3C_1C))
PORT_DIPSETTING(0x06, DEF_STR(2C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(2C_3C))
PORT_DIPSETTING(0x03, DEF_STR(1C_2C))
PORT_DIPSETTING(0x05, DEF_STR(1C_3C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
/* TODO: bonus scores are different for 5 lives */
PORT_DIPNAME(0x38, 0x10, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x20, "20K 60K 60K")
PORT_DIPSETTING(0x18, "20K 60K")
PORT_DIPSETTING(0x10, "20K 70K 70K")
PORT_DIPSETTING(0x30, "20K 80K 80K")
PORT_DIPSETTING(0x38, "30K 80K")
PORT_DIPSETTING(0x08, "30K 100K 100K")
PORT_DIPSETTING(0x28, "30K 120K 120K")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x80, "3")
PORT_DIPSETTING(0x40, "4")
PORT_DIPSETTING(0xc0, "5")

PORT_START("DSW1")      // DSW1 
PORT_DIPNAME(0x01, 0x01, "2 Credits Game")
PORT_DIPSETTING(0x00, "1 Player")
PORT_DIPSETTING(0x01, "2 Players")
PORT_DIPNAME(0x06, 0x06, DEF_STR(Difficulty))
PORT_DIPSETTING(0x06, "Easy")
PORT_DIPSETTING(0x00, "Medium")
PORT_DIPSETTING(0x02, "Hard")
PORT_DIPSETTING(0x04, "Hardest")
PORT_DIPNAME(0x08, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, "Freeze")
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BITX(0x20, 0x20, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, IP_JOY_NONE)
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unknown))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IO1")      // FAKE 

PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO2")      // FAKE 
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_2WAY | IPF_COCKTAIL)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO3")      // FAKE 
// the button here is used to trigger the sound in the test screen 
PORT_BITX(0x03, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_START1, 1)
PORT_BIT_IMPULSE(0x08, IP_ACTIVE_LOW, IPT_START2, 1)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN2, 1)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN3, 1)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)
INPUT_PORTS_END



ROM_START(galaga)
ROM_REGION(0x10000, REGION_CPU1, 0)     /* 64k for code for the first CPU  */
ROM_LOAD("gg1-1b.3p", 0x0000, 0x1000, CRC(ab036c9f) SHA1(ca7f5da42d4e76fd89bb0b35198a23c01462fbfe))
ROM_LOAD("gg1-2b.3m", 0x1000, 0x1000, CRC(d9232240) SHA1(ab202aa259c3d332ef13dfb8fc8580ce2a5a253d))
ROM_LOAD("gg1-3.2m", 0x2000, 0x1000, CRC(753ce503) SHA1(481f443aea3ed3504ec2f3a6bfcf3cd47e2f8f81))
ROM_LOAD("gg1-4b.2l", 0x3000, 0x1000, CRC(499fcc76) SHA1(ddb8b121903646c320939c7d13f4aa4ebb130378))

ROM_REGION(0x10000, REGION_CPU2, 0)     /* 64k for the second CPU */
ROM_LOAD("gg1-5b.3f", 0x0000, 0x1000, CRC(bb5caae3) SHA1(e957a581463caac27bc37ca2e2a90f27e4f62b6f))

ROM_REGION(0x10000, REGION_CPU3, 0)     /* 64k for the third CPU  */
ROM_LOAD("gg1-7b.2c", 0x0000, 0x1000, CRC(d016686b) SHA1(44c1a04fba3c7c826ff484185cb881b4b22e6657))

ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("gg1-9.4l", 0x0000, 0x1000, CRC(58b2f47c) SHA1(62f1279a784ab2f8218c4137c7accda00e6a3490))
//ROM_LOAD("gg1-11.4d", 0x1000, 0x1000, CRC(ad447c80) SHA1(e697c180178cabd1d32483c5d8889a40633f7857))
//ROM_LOAD("gg1-10.4f", 0x2000, 0x1000, CRC(dd6f1afc) SHA1(c340ed8c25e0979629a9a1730edc762bd72d0cff))


ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("gg1-11.4d", 0x0000, 0x1000, CRC(ad447c80) SHA1(e697c180178cabd1d32483c5d8889a40633f7857))
ROM_LOAD("gg1-10.4f", 0x1000, 0x1000, CRC(dd6f1afc) SHA1(c340ed8c25e0979629a9a1730edc762bd72d0cff))

ROM_REGION(0x0220, REGION_PROMS, 0)
ROM_LOAD("prom-5.5n", 0x0000, 0x0020, CRC(54603c6b) SHA1(1a6dea13b4af155d9cb5b999a75d4f1eb9c71346))	/* palette */
ROM_LOAD("prom-4.2n", 0x0020, 0x0100, CRC(59b6edab) SHA1(0281de86c236c88739297ff712e0a4f5c8bf8ab9))	/* char lookup table */
ROM_LOAD("prom-3.1c", 0x0120, 0x0100, CRC(4a04bb6b) SHA1(cdd4bc1013f5c11984fdc4fd10e2d2e27120c1e5))	/* sprite lookup table */

ROM_REGION(0x0100, REGION_SOUND1, 0)
ROM_LOAD("prom-1.1d", 0x0000, 0x0100, CRC(7a2815b4) SHA1(085ada18c498fdb18ecedef0ea8fe9217edb7b46))
//ROM_LOAD("prom-2.5c", 0x0100, 0x0100, CRC(77245b66) SHA1(0c4d0bee858b97632411c440bea6948a74759746))	/* timing - not used */
ROM_END



// Galaga
AAE_DRIVER_BEGIN(drv_galaga, "galaga", "Galaga")
AAE_DRIVER_ROM(rom_galaga)
AAE_DRIVER_FUNCS(&init_galaga, &run_galaga, &end_galaga)
AAE_DRIVER_INPUT(input_ports_galaga)
AAE_DRIVER_SAMPLES(galaga_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3072000,
		/*div*/      400,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &galagaint,
		/*r8*/       GalagaCPU1_Read,
		/*w8*/       GalagaCPU1_Write,
		/*pr*/       GalagaPortRead,
		/*pw*/       GalagaPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1
	AAE_CPU_ENTRY(
		CPU_MZ80, 3072000, 400, 1, INT_TYPE_INT, &galagaint2,
		GalagaCPU2_Read, GalagaCPU2_Write,
		GalagaPortRead, GalagaPortWrite,
		nullptr, nullptr
	),
	// CPU2
	AAE_CPU_ENTRY(
		CPU_MZ80, 3072000, 400, 2, INT_TYPE_INT, &galagaint3,
		GalagaCPU3_Read, GalagaCPU3_Write,
		GalagaPortRead, GalagaPortWrite,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 224 - 1, 0, 288 - 1)
AAE_DRIVER_RASTER(galaga_gfxdecodeinfo, 32 + 64, 64 * 4, galaga_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_galaga)