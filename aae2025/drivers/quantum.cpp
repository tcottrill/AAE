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

#include "quantum.h"
#include "./68000/m68k.h"
#include "aae_mame_driver.h"
#include "cpu_control.h"
#include "aae_avg.h"
#include "earom.h"
#include "pokyintf.h"
#include "math.h"
#include "timer.h"

#pragma warning(disable : 4838)

/*
QUANTUM MEMORY MAP (per schem):

000000-003FFF	ROM0
004000-004FFF	ROM1
008000-00BFFF	ROM2
00C000-00FFFF	ROM3
010000-013FFF	ROM4

018000-01BFFF	RAM0
01C000-01CFFF	RAM1

940000			TRACKBALL
948000			SWITCHES
950000			COLORRAM
958000			CONTROL (LED and coin control)
960000-970000	RECALL (nvram read)
968000			VGRST (vector reset)
970000			VGGO (vector go)
978000			WDCLR (watchdog)
900000			NVRAM (nvram write)
840000			I/OS (sound and dip switches)
800000-801FFF	VMEM (vector display list)
940000			I/O (shematic label really - covered above)
900000			DTACK1
*/

//#define BYTESWAP(x) ((((uint16_t)(x))>>8) | (((uint16_t)(x))<<8))

unsigned char program_rom[0x14000];
unsigned char main_ram[0x5000];
unsigned char nv_ram[0x200];


void  quantum_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K1);
}


void quantum_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		osd_fwrite(file, nv_ram, 0x200);
	}
	else
	{
		if (file)
		{
			osd_fread(file, nv_ram, 0x200);
		}
		else
		{
			memset(nv_ram, 0, 0x200);
		}
	}
}

READ16_HANDLER(quantum_trackball_r)
{
	return (readinputportbytag("IN2") << 4) | readinputportbytag("IN3");
}

READ16_HANDLER(quantum_switches_r)
{
	return (input_port_0_r(0) | (avg_check() ? 1 : 0));
}

static int quantum_input_1_r(int offset)
{
	return (readinputportbytag("DSW0") << (7 - (offset - POT0_C))) & 0x80;
}
static int quantum_input_2_r(int offset)
{
	return (readinputportbytag("DSW1") << (7 - (offset - POT0_C))) & 0x80;
}

WRITE16_HANDLER(quantum_led_write)
{
	if (data & 0xff)
	{
		/* bits 0 and 1 are coin counters */
		//coin_counter_w(0, data & 2);
		//coin_counter_w(1, data & 1);
		/* bit 3 = select second trackball for cocktail mode? */

		/* bits 4 and 5 are LED controls */
		set_led_status(0, data & 0x10);
		set_led_status(1, data & 0x20);
		/* bits 6 and 7 flip screen */
		//vector_set_flip_x (data & 0x40);
		//vector_set_flip_y (data & 0x80);
		//vector_set_swap_xy(1);	/* vertical game */
	}
}

READ16_HANDLER(MRA_NOP16)
{
	//wrlog("--------------------Unhandled Read, %x data: %x", address);
	return 0;
}


READ16_HANDLER(quantum_snd_read)
{
	if (address & 0x20)
		return pokey2_r((address >> 1) % 0x10);
	else
		return pokey1_r((address >> 1) % 0x10);
}

WRITE16_HANDLER(quantum_snd_write)
{
	unsigned int data1;
	unsigned int data2;

	data1 = (data & 0xff00) >> 8;
	data2 = data & 0x00ff;

	if (address & 0x1) {
		pokey1_w((address >> 1) & 0xf, data1 & 0xff);
	}
	else {
		pokey2_w((address >> 1) & 0xf, data2 & 0xff);
	}
}


static struct POKEYinterface pokey_interface =
{
	2,			/* 2 chips */
	600000,
	200,	/* volume */
	6, //POKEY_DEFAULT_GAIN/2
	NO_CLIP,
	/* The 8 pot handlers */
	/* The 8 pot handlers */
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	/* The allpot handler */
	{ 0, 0 },
};


MEM_READ(QuantumReadByte)
{ 0x000000, 0x013fff, NULL, program_rom },
{ 0x018000, 0x01cfff, NULL, main_ram },
MEM_END

MEM_WRITE(QuantumWriteByte)
{ 0x000000, 0x013fff, MWA_ROM, NULL },
{ 0x018000, 0x01cfff, NULL, main_ram },
MEM_END

MEM_READ16(QuantumReadWord)
{ 0x000000, 0x013fff,  NULL, program_rom },
{ 0x018000, 0x01cfff,  NULL,main_ram },
{ 0x800000, 0x801fff,  NULL, vec_ram},
{ 0x840000, 0x84003f,  quantum_snd_read,NULL },
{ 0x900000, 0x9001ff,  NULL, nv_ram},
{ 0x940000, 0x940001,  quantum_trackball_r, NULL},
{ 0x948000, 0x948001,  quantum_switches_r, NULL },
{ 0x950000, 0x95001f, NULL, quantum_colorram },
{ 0x960000, 0x9601ff,  MRA_NOP16,NULL },
{ 0x978000, 0x978001,  MRA_NOP16,NULL },
MEM_END

MEM_WRITE16(QuantumWriteWord)
{ 0x000000, 0x013fff, MWA_ROM16, NULL },
{ 0x018000, 0x01cfff,  NULL, main_ram },
{ 0x800000, 0x801fff,  NULL,vec_ram},
{ 0x840000, 0x84003f,  quantum_snd_write,NULL },
{ 0x900000, 0x9001ff, NULL, nv_ram },
{ 0x950000, 0x95001f, NULL, quantum_colorram},
{ 0x958000, 0x958001, quantum_led_write,NULL },
{ 0x960000, 0x960001, MWA_NOP16,NULL },	// enable NVRAM?
{ 0x968000, 0x968001, avgdvg_reset_word_w,NULL },
//{ 0x970000, 0x970001,  avgdvg_go_word_w,NULL },
//{ 0x978000, 0x978001, watchdog_reset_wQ,NULL },
// the following is wrong, but it's the only way I found to fix the service mode
{ 0x978000, 0x978001,  avgdvg_go_word_w,NULL },
MEM_END

void run_quantum()
{
	watchdog_reset_w(0, 0, 0);
	pokey_sh_update();
}

int init_quantum()
{
	wrlog("Starting Quantum Init");
	memset(main_ram, 0x00, 0x4fff);
	memset(vec_ram, 0x00, 0x1fff);
	memset(program_rom, 0x00, 0x13fff);
	memset(nv_ram, 0x00, 0x200);

	memcpy(program_rom, Machine->memory_region[CPU0], 0x14000);
	byteswap(program_rom, 0x14000);

	init68k(QuantumReadByte, QuantumWriteByte, QuantumReadWord, QuantumWriteWord,CPU0);
	avg_start_quantum();

	//timer_set(TIME_IN_HZ(246), 0, quantum_interrupt);
	pokey_sh_start(&pokey_interface);
	wrlog("End Quantum Init");
	return 0;
}

void end_quantum()
{
	pokey_sh_stop();
}