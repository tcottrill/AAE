//============================================================================
// Dig Dug driver for AAE
//
// Triple-Z80 Namco hardware (1982)
// Derived from MAME 0.34 by Aaron Giles
//
// CPU #1: Z80 @ 3.125 MHz  -- main program, custom I/O
// CPU #2: Z80 @ 3.125 MHz  -- game logic co-processor
// CPU #3: Z80 @ 3.125 MHz  -- sound (Namco WSG)
//
// Shared RAM: $8000-$9FFF (all three CPUs)
//   $8000-$83FF  Video RAM (character codes)
//   $8400-$87FF  Color RAM
//   $8800-$9FFF  Work RAM
//   $8B80-$8BFF  Sprite RAM (code/color)
//   $9380-$93FF  Sprite RAM (position)
//   $9B80-$9BFF  Sprite RAM (control/flip)
//
// Custom I/O chip at $7000/$7100
// DIP switches at $6800-$6807 (bit-interleaved DSW0/DSW1)
// Namco WSG sound at $6800-$681F (CPU3 write)
// Video latches at $A000-$A005 (playfield select/enable/color)
// Flip screen at $A007
//============================================================================

#include "digdug.h"

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "namco.h"
#include "timer.h"

//============================================================================
// State
//============================================================================

static UINT8 interrupt_enable_1 = 0;
static UINT8 interrupt_enable_2 = 0;
static UINT8 interrupt_enable_3 = 0;
static int nmi_timer = -1;
static int flipscreen = 0;

// Playfield latches
static int playfield = 0;       // bits 0-1: playfield ROM bank select
static int alphacolor = 0;      // bit 0: alpha color select
static int playenable = 0;      // bit 0: playfield enable (0=on, 1=off)
static int playcolor = 0;       // bits 0-1: playfield palette select
static int pflastindex = -1;
static int pflastcolor = -1;

// Custom I/O chip state
static int customio_command = 0;
static int mode = 0;
static int credits = 0;
static int leftcoinpercred = 0, leftcredpercoin = 0;
static int rightcoinpercred = 0, rightcredpercoin = 0;
static unsigned char customio[16];

void digdug_nmi_generate(int param);

//============================================================================
// Namco WSG sound interface - 3 voices
//============================================================================
static struct namco_interface namco_interface =
{
	3125000 / 32,   // sample rate (~97.6 kHz)
	3,              // number of voices
	32,             // gain adjustment
	245             // playback volume
};

//============================================================================
// Video: visible area
//============================================================================
static const rectangle digdug_visible_area =
{
	0,              // min_x
	36 * 8 - 1,     // max_x  (288-1)
	0,              // min_y
	28 * 8 - 1      // max_y  (224-1)
};

//============================================================================
// GFX Layouts
//============================================================================

// Characters: 8x8, 1bpp, 128 chars
static struct GfxLayout charlayout =
{
	8, 8,           // 8x8 characters
	128,            // 128 characters
	1,              // 1 bit per pixel
	{ 0 },          // one bitplane
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8 * 8           // every char takes 8 consecutive bytes
};

// Sprites: 16x16, 2bpp, 256 sprites
static struct GfxLayout spritelayout =
{
	16, 16,         // 16x16 sprites
	256,            // 256 sprites
	2,              // 2 bits per pixel
	{ 0, 4 },       // the two bitplanes for 4 pixels are packed into one byte
	{ 0, 1, 2, 3, 8*8, 8*8+1, 8*8+2, 8*8+3,
	  16*8+0, 16*8+1, 16*8+2, 16*8+3, 24*8+0, 24*8+1, 24*8+2, 24*8+3 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8,
	  32*8, 33*8, 34*8, 35*8, 36*8, 37*8, 38*8, 39*8 },
	64 * 8          // every sprite takes 64 bytes
};

// Playfield tiles: 8x8, 2bpp, 256 tiles (REGION_GFX3)
static struct GfxLayout pftilelayout =
{
	8, 8,           // 8x8 tiles
	256,            // 256 tiles
	2,              // 2 bits per pixel
	{ 0, 4 },       // packed nibbles
	{ 8*8+0, 8*8+1, 8*8+2, 8*8+3, 0, 1, 2, 3 },  // bits packed in groups of four
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },   // characters are rotated 90 degrees
	16 * 8          // every char takes 16 bytes
};

struct GfxDecodeInfo digdug_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,              0,  8 },   // characters (1bpp, 8 colors)
	{ REGION_GFX2, 0, &spritelayout,          8*2, 64 },   // sprites
	{ REGION_GFX3, 0, &pftilelayout, 64*4 + 8*2,  64 },   // playfield tiles
	{ -1 }
};

//============================================================================
// Palette: 32 colors from PROM, plus lookup tables for chars/sprites/pf
//============================================================================
void digdug_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;

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

	// Characters: 1bpp, 8 color pairs (pen 0 = transparent bg, pen 1 = color)
	for (i = 0; i < 8; i++)
	{
		colortable[i * 2 + 0] = 0;
		colortable[i * 2 + 1] = 31 - i * 2;
	}

	// Sprites: 64 colors * 4 pens, from second PROM
	for (i = 0; i < 64 * 4; i++)
		colortable[8 * 2 + i] = 31 - ((color_prom[i + 32] & 0x0f) + 0x10);

	// Playfield tiles: 64 colors * 4 pens, from third PROM
	for (i = 64 * 4; i < 128 * 4; i++)
		colortable[8 * 2 + i] = 31 - (color_prom[i + 32] & 0x0f);
}

//============================================================================
// Video: start / stop
//============================================================================
static int digdug_vh_start(void)
{
	pflastindex = -1;
	pflastcolor = -1;
	videoram_size = 0x400;

	return generic_vh_start();
}

//============================================================================
// Video: playfield latch writes ($A000-$A005)
//============================================================================
WRITE_HANDLER(digdug_vh_latch_w)
{
	int offset = address & 0x0f;

	switch (offset)
	{
	case 0:
		playfield = (playfield & ~1) | (data & 1);
		break;
	case 1:
		playfield = (playfield & ~2) | ((data << 1) & 2);
		break;
	case 2:
		alphacolor = data & 1;
		break;
	case 3:
		playenable = data & 1;
		break;
	case 4:
		playcolor = (playcolor & ~1) | (data & 1);
		break;
	case 5:
		playcolor = (playcolor & ~2) | ((data << 1) & 2);
		break;
	}
}

//============================================================================
// Video: flip screen ($A007)
//============================================================================
WRITE_HANDLER(digdug_flipscreen_w)
{
	if (flipscreen != (data & 1))
	{
		flipscreen = data & 1;
		memset(dirtybuffer, 1, videoram_size);
	}
}

//============================================================================
// Video: screen refresh
//============================================================================
static void digdug_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh)
{
	int offs, pfindex, pfcolor_shift;
	unsigned char* pf;

	// Determine the playfield state
	if (playenable != 0)
	{
		pfindex = -1;
		pfcolor_shift = -1;
		pf = NULL;
	}
	else
	{
		pfindex = playfield;
		pfcolor_shift = playcolor;
		pf = memory_region(REGION_GFX4) + (pfindex << 10);
	}

	// Force a full redraw if the playfield bank or color changed
	if (pfindex != pflastindex || pfcolor_shift != pflastcolor)
	{
		memset(dirtybuffer, 1, videoram_size);
	}
	pflastindex = pfindex;
	pflastcolor = pfcolor_shift;

	int pfcolor_base = pfcolor_shift << 4;

	// Redraw dirty characters (and underlying playfield tiles)
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		if (dirtybuffer[offs])
		{
			int sx, sy, mx, my;

			dirtybuffer[offs] = 0;

			// Convert 32x32 memory layout to 36x28 screen coordinates
			mx = offs % 32;
			my = offs / 32;

			if (my <= 1)
			{
				sx = my + 34;
				sy = mx - 2;
			}
			else if (my >= 30)
			{
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

			unsigned char vrval = videoram[offs];

			if (pf)
			{
				// Draw the playfield tile first
				unsigned char pfval = pf[offs];
				drawgfx(tmpbitmap, Machine->gfx[2],
					pfval,
					(pfval >> 4) + pfcolor_base,
					flipscreen, flipscreen,
					8 * sx, 8 * sy,
					&digdug_visible_area, TRANSPARENCY_NONE, 0);

				// Overlay with the character (if not blank)
				if ((vrval & 0x7f) != 0x7f)
					drawgfx(tmpbitmap, Machine->gfx[0],
						vrval,
						(vrval >> 5) | ((vrval >> 4) & 1),
						flipscreen, flipscreen,
						8 * sx, 8 * sy,
						&digdug_visible_area, TRANSPARENCY_PEN, 0);
			}
			else
			{
				// No playfield  just draw the character
				drawgfx(tmpbitmap, Machine->gfx[0],
					vrval,
					(vrval >> 5) | ((vrval >> 4) & 1),
					flipscreen, flipscreen,
					8 * sx, 8 * sy,
					&digdug_visible_area, TRANSPARENCY_NONE, 0);
			}
		}
	}

	// Copy character/playfield layer to main bitmap
	copybitmap(bitmap, tmpbitmap, 0, 0, 0, 0, &digdug_visible_area, TRANSPARENCY_NONE, 0);

	// Draw sprites
	for (offs = 0; offs < spriteram_size; offs += 2)
	{
		// Is it on?
		if ((spriteram_3[offs + 1] & 2) == 0)
		{
			int sprite = spriteram[offs];
			int color  = spriteram[offs + 1];
			int x      = spriteram_2[offs + 1] - 40;
			int y      = 28 * 8 - spriteram_2[offs];
			int flipx  = spriteram_3[offs] & 1;
			int flipy  = spriteram_3[offs] & 2;

			if (flipscreen)
			{
				flipx = !flipx;
				flipy = !flipy;
			}

			if (x < 8) x += 256;

			// Normal size sprite (code < 0x80)
			if (sprite < 0x80)
			{
				drawgfx(bitmap, Machine->gfx[1],
					sprite, color, flipx, flipy, x, y,
					&digdug_visible_area, TRANSPARENCY_PEN, 0);
			}
			// Double size sprite (code >= 0x80)
			else
			{
				sprite = (sprite & 0xc0) | ((sprite & ~0xc0) << 2);

				if (!flipx && !flipy)
				{
					drawgfx(bitmap, Machine->gfx[1], 2 + sprite, color, flipx, flipy, x,      y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 3 + sprite, color, flipx, flipy, x + 16,  y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1],     sprite, color, flipx, flipy, x,      y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 1 + sprite, color, flipx, flipy, x + 16, y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
				}
				else if (flipx && flipy)
				{
					drawgfx(bitmap, Machine->gfx[1], 1 + sprite, color, flipx, flipy, x,      y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1],     sprite, color, flipx, flipy, x + 16,  y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 3 + sprite, color, flipx, flipy, x,      y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 2 + sprite, color, flipx, flipy, x + 16, y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
				}
				else if (flipy)
				{
					drawgfx(bitmap, Machine->gfx[1],     sprite, color, flipx, flipy, x,      y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 1 + sprite, color, flipx, flipy, x + 16,  y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 2 + sprite, color, flipx, flipy, x,      y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 3 + sprite, color, flipx, flipy, x + 16, y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
				}
				else // flipx only
				{
					drawgfx(bitmap, Machine->gfx[1], 3 + sprite, color, flipx, flipy, x,      y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 2 + sprite, color, flipx, flipy, x + 16,  y,      &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1], 1 + sprite, color, flipx, flipy, x,      y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
					drawgfx(bitmap, Machine->gfx[1],     sprite, color, flipx, flipy, x + 16, y - 16,  &digdug_visible_area, TRANSPARENCY_PEN, 0);
				}
			}
		}
	}
}

//============================================================================
// DSW reading: $6800-$6807 - bit-interleaved DSW0/DSW1
// Same pattern as Galaga and Bosco
//============================================================================
READ_HANDLER(digdug_dsw_r)
{
	int bit0, bit1;
	int offset = address;

	bit0 = (input_port_0_r(0) >> offset) & 1;
	bit1 = (input_port_1_r(0) >> offset) & 1;

	return bit0 | (bit1 << 1);
}

//============================================================================
// Shared RAM: $8000-$9FFF (all three CPUs share CPU0's memory)
//============================================================================
WRITE_HANDLER(digdug_sharedram_w)
{
	// Mark video RAM dirty for character redraw
	if (address < 0x400)
		dirtybuffer[address] = 1;

	Machine->memory_region[CPU0][address + 0x8000] = data;
}

READ_HANDLER(digdug_sharedram_r)
{
	return Machine->memory_region[CPU0][address + 0x8000];
}

//============================================================================
// Custom I/O chip
//============================================================================
WRITE_HANDLER(digdug_customio_data_w)
{
	int offset = address & 0x0f;
	customio[offset] = data;

	switch (customio_command)
	{
	case 0xc1:
		if (offset == 8)
		{
			leftcoinpercred  = customio[2] & 0x0f;
			leftcredpercoin  = customio[3] & 0x0f;
			rightcoinpercred = customio[4] & 0x0f;
			rightcredpercoin = customio[5] & 0x0f;
		}
		break;
	}
}

READ_HANDLER(digdug_customio_data_r)
{
	int offset = address & 0x0f;

	switch (customio_command)
	{
	case 0x71:
		if (offset == 0)
		{
			if (mode)   // switch mode
			{
				// bit 7 is the service switch
				return readinputport(4);
			}
			else        // credits mode: return number of credits in BCD
			{
				int in;
				static int leftcoininserted;
				static int rightcoininserted;

				in = readinputport(4);

				// check if the user inserted a coin
				if (leftcoinpercred > 0)
				{
					if ((in & 0x01) == 0 && credits < 99)
					{
						leftcoininserted++;
						if (leftcoininserted >= leftcoinpercred)
						{
							credits += leftcredpercoin;
							leftcoininserted = 0;
						}
					}
					if ((in & 0x02) == 0 && credits < 99)
					{
						rightcoininserted++;
						if (rightcoininserted >= rightcoinpercred)
						{
							credits += rightcredpercoin;
							rightcoininserted = 0;
						}
					}
				}
				else credits = 2;

				// check for 1 player start button
				if ((in & 0x10) == 0)
					if (credits >= 1) credits--;

				// check for 2 players start button
				if ((in & 0x20) == 0)
					if (credits >= 2) credits -= 2;

				return (credits / 10) * 16 + credits % 10;
			}
		}
		else if (offset == 1)
		{
			int p2 = readinputport(2);

			if (mode == 0)
			{
				// 8-position direction encoding:
				//     0
				//    7 1
				//   6 8 2
				//    5 3
				//     4
				if ((p2 & 0x01) == 0)       p2 = (p2 & ~0x0f) | 0x00;  // up
				else if ((p2 & 0x02) == 0)  p2 = (p2 & ~0x0f) | 0x02;  // right
				else if ((p2 & 0x04) == 0)  p2 = (p2 & ~0x0f) | 0x04;  // down
				else if ((p2 & 0x08) == 0)  p2 = (p2 & ~0x0f) | 0x06;  // left
				else                        p2 = (p2 & ~0x0f) | 0x08;  // center
			}
			return p2;
		}
		else if (offset == 2)
		{
			int p2 = readinputport(3);

			if (mode == 0)
			{
				if ((p2 & 0x01) == 0)       p2 = (p2 & ~0x0f) | 0x00;
				else if ((p2 & 0x02) == 0)  p2 = (p2 & ~0x0f) | 0x02;
				else if ((p2 & 0x04) == 0)  p2 = (p2 & ~0x0f) | 0x04;
				else if ((p2 & 0x08) == 0)  p2 = (p2 & ~0x0f) | 0x06;
				else                        p2 = (p2 & ~0x0f) | 0x08;
			}
			return p2;
		}
		break;

	case 0xb1:  // status check
		if (offset <= 2)
			return 0;
		break;

	case 0xd2:  // reading dipswitches directly
		if (offset == 0)
			return readinputport(0);
		else if (offset == 1)
			return readinputport(1);
		break;
	}

	return -1;
}

READ_HANDLER(digdug_customio_r)
{
	return customio_command;
}

WRITE_HANDLER(digdug_customio_w)
{
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

	case 0xa1:  // go into switch mode
		mode = 1;
		break;

	case 0xc1:
	case 0xe1:  // go into credit mode
		mode = 0;
		break;

	case 0xb1:  // status check - good time to reset credits
		credits = 0;
		break;
	}

	nmi_timer = timer_set(TIME_IN_USEC(50), CPU0, 0, digdug_nmi_generate);
}

//============================================================================
// NMI generator (triggered by custom I/O chip timer)
//============================================================================
void digdug_nmi_generate(int param)
{
	cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

//============================================================================
// Interrupt enables and handlers
//============================================================================
WRITE_HANDLER(digdug_interrupt_enable_1_w)
{
	interrupt_enable_1 = data & 1;
}

WRITE_HANDLER(digdug_interrupt_enable_2_w)
{
	interrupt_enable_2 = data & 1;
}

WRITE_HANDLER(digdug_interrupt_enable_3_w)
{
	interrupt_enable_3 = !(data & 1);
}

void digdugint1()
{
	if (interrupt_enable_1)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
	}
}

void digdugint2()
{
	if (interrupt_enable_2)
	{
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
	}
}

void digdugint3()
{
	if (interrupt_enable_3)
	{
		cpu_do_int_imm(CPU2, INT_TYPE_NMI);
	}
}

//============================================================================
// Halt: control CPU #2 and #3 reset lines
//============================================================================
WRITE_HANDLER(digdug_halt_w)
{
	static int reset23;

	data &= 1;
	if (data && !reset23)
	{
		cpu_needs_reset(1);
		cpu_needs_reset(2);
		cpu_enable(CPU1, 1);
		cpu_enable(CPU2, 1);
		LOG_INFO("Dig Dug: CPUs 2+3 enabled");
	}
	else if (!data)
	{
		cpu_enable(CPU1, 0);
		cpu_enable(CPU2, 0);
		LOG_INFO("Dig Dug: CPUs 2+3 disabled");
	}
	reset23 = data;
}

//============================================================================
// Sound write handler (CPU3 -> Namco WSG)
//============================================================================
WRITE_HANDLER(digdug_sound_w)
{
	namco_sound_w(address & 0x1f, data);
}

//============================================================================
// Memory Maps
//============================================================================

// Empty port handlers (Dig Dug uses no Z80 I/O ports)
PORT_READ(digdugPortRead)
PORT_END
PORT_WRITE(digdugPortWrite)
PORT_END

// CPU1: Main processor
MEM_READ(digdugCPU1_Read)
MEM_ADDR(0x6800, 0x6807, digdug_dsw_r)
MEM_ADDR(0x7000, 0x700f, digdug_customio_data_r)
MEM_ADDR(0x7100, 0x7100, digdug_customio_r)
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_r)
MEM_END

MEM_WRITE(digdugCPU1_Write)
MEM_ADDR(0x0000, 0x3fff, MWA_NOP)
MEM_ADDR(0x6820, 0x6820, digdug_interrupt_enable_1_w)
MEM_ADDR(0x6821, 0x6821, digdug_interrupt_enable_2_w)
MEM_ADDR(0x6822, 0x6822, digdug_interrupt_enable_3_w)
MEM_ADDR(0x6823, 0x6823, digdug_halt_w)
MEM_ADDR(0x6825, 0x6827, MWA_NOP)
MEM_ADDR(0x6830, 0x6830, watchdog_reset_w)         
MEM_ADDR(0x7000, 0x700f, digdug_customio_data_w)
MEM_ADDR(0x7100, 0x7100, digdug_customio_w)
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_w)
MEM_ADDR(0xa000, 0xa00f, digdug_vh_latch_w)
MEM_ADDR(0xa007, 0xa007, digdug_flipscreen_w)
MEM_END

// CPU2: Sub processor
MEM_READ(digdugCPU2_Read)
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_r)
MEM_END

MEM_WRITE(digdugCPU2_Write)
MEM_ADDR(0x0000, 0x1fff, MWA_NOP)
MEM_ADDR(0x6821, 0x6821, digdug_interrupt_enable_2_w)
MEM_ADDR(0x6830, 0x6830, watchdog_reset_w)         
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_w)
MEM_ADDR(0xa000, 0xa00f, digdug_vh_latch_w)
MEM_END

// CPU3: Sound processor
MEM_READ(digdugCPU3_Read)
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_r)
MEM_END

MEM_WRITE(digdugCPU3_Write)
MEM_ADDR(0x0000, 0x0fff, MWA_NOP)
MEM_ADDR(0x6800, 0x681f, digdug_sound_w)
MEM_ADDR(0x6822, 0x6822, digdug_interrupt_enable_3_w)
MEM_ADDR(0x8000, 0x9fff, digdug_sharedram_w)
MEM_END

//============================================================================
// init / run / end
//============================================================================
int init_digdug()
{
	LOG_INFO("Dig Dug Init called");

	// Reset state
	credits = 0;
	nmi_timer = -1;
	interrupt_enable_1 = 0;
	interrupt_enable_2 = 0;
	interrupt_enable_3 = 0;
	customio_command = 0;
	mode = 0;
	playfield = 0;
	alphacolor = 0;
	playenable = 0;
	playcolor = 0;
	pflastindex = -1;
	pflastcolor = -1;
	flipscreen = 0;

	// Set up video RAM pointers into CPU0's memory region
	videoram      = &Machine->memory_region[CPU0][0x8000];
	videoram_size = 0x400;
	colorram      = &Machine->memory_region[CPU0][0x8400];
	spriteram     = &Machine->memory_region[CPU0][0x8b80];
	spriteram_size = 0x80;
	spriteram_2   = &Machine->memory_region[CPU0][0x9380];
	spriteram_2_size = 0x80;
	spriteram_3   = &Machine->memory_region[CPU0][0x9b80];
	spriteram_3_size = 0x80;

	// Start with CPUs 2 and 3 off (CPU1 enables them via $6823)
	cpu_enable(CPU1, 0);
	cpu_enable(CPU2, 0);

	// Initialize video
	digdug_vh_start();

	// Start Namco sound interface
	namco_sh_start(&namco_interface);

	return 0;
}

void end_digdug()
{
	generic_vh_stop();
}

void run_digdug()
{
	digdug_vh_screenrefresh(Machine->scrbitmap, 0);
	namco_sh_update();
}

//============================================================================
// Input Ports
//============================================================================
INPUT_PORTS_START(digdug)
PORT_START("DSW0")  // DSW0
PORT_DIPNAME(0x07, 0x01, DEF_STR(Coin_B))
PORT_DIPSETTING(0x07, DEF_STR(3C_1C))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_1C))
PORT_DIPSETTING(0x05, DEF_STR(2C_3C))
PORT_DIPSETTING(0x06, DEF_STR(1C_2C))
PORT_DIPSETTING(0x02, DEF_STR(1C_3C))
PORT_DIPSETTING(0x04, DEF_STR(1C_6C))
PORT_DIPSETTING(0x00, DEF_STR(1C_7C))
PORT_DIPNAME(0x38, 0x18, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x20, "10k 40k 40k")
PORT_DIPSETTING(0x10, "10k 50k 50k")
PORT_DIPSETTING(0x30, "20k 60k 60k")
PORT_DIPSETTING(0x08, "20k 70k 70k")
PORT_DIPSETTING(0x28, "10k 40k")
PORT_DIPSETTING(0x18, "20k 60k")
PORT_DIPSETTING(0x38, "10k")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x40, "2")
PORT_DIPSETTING(0x80, "3")
PORT_DIPSETTING(0xc0, "5")

PORT_START("DSW1")  // DSW1
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x40, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0xc0, DEF_STR(2C_3C))
PORT_DIPSETTING(0x80, DEF_STR(1C_2C))
PORT_DIPNAME(0x20, 0x20, "Freeze")
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x08, 0x00, "Allow Continue")
PORT_DIPSETTING(0x08, DEF_STR(No))
PORT_DIPSETTING(0x00, DEF_STR(Yes))
PORT_DIPNAME(0x04, 0x04, DEF_STR(Cabinet))
PORT_DIPSETTING(0x04, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))
PORT_DIPNAME(0x03, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x02, "Medium")
PORT_DIPSETTING(0x01, "Hard")
PORT_DIPSETTING(0x03, "Hardest")

PORT_START("IO1")      // FAKE - Player 1 joystick + button
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO2")      // FAKE - Player 2 joystick + button
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_4WAY | IPF_COCKTAIL)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO3")      // FAKE - Coins + Start + Service
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_LOW, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x02, IP_ACTIVE_LOW, IPT_COIN2, 1)
PORT_BIT(0x0c, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_START1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_START2, 1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)
INPUT_PORTS_END

//============================================================================
// ROM Definitions from MAME (TM)
//============================================================================

// Dig Dug (Namco)
ROM_START(digdug)
ROM_REGION(0x10000, REGION_CPU1, 0)     /* 64k for code for the first CPU  */
ROM_LOAD("dd1a.1", 0x0000, 0x1000, CRC(a80ec984) SHA1(86689980410b9429cd7582c7a76342721c87d030))
ROM_LOAD("dd1a.2", 0x1000, 0x1000, CRC(559f00bd) SHA1(fde17785df21956d6fd06bcfe675c392dadb1524))
ROM_LOAD("dd1a.3", 0x2000, 0x1000, CRC(8cbc6fe1) SHA1(57b8a5777f8bb9773caf0cafe5408c8b9768cb25))
ROM_LOAD("dd1a.4", 0x3000, 0x1000, CRC(d066f830) SHA1(b0a615fe4a5c8742c1e4ef234ef34c369d2723b9))

ROM_REGION(0x10000, REGION_CPU2, 0)     /* 64k for the second CPU */
ROM_LOAD("dd1a.5", 0x0000, 0x1000, CRC(6687933b) SHA1(c16144de7633595ddc1450ddce379f48e7b2195a))
ROM_LOAD("dd1a.6", 0x1000, 0x1000, CRC(843d857f) SHA1(89b2ead7e478e119d33bfd67376cdf28f83de67a))

ROM_REGION(0x10000, REGION_CPU3, 0)     /* 64k for the third CPU  */
ROM_LOAD("dd1.7", 0x0000, 0x1000, CRC(a41bce72) SHA1(2b9b74f56aa7939d9d47cf29497ae11f10d78598))

ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)   /* characters (1bpp) */
ROM_LOAD("dd1.9", 0x0000, 0x0800, CRC(f14a6fe1) SHA1(0aa63300c2cb887196de590aceb98f3cf06fead4))

ROM_REGION(0x4000, REGION_GFX2, ROMREGION_DISPOSE)   /* sprites (2bpp) */
ROM_LOAD("dd1.15", 0x0000, 0x1000, CRC(e22957c8) SHA1(4700c63f4f680cb8ab8c44e6f3e1712aabd5daa4))
ROM_LOAD("dd1.14", 0x1000, 0x1000, CRC(2829ec99) SHA1(3e435c1afb2e44487cd7ba28a93ada2e5ccbb86d))
ROM_LOAD("dd1.13", 0x2000, 0x1000, CRC(458499e9) SHA1(578bd839f9218c3cf4feee1223a461144e455df8))
ROM_LOAD("dd1.12", 0x3000, 0x1000, CRC(c58252a0) SHA1(bd79e39e8a572d2b5c205e6de27ca23e43ec9f51))

ROM_REGION(0x1000, REGION_GFX3, ROMREGION_DISPOSE)   /* playfield tiles (2bpp) */
ROM_LOAD("dd1.11", 0x0000, 0x1000, CRC(7b383983) SHA1(57f1e8f5171d13f9f76bd091d81b4423b59f6b42))

ROM_REGION(0x1000, REGION_GFX4, 0)      /* 4k playfield graphics (tile map data) */
ROM_LOAD("dd1.10b", 0x0000, 0x1000, CRC(2cf399c2) SHA1(317c48818992f757b1bd0e3997fa99937f81b52c))

ROM_REGION(0x0220, REGION_PROMS, 0)
ROM_LOAD("136007.113", 0x0000, 0x0020, CRC(4cb9da99) SHA1(91a5852a15d4672c29fdcbae75921794651f960c))
ROM_LOAD("136007.111", 0x0020, 0x0100, CRC(00c7c419) SHA1(7ea149e8eb36920c3b84984b5ce623729d492fd3))
ROM_LOAD("136007.112", 0x0120, 0x0100, CRC(e9b3e08e) SHA1(a294cc4da846eb702d61678396bfcbc87d30ea95))

ROM_REGION(0x0100, REGION_SOUND1, 0)    /* sound PROM (Namco WSG waveforms) */
ROM_LOAD("136007.110", 0x0000, 0x0100, CRC(7a2815b4) SHA1(085ada18c498fdb18ecedef0ea8fe9217edb7b46))
ROM_END

//============================================================================
// Driver Definition
//============================================================================

// Dig Dug
AAE_DRIVER_BEGIN(drv_digdug, "digdug", "Dig Dug")
AAE_DRIVER_ROM(rom_digdug)
AAE_DRIVER_FUNCS(&init_digdug, &run_digdug, &end_digdug)
AAE_DRIVER_INPUT(input_ports_digdug)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0 - Main
	AAE_CPU_ENTRY(
		CPU_MZ80, 3125000, 400, 1, INT_TYPE_INT, &digdugint1,
		digdugCPU1_Read, digdugCPU1_Write,
		digdugPortRead, digdugPortWrite,
		nullptr, nullptr
	),
	// CPU1 - Sub
	AAE_CPU_ENTRY(
		CPU_MZ80, 3125000, 400, 1, INT_TYPE_INT, &digdugint2,
		digdugCPU2_Read, digdugCPU2_Write,
		digdugPortRead, digdugPortWrite,
		nullptr, nullptr
	),
	// CPU2 - Sound
	AAE_CPU_ENTRY(
		CPU_MZ80, 3125000, 400, 2, INT_TYPE_INT, &digdugint3,
		digdugCPU3_Read, digdugCPU3_Write,
		digdugPortRead, digdugPortWrite,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0, 36 * 8 - 1, 0, 28 * 8 - 1)
AAE_DRIVER_RASTER(digdug_gfxdecodeinfo, 32, 8 * 2 + 64 * 4 + 64 * 4, digdug_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_digdug)
