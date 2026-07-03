#include "aae_mame_driver.h"
#include "gaplus.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "timer.h"

#pragma warning( disable : 4838 4003 )

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

unsigned char* gaplus_sharedram;
unsigned char* gaplus_snd_sharedram;

unsigned char* gaplus_customio_1, * gaplus_customio_2, * gaplus_customio_3;
static int  interrupt_enable_2 = 0, interrupt_enable_3 = 0;
static int credits, coincounter1, coincounter2;

static int credmoned[] = { 1, 1, 2, 3 };
static int monedcred[] = { 1, 2, 1, 1 };

// ---------------------------------------------------------------------------
// Namco sound interface -- 8 voices, matches original gaplus hardware
// ---------------------------------------------------------------------------

static struct namco_interface namco_interface =
{
	24576000 / 1024,	/* 24000Hz sample rate */
	8,	 			/* number of voices */
	200,			/* playback volume */
	REGION_SOUND1,
   0	/* memory region */
};

static const char* gaplus_sample_names[] =
{
	"gaplus.zip",
	"bang.wav",
	"init.wav",
	0       /* end of array */
};

// ---------------------------------------------------------------------------
// Machine init
// ---------------------------------------------------------------------------

void gaplus_init_machine(void)
{
	interrupt_enable_2 = interrupt_enable_3 = 1;
	credits = coincounter1 = coincounter2 = 0;
}

// ---------------------------------------------------------------------------
// Shared RAM handlers
// The sound CPU sits in a tight polling loop waiting for work; spinning it
// until the next interrupt avoids burning host CPU cycles.
// ---------------------------------------------------------------------------

READ_HANDLER(gaplus_sharedram_r)
{
	return gaplus_sharedram[address];
}

WRITE_HANDLER(gaplus_sharedram_w)
{
	if (address == 0x082c)	/* 0x102c */
		gaplus_flipscreen_w(data);
	gaplus_sharedram[address] = data;
}

READ_HANDLER(gaplus_snd_sharedram_r)
{
	return gaplus_snd_sharedram[address];
}

WRITE_HANDLER(gaplus_snd_sharedram_w)
{
	gaplus_snd_sharedram[address] = data;
}

// ---------------------------------------------------------------------------
// Custom I/O chip write
// The custom I/O chips are write-to-set-mode, read-to-get-data devices.
// Writing to address 8 sets the operating mode used by the read handler.
// ---------------------------------------------------------------------------

WRITE_HANDLER(gaplus_customio_1_w)
{
	gaplus_customio_1[address] = data;
}

WRITE_HANDLER(gaplus_customio_2_w)
{
	gaplus_customio_2[address] = data;
}

WRITE_HANDLER(gaplus_customio_3_w)
{
	if ((address == 0x09) && (data >= 0x0f))
	{
		sample_start(0, 0, 0);
	}
	gaplus_customio_3[address] = data;
}

// ---------------------------------------------------------------------------
// gaplus custom I/O read -- chip 1 (joystick, coins, start, credits)
// ---------------------------------------------------------------------------

READ_HANDLER(gaplusa_customio_1_r)
{
	int mode, val, temp1, temp2;

	mode = gaplus_customio_1[8];
	if (mode == 4)	/* normal mode */
	{
		switch (address)
		{
		case 0:
			return (credits / 10);      /* high BCD of credits */
			break;
		case 1:
			return (credits % 10);      /* low BCD of credits */
			break;
		case 2:     /* Coin slots, high nibble of port 2 */
		{
			static int lastval;

			val = readinputport(2) >> 4;
			temp1 = readinputport(0) & 0x03;
			temp2 = (readinputport(0) >> 6) & 0x03;

			/* bit 0 is a trigger for the coin slot 1 */
			if ((val & 1) && ((val ^ lastval) & 1))
			{
				coincounter1++;
				if (coincounter1 >= credmoned[temp1])
				{
					credits += monedcred[temp1];
					coincounter1 -= credmoned[temp1];
				}
			}
			/* bit 1 is a trigger for the coin slot 2 */
			if ((val & 2) && ((val ^ lastval) & 2))
			{
				coincounter2++;
				if (coincounter2 >= credmoned[temp2])
				{
					credits += monedcred[temp2];
					coincounter2 -= credmoned[temp2];
				}
			}

			if (credits > 99)
				credits = 99;

			return lastval = val;
		}
		break;
		case 3:
		{
			static int lastval;

			val = readinputport(2) & 0x03;
			temp1 = readinputport(0) & 0x03;
			temp2 = (readinputport(0) >> 6) & 0x03;

			/* bit 0 is a trigger for the 1 player start */
			if ((val & 1) && ((val ^ lastval) & 1))
			{
				if (credits > 0)
					credits--;
				else
					val &= ~1;   /* otherwise you can start with no credits! */
			}
			/* bit 1 is a trigger for the 2 player start */
			if ((val & 2) && ((val ^ lastval) & 2))
			{
				if (credits >= 2)
					credits -= 2;
				else
					val &= ~2;   /* otherwise you can start with no credits! */
			}
			return lastval = val;
		}
		break;
		case 4:
			return (readinputport(3) & 0x0f);   /* 1P controls */
			break;
		case 5:
			return (readinputport(4) & 0x03);   /* 1P button 1 */
			break;
		case 6:
			return (readinputport(3) >> 4);     /* 2P controls */
			break;
		case 7:
			return ((readinputport(4) >> 2) & 0x03);    /* 2P button 1 */
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	else if (mode == 8)  /* IO tests chip 1 */
	{
		switch (address)
		{
		case 0:
			return 0x06;
			break;
		case 1:
			return 0x09;
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	else if (mode == 1)	/* test mode controls */
	{
		switch (address)
		{
		case 0:
			return (readinputport(2) & 0x03);	/* start 1 & 2 */
			break;
		case 1:
			return (readinputport(3) & 0x0f);	/* 1P controls */
			break;
		case 2:
			return (readinputport(3) >> 4);	/* 2P controls */
			break;
		case 3:
			return (readinputport(4) & 0x0f);	/* button 1 & 2 */
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	return gaplus_customio_1[address];
}
READ_HANDLER(gaplusa_customio_2_r)
{
	int val, mode;

	mode = gaplus_customio_2[8];
	if (mode == 5)  /* IO tests chip 2 */
	{
		switch (address)
		{
		case 0:
		case 1:
			return 0x0f;
			break;
		default:
			return gaplus_customio_2[address];
		}
	}
	else    if (mode == 4)	/* this values are read only by the game on power up */
	{
		switch (address)
		{
		case 1:
			val = readinputport(0) & 0x0f; /* credits/coin 1P & fighters */
			break;
		case 2:
			val = readinputport(1) >> 5;   /* bonus life */
			break;
		case 4:
			val = readinputport(1) & 0x0f; /* rank & test mode */
			break;
		case 7:
			val = readinputport(0) >> 6;   /* credits/coin 2P */
			break;
		default:
			val = gaplus_customio_2[address];
		}
		return val;
	}
	else
		return gaplus_customio_2[address];
}
READ_HANDLER(gaplusa_customio_3_r)
{
	int mode;

	mode = gaplus_customio_3[8];
	if (mode == 2)
	{
		switch (address)
		{
		case 2:
			return 0x0f;
			break;
		default:
			return gaplus_customio_3[address];
		}
	}
	else
	{
		switch (address)
		{
		case 0:
			return ((readinputport(0) & 0x20) >> 3);   /* cabinet */
			break;
		case 1:
			return 0x0f;
			break;
		case 2:
			return 0x0e;
			break;
		case 3:
			return 0x01;
			break;
		default:
			return gaplus_customio_3[address];
		}
	}
}

READ_HANDLER(gaplus_customio_1_r)
{
	int mode, val, temp1, temp2;

	mode = gaplus_customio_1[8];
	if (mode == 3)	/* normal mode */
	{
		switch (address)
		{
		case 0:     /* Coin slots, high nibble of port 2 */
		{
			static int lastval;

			val = readinputport(2) >> 4;
			temp1 = readinputport(0) & 0x03;
			temp2 = (readinputport(0) >> 6) & 0x03;

			/* bit 0 is a trigger for the coin slot 1 */
			if ((val & 1) && ((val ^ lastval) & 1))
			{
				coincounter1++;
				if (coincounter1 >= credmoned[temp1])
				{
					credits += monedcred[temp1];
					coincounter1 -= credmoned[temp1];
				}
			}
			/* bit 1 is a trigger for the coin slot 2 */
			if ((val & 2) && ((val ^ lastval) & 2))
			{
				coincounter2++;
				if (coincounter2 >= credmoned[temp2])
				{
					credits += monedcred[temp2];
					coincounter2 -= credmoned[temp2];
				}
			}

			if (credits > 99)
				credits = 99;

			return lastval = val;
		}
		break;
		case 1:
		{
			static int lastval;

			val = readinputport(2) & 0x03;
			temp1 = readinputport(0) & 0x03;
			temp2 = (readinputport(0) >> 6) & 0x03;

			/* bit 0 is a trigger for the 1 player start */
			if ((val & 1) && ((val ^ lastval) & 1))
			{
				if (credits > 0)
					credits--;
				else
					val &= ~1;   /* otherwise you can start with no credits! */
			}
			/* bit 1 is a trigger for the 2 player start */
			if ((val & 2) && ((val ^ lastval) & 2))
			{
				if (credits >= 2)
					credits -= 2;
				else
					val &= ~2;   /* otherwise you can start with no credits! */
			}
			return lastval = val;
		}
		break;
		case 2:
			return (credits / 10);      /* high BCD of credits */
			break;
		case 3:
			return (credits % 10);      /* low BCD of credits */
			break;
		case 4:
			return (readinputport(3) & 0x0f);   /* 1P controls */
			break;
		case 5:
			return (readinputport(4) & 0x03);   /* 1P button 1 */
			break;
		case 6:
			return (readinputport(3) >> 4);     /* 2P controls */
			break;
		case 7:
			return ((readinputport(4) >> 2) & 0x03);    /* 2P button 1 */
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	else if (mode == 5)  /* IO tests chip 1 */
	{
		switch (address)
		{
		case 0:
		case 1:
			return 0x0f;
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	else if (mode == 1)	/* test mode controls */
	{
		switch (address)
		{
		case 4:
			return (readinputport(2) & 0x03);	/* start 1 & 2 */
			break;
		case 5:
			return (readinputport(3) & 0x0f);	/* 1P controls */
			break;
		case 6:
			return (readinputport(3) >> 4);	/* 2P controls */
			break;
		case 7:
			return (readinputport(4) & 0x0f);	/* button 1 & 2 */
			break;
		default:
			return gaplus_customio_1[address];
		}
	}
	return gaplus_customio_1[address];
}
READ_HANDLER(gaplus_customio_2_r)
{
	int val, mode;

	mode = gaplus_customio_2[8];
	if (mode == 8)  /* IO tests chip 2 */
	{
		switch (address)
		{
		case 0:
			return 0x06;
			break;
		case 1:
			return 0x09;
			break;
		default:
			return gaplus_customio_2[address];
		}
	}
	else    if (mode == 1)	/* this values are read only by the game on power up */
	{
		switch (address)
		{
		case 0:
			val = readinputport(0) & 0x0f; /* credits/coin 1P & fighters */
			break;
		case 1:
			val = readinputport(1) >> 5;   /* bonus life */
			break;
		case 2:
			val = readinputport(1) & 0x0f; /* rank & test mode */
			break;
		case 3:
			val = readinputport(0) >> 6;   /* credits/coin 2P */
			break;
		default:
			val = gaplus_customio_2[address];
		}
		return val;
	}
	else
		return gaplus_customio_2[address];
}

READ_HANDLER(gaplus_customio_3_r)
{
	int mode;

	mode = gaplus_customio_3[8];
	if (mode == 2)
	{
		switch (address)
		{
		case 2:
			return 0x0f;
			break;
		default:
			return gaplus_customio_3[address];
		}
	}
	else
	{
		switch (address)
		{
		case 0:
			return ((readinputport(0) & 0x20) >> 3);   /* cabinet */
			break;
		case 1:
			return 0x0f;
			break;
		case 2:
			return 0x0e;
			break;
		case 3:
			return 0x01;
			break;
		default:
			return gaplus_customio_3[address];
		}
	}
}

// ---------------------------------------------------------------------------
// Interrupt enable / generate for both CPUs
// The hardware uses address lines (not data) to set enable state.
// Writing to address 2 enables; writing to address 3 disables (or vice versa,
// the exact polarity is captured in the address value the MAME source uses).
// ---------------------------------------------------------------------------

WRITE_HANDLER(gaplus_interrupt_enable_2_w)
{
	interrupt_enable_2 = address & 1;
}

/* CPU1 VBLANK interrupt callback */
void gaplus_interrupt_1(void)
{
	gaplus_starfield_update(); /* update starfields */
	//LOG_DEBUG("Interrupt called on CPU 1 at %d cycles", cpu_getcycles_cpu(0));
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

/* CPU2 VBLANK interrupt callback */
void gaplus_interrupt_2(void)
{
	if (interrupt_enable_2)
	{
		//LOG_DEBUG("Interrupt called on CPU 2 at %d cycles", cpu_getcycles_cpu(1));
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
	}
}

void gaplus_interrupt_3(void)
{
	if (interrupt_enable_3)
	{
		//LOG_DEBUG("Interrupt called on CPU 3 at %d cycles", cpu_getcycles_cpu(2));
		cpu_do_int_imm(CPU2, INT_TYPE_INT);
	}
}

WRITE_HANDLER(gaplus_reset_2_3_w)
{
	LOG_ERROR("GALAGA CPU 2_3 RESET CALLED!!!!");
	interrupt_enable_2 = interrupt_enable_3 = 1;
	// Transition the secondary CPUs from halted to active execution
	cpu_enable(CPU1, 1);
	cpu_enable(CPU2, 1);

	cpu_needs_reset(1);
	cpu_needs_reset(2);
	credits = coincounter1 = coincounter2 = 0;
}

WRITE_HANDLER(gaplus_interrupt_ctrl_2_w)
{
	interrupt_enable_2 = address & 1;
}

WRITE_HANDLER(gaplus_interrupt_ctrl_3a_w)
{
	interrupt_enable_3 = 1;
}

WRITE_HANDLER(gaplus_interrupt_ctrl_3b_w)
{
	interrupt_enable_3 = 0;
}

WRITE_HANDLER(namcosnd_w)
{
	mappy_sound_w(address, data);
}

// ---------------------------------------------------------------------------
// Video RAM / color RAM handlers
// Video and color RAM physically live in CPU0's region but are accessed by
// BOTH the main (CPU0) and sub (CPU1) CPUs.  The read handlers therefore go
// through the shared videoram/colorram pointers (which point into
// memory_region[CPU0]) rather than MRA_RAM, which is active-CPU relative.
// ---------------------------------------------------------------------------

READ_HANDLER(gaplus_videoram_r)
{
	return videoram[address];
}

READ_HANDLER(gaplus_colorram_r)
{
	return colorram[address];
}

WRITE_HANDLER(gaplus_videoram_w)
{
	if (videoram[address] != (unsigned char)data)
	{
		dirtybuffer[address] = 1;
		videoram[address] = (unsigned char)data;
	}
}

WRITE_HANDLER(gaplus_colorram_w)
{
	if (colorram[address] != (unsigned char)data)
	{
		dirtybuffer[address] = 1;
		colorram[address] = (unsigned char)data;
	}
}

// ---------------------------------------------------------------------------
// GFX layouts
// ---------------------------------------------------------------------------

/* All layouts are rotated 90 degrees vs. the original MAME gaplus layouts
   (X/Y address arrays swapped, new X reversed) so the tiles/sprites decode
   in portrait orientation -- matching the mappy convention, which lets the
   driver draw upright with ORIENTATION_DEFAULT instead of relying on a
   hardware ROT90 the AAE raster path does not apply. */

static struct GfxLayout charlayout1 =
{
	8,8,											/* 8*8 characters */
	256,											/* 256 characters */
	2,											  	/* 2 bits per pixel */
	{ 4, 6 },				 						/* the 2 bitplanes are packed into one nibble */
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 16 * 8, 16 * 8 + 1, 24 * 8, 24 * 8 + 1, 0, 1, 8 * 8, 8 * 8 + 1 },
	32 * 8
};

static struct GfxLayout charlayout2 =
{
	8,8,											/* 8*8 characters */
	256,											/* 256 characters */
	2,												/* 2 bits per pixel */
	{ 0, 2 },										/* the 2 bitplanes are packed into one nibble */
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 16 * 8, 16 * 8 + 1, 24 * 8, 24 * 8 + 1, 0, 1, 8 * 8, 8 * 8 + 1 },
	32 * 8
};

static struct GfxLayout spritelayout1 =
{
	16,16,			/* 16*16 sprites */
	128,			/* 128 sprites */
	3,				/* 3 bits per pixel */
	{ 0, 8192 * 8 + 0, 8192 * 8 + 4 },
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	  7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8		   /* every sprite takes 64 bytes */
};

static struct GfxLayout spritelayout2 =
{
	16,16,			/* 16*16 sprites */
	128,			/* 128 sprites */
	3,				/* 3 bits per pixel */
	{ 4, 8192 * 8 * 2 + 0, 8192 * 8 * 2 + 4 },
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	  7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8		   /* every sprite takes 64 bytes */
};

static struct GfxLayout spritelayout3 = {
	16,16,										   /* 16*16 sprites */
	128,										   /* 128 sprites */
	3,											   /* 3 bits per pixel (one is always 0) */
	{ 8192 * 8 + 0, 0, 4 },							   /* the two bitplanes are packed into one byte */
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	  7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8											/* every sprite takes 64 bytes */
};

struct GfxDecodeInfo gaplus_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &charlayout1,      0, 64 },
	{ REGION_GFX1, 0x0000, &charlayout2,      0, 64 },
	{ REGION_GFX2, 0x0000, &spritelayout1, 64 * 4, 64 },
	{ REGION_GFX2, 0x0000, &spritelayout2, 64 * 4, 64 },
	{ REGION_GFX2, 0x6000, &spritelayout3, 64 * 4, 64 },
	{ -1 } /* end of table */
};

// ---------------------------------------------------------------------------
// Video start / stop / refresh -- implementation is in gaplus_video.cpp
// Declarations here; the actual code lives in a companion file following the
// same split used in the original MAME source (vidhrdw/gaplus.c).
// ---------------------------------------------------------------------------

/* gaplus_scroll is read by gaplus_vh_screenrefresh; exposed via gaplus.h */
unsigned char gaplus_scroll = 0;

/* forward declarations implemented in gaplus_video.cpp */
extern int  gaplus_vh_start(void);
extern void gaplus_vh_stop(void);
extern void gaplus_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh);

// ---------------------------------------------------------------------------
// run_* callbacks: called every frame by the AAE emulator core
// ---------------------------------------------------------------------------

void run_gaplus(void)
{
	watchdog_reset_w(0, 0, 0);
	gaplus_vh_screenrefresh(Machine->scrbitmap, 0);
	namco_sh_update();
}

// ---------------------------------------------------------------------------
// init_* functions: pointer fixup and subsystem startup
// ---------------------------------------------------------------------------

int init_gaplus(void)
{
	cpu_setOPbaseoverride(nullptr);
	/* video RAM at 0x0000, color RAM at 0x0400 (both in CPU0's region,
	   shared between the main and sub CPUs) */
	videoram = &Machine->memory_region[CPU0][0x0000];
	videoram_size = 0x0400;
	colorram = &Machine->memory_region[CPU0][0x0400];

	/* shared RAM (incl. sprite RAM) at 0x0800; custom I/O chips at 0x6800;
	   sound CPU shared RAM at 0x6040 -- all within CPU0's region */
	gaplus_sharedram = &Machine->memory_region[CPU0][0x0800];
	gaplus_customio_1 = &Machine->memory_region[CPU0][0x6800];
	gaplus_customio_2 = &Machine->memory_region[CPU0][0x6810];
	gaplus_customio_3 = &Machine->memory_region[CPU0][0x6820];

	gaplus_snd_sharedram = &Machine->memory_region[CPU0][0x6000];
	interrupt_enable_2 = interrupt_enable_3 = 1;
	credits = coincounter1 = coincounter2 = 0;

	/* gaplus runs all three CPUs from the start; the main CPU pulses the
	   sub/sound CPUs via gaplus_reset_2_3_w (0x8c00) rather than halt lines */
	cpu_enable(CPU0, 1);
	cpu_enable(CPU1, 0);
	cpu_enable(CPU2, 0);

	gaplus_init_machine();
	gaplus_vh_start();
	namco_sh_start(&namco_interface);

	return 1;
}

void end_gaplus(void)
{
	gaplus_vh_stop();
}

// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------

/*
 * CPU1 (MAIN) read map -- gaplus
 * Video RAM (0000-03ff) and color RAM (0400-07ff) are read through dedicated
 * handlers so both the main and sub CPU see the same shared CPU0 region.
 */
MEM_READ(gaplus_readmem_cpu1)
MEM_ADDR(0x0000, 0x03ff, gaplus_videoram_r)		/* video RAM */
MEM_ADDR(0x0400, 0x07ff, gaplus_colorram_r)		/* color RAM */
MEM_ADDR(0x0800, 0x1fff, gaplus_sharedram_r)	/* shared RAM with CPU #2 & spriteram */
MEM_ADDR(0x6000, 0x63ff, gaplus_snd_sharedram_r)	/* shared RAM with CPU #3 */
MEM_ADDR(0x6800, 0x680f, gaplus_customio_1_r)	/* custom I/O chip #1 interface */
MEM_ADDR(0x6810, 0x681f, gaplus_customio_2_r)	/* custom I/O chip #2 interface */
MEM_ADDR(0x6820, 0x682f, gaplus_customio_3_r)	/* custom I/O chip #3 interface */
MEM_ADDR(0x7820, 0x782f, MRA_RAM)				/* ??? */
MEM_ADDR(0x7c00, 0x7c01, MRA_NOP)				/* ??? */
MEM_ADDR(0xa000, 0xffff, MRA_ROM)				/* ROM */
MEM_END

/* CPU1 (MAIN) write map -- gaplus */
MEM_WRITE(gaplus_writemem_cpu1)
MEM_ADDR(0x0000, 0x03ff, gaplus_videoram_w)		/* video RAM */
MEM_ADDR(0x0400, 0x07ff, gaplus_colorram_w)		/* color RAM */
MEM_ADDR(0x0800, 0x1fff, gaplus_sharedram_w)	/* shared RAM with CPU #2 */
MEM_ADDR(0x6000, 0x63ff, gaplus_snd_sharedram_w)	/* shared RAM with CPU #3 */
MEM_ADDR(0x6800, 0x680f, gaplus_customio_1_w)	/* custom I/O chip #1 interface */
MEM_ADDR(0x6810, 0x681f, gaplus_customio_2_w)	/* custom I/O chip #2 interface */
MEM_ADDR(0x6820, 0x682f, gaplus_customio_3_w)	/* custom I/O chip #3 interface */
MEM_ADDR(0x7820, 0x782f, MWA_RAM)				/* ??? */
MEM_ADDR(0x8c00, 0x8c00, gaplus_reset_2_3_w)	/* reset CPU #2 & #3 */
MEM_ADDR(0xa000, 0xa003, gaplus_starfield_control_w)	/* starfield control */
MEM_ADDR(0xa000, 0xffff, MWA_ROM)				/* ROM */
MEM_END

/* CPU2 (SUB) read map -- gaplus */
MEM_READ(gaplus_readmem_cpu2)
MEM_ADDR(0x0000, 0x03ff, gaplus_videoram_r)		/* video RAM */
MEM_ADDR(0x0400, 0x07ff, gaplus_colorram_r)		/* color RAM */
MEM_ADDR(0x0800, 0x1fff, gaplus_sharedram_r)	/* shared RAM with CPU #1 & spriteram */
MEM_ADDR(0xa000, 0xffff, MRA_ROM)				/* ROM */
MEM_END

/* CPU2 (SUB) write map -- gaplus */
MEM_WRITE(gaplus_writemem_cpu2)
MEM_ADDR(0x0000, 0x03ff, gaplus_videoram_w)		/* video RAM */
MEM_ADDR(0x0400, 0x07ff, gaplus_colorram_w)		/* color RAM */
MEM_ADDR(0x0800, 0x1fff, gaplus_sharedram_w)	/* shared RAM with CPU #1 */
MEM_ADDR(0x6080, 0x6081, gaplus_interrupt_ctrl_2_w)	/* IRQ 2 enable */
MEM_ADDR(0xa000, 0xffff, MWA_ROM)				/* ROM */
MEM_END

/* CPU3 (SOUND) read map -- gaplus */
MEM_READ(gaplus_readmem_cpu3)
MEM_ADDR(0x0000, 0x003f, MRA_RAM)				/* sound registers */
MEM_ADDR(0x0000, 0x03ff, gaplus_snd_sharedram_r)	/* shared RAM with CPU #1 */
MEM_ADDR(0xe000, 0xffff, MRA_ROM)				/* ROM */
MEM_END

/* CPU3 (SOUND) write map -- gaplus */
MEM_WRITE(gaplus_writemem_cpu3)
MEM_ADDR(0x0000, 0x003f, namcosnd_w)			/* sound registers */
MEM_ADDR(0x0000, 0x03ff, gaplus_snd_sharedram_w)	/* shared RAM with the main CPU */
MEM_ADDR(0x3000, 0x3000, watchdog_reset_w)		/* watchdog */
MEM_ADDR(0x4000, 0x4000, gaplus_interrupt_ctrl_3a_w)	/* interrupt enable */
MEM_ADDR(0x6000, 0x6000, gaplus_interrupt_ctrl_3b_w)	/* interrupt disable */
MEM_ADDR(0xe000, 0xffff, MWA_ROM)				/* ROM */
MEM_END
// ---------------------------------------------------------------------------
// Input ports
// ---------------------------------------------------------------------------

INPUT_PORTS_START(gaplus)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x03, DEF_STR(3C_1C))
PORT_DIPSETTING(0x02, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x04, "2")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x08, "4")
PORT_DIPSETTING(0x0c, "5")
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, DEF_STR(Cabinet))
PORT_DIPSETTING(0x20, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(0xc0, DEF_STR(3C_1C))
PORT_DIPSETTING(0x80, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x40, DEF_STR(1C_2C))

PORT_START("DSW1")
PORT_DIPNAME(0x07, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x01, "1")
PORT_DIPSETTING(0x02, "2")
PORT_DIPSETTING(0x03, "3")
PORT_DIPSETTING(0x04, "4")
PORT_DIPSETTING(0x05, "5")
PORT_DIPSETTING(0x06, "6")
PORT_DIPSETTING(0x07, "7")
PORT_SERVICE(0x08, IP_ACTIVE_HIGH)
PORT_BITX(0x10, 0x00, IPT_DIPSWITCH_NAME | IPF_CHEAT, "Rack Test", OSD_KEY_F1, IP_JOY_NONE)
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x10, DEF_STR(On))
PORT_DIPNAME(0xe0, 0xe0, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0xe0, "30k 70k and every 70k")
PORT_DIPSETTING(0xc0, "30k 100k and every 100k")
PORT_DIPSETTING(0xa0, "30k 100k and every 200k")
PORT_DIPSETTING(0x80, "50k 100k and every 100k")
PORT_DIPSETTING(0x60, "50k 100k and every 200k")
PORT_DIPSETTING(0x00, "50k 150k and every 150k")
PORT_DIPSETTING(0x40, "50k 150k and every 300k")
PORT_DIPSETTING(0x20, "50k 150k")

PORT_START("IN0")   /* FAKE: joystick -- read by gaplus_customio_r_1 port 3 */
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_HIGH, IPT_START1, 1)
PORT_BIT_IMPULSE(0x02, IP_ACTIVE_HIGH, IPT_START2, 1)
/* 0x08 service switch (not implemented yet) */
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_HIGH, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_HIGH, IPT_COIN2, 1)

PORT_START("IN1")   /* FAKE: coins and start -- read by gaplus_customio_r_1 port 4 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_PLAYER2)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_PLAYER2)

PORT_START("IN2")   /* FAKE: coins and start -- read by gaplus_customio_r_1 port 4 */
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1, 1)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2, 1)
PORT_BITX(0x08, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)

INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROM definitions
// The GFX region (REGION_GFX1) is used as a temporary decode buffer that
// becomes permanent after gaplus_vh_convert_color_prom runs.
// ---------------------------------------------------------------------------
ROM_START(gaplus)
ROM_REGION(0x10000, REGION_CPU1, 0) /* 64k for the MAIN CPU */
ROM_LOAD("gp2-4.64", 0xa000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-3.64", 0xc000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-2.64", 0xe000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))

ROM_REGION(0x10000, REGION_CPU2, 0) /* 64k for the SUB CPU */
ROM_LOAD("gp2-8.64", 0xa000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-7.64", 0xc000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-6.64", 0xe000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))

ROM_REGION(0x10000, REGION_CPU3, 0) /* 64k for the SOUND CPU */
ROM_LOAD("gp2-1.64", 0xe000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("gp2-5.64", 0x0000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))

ROM_REGION(0xa000, REGION_GFX2, REGIONFLAG_DISPOSE)
ROM_LOAD("gp2-9.64", 0x0000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-11.64", 0x2000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-10.64", 0x4000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-12.64", 0x6000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
/* 0xa000-0xbfff empty space to decode sprite set #3 as 3 bits per pixel */

ROM_REGION(0x0800, REGION_PROMS, 0)
ROM_LOAD("gp2-1p.bin", 0x0000, 0x0100, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-1n.bin", 0x0100, 0x0100, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-2n.bin", 0x0200, 0x0100, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-6s.bin", 0x0300, 0x0100, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-6p.bin", 0x0400, 0x0200, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-6n.bin", 0x0600, 0x0200, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))

ROM_REGION(0x0100, REGION_SOUND1, 0) /* sound prom */
ROM_LOAD("gp2-3f.bin", 0x0000, 0x0100, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_END

ROM_START(gaplusa)
ROM_REGION(0x10000, REGION_CPU1, 0) /* 64k for the MAIN CPU */
ROM_LOAD("gp2-4.8d", 0xa000, 0x2000, CRC(e525d75d) SHA1(93fcd8b940491abf6344181811d0b35765d7e45c))
ROM_LOAD("gp2-3b.8c", 0xc000, 0x2000, CRC(d77840a4) SHA1(81402b28a2d5ac2d1301252534afa0cb65d7e162))
ROM_LOAD("gp2-2b.8b", 0xe000, 0x2000, CRC(b3cb90db) SHA1(025c2f3978772e1ecbbf36842dc7c2203ee91a1f))

ROM_REGION(0x10000, REGION_CPU2, 0) /* 64k for the SUB CPU */
ROM_LOAD("gp2-8.11d", 0xa000, 0x2000, CRC(42b9fd7c) SHA1(f230eb0ad757f0714c0ac81c812e950778452947))
ROM_LOAD("gp2-7.11c", 0xc000, 0x2000, CRC(0621f7df) SHA1(b86020f819fefb134cb57e203f7c90b1b29581c8))
ROM_LOAD("gp2-6.11b", 0xe000, 0x2000, CRC(75b18652) SHA1(398059da967c80321a9ec94d982a6c0b3c970c5f))

ROM_REGION(0x10000, REGION_CPU3, 0) /* 64k for the SOUND CPU */
ROM_LOAD("gp2-1.4b", 0xe000, 0x2000, CRC(ed8aa206) SHA1(4e0a31d84cb7aca497485dbe0240009d58275765))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("gp2-5.8s", 0x0000, 0x2000, CRC(f3d19987) SHA1(a0107fa4659597ac42c875ab1c0deb845534268b))	/* characters */
/* 0x2000-0x3fff  will be unpacked from 0x0000-0x1fff */

ROM_REGION(0xa000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("gp2-11.11p", 0x0000, 0x2000, CRC(57740ff9) SHA1(16873e0ac5f975768d596d7d32af7571f4817f2b))	/* objects */
ROM_LOAD("gp2-10.11n", 0x2000, 0x2000, CRC(6cd8ce11) SHA1(fc346e98737c9fc20810e32d4c150ae4b4051979))	/* objects */
ROM_LOAD("gp2-12.11r", 0x4000, 0x2000, CRC(7316a1f1) SHA1(368e4541a5151e906a189712bc05192c2ceec8ae))	/* objects */
ROM_LOAD("gp2-9.11m", 0x6000, 0x2000, CRC(e6a9ae67) SHA1(99c1e67c3b216aa1b63f199e21c73cdedde80e1b))	/* objects */
/* 0xa000-0xbfff empty space to decode sprite set #3 as 3 bits per pixel */

ROM_REGION(0x0800, REGION_PROMS, 0)
ROM_LOAD("gp2-3.1p", 0x0000, 0x0100, CRC(a5091352) SHA1(dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3))	/* red palette ROM (4 bits) */
ROM_LOAD("gp2-1.1n", 0x0100, 0x0100, CRC(8bc8022a) SHA1(c76f9d9b066e268621d41a703c5280261234709a))	/* green palette ROM (4 bits) */
ROM_LOAD("gp2-2.2n", 0x0200, 0x0100, CRC(8dabc20b) SHA1(64d7b333f529d3ba66aeefd380fd1cbf9ddf460d))	/* blue palette ROM (4 bits) */
ROM_LOAD("gp2-7.6s", 0x0300, 0x0100, CRC(2faa3e09) SHA1(781ffe9088476798409cb922350eff881590cf35))	/* char color ROM */
ROM_LOAD("gp2-6.6p", 0x0400, 0x0200, CRC(6f99c2da) SHA1(955dcef363870ee8e91edc73b9ea3ce489738aad))	/* sprite color ROM (lower 4 bits) */
ROM_LOAD("gp2-5.6n", 0x0600, 0x0200, CRC(c7d31657) SHA1(a93a5bc448dc127e1389d10a9cb06acadfe940cf))	/* sprite color ROM (upper 4 bits) */

ROM_REGION(0x0100, REGION_SOUND1, 0) /* sound prom */
ROM_LOAD("gp2-4.3f", 0x0000, 0x0100, CRC(2d9fbdd8) SHA1(e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4))

ROM_END

// ---------------------------------------------------------------------------
// Driver descriptors
// cpu_slices = 100 as in original MAME for tight dual-CPU synchronization.
// ---------------------------------------------------------------------------

/* ---- gaplus (US) ---- */
AAE_DRIVER_BEGIN(drv_gaplus, "gaplus", "gaplus (US)")
AAE_DRIVER_ROM(rom_gaplus)
AAE_DRIVER_FUNCS(&init_gaplus, &run_gaplus, &end_gaplus)
AAE_DRIVER_INPUT(input_ports_gaplus)
AAE_DRIVER_SAMPLES(gaplus_sample_names)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	/* CPU0: main game CPU */
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1536000,
		/*div*/      200,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &gaplus_interrupt_1,
		/*r8*/       gaplus_readmem_cpu1,
		/*w8*/       gaplus_writemem_cpu1,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	/* CPU1: sound CPU */
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1536000,
		/*div*/      200,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &gaplus_interrupt_2,
		/*r8*/       gaplus_readmem_cpu2,
		/*w8*/       gaplus_writemem_cpu2,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1536000,
		/*div*/      200,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &gaplus_interrupt_3,
		/*r8*/       gaplus_readmem_cpu3,
		/*w8*/       gaplus_writemem_cpu3,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),

	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(gaplus_gfxdecodeinfo, 256, 64 * 4 + 64 * 8, gaplus_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_gaplus)