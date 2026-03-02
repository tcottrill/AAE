#include "aae_mame_driver.h"
#include "yiear.h"
#include "old_mame_raster.h"
#include "rawinput.h"
#include "driver_registry.h"
#include "namco.h"
#include "timer.h"

extern int m6809_slapstic;

#pragma warning( disable : 4838 4003 )


static const char* yiear_samples[] = {
	"yiear.zip",
	"gong.wav","fall.wav","health.wav",
	"star.wav","nuncha.wav","chain.wav",
	"impact.wav","fan.wav","pole.wav","jump.wav",
	"intro.wav","stage1.wav","stage2.wav","panick.wav",
	"win.wav","lose.wav","gameover.wav","highscor.wav",
	"punch.wav","kick.wav","shout.wav","oof.wav",
	"buchu.wav","shisha.wav","perfect.wav",
	0
};

// Simple Audio Functions to move eventually


enum {
	gong = 0, fall, health, star, nuncha, chain, impact, fan, pole, jump,
	intro, stage1, stage2, panick, win, lose, gameover, highscore,
	punch, kick, shout, oof, buchu, shisha, perfect
};

unsigned char* yiear_soundport;

int yiear_sound1, yiear_sound2, yiear_timer1; /* simulate a simple queue */


int yiear_sh_start(void) {
	yiear_timer1 = yiear_sound1 = yiear_sound2 = -1;
	return 0;
}

void yiear_sh_update(void) {
	//if (Machine->samples == 0) return;
	if (yiear_timer1 > 0) {
		yiear_timer1--; /* wait for current sound to complete */
	}
	else if (yiear_sound1 >= 0) {
		//struct GameSample* sample = Machine->samples->sample[yiear_sound1];
		//if (sample)
	//	{
			sample_start(2, yiear_sound1, 0);
			yiear_timer1 = 100;// sample->length * 60 / sample->smpfreq; /* compute ticks until finished */
			// Note: I'll get around to correcting this eventually.  
			yiear_sound1 = yiear_sound2;
			yiear_sound2 = -1;
	//	}
	}
}


void yiear_QueueSound(int which) {
	if (yiear_sound1 < 0) yiear_sound1 = which;
	else if (yiear_sound2 < 0) yiear_sound2 = which;
}

void yiear_NoSound(void) {
	sample_stop(0); /* immediate sounds */
	sample_stop(1);

	sample_stop(2); /* queued sound */

	sample_stop(3); /* music */

	//	yiear_sh_init(0); /* clear queue */
}


void yiear_PlaySound(int which) {
	static int chan = 0;

	sample_start(chan, which, 0);
	chan = 1 - chan;
}

void yiear_PlayMusic(int which, int loop) {
	sample_start(3, which, loop);
}

WRITE_HANDLER( yiear_audio_out_w) {
	yiear_soundport[address] = data; /* write so RAM test doesn't fail */

	//if (Machine->samples == 0) return;

	switch (data) {
	case 0:		yiear_NoSound(); break;

		/* junk - these are sent only during the RAM test
		case 85:
		case 170:
		case 255:
		case 87:
		break;
		*/

	case 1:		yiear_QueueSound(gong); break;
	case 4:		yiear_QueueSound(fall); break;
	case 5:		yiear_PlaySound(health); break;
	case 6:		yiear_PlaySound(star); break;
	case 7:		yiear_PlaySound(nuncha); break;
	case 8:		yiear_PlaySound(chain); break;
	case 9:		yiear_PlaySound(impact); break;
	case 10:	yiear_PlaySound(fan); break;
	case 11:	yiear_PlaySound(pole); break;
	case 12:	yiear_PlaySound(jump); break;

	case 64:	yiear_PlayMusic(intro, 0); break;
	case 65:	yiear_PlayMusic(stage1, 1); break;
	case 66:	yiear_PlayMusic(stage2, 1); break;
	case 67:	yiear_PlayMusic(panick, 1); break;
	case 68:	yiear_PlayMusic(win, 0); break;
	case 69:	yiear_PlayMusic(lose, 0); break;
	case 70:	yiear_PlayMusic(gameover, 0); break;
	case 71:	yiear_PlayMusic(highscore, 1); break;

	case 128:	yiear_PlaySound(punch); break;
	case 129:	yiear_PlaySound(kick); break;
	case 131:	yiear_PlaySound(shout); break;
	case 133:	yiear_PlaySound(oof); break;
	case 135:	yiear_QueueSound(buchu); break;
	case 136:	yiear_QueueSound(shisha); break;

	case 153:	yiear_QueueSound(perfect); break;
	}
}












static int flipscreen=0;

static struct rectangle spritevisiblearea =
{
	 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1
};

static struct GfxLayout charlayout =
{
	8,8,	/* 8 by 8 */
	512,	/* 512 characters */
	4,		/* 4 bits per pixel */
	{ 4, 0, 512 * 16 * 8 + 4, 512 * 16 * 8 + 0 },		/* plane */
	{ 0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3 },		/* x */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },	/* y */
	16 * 8
};

static struct GfxLayout spritelayout =
{
	16,16,	/* 16x16 sprites */
	512,	/* 512 sprites */
	4,		/* 4 bits per pixel */
	{ 4, 0, 512 * 64 * 8 + 4, 512 * 64 * 8 + 0 },	/* plane offsets */
	{ 0 * 8 * 8 + 0, 0 * 8 * 8 + 1, 0 * 8 * 8 + 2, 0 * 8 * 8 + 3, 1 * 8 * 8 + 0, 1 * 8 * 8 + 1, 1 * 8 * 8 + 2, 1 * 8 * 8 + 3,
	  2 * 8 * 8 + 0, 2 * 8 * 8 + 1, 2 * 8 * 8 + 2, 2 * 8 * 8 + 3, 3 * 8 * 8 + 0, 3 * 8 * 8 + 1, 3 * 8 * 8 + 2, 3 * 8 * 8 + 3 },
	{  0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
	  32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
	64 * 8    /* each sprite takes 64 bytes */
};

struct GfxDecodeInfo yiear_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,   16, 1 },
	{ REGION_GFX2, 0, &spritelayout,  0, 1 },
	{ -1 } /* end of array */
};




static int irq_enable, nmi_enable;

WRITE_HANDLER(yiear_interrupt_enable_w)
{
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		//memset(dirtybuffer, 1, videoram_size);
	}
	/* bit 1 is NMI enable */
	nmi_enable = data & 0x02;

	/* bit 2 is IRQ enable */
	irq_enable = data & 0x04;

}

void yiear_interrupt()
{
	if (irq_enable) { cpu_do_int_imm(CPU0, INT_TYPE_INT); LOG_INFO("Interrupt Requested"); }
	//else return ignore_interrupt();
}

void yiear_nmi_interrupt(int dummy )
{
	if (nmi_enable) cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	//else return ignore_interrupt();
}

static int yiear_speech_r(int offset)
{
	return rand();
	/* maybe bit 0 is VLM5030 busy pin??? */
//	if (VLM5030_BSY()) return 1;
	//else 
		//return 0;
}

void yiear_speech_st(int offset, int data)
{
	/* no idea if this is correct... */
	//VLM5030_ST(1);
	//VLM5030_ST(0);
}

//**************************************************************************/
void yiear_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])
	
	LOG_INFO("Running convert color prom");

	for (i = 0; i < (INT)Machine->drv->total_colors; i++)
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


	/* sprites lookup table */
	for (i = 0; i < TOTAL_COLORS(1); i++)
		COLOR(1, i) = i;

	/* characters lookup table */
	for (i = 0; i < TOTAL_COLORS(0); i++)
		COLOR(0, i) = i + 16;
}



WRITE_HANDLER( yiear_4f00_w) 
{
	/* bit 0 flips screen */
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		//memset(dirtybuffer, 1, videoram_size);
	}

	/* bits 3 and 4 are for coin counters - we ignore them */
}



/***************************************************************************

  Draw the game screen in the given osd_bitmap.
  Do NOT call osd_update_display() from this function, it will be called by
  the main emulation engine.

***************************************************************************/
void yiear_vh_screenrefresh()
{
	int offs;


	/* for every character in the Video RAM, check if it has been modified */
	/* since last time and update it accordingly. */
	for (offs = videoram_size - 2; offs >= 0; offs -= 2)
	{
			int sx, sy, flipx, flipy;

			sx = (offs / 2) % 32;
			sy = (offs / 2) / 32;
			flipx = videoram[offs] & 0x80;
			flipy = videoram[offs] & 0x40;
			if (flipscreen)
			{
				sx = 31 - sx;
				sy = 31 - sy;
				flipx = !flipx;
				flipy = !flipy;
			}

			drawgfx(tmpbitmap, Machine->gfx[0],
				videoram[offs + 1] | ((videoram[offs] & 0x10) << 4),
				0,
				flipx, flipy,
				8 * sx, 8 * sy,
				0, TRANSPARENCY_NONE, 0);
		
	}


	/* copy the temporary bitmap to the screen */
	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);


	for (offs = spriteram_size - 2; offs >= 0; offs -= 2)
	{
		int sx, sy, flipx, flipy;


		sy = 240 - spriteram[offs + 1];
		sx = spriteram_2[offs];
		flipx = ~spriteram[offs] & 0x40;
		flipy = spriteram[offs] & 0x80;

		if (flipscreen)
		{
			sy = 240 - sy;
			flipy = !flipy;
		}

		if (offs < 0x26)
		{
			sy++;	/* fix title screen & garbage at the bottom of the screen */
		}

		drawgfx(main_bitmap, Machine->gfx[1],
			spriteram_2[offs + 1] + 256 * (spriteram[offs] & 1),
			0,
			flipx, flipy,
			sx, sy,
			&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
	}
}


//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
//MAIN HANDLERS
//////////////////////////////////////////////////////////////

void run_yiear()
{
	watchdog_reset_w(0, 0, 0);
	yiear_vh_screenrefresh();
}

void end_yiear()
{


}


MEM_READ(yiear_readmem)
{ 0x4c00, 0x4c00, ip_port_3_r },
{ 0x4d00, 0x4d00, ip_port_4_r },
{ 0x4e00, 0x4e00, ip_port_0_r },
{ 0x4e01, 0x4e01, ip_port_1_r },
{ 0x4e02, 0x4e02, ip_port_2_r },
{ 0x4e03, 0x4e03, ip_port_5_r },
{ 0x5000, 0x5fff, MRA_RAM },
{ 0x8000, 0xffff, MRA_ROM },
MEM_END

MEM_WRITE(yiear_writemem)
{0x4000, 0x4000, yiear_interrupt_enable_w},
{ 0x4f00, 0x4f00, yiear_4f00_w },
//{ 0x5030, 0x51AF, MWA_RAM, &spriteram, &spriteram_size },
{ 0x5607, 0x5607, yiear_audio_out_w, &yiear_soundport },
{ 0x5000, 0x57FF, MWA_RAM },	/* sprites and audio are in this area */
//{ 0x5800, 0x5FFF, videoram_w, &videoram, &videoram_size },
{ 0x8000, 0xFFFF, MWA_ROM }, 
MEM_END


int yiear_vh_start(void)
{
	LOG_INFO("VH_START CALLED");
	//FOR RASTER, VIDEORAM POINTER, SPRITERAM POINTER NEED TO BE SET MANUALLY
	videoram = &Machine->memory_region[CPU0][0x5800];
	videoram_size = 0x800;
	spriteram = &Machine->memory_region[CPU0][0x5000];
	spriteram_size = 0x30;
	spriteram_2 = &Machine->memory_region[CPU0][0x5400];
	spriteram_2_size = 0x30;
	//colorram = &Machine->memory_region[Machine->drv->cpu[0].memory_region][0x4400];
	return generic_vh_start();
}

int init_yiear(void)
{
	LOG_INFO("INIT Called");

	videoram = &Machine->memory_region[CPU0][0x5800];
	videoram_size = 0x800;
	spriteram = &Machine->memory_region[CPU0][0x5000];
	spriteram_size = 0x30;
	spriteram_2 = &Machine->memory_region[CPU0][0x5400];
	spriteram_2_size = 0x30;

	yiear_soundport = &Machine->memory_region[CPU0][0x5607];

	//colorram = &Machine->memory_region[Machine->drv->cpu[0].memory_region][0x4400];
//	timer_set(TIME_IN_HZ(80), 0, yiear_nmi_interrupt);

	cpu_setOPbaseoverride(nullptr);
	m6809_slapstic = 0;
	yiear_vh_start();
	yiear_sh_start();
	irq_enable = 1;
	return 1;
}


//INPUTS

INPUT_PORTS_START(yiear)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON3)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("IN2")	/* IN2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_COCKTAIL)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("DSW0")	/* DSW0 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Lives))
PORT_DIPSETTING(0x03, "1")
PORT_DIPSETTING(0x02, "2")
PORT_DIPSETTING(0x01, "3")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x04, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x04, DEF_STR(Cocktail))
PORT_DIPNAME(0x08, 0x08, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x08, "30000 80000")
PORT_DIPSETTING(0x00, "40000 90000")
PORT_DIPNAME(0x10, 0x10, "Unknown DSW1 4")
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, "Difficulty?")
PORT_DIPSETTING(0x20, "Easy")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPNAME(0x40, 0x40, "Unknown DSW1 6")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")	/* DSW1 */
PORT_DIPNAME(0x01, 0x01, DEF_STR(Flip_Screen))
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x02, "Number of Controllers")
PORT_DIPSETTING(0x02, "1")
PORT_DIPSETTING(0x00, "2")
PORT_SERVICE(0x04, IP_ACTIVE_LOW)
PORT_DIPNAME(0x08, 0x08, "Unknown DSW2 4")
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, "Unknown DSW2 5")
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, "Unknown DSW2 6")
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, "Unknown DSW2 7")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, "Unknown DSW2 8")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW2")	/* DSW2 */
PORT_DIPNAME(0x0f, 0x0f, DEF_STR(Coin_A))
PORT_DIPSETTING(0x02, DEF_STR(4C_1C))
PORT_DIPSETTING(0x05, DEF_STR(3C_1C))
PORT_DIPSETTING(0x08, DEF_STR(2C_1C))
PORT_DIPSETTING(0x04, DEF_STR(3C_2C))
PORT_DIPSETTING(0x01, DEF_STR(4C_3C))
PORT_DIPSETTING(0x0f, DEF_STR(1C_1C))
PORT_DIPSETTING(0x03, DEF_STR(3C_4C))
PORT_DIPSETTING(0x07, DEF_STR(2C_3C))
PORT_DIPSETTING(0x0e, DEF_STR(1C_2C))
PORT_DIPSETTING(0x06, DEF_STR(2C_5C))
PORT_DIPSETTING(0x0d, DEF_STR(1C_3C))
PORT_DIPSETTING(0x0c, DEF_STR(1C_4C))
PORT_DIPSETTING(0x0b, DEF_STR(1C_5C))
PORT_DIPSETTING(0x0a, DEF_STR(1C_6C))
PORT_DIPSETTING(0x09, DEF_STR(1C_7C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0xf0, 0xf0, DEF_STR(Coin_B))
PORT_DIPSETTING(0x20, DEF_STR(4C_1C))
PORT_DIPSETTING(0x50, DEF_STR(3C_1C))
PORT_DIPSETTING(0x80, DEF_STR(2C_1C))
PORT_DIPSETTING(0x40, DEF_STR(3C_2C))
PORT_DIPSETTING(0x10, DEF_STR(4C_3C))
PORT_DIPSETTING(0xf0, DEF_STR(1C_1C))
PORT_DIPSETTING(0x30, DEF_STR(3C_4C))
PORT_DIPSETTING(0x70, DEF_STR(2C_3C))
PORT_DIPSETTING(0xe0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x60, DEF_STR(2C_5C))
PORT_DIPSETTING(0xd0, DEF_STR(1C_3C))
PORT_DIPSETTING(0xc0, DEF_STR(1C_4C))
PORT_DIPSETTING(0xb0, DEF_STR(1C_5C))
PORT_DIPSETTING(0xa0, DEF_STR(1C_6C))
PORT_DIPSETTING(0x90, DEF_STR(1C_7C))/* 0x00 gives invalid */
INPUT_PORTS_END



/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START(yiear)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("407_i08.10d", 0x08000, 0x4000, CRC(e2d7458b) SHA1(1b192130b5cd879ab686a21aa2b518c90edd89aa))
ROM_LOAD("407_i07.8d", 0x0c000, 0x4000, CRC(7db7442e) SHA1(d604a995a5505251904447ad697fc9e7f94bf241))

ROM_REGION(0x04000, REGION_GFX1, 0)
ROM_LOAD("407_c01.6h", 0x00000, 0x2000, CRC(b68fd91d) SHA1(c267931d69794c292b7ebae5bc35ad842194683a)) /* was listed as g16_1 */
ROM_LOAD("407_c02.7h", 0x02000, 0x2000, CRC(d9b167c6) SHA1(a2fd10bddfa4e95e9d49892737ace146209bfa2b)) /* was listed as g15_2 */

ROM_REGION(0x10000, REGION_GFX2, 0)
ROM_LOAD("407_d05.16h", 0x00000, 0x4000, CRC(45109b29) SHA1(0794935b490497b21b99045c90231b7bac151d42)) /* was listed as g04_5 */
ROM_LOAD("407_d06.17h", 0x04000, 0x4000, CRC(1d650790) SHA1(5f2a4983b20251c712358547a7c62c0331c6cb6f)) /* was listed as g03_6 */
ROM_LOAD("407_d03.14h", 0x08000, 0x4000, CRC(e6aa945b) SHA1(c5757d16c28f5966fd04675c0c640ef9b6b76ca5)) /* was listed as g06_3 */
ROM_LOAD("407_d04.15h", 0x0c000, 0x4000, CRC(cc187c22) SHA1(555ba18a9648681e5140b3fd84af16959ee5296d)) /* was listed as g05_4 */

ROM_REGION(0x0020, REGION_PROMS, 0)
ROM_LOAD("407c10.1g", 0x00000, 0x0020, CRC(c283d71f) SHA1(10cd39f4e951ba6ca5610081c8c1fcd9d68b34d2))  /* Color BPROM type is TBP18S030N or compatible */

ROM_REGION(0x2000, REGION_SOUND1, 0)	/* 8k for the VLM5030 data */
ROM_LOAD("407_c09.8b", 0x00000, 0x2000, CRC(f75a1539) SHA1(f139f6cb41351eb81ee47d777db03012aa5fadb1))
ROM_END

ROM_START(yiear2)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code */
ROM_LOAD("d12_8.bin", 0x08000, 0x4000, CRC(49ecd9dd) SHA1(15692029351e87837cc5a251947ff315fd723aa4))
ROM_LOAD("d14_7.bin", 0x0c000, 0x4000, CRC(bc2e1208) SHA1(a5a0c78ff4e02bd7da3eab3842dfe99956e74155))

ROM_REGION(0x04000, REGION_GFX1, 0)
ROM_LOAD("g16_1.bin", 0x00000, 0x2000, CRC(b68fd91d) SHA1(c267931d69794c292b7ebae5bc35ad842194683a))
ROM_LOAD("g15_2.bin", 0x02000, 0x2000, CRC(d9b167c6) SHA1(a2fd10bddfa4e95e9d49892737ace146209bfa2b))

ROM_REGION(0x10000, REGION_GFX2, 0)
ROM_LOAD("g04_5.bin", 0x00000, 0x4000, CRC(45109b29) SHA1(0794935b490497b21b99045c90231b7bac151d42))
ROM_LOAD("g03_6.bin", 0x04000, 0x4000, CRC(1d650790) SHA1(5f2a4983b20251c712358547a7c62c0331c6cb6f))
ROM_LOAD("g06_3.bin", 0x08000, 0x4000, CRC(e6aa945b) SHA1(c5757d16c28f5966fd04675c0c640ef9b6b76ca5))
ROM_LOAD("g05_4.bin", 0x0c000, 0x4000, CRC(cc187c22) SHA1(555ba18a9648681e5140b3fd84af16959ee5296d))

ROM_REGION(0x0020, REGION_PROMS, 0)
ROM_LOAD("yiear.clr", 0x00000, 0x0020, CRC(c283d71f) SHA1(10cd39f4e951ba6ca5610081c8c1fcd9d68b34d2))

ROM_REGION(0x2000, REGION_SOUND1, 0)	/* 8k for the VLM5030 data */
ROM_LOAD("a12_9.bin", 0x00000, 0x2000, CRC(f75a1539) SHA1(f139f6cb41351eb81ee47d777db03012aa5fadb1))
ROM_END

///PORT HANDLERS

// yiear
AAE_DRIVER_BEGIN(drv_yiear, "yiear", "Yie Ar Kung-Fu (set 1)")
AAE_DRIVER_ROM(rom_yiear)
AAE_DRIVER_FUNCS(&init_yiear, &run_yiear, &end_yiear)
AAE_DRIVER_INPUT(input_ports_yiear)
AAE_DRIVER_SAMPLES(yiear_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1152000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &yiear_interrupt,
		/*r8*/       yiear_readmem,
		/*w8*/       yiear_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 16, 239 )
AAE_DRIVER_RASTER(yiear_gfxdecodeinfo,32, 32, yiear_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_yiear)





