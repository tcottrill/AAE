// ============================================================================
// xevious.cpp -- AAE driver for Xevious
//
// Derived from MAME 0.34 sources:
//   src/drivers/xevious.c   (driver + memory maps + input ports + ROM defs)
//   src/machine/xevious.c   (machine emulation: custom I/O, interrupts, etc.)
//   Original MAME code copyright the MAME Team.
//
// Hardware: Triple Z80 @ 3.125 MHz, Namco custom I/O chip,
//           Namco WSG 3-voice sound, background + foreground scroll planes,
//           3 sprite sets, "planet map" ROM lookup (schematic 9B).
//
// Games covered:
//   xevious   - Xevious (Namco)
//
// Memory map: see xevious.h header and inline comments below.
// ============================================================================

#include "xevious.h"

#include "aae_mame_driver.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "cpu_control.h"
#include "inptport.h"
#include "timer.h"

#pragma warning(disable : 4838 4003)


// ============================================================================
// Forward declarations for video routines in xevious_video.cpp
// ============================================================================
extern unsigned char* xevious_videoram2;
extern unsigned char* xevious_colorram2;

// These are declared in xevious_video.cpp:
//   xevious_vh_latch_w, xevious_videoram2_w, xevious_colorram2_w

// ============================================================================
// Machine state (from xevious_machine_34.c)
// ============================================================================

static int interrupt_enable_1, interrupt_enable_2, interrupt_enable_3;


/* Shared RAM pointer */
static unsigned char* xevious_sharedram;

// Planet map ROMs (schematic 9B)
static unsigned char* rom2a;
static unsigned char* rom2b;
static unsigned char* rom2c;
static int xevious_bs[2];

// Custom I/O chip state
static int customio_command;
static int io_mode, credits;
static int auxcoinpercred, auxcredpercoin;
static int leftcoinpercred, leftcredpercoin;
static int rightcoinpercred, rightcredpercoin;
static unsigned char customio[16];

// NMI timer handle
static int nmi_timer = -1;

// Namco joystick direction lookup table
//   Input bitmap: bit0=UP, bit1=RIGHT, bit2=DOWN, bit3=LEFT
//   Output:       0=U, 1=UR, 2=R, 3=DR, 4=D, 5=DL, 6=L, 7=UL, 8=center
static unsigned char namco_key[16] =
/*  LDRU  LDR  LDU  LD   LRU  LR   LU   L    DRU  DR   DU   D    RU   R    U    NON */
{ 5,   5,   5,   5,   7,   6,   7,   6,   3,   3,   4,   4,   1,   2,   0,   8 };

// ============================================================================
// Planet map ROM lookup (schematic 9B)
// ============================================================================

WRITE_HANDLER(xevious_bs_w)
{
	xevious_bs[address & 0x01] = data;
}

READ_HANDLER(xevious_bb_r)
{
	int adr_2b = 0;
	int adr_2c = 0;
	int dat1 = 0;
	int  dat2 = 0;
	int offset = address & 0x01;

	/* get BS to 12-bit data from ROMs 2A and 2B */
	adr_2b = ((xevious_bs[1] & 0x7e) << 6) | ((xevious_bs[0] & 0xfe) >> 1);

	if (adr_2b & 1)
	{
		/* high nibble select */
		dat1 = ((rom2a[adr_2b >> 1] & 0xf0) << 4) | rom2b[adr_2b];
	}
	else
	{
		/* low nibble select */
		dat1 = ((rom2a[adr_2b >> 1] & 0x0f) << 8) | rom2b[adr_2b];
	}

	adr_2c = (dat1 & 0x1ff) << 2;

	if (offset & 0x01)
		adr_2c += (1 << 11);   /* signal 4H to A11 */

	if ((xevious_bs[0] & 1) ^ ((dat1 >> 10) & 1))
		adr_2c |= 1;

	if ((xevious_bs[1] & 1) ^ ((dat1 >> 9) & 1))
		adr_2c |= 2;

	if (offset & 0x01)
	{
		/* return BB1 */
		dat2 = rom2c[adr_2c];
	}
	else
	{
		/* return BB0 */
		dat2 = rom2c[adr_2c];
		/* swap bits 6 & 7 */
		dat2 = (dat2 & 0x3f) | ((dat2 & 0x80) >> 1) | ((dat2 & 0x40) << 1);
		/* flip x & y */
		dat2 ^= (dat1 >> 4) & 0x40;
		dat2 ^= (dat1 >> 2) & 0x80;
	}

	return dat2;
}

// ============================================================================
// DIP switch read -- interleaves two DIP switch banks
// ============================================================================

READ_HANDLER(xevious_dsw_r)
{
	int bit0, bit1;

	bit0 = (input_port_0_r(0) >> address) & 1;
	bit1 = (input_port_1_r(0) >> address) & 1;

	return bit0 | (bit1 << 1);
}

// ============================================================================
// Custom I/O chip emulation
// ============================================================================

WRITE_HANDLER(xevious_customio_data_w)
{
	customio[address] = data;

	switch (customio_command)
	{
	case 0xa1:
		if (address == 0)
		{
			if (data == 0x05)
				io_mode = 1;    // switch mode
			else
			{
				credits = 0;
				io_mode = 0;    // credit mode
			}
		}
		else if (address == 7)
		{
			auxcoinpercred = customio[1];
			auxcredpercoin = customio[2];
			leftcoinpercred = customio[3];
			leftcredpercoin = customio[4];
			rightcoinpercred = customio[5];
			rightcredpercoin = customio[6];
		}
		break;

	case 0x68:
		if (address == 6)
		{
			/* it is not known how the parameters control the explosion. */
			/* We just use samples. */
			if (memcmp(customio, "\x40\x40\x40\x01\xff\x00\x20", 7) == 0)
			{
				sample_start(0, 0, 0);
			}
			else if (memcmp(customio, "\x30\x40\x00\x02\xdf\x00\x10", 7) == 0)
			{
				sample_start(0, 1, 0);
			}
			else if (memcmp(customio, "\x30\x10\x00\x80\xff\x00\x10", 7) == 0)
			{
				sample_start(0, 2, 0);
			}
			else if (memcmp(customio, "\x30\x80\x80\x01\xff\x00\x10", 7) == 0)
			{
				sample_start(0, 3, 0);
			}
			else
			{
				LOG_ERROR("%04x: custom IO offset %02x\n", cpu_getpc(), address);
				LOG_ERROR("data[0]=%02x\n", customio[0]);
				LOG_ERROR("data[1]=%02x\n", customio[1]);
				LOG_ERROR("data[2]=%02x\n", customio[2]);
				LOG_ERROR("data[3]=%02x\n", customio[3]);
				LOG_ERROR("data[4]=%02x\n", customio[4]);
				LOG_ERROR("data[5]=%02x\n", customio[5]);
				LOG_ERROR("data[6]=%02x\n", customio[6]);
			}
		}
		break;
	}
}

READ_HANDLER(xevious_customio_data_r)
{
	switch (customio_command)
	{
	case 0x71:  // read input
	case 0xb1:  // issued after 0xe1 (go into credit mode)
		if (address == 0)
		{
			if (io_mode)    // switch mode
			{
				return readinputport(4);
			}
			else            // credits mode: return credits in BCD
			{
				int in;
				static int leftcoininserted;
				static int rightcoininserted;
				static int auxcoininserted;

				in = readinputport(4);

				if (leftcoinpercred > 0)
				{
					if ((in & 0x10) == 0 && credits < 99)
					{
						leftcoininserted++;
						if (leftcoininserted >= leftcoinpercred)
						{
							credits += leftcredpercoin;
							leftcoininserted = 0;
						}
					}
					if ((in & 0x20) == 0 && credits < 99)
					{
						rightcoininserted++;
						if (rightcoininserted >= rightcoinpercred)
						{
							credits += rightcredpercoin;
							rightcoininserted = 0;
						}
					}
					if ((in & 0x40) == 0 && credits < 99)
					{
						auxcoininserted++;
						if (auxcoininserted >= auxcoinpercred)
						{
							credits += auxcredpercoin;
							auxcoininserted = 0;
						}
					}
				}
				else
				{
					credits = 2;
				}

				// 1P start
				if ((in & 0x04) == 0)
					if (credits >= 1) credits--;

				// 2P start
				if ((in & 0x08) == 0)
					if (credits >= 2) credits -= 2;

				return (credits / 10) * 16 + credits % 10;
			}
		}
		else if (address == 1)
		{
			int in = readinputport(2);  // player 1
			if (io_mode == 0)
				in = namco_key[in & 0x0f] | (in & 0xf0);
			return in;
		}
		else if (address == 2)
		{
			int in = readinputport(3);  // player 2
			if (io_mode == 0)
				in = namco_key[in & 0x0f] | (in & 0xf0);
			return in;
		}
		break;

	case 0x74:  // protection data read
		if (address == 3)
		{
			if (customio[0] == 0x80 || customio[0] == 0x10)
				return 0x05;    // 1st check
			else
				return 0x95;    // 2nd check
		}
		else
			return 0x00;
		break;
	}

	return 0xff;
}

READ_HANDLER(xevious_customio_r)
{
	return customio_command;
}

void xevious_nmi_generate(int param)
{
	cpu_do_int_imm(CPU0, INT_TYPE_NMI);
}

WRITE_HANDLER(xevious_customio_w)
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
	}

	nmi_timer = timer_set(TIME_IN_USEC(50), CPU0, 0, xevious_nmi_generate);
}

// ============================================================================
// CPU halt / reset control
// ============================================================================

WRITE_HANDLER(xevious_halt_w)
{
	static int reset23;

	data &= 1;
	if (data && !reset23)
	{
		cpu_needs_reset(1);
		cpu_needs_reset(2);
		cpu_enable(CPU1, 1);
		cpu_enable(CPU2, 1);
	}
	else if (!data)
	{
		cpu_enable(CPU1, 0);
		cpu_enable(CPU2, 0);
	}

	reset23 = data;
}

// ============================================================================
// Interrupt enable / handlers
// ============================================================================

WRITE_HANDLER(xevious_interrupt_enable_1_w)
{
	interrupt_enable_1 = (data & 1);
}

WRITE_HANDLER(xevious_interrupt_enable_2_w)
{
	interrupt_enable_2 = (data & 1);
}

WRITE_HANDLER(xevious_interrupt_enable_3_w)
{
	interrupt_enable_3 = !(data & 1);
}

void xevious_interrupt_1(void)
{
	if (interrupt_enable_1)
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

void xevious_interrupt_2(void)
{
	if (interrupt_enable_2)
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
}

void xevious_interrupt_3(void)
{
	if (interrupt_enable_3)
		cpu_do_int_imm(CPU2, INT_TYPE_NMI);
}

/***************************************************************************
  Shared RAM handlers
***************************************************************************/

READ_HANDLER(xevious_sharedram_r)
{
	return xevious_sharedram[address];
}

WRITE_HANDLER(xevious_sharedram_w)
{
	xevious_sharedram[address] = data;
}

// ============================================================================
// Video RAM write-through handlers (foreground)
// ============================================================================

WRITE_HANDLER(xevious_videoram_w)
{
	if (videoram[address] != (unsigned char)data)
	{
		dirtybuffer[address] = 1;
		videoram[address] = (unsigned char)data;
	}
}

WRITE_HANDLER(xevious_colorram_w)
{
	if (colorram[address] != (unsigned char)data)
	{
		dirtybuffer[address] = 1;
		colorram[address] = (unsigned char)data;
	}
}

// ============================================================================
// Video RAM write-through handlers (background) -- call into xevious_video.cpp
// ============================================================================

// Implemented in xevious_video.cpp:
extern void xevious_videoram2_w_impl(int offset, int data);
extern void xevious_colorram2_w_impl(int offset, int data);
extern void xevious_vh_latch_w_impl(int offset, int data);

WRITE_HANDLER(xevious_videoram2_w)
{
	xevious_videoram2_w_impl(address, data);
}

WRITE_HANDLER(xevious_colorram2_w)
{
	xevious_colorram2_w_impl(address, data);
}

WRITE_HANDLER(xevious_vh_latch_w)
{
	xevious_vh_latch_w_impl(address, data);
}

// ============================================================================
// Sound write handler (Pengo/Namco 3-voice through CPU3)
// ============================================================================

WRITE_HANDLER(xevious_sound_w)
{
	pengo_sound_w(address, data);
}

/***************************************************************************
  Null write handler (ROM, watchdog)
***************************************************************************/

WRITE_HANDLER(xevious_no_write)
{
}

// ============================================================================
// Memory maps
// ============================================================================

// --- CPU 1 (Master) ---
MEM_READ(XeviousCPU1_Read)
MEM_ADDR(0x6800, 0x6807, xevious_dsw_r)
MEM_ADDR(0x7000, 0x700f, xevious_customio_data_r)
MEM_ADDR(0x7100, 0x7100, xevious_customio_r)
MEM_ADDR(0x7800, 0xcfff, xevious_sharedram_r)
MEM_ADDR(0xf000, 0xffff, xevious_bb_r)
MEM_END

MEM_WRITE(XeviousCPU1_Write)
MEM_ADDR(0x0000, 0x3fff, xevious_no_write)
MEM_ADDR(0x6820, 0x6820, xevious_interrupt_enable_1_w)
MEM_ADDR(0x6821, 0x6821, xevious_interrupt_enable_2_w)
MEM_ADDR(0x6822, 0x6822, xevious_interrupt_enable_3_w)
MEM_ADDR(0x6823, 0x6823, xevious_halt_w)
MEM_ADDR(0x6830, 0x683f, watchdog_reset_w)  // watchdog
MEM_ADDR(0x7000, 0x700f, xevious_customio_data_w)
MEM_ADDR(0x7100, 0x7100, xevious_customio_w)
MEM_ADDR(0x7800, 0xafff, xevious_sharedram_w)
MEM_ADDR(0xb000, 0xb7ff, xevious_colorram_w)
MEM_ADDR(0xb800, 0xbfff, xevious_colorram2_w)
MEM_ADDR(0xc000, 0xc7ff, xevious_videoram_w)
MEM_ADDR(0xc800, 0xcfff, xevious_videoram2_w)
MEM_ADDR(0xd000, 0xd07f, xevious_vh_latch_w)
MEM_ADDR(0xf000, 0xffff, xevious_bs_w)
MEM_END

// --- CPU 2 (Motion) ---
MEM_READ(XeviousCPU2_Read)
MEM_ADDR(0x6800, 0x6807, xevious_dsw_r)
MEM_ADDR(0x7800, 0xcfff, xevious_sharedram_r)
MEM_ADDR(0xf000, 0xffff, xevious_bb_r)
MEM_END

MEM_WRITE(XeviousCPU2_Write)
MEM_ADDR(0x0000, 0x1fff, xevious_no_write)
MEM_ADDR(0x6823, 0x6823, xevious_halt_w)
MEM_ADDR(0x6830, 0x683f, watchdog_reset_w)  // watchdog
MEM_ADDR(0x7800, 0xafff, xevious_sharedram_w)
MEM_ADDR(0xb000, 0xb7ff, xevious_colorram_w)
MEM_ADDR(0xb800, 0xbfff, xevious_colorram2_w)
MEM_ADDR(0xc000, 0xc7ff, xevious_videoram_w)
MEM_ADDR(0xc800, 0xcfff, xevious_videoram2_w)
MEM_ADDR(0xd000, 0xd07f, xevious_vh_latch_w)
MEM_ADDR(0xf000, 0xffff, xevious_bs_w)
MEM_END

// --- CPU 3 (Sound) ---
MEM_READ(XeviousCPU3_Read)
MEM_ADDR(0x7800, 0xcfff, xevious_sharedram_r)
MEM_END

MEM_WRITE(XeviousCPU3_Write)
MEM_ADDR(0x0000, 0x0fff, xevious_no_write)
MEM_ADDR(0x6800, 0x681f, xevious_sound_w)
MEM_ADDR(0x6822, 0x6822, xevious_interrupt_enable_3_w)
MEM_ADDR(0x7800, 0xcfff, xevious_sharedram_w)
MEM_END

// --- Port maps (empty -- no Z80 I/O ports used) ---
PORT_READ(XeviousPortRead)
PORT_END
PORT_WRITE(XeviousPortWrite)
PORT_END

// ============================================================================
// Namco sound interface
// ============================================================================

static struct namco_interface xevious_namco_interface =
{
	3125000 / 32,   // sample rate
	3,              // number of voices
	255,            // playback volume
	6,              // memory region (sound PROM)
	0               // mono
};

// ============================================================================
// Sample names
// ============================================================================

static const char* xevious_samples[] =
{
	"xevious.zip",
	"explo1.wav",	/* ground target explosion */
	"explo2.wav",	/* Solvalou explosion */
	"explo3.wav",	/* credit */
	"explo4.wav",	/* Garu Zakato explosion */
	0	/* end of array */
};

// ============================================================================
// Init / Run / End
// ============================================================================

int init_xevious(void)
{
	// Start with CPU's 2 and 3 off.
	cpu_enable(1, 0);
	cpu_enable(2, 0);

	// Set up planet map ROM pointers (region 5 in the old MAME numbering;
	// AAE loads this as REGION_USER1 which maps to memory_region index
	// that we determine from the ROM table).
	// In the AAE ROM loading scheme, REGION_USER1 is memory_region[5].
	rom2a = memory_region(REGION_GFX4);
	rom2b = memory_region(REGION_GFX4) + 0x1000;
	rom2c = memory_region(REGION_GFX4) + 0x3000;

	nmi_timer = 0;

	interrupt_enable_1 = 0;
	interrupt_enable_2 = 0;
	interrupt_enable_3 = 0;

	customio_command = 0;
	io_mode = 0;
	credits = 0;
	memset(customio, 0, sizeof(customio));
	xevious_bs[0] = 0;
	xevious_bs[1] = 0;

	/* Set up shared memory pointers.
	   Shared RAM is at 0x7800 in CPU1's address space. */
	xevious_sharedram = &Machine->memory_region[CPU0][0x7800];


	// Set up video RAM pointers.
	// In the shared memory space:
	//   0xB000-0xB7FF = foreground colorram
	//   0xB800-0xBFFF = background colorram2
	//   0xC000-0xC7FF = foreground videoram
	//   0xC800-0xCFFF = background videoram2
	//   0x8780-0x87FF = spriteram_2 (X/Y positions)
	//   0x9780-0x97FF = spriteram_3 (attributes)
	//   0xA780-0xA7FF = spriteram   (char/color)
	videoram = &Machine->memory_region[CPU0][0xC000];
	colorram = &Machine->memory_region[CPU0][0xB000];
	videoram_size = 0x800;

	xevious_videoram2 = &Machine->memory_region[CPU0][0xC800];
	xevious_colorram2 = &Machine->memory_region[CPU0][0xB800];

	spriteram = &Machine->memory_region[CPU0][0xA780];
	spriteram_2 = &Machine->memory_region[CPU0][0x8780];
	spriteram_3 = &Machine->memory_region[CPU0][0x9780];
	spriteram_size = 0x80;

	// Start video hardware
	xevious_vh_start();

	// Start Namco sound interface
	namco_sh_start(&xevious_namco_interface);

	return 0;
}

void run_xevious(void)
{
	watchdog_reset_w(0, 0, 0);
	xevious_vh_screenrefresh();
	namco_sh_update();
}

void end_xevious(void)
{
	xevious_vh_stop();

	if (nmi_timer)
	{
		timer_remove(nmi_timer);
		nmi_timer = 0;
	}
}

// ============================================================================
// GFX layouts
// ============================================================================

// Foreground characters: 8x8, 1bpp, 512 chars
static struct GfxLayout charlayout =
{
	8, 8,       // 8x8
	512,        // 512 characters
	1,          // 1 bit per pixel
	{ 0 },
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	8 * 8         // 8 bytes per char
};

// Background tiles: 8x8, 2bpp, 512 tiles
static struct GfxLayout bgcharlayout =
{
	8, 8,
	512,
	2,
	{ 0, 512 * 8 * 8 },
	{ 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	8 * 8
};

// Sprite set #1: 16x16, 3bpp, 128 sprites
static struct GfxLayout spritelayout1 =
{
	16, 16,
	128,
	3,
	{ 128 * 64 * 8 + 4, 0, 4 },
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	   7 * 8,  6 * 8,  5 * 8,  4 * 8,  3 * 8,  2 * 8,  1 * 8,  0 * 8 },
	{ 0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8
};

// Sprite set #2: 16x16, 3bpp, 128 sprites
static struct GfxLayout spritelayout2 =
{
	16, 16,
	128,
	3,
	{ 0, 128 * 64 * 8, 128 * 64 * 8 + 4 },
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	   7 * 8,  6 * 8,  5 * 8,  4 * 8,  3 * 8,  2 * 8,  1 * 8,  0 * 8 },
	{ 0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8
};

// Sprite set #3: 16x16, 3bpp, 64 sprites
static struct GfxLayout spritelayout3 =
{
	16, 16,
	64,
	3,
	{ 64 * 64 * 8, 0, 4 },
	{ 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
	   7 * 8,  6 * 8,  5 * 8,  4 * 8,  3 * 8,  2 * 8,  1 * 8,  0 * 8 },
	{ 0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
	  16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
	64 * 8
};

struct GfxDecodeInfo xevious_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &charlayout,       128 * 4 + 64 * 8,  64 },   // foreground
	{ REGION_GFX1, 0x1000, &bgcharlayout,     0,            128 },   // background
	{ REGION_GFX2, 0x0000, &spritelayout1,    128 * 4,         64 },   // sprite set #1
	{ REGION_GFX2, 0x2000, &spritelayout2,    128 * 4,         64 },   // sprite set #2
	{ REGION_GFX2, 0x6000, &spritelayout3,    128 * 4,         64 },   // sprite set #3
	{ -1 }
};

// ============================================================================
// Input ports
// ============================================================================

INPUT_PORTS_START(xevious)

PORT_START("DSW0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_DIPNAME(0x02, 0x02, "Flags Award Bonus Life")
PORT_DIPSETTING(0x00, DEF_STR(No))
PORT_DIPSETTING(0x02, DEF_STR(Yes))
PORT_DIPNAME(0x0c, 0x0c, "Right Coin")
PORT_DIPSETTING(0x0c, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, "1 Coin/2 Credits")
PORT_DIPSETTING(0x04, "1 Coin/3 Credits")
PORT_DIPSETTING(0x00, "1 Coin/6 Credits")
PORT_DIPNAME(0x10, 0x10, DEF_STR(Unknown))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x60, 0x60, DEF_STR(Difficulty))
PORT_DIPSETTING(0x40, DEF_STR(Easy))
PORT_DIPSETTING(0x60, DEF_STR(Normal))
PORT_DIPSETTING(0x20, DEF_STR(Hard))
PORT_DIPSETTING(0x00, DEF_STR(Hardest))
PORT_DIPNAME(0x80, 0x80, "Freeze")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")
PORT_DIPNAME(0x03, 0x03, "Left Coin")
PORT_DIPSETTING(0x01, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, "2 Coins/3 Credits")
PORT_DIPSETTING(0x02, DEF_STR(1C_2C))
PORT_DIPNAME(0x1c, 0x1c, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x18, "10K 40K")
PORT_DIPSETTING(0x14, "10K 50K")
PORT_DIPSETTING(0x10, "20K 50K")
PORT_DIPSETTING(0x0c, "20K 70K")
PORT_DIPSETTING(0x08, "20K 80K")
PORT_DIPSETTING(0x1c, "20K 60K")
PORT_DIPSETTING(0x04, "20K and 60K")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x60, 0x60, DEF_STR(Lives))
PORT_DIPSETTING(0x40, "1")
PORT_DIPSETTING(0x20, "2")
PORT_DIPSETTING(0x60, "3")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IO1")       // Player 1 (FAKE -- read by custom I/O chip)
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO2")       // Player 2 (FAKE)
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO3")       // Coins / Start (FAKE)
PORT_BIT(0x03, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_START1, 1)
PORT_BIT_IMPULSE(0x08, IP_ACTIVE_LOW, IPT_START2, 1)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN2, 1)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN3, 1)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)

INPUT_PORTS_END

// ============================================================================
// ROM definitions
// ============================================================================

ROM_START(xevious)

// CPU 1 (Master): 16k ROM
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("xvi_1.3p", 0x0000, 0x1000, CRC(09964dda) SHA1(4882b25b0938a903f3a367455ba788a30759b5b0))
ROM_LOAD("xvi_2.3m", 0x1000, 0x1000, CRC(60ecce84) SHA1(8adc60a5fcbca74092518dbc570ffff0f04c5b17))
ROM_LOAD("xvi_3.2m", 0x2000, 0x1000, CRC(79754b7d) SHA1(c6a154858716e1f073b476824b183de20e06d093))
ROM_LOAD("xvi_4.2l", 0x3000, 0x1000, CRC(c7d4bbf0) SHA1(4b846de204d08651253d3a141677c8a31626af07))

// CPU 2 (Motion): 8k ROM
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("xvi_5.3f", 0x0000, 0x1000, CRC(c85b703f) SHA1(15f1c005b9d806a384ab1f2240b9c580bfe83893))
ROM_LOAD("xvi_6.3j", 0x1000, 0x1000, CRC(e18cdaad) SHA1(6b79efee1a9642edb9f752101737132401248aed))

// CPU 3 (Sound): 4k ROM
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("xvi_7.2c", 0x0000, 0x1000, CRC(dd35cf1c) SHA1(f8d1f8e019d8198308443c2e7e815d0d04b23d14))

// GFX1: foreground (1k) + background tiles (2k)
ROM_REGION(0x3000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("xvi_12.3b", 0x0000, 0x1000, CRC(088c8b26) SHA1(9c3b61dfca2f84673a78f7f66e363777a8f47a59))	/* foreground characters */
ROM_LOAD("xvi_13.3c", 0x1000, 0x1000, CRC(de60ba25) SHA1(32bc09be5ff8b52ee3a26e0ac3ebc2d4107badb7))	/* bg pattern B0 */
ROM_LOAD("xvi_14.3d", 0x2000, 0x1000, CRC(535cdbbc) SHA1(fb9ffe5fc43e0213231267e98d605d43c15f61e8))	/* bg pattern B1 */
// GFX2: sprites (8k total: sets #1, #2, #3)
// Laid out so sets decode correctly with the layouts above.
// 0x0000-0x1FFF = set #1 planes 0/1 (xe-4m)
// 0x2000-0x3FFF = set #1 plane 2 / set #2 plane 0 (xe-4r)
// 0x4000-0x5FFF = set #2 planes 1/2 (xe-4p)
// 0x6000-0x6FFF = set #3 planes 0/1 (xe-4n)
// 0x7000-0x7FFF = empty (pad for 3bpp decode of set #3)
ROM_REGION(0x8000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("xvi_15.4m", 0x0000, 0x2000, CRC(dc2c0ecb) SHA1(19ddbd9805f77f38c9a9a1bb30dba6c720b8609f))
ROM_LOAD("xvi_18.4r", 0x2000, 0x2000, CRC(02417d19) SHA1(b5f830dd2cf25cf154308d2e640f0ecdcda5d8cd))	/* sprite set #1, plane 2, set #2, plane 2 */
ROM_LOAD("xvi_17.4p", 0x4000, 0x2000, CRC(dfb587ce) SHA1(acff2bf5cde85a16cdc98a52cdea11f77fadf25a))	/* sprite set #2, planes 0/1 */
ROM_LOAD("xvi_16.4n", 0x6000, 0x1000, CRC(605ca889) SHA1(3bf380ef76c03822a042ecc73b5edd4543c268ce))

// Color PROMs
ROM_REGION(0x0700, REGION_PROMS, 0)
ROM_LOAD("xvi-8.6a", 0x0000, 0x0100, CRC(5cc2727f) SHA1(0dc1e63a47a4cb0ba75f6f1e0c15e408bb0ee2a1))  // palette red
ROM_LOAD("xvi-9.6d", 0x0100, 0x0100, CRC(5c8796cc) SHA1(63015e3c0874afc6b1ca032f1ffb8f90562c77c8))  // palette green
ROM_LOAD("xvi-10.6e", 0x0200, 0x0100, CRC(3cb60975) SHA1(c94d5a5dd4d8a08d6d39c051a4a722581b903f45))  // palette blue
ROM_LOAD("xvi-7.4h", 0x0300, 0x0200, CRC(22d98032) SHA1(ec6626828c79350417d08b98e9631ad35edd4a41))  // bg tile lookup
ROM_LOAD("xvi-6.4f", 0x0500, 0x0200, CRC(483a99f0) SHA1(a4bdf58c190ca16fc7b976c97f41087a61fdb8b8))  // sprite lookup

// Planet map ROMs (REGION_USER1 = index 5 in AAE)
ROM_REGION(0x4000, REGION_GFX4, 0)
ROM_LOAD("xvi_9.2a", 0x0000, 0x1000, CRC(57ed9879) SHA1(3106d1aacff06cf78371bd19967141072b32b7d7))
ROM_LOAD("xvi_10.2b", 0x1000, 0x2000, CRC(ae3ba9e5) SHA1(49064b25667ffcd81137cd5e800df4b78b182a46))
ROM_LOAD("xvi_11.2c", 0x3000, 0x1000, CRC(31e244dd) SHA1(3f7eac12863697a98e1122111801606759e44b2a))

// Sound PROM
ROM_REGION(0x0100, REGION_SOUND1, 0)
ROM_LOAD("xvi-2.7n", 0x0000, 0x0100, CRC(550f06bc) SHA1(816a0fafa0b084ac11ae1af70a5186539376fc2a))

ROM_END

// ============================================================================
// AAE Driver descriptor
// ============================================================================

AAE_DRIVER_BEGIN(drv_xevious, "xevious", "Xevious (Namco)")
AAE_DRIVER_ROM(rom_xevious)
AAE_DRIVER_FUNCS(&init_xevious, &run_xevious, &end_xevious)
AAE_DRIVER_INPUT(input_ports_xevious)
AAE_DRIVER_SAMPLES(xevious_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0 -- Master
	AAE_CPU_ENTRY(
		CPU_MZ80,
		3125000,        // 3.125 MHz
		400,            // divisions
		1,              // interrupts per frame
		INT_TYPE_INT,
		&xevious_interrupt_1,
		XeviousCPU1_Read,
		XeviousCPU1_Write,
		XeviousPortRead,
		XeviousPortWrite,
		nullptr,
		nullptr
	),
	// CPU1 -- Motion
	AAE_CPU_ENTRY(
		CPU_MZ80,
		3125000,
		400,
		1,
		INT_TYPE_INT,
		&xevious_interrupt_2,
		XeviousCPU2_Read,
		XeviousCPU2_Write,
		XeviousPortRead,
		XeviousPortWrite,
		nullptr,
		nullptr
	),
	// CPU2 -- Sound
	AAE_CPU_ENTRY(
		CPU_MZ80,
		3125000,
		400,
		2,              // 2 interrupts per frame for sound CPU
		INT_TYPE_INT,
		&xevious_interrupt_3,
		XeviousCPU3_Read,
		XeviousCPU3_Write,
		XeviousPortRead,
		XeviousPortWrite,
		nullptr,
		nullptr
	),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(xevious_gfxdecodeinfo, 128 + 1, 128 * 4 + 64 * 8 + 64 * 2, xevious_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_xevious)