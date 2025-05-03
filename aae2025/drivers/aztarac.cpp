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
#include "aae_mame_driver.h"
#include "cpu_control.h"
#include "AY8910.H"
#include "vector.h"
#include "math.h"
#include "timer.h"

#pragma warning(disable : 4838)

#define AVECTOR(x, y, color, intensity) \
vector_add_point (xcenter + ((x) << 16), ycenter - ((y) << 16), color, intensity)

int16_t* aztarac_vectorram;

static int xcenter, ycenter;


unsigned char aztarac_program_rom[0x00c000];
unsigned char aztarac_main_ram[0x2000];
unsigned char generic_nvram[0x200];


static struct AY8910interface ay8910_interface =
{
	4,	/* 4 chips */
	2000000,	/* 2 MHz */
	{ 15, 15, 15, 15 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 }
};

static int aztarac_irq_callback(int irqline)
{
	return 0xc;
}

void  aztarac_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K4);
}

void  aztarac_sound_interrupt()
{
	cpu_do_int_imm(CPU1, INT_TYPE_INT);
}

void read_vectorram(int addr, int* x, int* y, int* c)
{
	*c = aztarac_vectorram[addr] & 0xffff;
	*x = aztarac_vectorram[addr + 0x800] & 0x03ff;
	*y = aztarac_vectorram[addr + 0x1000] & 0x03ff;
	if (*x & 0x200) *x |= 0xfffffc00;
	if (*y & 0x200) *y |= 0xfffffc00;
}


/*************************************
 *
 *	NVRAM handler
 *
 *************************************/

READ_HANDLER16(nvram_r)
{
//	return ((int16_t*)generic_nvram)[offset] | 0xfff0;
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
			memset(generic_nvram, 0, 0x200);
		}
	}
}

/*************************************
 *
 *	Input ports
 *
 *************************************/

READ_HANDLER16(joystick_r)
{
	return (((input_port_0_r(address) - 0xf) << 8) |
		((input_port_1_r(address) - 0xf) & 0xff));
}



static int aztarac_input_1_r(int offset)
{
	
}
static int aztarac_input_2_r(int offset)
{

}

WRITE_HANDLER16(aztarac_ubr_w)
{
	int x, y, c, intensity, xoffset, yoffset, color;
	int defaddr, objaddr = 0, ndefs;

	if (data) /* data is the global intensity (always 0xff in Aztarac). */
	{
		vector_clear_list();

		while (1)
		{
			read_vectorram(objaddr, &xoffset, &yoffset, &c);
			objaddr++;

			if (c & 0x4000)
				break;

			if ((c & 0x2000) == 0)
			{
				defaddr = (c >> 1) & 0x7ff;
				AVECTOR(xoffset, yoffset, 0, 0);

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
							AVECTOR(x + xoffset, y + yoffset, 0, 0);
						else
							AVECTOR(x + xoffset, y + yoffset, color, intensity);
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
						AVECTOR(x + xoffset, y + yoffset, color, c >> 8);
					}
				}
			}
		}
	}
}



static int sound_status;

READ_HANDLER16(aztarac_sound_r)
{
	//if (Machine->sample_rate)
		return sound_status & 0x01;
//	else
	//	return 1;
}

WRITE_HANDLER16(aztarac_sound_w)
{
	if (data & 0xff)
	{
		data &= 0xff;
		//soundlatch_w(offset, data);
		sound_status ^= 0x21;
		if (sound_status & 0x20)
			cpu_do_int_imm(CPU1, INT_TYPE_INT);
	}
}

READ_HANDLER(aztarac_snd_command_r)
{
	sound_status |= 0x01;
	sound_status &= ~0x20;
	return 0;// soundlatch_r(offset);
}

READ_HANDLER(aztarac_snd_status_r)
{
	return sound_status & ~0x01;
}

WRITE_HANDLER(aztarac_snd_status_w)
{
	sound_status &= ~0x10;
}

int aztarac_snd_timed_irq(void)
{
	sound_status ^= 0x10;

	if (sound_status & 0x10)
		//return interrupt();
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
	else
		return 0;
	return 0;
}



WRITE_HANDLER16(QMWA_NOP)
{
	wrlog("NOOP Write Address: %x Data: %x", address, data);
}

READ_HANDLER16 (UN_READ)
{
	wrlog("--------------------Unhandled Read, %x data: %x", address);
	return 0;
}

WRITE_HANDLER16(UN_WRITE)
{
	wrlog("--------------------Unhandled Read, %x data: %x", address, data);
}

READ_HANDLER16(QMRA_NOP)
{
	return 0x00;
}

WRITE_HANDLER16( watchdog_reset_wQ)
{
	watchdog_reset_w(0, 0, 0);
}

READ_HANDLER16(aztarac_snd_read)
{
	return 0;
}

WRITE_HANDLER16(aztarac_snd_write)
{
	
}

WRITE_HANDLER16(aztarac_colorram_w)
{
	int r, g, b;
	int bit0, bit1, bit2, bit3;

	address = (address & 0xff) >> 1;
	data = data & 0x00ff;

	bit3 = (~data >> 3) & 1;
	bit2 = (~data >> 2) & 1;
	bit1 = (~data >> 1) & 1;
	bit0 = (~data >> 0) & 1;

	g = bit1 * 0xaa + bit0 * 0x54; //54
	b = bit2 * 0xdf;
	r = bit3 * 0xe9; //ce

	if (r > 255)r = 255;
	// wrlog("vec color set R %d G %d B %d",r,g,b);
	vec_colors[address].r = r;
	vec_colors[address].g = g;
	vec_colors[address].b = b;
}

WRITE_HANDLER16(avgdvg_resetQ)
{
	wrlog("AVG Reset");
}




MEM_READ(AztaracReadByte)
{ 0x000000, 0x00bfff, NULL, aztarac_program_rom },
{ 0xff8000, 0xffffff,  NULL,aztarac_main_ram },
MEM_END

MEM_WRITE(AztaracWriteByte)
{ 0x000000, 0x00bfff, MWA_ROM, NULL },
{ 0xff8000, 0xffffff,  NULL,aztarac_main_ram },
MEM_END

MEM_READ16(AztaracReadWord)
{0x000000, 0x00bfff, NULL, aztarac_program_rom },

{ 0xff8000, 0xffafff,  NULL, vec_ram},
{ 0x027000, 0x027001, joystick_r },
//{ 0x027004, 0x027005, input_port_3_word_r },
{ 0x027008, 0x027009,  aztarac_snd_read,NULL },
//{ 0x02700c, 0x02700d, input_port_2_word_r },
{ 0x022000, 0x022fff,  NULL, generic_nvram },
{ 0x02700e, 0x02700f,  UN_READ,NULL },
{ 0xff8000, 0xffffff,  NULL,aztarac_main_ram },
MEM_END

MEM_WRITE16(AztaracWriteWord)
{ 0x000000, 0x00bfff, NULL, aztarac_program_rom },
{ 0xff8000, 0xffafff,  NULL,vec_ram},
{ 0x840000, 0x84003f,  aztarac_snd_write,NULL },
{ 0x022000, 0x022fff, NULL, generic_nvram },
{ 0xffb000, 0xffb001, aztarac_ubr_w },
{ 0xff8000, 0xffafff,  NULL, vec_ram },
{ 0xff8000, 0xffffff,  NULL,aztarac_main_ram },
MEM_END

MEM_READ(AztaracSoundMemRead)
{0x0000, 0x1fff, MRA_ROM},
{ 0x8000, 0x87ff, MRA_RAM },
//{ 0x8800, 0x8800, aztarac_snd_command_r },
//{ 0x8c00, 0x8c01, AY8910_read_port_0_r },
//{ 0x8c02, 0x8c03, AY8910_read_port_1_r },
//{ 0x8c04, 0x8c05, AY8910_read_port_2_r },
//{ 0x8c06, 0x8c07, AY8910_read_port_3_r },
//{ 0x9000, 0x9000, aztarac_snd_status_r },
MEM_END

MEM_WRITE(AztaracSoundMemWrite)
{0x0000, 0x1fff, MWA_ROM},
{ 0x8000, 0x87ff, MWA_RAM },
//{ 0x8c00, 0x8c00, AY8910_write_port_0_w },
//{ 0x8c01, 0x8c01, AY8910_control_port_0_w },
//{ 0x8c02, 0x8c02, AY8910_write_port_1_w },
//{ 0x8c03, 0x8c03, AY8910_control_port_1_w },
//{ 0x8c04, 0x8c04, AY8910_write_port_2_w },
//{ 0x8c05, 0x8c05, AY8910_control_port_2_w },
//{ 0x8c06, 0x8c06, AY8910_write_port_3_w },
//{ 0x8c07, 0x8c07, AY8910_control_port_3_w },
//{ 0x9000, 0x9000, aztarac_snd_status_w }, 
MEM_END

PORT_READ(AztaracSoundPortRead)
PORT_END

PORT_WRITE(AztaracSoundPortWrite)
PORT_END

void run_aztarac()
{
	watchdog_reset_w(0, 0, 0);

}

int init_aztarac()
{
	wrlog("Starting aztarac Init");
	memset(aztarac_main_ram, 0x00, 0x1fff);
	memset(vec_ram, 0x00, 0x1fff);
	memset(aztarac_program_rom, 0x00, 0x00C000);
	memset(generic_nvram, 0x00, 0x200);

	memcpy(aztarac_program_rom, Machine->memory_region[CPU0], 0x00C000);
	byteswap(aztarac_program_rom, 0x00C000);

	init68k(AztaracReadByte, AztaracWriteByte, AztaracReadWord, AztaracWriteWord,CPU0);
	
	//TODO: Implement
	//cpu_set_irq_callback(0, aztarac_irq_callback);
		
	wrlog("End aztarac Init");
	return 0;
}

void end_aztarac()
{
	
}


void aztarac_video_init(int scale)
{
	int xmin = Machine->drv->visible_area.min_x;
	int ymin = Machine->drv->visible_area.min_y;
	int xmax = Machine->drv->visible_area.max_x;
	int ymax = Machine->drv->visible_area.max_y;

	xcenter = ((xmax + xmin) / 2) << 16;
	ycenter = ((ymax + ymin) / 2) << 16;

	vector_set_shift(16);
	VECTOR_START();
}