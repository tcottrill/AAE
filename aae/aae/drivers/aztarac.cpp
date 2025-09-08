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

#include "aztarac.h"
#include "./68000/m68k.h"
#include "./68000/m68kcpu.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "AY8910.H"
#include "math.h"
#include "timer.h"
#include "emu_vector_draw.h"
#include "aae_avg.h"

#define READ_WORD16(a)    (*(UINT16 *)(a))
#define WRITE_WORD16(a,d) (*(UINT16 *)(a) = (d))

#pragma warning(disable : 4838)

// Video Variables
static int last_x = 0;
static int last_y = 0;
UINT8* aztarac_vectorram;
static int xcenter, ycenter;

#define YC 638

static void add_vector(int x, int y, int color, int intensity)
{
	//LOG_INFO("ADD Vector Called, %x %x %x %x", x << 16 , y << 16, color, intensity);
	int new_x = (((xcenter + (x << 16)) >> 16) + 512);
	int new_y = (((ycenter - (y << 16)) >> 16) + YC);

	//add_line(last_x+512, last_y+512, ((xcenter + (x << 16)) >> 16) +512, ((ycenter - (y << 16)) >> 16) + 512, intensity, color);
	add_line((float) (last_x + 512), (float) (1024 - (last_y + YC)),(float)(new_x),(float)(1024 - new_y), intensity, color);

	last_x = (xcenter + (x << 16)) >> 16;
	last_y =  (ycenter - (y << 16)) >> 16;
	//LOG_INFO("last_x %x last_y %x ", last_x, last_y);
}

unsigned char aztarac_program_rom[0x00c000];
unsigned char aztarac_main_ram[0x2000];
unsigned char generic_nvram[0x200];

static int sound_command = 0;
static int sound_status = 0;

static struct AY8910interface ay8910_interface =
{
	4,	/* 4 chips */
	2000000,	/* 2 MHz */
	{ 25, 25, 25, 25 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 }
};

int aztarac_irq_callback(int irqline)
{
	return 0xc;
}

void  aztarac_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K4);
}

void  aztarac_sound_interrupt()
{
	sound_status ^= 0x10;
	if (sound_status & 0x10) cpu_do_int_imm(CPU1, INT_TYPE_INT);
}

void read_vectorram(int addr, int* x, int* y, int* c)
{
	addr <<= 1;
	*c = READ_WORD16(&aztarac_vectorram[addr]) & 0xffff;
	*x = READ_WORD16(&aztarac_vectorram[addr + 0x1000]) & 0x03ff;
	*y = READ_WORD16(&aztarac_vectorram[addr + 0x2000]) & 0x03ff;
	if (*x & 0x200) *x |= 0xfffffc00;
	if (*y & 0x200) *y |= 0xfffffc00;
}

/*************************************
 *
 *	NVRAM handler
 *
 *************************************/

READ16_HANDLER(nvram_r)
{
	return ((int16_t*)generic_nvram)[address] | 0xfff0;
}

void aztarac_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		osd_fwrite(file, generic_nvram, 0x200);
	}
	else
	{
		if (file)
		{
			osd_fread(file, generic_nvram, 0x200);
		}
		else
		{
			memset(generic_nvram, 0xff, 0x200);
		}
	}
}

/*************************************
 *
 *	Input ports
 *
 *************************************/

READ16_HANDLER(joystick_r)
{
	return (((input_port_0_r(address) - 0xf) << 8) |
		((input_port_1_r(address) - 0xf) & 0xff));
}

READ_HANDLER(joystick_rb)
{
	return (((input_port_0_r(address) - 0xf) << 8) |
		((input_port_1_r(address) - 0xf) & 0xff));
}

WRITE16_HANDLER(aztarac_ubr_w)
{
	int x, y, c, intensity, xoffset, yoffset, color;
	int defaddr, objaddr = 0, ndefs;

	if (data & 1) /* data is the global intensity (always 0xff in Aztarac). */
	{
		cache_clear();

		while (1)
		{
			read_vectorram(objaddr, &xoffset, &yoffset, &c);
			objaddr++;
			if (c & 0x4000)
				break;

			if ((c & 0x2000) == 0)
			{
				defaddr = (c >> 1) & 0x7ff;
				add_vector(xoffset, yoffset, 0, 0);

				read_vectorram(defaddr, &x, &ndefs, &c);
				ndefs++;

				if (c & 0xff00)
				{
					/* latch color only once */
					intensity = (c >> 8);
					color = VECTOR_COLOR222(c & 0x3f);
					while (ndefs--)
					{
						defaddr++;
						read_vectorram(defaddr, &x, &y, &c);
						if ((c & 0xff00) == 0)
							add_vector(x + xoffset, y + yoffset, 0, 0);
						else
							add_vector(x + xoffset, y + yoffset, color, intensity);
					}
				}
				else
				{
					/* latch color for every definition */
					while (ndefs--)
					{
						defaddr++;
						read_vectorram(defaddr, &x, &y, &c);
						color = VECTOR_COLOR222(c & 0x3f);
						add_vector(x + xoffset, y + yoffset, color, c >> 8);
					}
				}
			}
		}
	}
}

WRITE_HANDLER(aztarac_ubr_wb)
{
	aztarac_ubr_w(address, (UINT16)data, 0);
}

READ16_HANDLER(watchdog_reset16_r)
{
	return 0;
}

WRITE16_HANDLER(aztarac_sound_w)
{
	if (data & 0xff)
	{
		data &= 0xff;
		sound_command = data;
		sound_status ^= 0x21;
		if (sound_status & 0x20) { cpu_do_int_imm(CPU1, INT_TYPE_INT); }
	}
}

WRITE_HANDLER(aztarac_sound_wb)
{
	aztarac_sound_w(address, data, 0);
}

READ_HANDLER(aztarac_snd_command_r)
{
	sound_status |= 0x01;
	sound_status &= ~0x20;
	return sound_command;
}

WRITE_HANDLER(aztarac_snd_status_w)
{
	sound_status &= ~0x10;
}

READ_HANDLER(aztarac_snd_status_r)
{
	return sound_status & ~0x01;
}

READ16_HANDLER(aztarac_sound_r)
{
	return sound_status & 0x01;
}

READ_HANDLER(aztarac_snd_rb)
{
	return sound_status & 0x01;
}

// 8-bit maps
MEM_READ(AztaracReadByte)
MEM_ADDR8(0x027000, 0x027001, joystick_rb, NULL)
MEM_ADDR8(0x027004, 0x027005, ip_port_3_r, NULL)
MEM_ADDR8(0x027008, 0x027009, aztarac_snd_rb, NULL)
MEM_ADDR8(0x02700c, 0x02700d, ip_port_2_r, NULL)
MEM_ADDR8(0x000000, 0x00bfff, NULL, aztarac_program_rom)
MEM_ADDR8(0xff8000, 0xffafff, NULL, vec_ram)
MEM_ADDR8(0xffe000, 0xffffff, NULL, aztarac_main_ram)
MEM_END

MEM_WRITE(AztaracWriteByte)
MEM_ADDR(0x000000, 0x00bfff, MWA_ROM)
MEM_ADDR(0x027008, 0x027009, aztarac_sound_wb)
MEM_ADDR8(0xff8000, 0xffafff, NULL, vec_ram)
MEM_ADDR(0xffb000, 0xffb001, aztarac_ubr_wb)
MEM_ADDR8(0xffe000, 0xffffff, NULL, aztarac_main_ram)
MEM_END


// 16-bit maps
MEM_READ16(AztaracReadWord)
MEM_ADDR16(0x000000, 0x00bfff, NULL, aztarac_program_rom)
MEM_ADDR16(0x022000, 0x022fff, nvram_r, NULL)
MEM_ADDR16(0x027000, 0x027001, joystick_r, NULL)
MEM_ADDR16(0x027004, 0x027005, ip_port_3_word_r, NULL)
MEM_ADDR16(0x027008, 0x027009, aztarac_sound_r, NULL)
MEM_ADDR16(0x02700c, 0x02700d, ip_port_2_word_r, NULL)
MEM_ADDR16(0x02700e, 0x02700f, watchdog_reset16_r, NULL)
MEM_ADDR16(0xff8000, 0xffafff, NULL, vec_ram)
MEM_ADDR16(0xffe000, 0xffffff, NULL, aztarac_main_ram)
MEM_END

MEM_WRITE16(AztaracWriteWord)
MEM_ADDR16(0x000000, 0x00bfff, NULL, aztarac_program_rom)
MEM_ADDR16(0x022000, 0x022fff, NULL, generic_nvram)
MEM_ADDR16(0x027008, 0x027009, aztarac_sound_w, NULL)
MEM_ADDR16(0xff8000, 0xffafff, NULL, vec_ram)
MEM_ADDR16(0xffb000, 0xffb001, aztarac_ubr_w, NULL)
MEM_ADDR16(0xffe000, 0xffffff, NULL, aztarac_main_ram)
MEM_END

MEM_READ(AztaracSoundMemRead)
MEM_ADDR(0x0000, 0x1fff, MRA_ROM)
MEM_ADDR(0x8000, 0x87ff, MRA_RAM)
MEM_ADDR(0x8800, 0x8800, aztarac_snd_command_r)
MEM_ADDR(0x8c00, 0x8c01, AY8910_read_port_0_r)
MEM_ADDR(0x8c02, 0x8c03, AY8910_read_port_1_r)
MEM_ADDR(0x8c04, 0x8c05, AY8910_read_port_2_r)
MEM_ADDR(0x8c06, 0x8c07, AY8910_read_port_3_r)
MEM_ADDR(0x9000, 0x9000, aztarac_snd_status_r)
MEM_END

MEM_WRITE(AztaracSoundMemWrite)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x8000, 0x87ff, MWA_RAM)
MEM_ADDR(0x8c00, 0x8c00, AY8910_write_port_0_w)
MEM_ADDR(0x8c01, 0x8c01, AY8910_control_port_0_w)
MEM_ADDR(0x8c02, 0x8c02, AY8910_write_port_1_w)
MEM_ADDR(0x8c03, 0x8c03, AY8910_control_port_1_w)
MEM_ADDR(0x8c04, 0x8c04, AY8910_write_port_2_w)
MEM_ADDR(0x8c05, 0x8c05, AY8910_control_port_2_w)
MEM_ADDR(0x8c06, 0x8c06, AY8910_write_port_3_w)
MEM_ADDR(0x8c07, 0x8c07, AY8910_control_port_3_w)
MEM_ADDR(0x9000, 0x9000, aztarac_snd_status_w)
MEM_END

PORT_READ(AztaracSoundPortRead)
PORT_END

PORT_WRITE(AztaracSoundPortWrite)
PORT_END

void run_aztarac()
{
	// Hack I need to figure out a workaround for. I didn't code for this unfortunately.
	// It  has to be called AFTER CPU init, and init_aztarac has to be called BEFORE init,
	// and I don't have another place to init. 
	static int poop = 0;
	if (poop == 0) { poop == 1; m68k_set_int_ack_callback(aztarac_irq_callback);
	}
	AY8910_sh_update();
	watchdog_reset_w(0, 0, 0);
}

int init_aztarac()
{
	LOG_INFO("Starting aztarac Init");
	memset(aztarac_main_ram, 0x00, 0x1fff);
	memset(aztarac_program_rom, 0x00, 0x00C000);
	memset(generic_nvram, 0xff, 0x200);

	memcpy(aztarac_program_rom, Machine->memory_region[CPU0], 0x00C000);
	byteswap(aztarac_program_rom, 0x00C000);

	//init68k(AztaracReadByte, AztaracWriteByte, AztaracReadWord, AztaracWriteWord, CPU0);
	//init_z80(AztaracSoundMemRead, AztaracSoundMemWrite, AztaracSoundPortRead, AztaracSoundPortWrite, CPU1);
	AY8910_sh_start(&ay8910_interface);
	//m68k_set_int_ack_callback(aztarac_irq_callback);
	aztarac_vectorram = vec_ram;
	LOG_INFO("End aztarac Init");
	return 0;
}

void end_aztarac()
{
	AY8910clear();
}

void aztarac_video_init(int scale)
{
	int xmin = Machine->drv->visible_area.min_x;
	int ymin = Machine->drv->visible_area.min_y;
	int xmax = Machine->drv->visible_area.max_x;
	int ymax = Machine->drv->visible_area.max_y;

	//xcenter = ((xmax + xmin) / 2) << 16;
	//ycenter = ((ymax + ymin) / 2) << 16;

	xcenter = 512 << 16;
	ycenter = 512 << 16;

	//vector_set_shift(16);
	//VECTOR_START();
}

/*************************************
	 *
	 *	Port definitions
	 *
	 *************************************/
INPUT_PORTS_START(aztarac)
PORT_START("IN0") /* IN0 */
PORT_ANALOG(0x1f, 0xf, IPT_AD_STICK_X | IPF_CENTER, 100, 1, 0, 0, 0x1e)
//PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 100, 5, 0, 0x20, 0xe0)
PORT_START("IN1") /* IN1 */
//PORT_ANALOG(0x1f, 0xf, IPT_AD_STICK_Y | IPF_CENTER | IPF_REVERSE, 100, 1, 0, 0x1e)
PORT_ANALOG(0x1f, 0xf, IPT_AD_STICK_Y | IPF_CENTER | IPF_REVERSE, 100, 1, 0, 0, 0xe1)

PORT_START("IN2")
//PORT_ANALOGX(0xff, 0x00, IPT_DIAL | IPF_REVERSE, 25, 10, 0, 0, ,0, KEYCODE_Z, KEYCODE_X,0,0)
PORT_ANALOGX(0xff, 0x00, IPT_DIAL | IPF_REVERSE, 25, 10, 0, 0, 0, OSD_KEY_Z, OSD_KEY_X, 0, 0)

PORT_START("IN3")
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)
INPUT_PORTS_END


ROM_START(aztarac)
ROM_REGION(0xc000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("6.l8", 0x0000, 0x1000, CRC(25f8da18) SHA1(e8179ba3683e39c8225b549ead74c8af2d0a0b3e))
ROM_LOAD16_BYTE("0.n8", 0x0001, 0x1000, CRC(4e20626) SHA1(2b6a04992037257830df2c01a6da748fb4449f79))
ROM_LOAD16_BYTE("7.l7", 0x2000, 0x1000, CRC(230e244c) SHA1(42283a368144acf2aad2ef390e312e0951c3ea64))
ROM_LOAD16_BYTE("1.n7", 0x2001, 0x1000, CRC(37b12697) SHA1(da288b077902e3205600a021c3fac5730f9fb832))
ROM_LOAD16_BYTE("8.l6", 0x4000, 0x1000, CRC(1293fb9d) SHA1(5a8d512372fd38f1a55f990f5c3eb51833c463d8))
ROM_LOAD16_BYTE("2.n6", 0x4001, 0x1000, CRC(712c206a) SHA1(eb29f161189c14d84896502940e3ab6cc3bd1cd0))
ROM_LOAD16_BYTE("9.l5", 0x6000, 0x1000, CRC(743a6501) SHA1(da83a8f756466bcd94d4b0cc28a1a1858e9532f3))
ROM_LOAD16_BYTE("3.n5", 0x6001, 0x1000, CRC(a65cbf99) SHA1(dd06f98c0989604bd4ac6317e545e1fcf6722e75))
ROM_LOAD16_BYTE("a.l4", 0x8000, 0x1000, CRC(9cf1b0a1) SHA1(dd644026f49d8430c0b4cf4c750dc33c013c19fc))
ROM_LOAD16_BYTE("4.n4", 0x8001, 0x1000, CRC(5f0080d5) SHA1(fb1303f9a02067faea2ac4d523051c416de9cf35))
ROM_LOAD16_BYTE("b.l3", 0xa000, 0x1000, CRC(8cc7f7fa) SHA1(fefb9a4fdd63878bc5d8138e3e8456cb6638425a))
ROM_LOAD16_BYTE("5.n3", 0xa001, 0x1000, CRC(40452376) SHA1(1d058b7ecd2bbff3393950aab9215b262908475b))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("c.j4", 0x0000, 0x1000, CRC(e897dfcd) SHA1(750df3d08512d8098a13ec62677831efa164c126))
ROM_LOAD("d.j3", 0x1000, 0x1000, CRC(4016de77) SHA1(7232ec003f1b9d3623d762f3270108a1d1837846))
ROM_END

// Aztarac
AAE_DRIVER_BEGIN(drv_aztarac, "aztarac", "Aztarac")
AAE_DRIVER_ROM(rom_aztarac)
AAE_DRIVER_FUNCS(&init_aztarac, &run_aztarac, &end_aztarac)
AAE_DRIVER_INPUT(input_ports_aztarac)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
//AAE_DRIVER_CPU2(CPU_68000, 8000000, 100, 1, INT_TYPE_68K4, &aztarac_interrupt, CPU_MZ80, 2000000, 100, 100, INT_TYPE_NONE, &aztarac_sound_interrupt)
AAE_DRIVER_CPUS(
	// CPU0: MC68000 @ 8 MHz, byte+word handlers, 1 interrupt pass/frame (level 4)
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     8000000,
		/*div*/      100,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_68K4,
		/*int cb*/   &aztarac_interrupt,
		/*r8*/       AztaracReadByte,
		/*w8*/       AztaracWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      AztaracReadWord,
		/*w16*/      AztaracWriteWord
	),
	// CPU1: Z80 @ 2 MHz, sound CPU, 100 passes/frame, no core-driven INT 
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     2000000,
		/*div*/      100,
		/*ipf*/      100,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &aztarac_sound_interrupt, 
		/*r8*/       AztaracSoundMemRead,
		/*w8*/       AztaracSoundMemWrite,
		/*pr*/       AztaracSoundPortRead,
		/*pw*/       AztaracSoundPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 400, 0, 300)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x2000)
AAE_DRIVER_NVRAM(aztarac_nvram_handler)
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_aztarac)
