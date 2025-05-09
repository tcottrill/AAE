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
#include "cpu_control.h"
#include "AY8910.H"
#include "math.h"
#include "timer.h"
#include "emu_vector_draw.h"

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
	//wrlog("ADD Vector Called, %x %x %x %x", x << 16 , y << 16, color, intensity);
	int new_x = (((xcenter + (x << 16)) >> 16) + 512);
	int new_y = (((ycenter - (y << 16)) >> 16) + YC);

	//add_line(last_x+512, last_y+512, ((xcenter + (x << 16)) >> 16) +512, ((ycenter - (y << 16)) >> 16) + 512, intensity, color);
	add_line(last_x + 512, 1024 - (last_y + YC), new_x, 1024 - new_y, intensity, color);

	last_x = (xcenter + (x << 16)) >> 16;
	last_y =  (ycenter - (y << 16)) >> 16;
	//wrlog("last_x %x last_y %x ", last_x, last_y);
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

MEM_READ(AztaracReadByte)
{ 0x027000, 0x027001, joystick_rb },
{ 0x027004, 0x027005, ip_port_3_r },
{ 0x027008, 0x027009, aztarac_snd_rb },
{ 0x02700c, 0x02700d, ip_port_2_r },
{ 0x000000, 0x00bfff, NULL, aztarac_program_rom },
{ 0xff8000, 0xffafff, NULL, vec_ram },
{ 0xffe000, 0xffffff, NULL, aztarac_main_ram },
MEM_END

MEM_WRITE(AztaracWriteByte)
{ 0x000000, 0x00bfff, MWA_ROM },
{ 0x027008, 0x027009, aztarac_sound_wb },
{ 0xff8000, 0xffafff, NULL, vec_ram },
{ 0xffb000, 0xffb001, aztarac_ubr_wb },
{ 0xffe000, 0xffffff, NULL, aztarac_main_ram },
MEM_END

MEM_READ16(AztaracReadWord)
{ 0x000000, 0x00bfff, NULL, aztarac_program_rom },
{ 0x022000, 0x022fff, nvram_r },
{ 0x027000, 0x027001, joystick_r },
{ 0x027004, 0x027005, ip_port_3_word_r },
{ 0x027008, 0x027009, aztarac_sound_r },
{ 0x02700c, 0x02700d, ip_port_2_word_r },
{ 0x02700e, 0x02700f, watchdog_reset16_r },
{ 0xff8000, 0xffafff, NULL, vec_ram },
{ 0xffe000, 0xffffff, NULL, aztarac_main_ram },
MEM_END

MEM_WRITE16(AztaracWriteWord)
{ 0x000000, 0x00bfff, NULL, aztarac_program_rom },
{ 0x022000, 0x022fff, NULL, generic_nvram },
{ 0x027008, 0x027009, aztarac_sound_w },
{ 0xff8000, 0xffafff, NULL,vec_ram },
{ 0xffb000, 0xffb001, aztarac_ubr_w },
{ 0xffe000, 0xffffff, NULL,aztarac_main_ram },
MEM_END

MEM_READ(AztaracSoundMemRead)
{ 0x0000, 0x1fff, MRA_ROM },
{ 0x8000, 0x87ff, MRA_RAM },
{ 0x8800, 0x8800, aztarac_snd_command_r },
{ 0x8c00, 0x8c01, AY8910_read_port_0_r },
{ 0x8c02, 0x8c03, AY8910_read_port_1_r },
{ 0x8c04, 0x8c05, AY8910_read_port_2_r },
{ 0x8c06, 0x8c07, AY8910_read_port_3_r },
{ 0x9000, 0x9000, aztarac_snd_status_r },
MEM_END

MEM_WRITE(AztaracSoundMemWrite)
{ 0x0000, 0x1fff, MWA_ROM },
{ 0x8000, 0x87ff, MWA_RAM },
{ 0x8c00, 0x8c00, AY8910_write_port_0_w },
{ 0x8c01, 0x8c01, AY8910_control_port_0_w },
{ 0x8c02, 0x8c02, AY8910_write_port_1_w },
{ 0x8c03, 0x8c03, AY8910_control_port_1_w },
{ 0x8c04, 0x8c04, AY8910_write_port_2_w },
{ 0x8c05, 0x8c05, AY8910_control_port_2_w },
{ 0x8c06, 0x8c06, AY8910_write_port_3_w },
{ 0x8c07, 0x8c07, AY8910_control_port_3_w },
{ 0x9000, 0x9000, aztarac_snd_status_w },
MEM_END

PORT_READ(AztaracSoundPortRead)
PORT_END

PORT_WRITE(AztaracSoundPortWrite)
PORT_END

void run_aztarac()
{
	AY8910_sh_update();
	watchdog_reset_w(0, 0, 0);
}

int init_aztarac()
{
	wrlog("Starting aztarac Init");
	memset(aztarac_main_ram, 0x00, 0x1fff);
	memset(aztarac_program_rom, 0x00, 0x00C000);
	memset(generic_nvram, 0xff, 0x200);

	memcpy(aztarac_program_rom, Machine->memory_region[CPU0], 0x00C000);
	byteswap(aztarac_program_rom, 0x00C000);

	init68k(AztaracReadByte, AztaracWriteByte, AztaracReadWord, AztaracWriteWord, CPU0);
	init_z80(AztaracSoundMemRead, AztaracSoundMemWrite, AztaracSoundPortRead, AztaracSoundPortWrite, CPU1);
	AY8910_sh_start(&ay8910_interface);
	m68k_set_int_ack_callback(aztarac_irq_callback);
	aztarac_vectorram = vec_ram;
	// Required for now until I can change the rendering back end again. 
	config.gain = 0;

	wrlog("End aztarac Init");
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