//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================
// Food Fight (Atari, 1983) - 68000 + 3x POKEY, raster (playfield + motion objects)
// Driver by Aaron Giles (MAME); adapted to AAE conventions.
//==========================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "aae_pokey.h"
#include "old_mame_raster.h"
#include "timer.h"
int foodf_nvram_size=0x200;

// Read a 16-bit word from a host-order (little-endian) RAM byte array, matching
// the order the AAE 68000 core stores words.
#define RDWORD(p)		(*(UINT16*)(p))
#define WRWORD(a,d)     (*(UINT16*)(a) = (d))

#define WRITE_WORD_FF(a,d)       (*(unsigned short *)(a) = (d))
// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
unsigned char foodf_program_rom[0x10000];
unsigned char foodf_main_ram[0x8000];     // 0x014000-0x01BFFF
unsigned char foodf_sprite_ram[0x1000];   // 0x01C000-0x01CFFF (motion objects)
unsigned char foodf_playfield_ram[0x800]; // 0x800000-0x8007FF
unsigned char foodf_paletteram[0x400];    // 0x950000-0x9503FF (write-only, D0-D7)
unsigned char foodf_nvram[0x200];   // 0x900000-0x9001FF, 4 bits per location

static int whichport = 0;                 // analog input multiplexer select

// ---------------------------------------------------------------------------
// Interrupts
//   INT 1: asserted every interrupt pass (the 32V signal), ipf = 4
//   INT 2: once per frame, ~100us after the start-of-frame pass
// ---------------------------------------------------------------------------
void foodf_delayed_interrupt(int param)
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K2);
}

void foodf_interrupt()
{
	if (cpu_getiloops() == 0)
	timer_set(TIME_IN_USEC(100), ONE_SHOT, 0, foodf_delayed_interrupt);

	cpu_do_int_imm(CPU0, INT_TYPE_68K1);
}

// ---------------------------------------------------------------------------
// NVRAM (4-bit wide; packed two-nibbles-per-byte with an address XOR)
// ---------------------------------------------------------------------------
READ16_HANDLER(foodf_nvram_r)
{
	//LOG_INFO("NVRAM read: offset %03x data %03x", address, foodf_nvram[address]);
	return RDWORD(&foodf_nvram[address]) & 0x0f;

}
WRITE16_HANDLER(foodf_nvram_w)
{
	//LOG_INFO("NVRAM write: offset %03x, data %01x", address, data);
	/* WRITE16_HANDLER passes a bare 16-bit value with no mem_mask in the high
	   half, and the region is wired word-only, so this is a plain store with
	   nothing to read-modify-write. */
	WRWORD(&foodf_nvram[address], data);
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
// Each even sub-address returns the currently selected analog port.
READ16_HANDLER(foodf_analog_r)  
{ return (UINT16)readinputport(whichport); }


WRITE16_HANDLER(foodf_analog_w) 
{
	whichport = 3 - (((address & 7) / 2) & 3); 
}


// Coins / starts / throws / test (active low).
READ16_HANDLER(foodf_digital_r) { return (UINT16)input_port_4_r(0); }
READ_HANDLER(foodf_digital_rb)  { return (UINT8)input_port_4_r(0); }

// PFFlip / Update / INT3RST / INT4RST / LEDs / coin counters.
// Not required for boot; left as a no-op for now.
WRITE16_HANDLER(foodf_digital_w) { }
WRITE_HANDLER(foodf_digital_wb)  { }

READ16_HANDLER(foodf_nop16_r) { return 0; }
READ_HANDLER(foodf_nop_rb)    { return 0; }

// ---------------------------------------------------------------------------
// Palette RAM: 3-3-2 RGB written one entry at a time. Apply immediately.
// ---------------------------------------------------------------------------
static void palette_write(int offset, int d)
{
	offset &= 0x3ff;
	d &= 0xff;
	int bit0, bit1, bit2, r, g, b;

	foodf_paletteram[offset] = (unsigned char)d;

	/* only the bottom 8 bits are used */
	/* red component */
	bit0 = (d >> 0) & 0x01;
	bit1 = (d >> 1) & 0x01;
	bit2 = (d >> 2) & 0x01;
	r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	/* green component */
	bit0 = (d >> 3) & 0x01;
	bit1 = (d >> 4) & 0x01;
	bit2 = (d >> 5) & 0x01;
	g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	/* blue component */
	bit0 = 0;
	bit1 = (d >> 6) & 0x01;
	bit2 = (d >> 7) & 0x01;
	b = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;


	osd_modify_pen(offset / 2, (unsigned char)r, (unsigned char)g, (unsigned char)b);
}
WRITE16_HANDLER(foodf_paletteram_w) { palette_write(address, data); }
WRITE_HANDLER(foodf_paletteram_wb)  { palette_write(address, data); }

// ---------------------------------------------------------------------------
// POKEY (3 chips, byte-wide on the 68000 bus; register = (addr>>1) & 0x0f)
// ---------------------------------------------------------------------------
READ16_HANDLER(foodf_pokey1_r) { return (UINT16)pokey1_r((address >> 1) & 0x0f); }
READ16_HANDLER(foodf_pokey2_r) { return (UINT16)pokey2_r((address >> 1) & 0x0f); }
READ16_HANDLER(foodf_pokey3_r) { return (UINT16)pokey3_r((address >> 1) & 0x0f); }
WRITE16_HANDLER(foodf_pokey1_w) 
{
	LOG_INFO("ADDRESS Pokey 1 %x data %x", address, data); 
	pokey1_w((address >> 1) , data); 
}

WRITE16_HANDLER(foodf_pokey2_w) 
{
	//LOG_INFO("ADDRESS Pokey 2 %x data %x", address, data); 
	pokey2_w((address >> 1) & 0x0f, data & 0xff); 
}

WRITE16_HANDLER(foodf_pokey3_w) 
{ 
	//LOG_INFO("ADDRESS Pokey 3 %x data %x", address, data); 
	pokey3_w((address >> 1) & 0x0f, data & 0xff); 
}



static int pot_r(int offset)
{
	return (readinputport(5) >> offset) << 7;
}

static struct POKEYinterface foodf_pokey_interface =
{
	3,	/* 3 chips */
	600000,	/* .6 MHz */
	{ 33, 33, 33 },
	/* The 8 pot handlers */
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	{ pot_r, 0, 0 },
	/* The allpot handler */
	{ 0, 0, 0 }
};

// ---------------------------------------------------------------------------
// GFX
// ---------------------------------------------------------------------------
static struct GfxLayout charlayout =
{
	8, 8,
	512,
	2,
	{ 0, 4 },
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
	8 * 16
};

static struct GfxLayout spritelayout =
{
	16, 16,
	256,
	2,
	{ 8 * 0x2000, 0 },
	{ 8 * 16 + 0, 8 * 16 + 1, 8 * 16 + 2, 8 * 16 + 3, 8 * 16 + 4, 8 * 16 + 5, 8 * 16 + 6, 8 * 16 + 7,
	  0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
	  8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
	8 * 32
};

struct GfxDecodeInfo foodf_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,   0, 64 },   /* playfield characters 8x8 */
	{ REGION_GFX2, 0, &spritelayout, 0, 64 },   /* motion objects 16x16 */
	{ -1 }
};

// Food Fight has no color PROM: the 256-entry palette is built from RAM.
// Identity colortable; palette starts black and is filled by paletteram writes.
void foodf_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	for (int i = 0; i < 256; i++)
	{
		palette[3 * i + 0] = 0;
		palette[3 * i + 1] = 0;
		palette[3 * i + 2] = 0;
		colortable[i] = (unsigned char)i;
	}
}

// ---------------------------------------------------------------------------
// Video refresh: redraw playfield into tmpbitmap, blit, then overlay sprites.
// ---------------------------------------------------------------------------
void foodf_vh_screenrefresh()
{
	int offs;

	/* playfield: 32x32 tiles, one word each */
	for (offs = (int)sizeof(foodf_playfield_ram) - 2; offs >= 0; offs -= 2)
	{
		int data = RDWORD(&foodf_playfield_ram[offs]);
		int color = (data >> 8) & 0x3f;
		int pict = (data & 0xff) | ((data >> 7) & 0x100);
		int sx = ((offs / 2) / 32 + 1) % 32;
		int sy = (offs / 2) % 32;

		drawgfx(tmpbitmap, Machine->gfx[0],
			pict, color,
			0, 0,
			8 * sx, 8 * sy,
			&Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);

	/* motion objects */
	for (offs = 0; offs < (int)sizeof(foodf_sprite_ram); offs += 4)
	{
		int data1 = RDWORD(&foodf_sprite_ram[offs]);
		int data2 = RDWORD(&foodf_sprite_ram[offs + 2]);

		int pict = data1 & 0xff;
		int color = (data1 >> 8) & 0x1f;
		int xpos = (data2 >> 8) & 0xff;
		int ypos = (0xff - data2 - 16) & 0xff;
		int hflip = (data1 >> 15) & 1;
		int vflip = (data1 >> 14) & 1;

		drawgfx(main_bitmap, Machine->gfx[1],
			pict, color, hflip, vflip,
			xpos, ypos,
			&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);

		/* wraparound copy (needed for the end-of-level animation) */
		drawgfx(main_bitmap, Machine->gfx[1],
			pict, color, hflip, vflip,
			xpos - 256, ypos,
			&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
	}
}

// ---------------------------------------------------------------------------
// 16-bit memory maps
// ---------------------------------------------------------------------------
MEM_READ16(FoodfReadWord)
MEM_ADDR16(0x000000, 0x00ffff, NULL, foodf_program_rom)
MEM_ADDR16(0x014000, 0x01bfff, NULL, foodf_main_ram)
MEM_ADDR16(0x01c000, 0x01cfff, NULL, foodf_sprite_ram)
MEM_ADDR16(0x800000, 0x8007ff, NULL, foodf_playfield_ram)
MEM_ADDR16(0x900000, 0x9001ff, foodf_nvram_r, NULL)
MEM_ADDR16(0x940000, 0x940007, foodf_analog_r, NULL)
MEM_ADDR16(0x948000, 0x948003, foodf_digital_r, NULL)
MEM_ADDR16(0x94c000, 0x94c003, foodf_nop16_r, NULL)
MEM_ADDR16(0x958000, 0x958003, foodf_nop16_r, NULL)
MEM_ADDR16(0xa40000, 0xa4001f, foodf_pokey1_r, NULL)
MEM_ADDR16(0xa80000, 0xa8001f, foodf_pokey2_r, NULL)
MEM_ADDR16(0xac0000, 0xac001f, foodf_pokey3_r, NULL)
MEM_END

MEM_WRITE16(FoodfWriteWord)
MEM_ADDR16(0x000000, 0x00ffff, MWA_ROM16, NULL)
MEM_ADDR16(0x014000, 0x01bfff, NULL, foodf_main_ram)
MEM_ADDR16(0x01c000, 0x01cfff, NULL, foodf_sprite_ram)
MEM_ADDR16(0x800000, 0x8007ff, NULL, foodf_playfield_ram)
MEM_ADDR16(0x900000, 0x9001ff, foodf_nvram_w, NULL)
MEM_ADDR16(0x944000, 0x944007, foodf_analog_w, NULL)
MEM_ADDR16(0x948000, 0x948003, foodf_digital_w, NULL)
MEM_ADDR16(0x950000, 0x9501ff, foodf_paletteram_w, NULL)
MEM_ADDR16(0x954000, 0x954003, MWA_NOP16, NULL)
MEM_ADDR16(0x958000, 0x958003, MWA_NOP16, NULL)
MEM_ADDR16(0xa40000, 0xa4001f, foodf_pokey1_w, NULL)
MEM_ADDR16(0xa80000, 0xa8001f, foodf_pokey2_w, NULL)
MEM_ADDR16(0xac0000, 0xac001f, foodf_pokey3_w, NULL)
MEM_END

// 8-bit maps: direct RAM/ROM so byte accesses to work RAM / object RAM resolve.
MEM_READ(FoodfReadByte)
MEM_ADDR8(0x000000, 0x00ffff, NULL, foodf_program_rom)
MEM_ADDR8(0x014000, 0x01bfff, NULL, foodf_main_ram)
MEM_ADDR8(0x01c000, 0x01cfff, NULL, foodf_sprite_ram)
MEM_ADDR8(0x800000, 0x8007ff, NULL, foodf_playfield_ram)
MEM_ADDR8(0x948000, 0x948003, foodf_digital_rb, NULL)
MEM_ADDR8(0x94c000, 0x94c003, foodf_nop_rb, NULL)
MEM_ADDR8(0x958000, 0x958003, foodf_nop_rb, NULL)

MEM_END

MEM_WRITE(FoodfWriteByte)
MEM_ADDR(0x000000, 0x00ffff, MWA_ROM)
MEM_ADDR8(0x014000, 0x01bfff, NULL, foodf_main_ram)
MEM_ADDR8(0x01c000, 0x01cfff, NULL, foodf_sprite_ram)
MEM_ADDR8(0x800000, 0x8007ff, NULL, foodf_playfield_ram)
MEM_ADDR8(0x948000, 0x948003, foodf_digital_wb, NULL)
MEM_ADDR8(0x950000, 0x9501ff, foodf_paletteram_wb, NULL)

MEM_END

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void run_foodf()
{
	pokey_sh_update();
	foodf_vh_screenrefresh();
	watchdog_reset_w(0, 0, 0);
}

int init_foodf()
{
	LOG_INFO("Starting Food Fight Init");

	memset(foodf_main_ram, 0x00, sizeof(foodf_main_ram));
	memset(foodf_sprite_ram, 0x00, sizeof(foodf_sprite_ram));
	memset(foodf_playfield_ram, 0x00, sizeof(foodf_playfield_ram));
	memset(foodf_paletteram, 0x00, sizeof(foodf_paletteram));
	
	static unsigned char factory_nvram[] =
	{
		0x10,0x00,0x10,0x00,0xf1,0x00,0xca,0xb8,0x00,0x00,0x10,0x20,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa1,0x20,0x00,0x00,0xcd,0x10,
		0x14,0xa4,0x49,0x10,0x02,0x75,0x45,0x84,0x00,0xa4,0x24,0x25,0xe0,0x00,0xf0,0x00,
		0x10,0x00,0xd0,0x00,0x50,0x00,0x10,0x00,0x10,0x00,0x41,0x00,0x10,0x00,0x10,0x00,
		0x00,0x00,0x00,0x00,0x08,0x08,0x00,0x00,0x08,0x08,0x08,0x08,0x00,0x00,0x08,0x08
	};
	int i;

	/* Lay down the full factory-default NVRAM (4 bits per location, two nibbles
	   packed per factory byte). The generic NVRAM handler is registered below
	   with fill = -1, so first boot keeps these defaults while later boots load
	   the saved NVRAM file over them. */
	memset(foodf_nvram, 0, foodf_nvram_size);
	for (i = 0; i < foodf_nvram_size; i += 4)
	{
		WRWORD(&foodf_nvram[i], factory_nvram[i / 4] >> 4);
		WRWORD(&foodf_nvram[i + 2], factory_nvram[i / 4] & 0x0f);
	}
	nvram_set_region(foodf_nvram, foodf_nvram_size, -1);

	memcpy(foodf_program_rom, Machine->memory_region[CPU0], 0x10000);
	byteswap(foodf_program_rom, 0x10000);

	pokey_sh_start(&foodf_pokey_interface);

	// Point the generic raster globals at our RAM so generic_vh_start() can
	// size its dirty buffer and allocate tmpbitmap. We redraw the whole
	// playfield every frame, so the dirty tracking itself is unused.
	videoram = foodf_playfield_ram;
	videoram_size = sizeof(foodf_playfield_ram);
	spriteram = foodf_sprite_ram;
	spriteram_size = sizeof(foodf_sprite_ram);

	generic_vh_start();   // allocate tmpbitmap (dirtybuffer sized to videoram_size)

	LOG_INFO("End Food Fight Init");
	return 0;
}

void end_foodf()
{
	generic_vh_stop();
	pokey_sh_stop();
}

// ---------------------------------------------------------------------------
// Input ports
// ---------------------------------------------------------------------------
INPUT_PORTS_START(foodf)
PORT_START("IN0")   /* analog X, player 1 */
PORT_ANALOG(0xff, 0x7f, IPT_AD_STICK_X | IPF_REVERSE, 100, 10, 0, 0xff)

PORT_START("IN1")   /* analog X, player 2 (cocktail) */
PORT_ANALOG(0xff, 0x7f, IPT_AD_STICK_X | IPF_PLAYER2 | IPF_REVERSE | IPF_COCKTAIL, 100, 10, 0, 0xff)

PORT_START("IN2")   /* analog Y, player 1 */
PORT_ANALOG(0xff, 0x7f, IPT_AD_STICK_Y | IPF_REVERSE, 100, 10, 0, 0xff)

PORT_START("IN3")   /* analog Y, player 2 (cocktail) */
PORT_ANALOG(0xff, 0x7f, IPT_AD_STICK_Y | IPF_PLAYER2 | IPF_REVERSE | IPF_COCKTAIL, 100, 10, 0, 0xff)

PORT_START("IN4")   /* digital */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)

PORT_START("SW1")	/* SW1 */
PORT_DIPNAME(0x07, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x05, "1 for every 2")
PORT_DIPSETTING(0x02, "1 for every 4")
PORT_DIPSETTING(0x01, "1 for every 5")
PORT_DIPSETTING(0x06, "2 for every 4")
PORT_DIPNAME(0x08, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x08, DEF_STR(1C_2C))
PORT_DIPNAME(0x30, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0x20, DEF_STR(1C_4C))
PORT_DIPSETTING(0x10, DEF_STR(1C_5C))
PORT_DIPSETTING(0x30, DEF_STR(1C_6C))
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x80, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0xc0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x40, DEF_STR(Free_Play))

INPUT_PORTS_END

// ---------------------------------------------------------------------------
// ROMs
// ---------------------------------------------------------------------------
ROM_START(foodf)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("136020-301.8c", 0x000001, 0x002000, CRC(dfc3d5a8) SHA1(7abe5e9c27098bd8c93cc06f1b9e3db0744019e9))
ROM_LOAD16_BYTE("136020-302.9c", 0x000000, 0x002000, CRC(ef92dc5c) SHA1(eb41291615165f549a68ebc6d4664edef1a04ac5))
ROM_LOAD16_BYTE("136020-303.8d", 0x004001, 0x002000, CRC(64b93076) SHA1(efa4090d96aa0ffd4192a045f174ac5960810bca))
ROM_LOAD16_BYTE("136020-204.9d", 0x004000, 0x002000, CRC(ea596480) SHA1(752aa33a8e8045650dd32ec7c7026e00d7896e0f))
ROM_LOAD16_BYTE("136020-305.8e", 0x008001, 0x002000, CRC(e6cff1b1) SHA1(7c7ad2dcdff60fc092e8a825c5a6de6b506523de))
ROM_LOAD16_BYTE("136020-306.9e", 0x008000, 0x002000, CRC(95159a3e) SHA1(f180126671776f62242ec9fd4a82a581c551ffce))
ROM_LOAD16_BYTE("136020-307.8f", 0x00c001, 0x002000, CRC(17828dbb) SHA1(9d8e29a5e56a8a9c5db8561e4c20ff22f69b46ca))
ROM_LOAD16_BYTE("136020-208.9f", 0x00c000, 0x002000, CRC(608690c9) SHA1(419020c69ce6fded0d9af44ead8ec4727468d58b))

ROM_REGION(0x2000, REGION_GFX1, ROMREGION_DISPOSE)
ROM_LOAD("136020-109.6lm", 0x000000, 0x002000, CRC(c13c90eb) SHA1(ebd2bbbdd7e184851d1ab4b5648481d966c78cc2))

ROM_REGION(0x4000, REGION_GFX2, ROMREGION_DISPOSE)
ROM_LOAD("136020-110.4e", 0x000000, 0x002000, CRC(8870e3d6) SHA1(702007d3d543f872b5bf5d00b49f6e05b46d6600))
ROM_LOAD("136020-111.4d", 0x002000, 0x002000, CRC(84372edf) SHA1(9beef3ff3b28405c45d691adfbc233921073be47))
ROM_END

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------
AAE_DRIVER_BEGIN(drv_foodf, "foodf", "Food Fight (Atari)")
AAE_DRIVER_ROM(rom_foodf)
AAE_DRIVER_FUNCS(&init_foodf, &run_foodf, &end_foodf)
AAE_DRIVER_INPUT(input_ports_foodf)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     6000000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_68K1,
		/*int cb*/   &foodf_interrupt,
		/*r8*/       FoodfReadByte,
		/*w8*/       FoodfWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      FoodfReadWord,
		/*w16*/      FoodfWriteWord
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_MODIFIES_PALETTE, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(256, 256, 0, 255, 0, 223)
AAE_DRIVER_RASTER(foodf_gfxdecodeinfo, 256, 256, foodf_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_foodf)
