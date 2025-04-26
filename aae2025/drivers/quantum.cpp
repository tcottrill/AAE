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
#include "vector.h"
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

static unsigned char program_rom[0x14000];
static unsigned char main_ram[0x5000];
unsigned char nv_ram[0x200];

void  quantum_interrupt()
{
	wrlog("Quantum Interrupt");
	cpu_do_int_imm(CPU0, INT_TYPE_68K1);
}

void QSaveScore(void)
{
	char temppath[255];
	FILE* fp;
	int i;

	strcpy(temppath, "hi\\");
	strcat(temppath, driver[gamenum].name);
	strcat(temppath, ".aae");
	//wrlog("TEMPPATH SAVING is %s",temppath);
	fp = fopen(temppath, "w");
	if (fp == NULL) { wrlog("Error - I couldn't write Quantum High Score to path %s.", temppath); }
	for (i = 0; i < 0x200; i++) { fprintf(fp, "%c", nv_ram[i]); }fclose(fp);
}
int QLoadScore(void)
{
	FILE* fp;
	char c;
	int i = 0;
	char temppath[255];
	strcpy(temppath, "hi\\");
	strcat(temppath, driver[gamenum].name);
	strcat(temppath, ".aae");
	// wrlog("TEMPPATH LOADING is %s",temppath);
	fp = fopen(temppath, "r");

	if (fp == NULL) { wrlog("Error - Quantum High Score not found."); }
	if (fp != NULL)
	{
		do {
			c = getc(fp);    /* get one character from the file */
			nv_ram[i] = c;         /* display it on the monitor       */
			i++;
		} while (i < 0x200);    /* repeat until EOF (end of file)  */
		fclose(fp);
		return 1;
	}
	else return 0;
}

static void byteswap(unsigned char* mem, int length)
{
	int i, j;
	for (i = 0; i < (length / 2); i += 1)
	{
		j = mem[i * 2 + 0];
		mem[i * 2 + 0] = mem[i * 2 + 1];
		mem[i * 2 + 1] = j;
	}
}
void my_reset_handler(void) { wrlog("_____RESET CALLED _____"); }

void vec_ram_w(unsigned address, unsigned data)
{
	//wrlog("Vector Ram Write Address: %x data %x  Datapart1 %x Data part2 %x",address,data,(data & 0xff00)>>8,(data & 0x00ff) );
	vec_ram[address - 0x800000] = (data & 0xff00) >> 8;
	vec_ram[(address + 1) - 0x800000] = data & 0x00ff;
}
unsigned vec_ram_r(unsigned address)
{
	int c;
	c = vec_ram[address - 0x800000] << 8 | vec_ram[(address + 1) - 0x800000];
	//wrlog("Vector Ram READ: Data Returned %x",(vec_ram[address-0x800000]<<8 | vec_ram[(address+1)-0x800000]) );
	return c;
}

unsigned quantum_trackball_r(unsigned address)
{
	return (readinputportbytag("IN2") << 4) | readinputportbytag("IN3");
}

unsigned quantum_switches_r(unsigned address)
{
	return input_port_0_r(0) | 0x00;
}

int quantum_input_1_r(int offset)
{
	return (readinputportbytag("DSW0") << (7 - (offset - POT0_C))) & 0x80;
}

int quantum_input_2_r(int offset)
{
	return (readinputportbytag("DSW1") << (7 - (offset - POT0_C))) & 0x80;
}

void quantum_led_write(unsigned address, unsigned data)
{
	if (data & 0xff)
	{
		/* bits 0 and 1 are coin counters */

		/* bit 3 = select second trackball for cocktail mode? */

		/* bits 4 and 5 are LED controls */
		set_led_status(0, data & 0x10);
		set_led_status(1, data & 0x20);
		/* bits 6 and 7 flip screen */
		//vector_set_flip_x (data & 0x40);
		//vector_set_flip_y (data & 0x80);
	}
}
void QMWA_NOP(unsigned address, unsigned data)
{
	wrlog("NOOP Write Address: %x Data: %x", address, data);
}

void UN_READ(unsigned address)
{
	wrlog("--------------------Unhandled Read, %x data: %x", address);
}

void UN_WRITE(unsigned address, unsigned data)
{
	wrlog("--------------------Unhandled Read, %x data: %x", address, data);
}

unsigned QMRA_NOP(unsigned address)
{
	return 0x00;
}//wrlog("WATCHDOG read");

void watchdog_reset_wQ(unsigned address, unsigned data)
{
	watchdog_reset_w(0, 0, 0);
}//wrlog("watchdog reset WRITE");

unsigned quantum_snd_read(unsigned address)
{
	address -= 0x840000;
	if (address & 0x20)
		return pokey2_r((address >> 1) % 0x10);
	else
		return pokey1_r((address >> 1) % 0x10);
}

void quantum_snd_write(unsigned address, unsigned data)
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

void quantum_colorram_w(unsigned address, unsigned data)
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

void avgdvg_resetQ(unsigned address, unsigned data)
{
	wrlog("AVG Reset");
}

void avgdvg_goQ(unsigned address, unsigned data)
{
	avg_clear();
	avg_go();
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
/*
struct STARSCREAM_PROGRAMREGION quantum_fetch[] = {
	 {0x000000, 0x013FFF, (unsigned)program_rom - 0x000000},
	 {0x018000, 0x01CFFF, (unsigned)main_ram - 0x018000},
	 {0x900000, 0x9001ff, (unsigned)nv_ram - 0x900000},
	 {-1, -1, (unsigned)NULL}
};

struct STARSCREAM_DATAREGION quantum_readbyte[] =
{
	{ 0x000000, 0x013fff,  NULL, program_rom },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff,  vec_ram_r,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_read,NULL },
	{ 0x900000, 0x9001ff,  NULL, nv_ram},
	{ 0x940000, 0x940001,  quantum_trackball_r, NULL},
	{ 0x948000, 0x948001,  quantum_switches_r, NULL },
	//{ 0x950000, 0x95001f,  NULL, color_ram},
	{ 0x960000, 0x9601ff,  UN_READ,NULL },
	{ 0x978000, 0x978001,  QMRA_NOP,NULL },
	//{ 0x000000, 0xfffff, UN_READ, NULL },
	 {-1, -1, NULL, NULL}
};

struct STARSCREAM_DATAREGION quantum_readword[] =
{
	{ 0x000000, 0x013fff,  NULL, program_rom },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff,  vec_ram_r,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_read,NULL },
	{ 0x900000, 0x9001ff,  NULL, nv_ram},
	{ 0x940000, 0x940001,  quantum_trackball_r, NULL},
	{ 0x948000, 0x948001,  quantum_switches_r, NULL },
	//{ 0x950000, 0x95001f,  NULL, color_ram},
	{ 0x960000, 0x9601ff,  UN_READ,NULL },
	{ 0x978000, 0x978001,  QMRA_NOP,NULL },
	//{ 0x000000, 0xfffff, UN_READ, NULL },
	 {-1, -1, NULL, NULL}
};

struct STARSCREAM_DATAREGION quantum_writebyte[] =
{
	{ 0x000000, 0x013fff,  UN_WRITE,NULL },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff,  vec_ram_w,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_write,NULL },
	{ 0x900000, 0x9001ff,  NULL,nv_ram },
	{ 0x950000, 0x95001f,  quantum_colorram_w,NULL},
	{ 0x958000, 0x958001,  quantum_led_write,NULL },
	{ 0x960000, 0x960001,  QMWA_NOP,NULL },	// enable NVRAM?
	{ 0x968000, 0x968001,  avgdvg_resetQ,NULL },
	//{ 0x970000, 0x970001,  avgdvg_goQ,NULL },
	//{ 0x970000, 0x970001, NULL,qavggo },
	//{ 0x978000, 0x978001,  watchdog_reset_w,NULL },
	// the following is wrong, but it's the only way I found to fix the service mode
	//{ 0x978000, 0x978001,  avgdvg_goQ,NULL },
	//{ 0x000000, 0xfffff, UN_WRITE, NULL },
	{-1, -1, NULL, NULL}
};

struct STARSCREAM_DATAREGION quantum_writeword[] =
{
	{ 0x000000, 0x013fff, UN_WRITE,NULL },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff, vec_ram_w,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_write,NULL },
	{ 0x900000, 0x9001ff, NULL,nv_ram },
	{ 0x950000, 0x95001f, quantum_colorram_w, NULL},
	{ 0x958000, 0x958001, quantum_led_write,NULL },
	{ 0x960000, 0x960001, QMWA_NOP,NULL },	// enable NVRAM?
	{ 0x968000, 0x968001, avgdvg_resetQ,NULL },
	//{ 0x970000, 0x970001,  avgdvg_goQ,NULL },
	{ 0x978000, 0x978001, watchdog_reset_wQ,NULL },
	// the following is wrong, but it's the only way I found to fix the service mode
	{ 0x978000, 0x978001,  avgdvg_goQ,NULL },
	{ 0x000000, 0xfffff, UN_WRITE, NULL },
	 {-1, -1, NULL, NULL}
};
*/

/*--------------------------------------------------------------------------*/
/* Memory handlers                                                          */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_bus_8(unsigned int address)
{
	return 0;
}

unsigned int m68k_read_bus_16(unsigned int address)
{
	return 0;
}

void m68k_unused_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %08X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_8_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %02X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_16_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %04X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_lockup_w_8(unsigned int address, unsigned int value)
{
	m68k_end_timeslice();
}

void m68k_lockup_w_16(unsigned int address, unsigned int value)
{
	m68k_end_timeslice();
}

unsigned int m68k_lockup_r_8(unsigned int address)
{
	m68k_end_timeslice();
	return -1;
}

unsigned int m68k_lockup_r_16(unsigned int address)
{
	m68k_end_timeslice();
	return -1;
}

/*--------------------------------------------------------------------------*/
/* 68000 memory handlers                                                    */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_memory_8(unsigned int address)
{
	if (address <= 0x14000) { return READ_BYTE(program_rom, address); }
	else
	if ((address >= 0x18000) && (address <= 0x1D000))
	{
		return READ_BYTE(main_ram, address - 0x18000);
	}

	wrlog("Unhandled Memory 8 Read: addr: %x", address);
	return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	//wrlog("Write Memory 8, addr: %x, data %x", address, value);

	if (address <= 0x14000) { wrlog("attempted rom write"); return; }
	else
	if ((address >= 0x18000) && (address <= 0x1D000))
		{
			//wrlog("Ram read %x", address);
			WRITE_BYTE(main_ram, address - 0x18000, value);
			return;
		}

	wrlog("Unhandled Memory 8 Read: addr: %x", address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	if (address <= 0x14000) { return READ_WORD(program_rom, address); }
	else
	if ((address >= 0x18000) && (address <= 0x1D000)) {	return READ_WORD(main_ram, address - 0x18000);	}
	else
	if ((address >= 0x800000) && (address <= 0x801FFF)) {return vec_ram_r(address);	}
	else
	if ((address >= 0x840000) && (address <= 0x84003f)) { return quantum_snd_read(address); }
	else
	if ((address >= 0x940000) && (address <= 0x940001)) { return quantum_trackball_r(address); }
	else	
	if ((address >= 0x948000) && (address <= 0x948001)) { return quantum_switches_r(address); }
	else
	if ((address == 0x978000) || (address == 0x978001)) { return 0; }
	else
	if ((address >= 0x900000) && (address <= 0x9001ff)) { return READ_WORD(nv_ram, address - 0x900000);}
	wrlog("Unhandled Read 16: %x ", address);
	return 0;
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	//wrlog("Write Memory 16, addr: %x, data %x", address, value);

	if (address <= 0x14000) { wrlog("Attempted write on rom"); return; }
	else
	if ((address >= 0x18000) && (address <= 0x1D000))  {WRITE_WORD(main_ram, address - 0x18000, value);	return;	}
	else
	if ((address >= 0x800000) && (address <= 0x801FFF)) { vec_ram_w(address,value); return; }
	else
	if ((address >= 0x840000) && (address <= 0x84003f)) { quantum_snd_write(address, value); return; }
	else
	if ((address >= 0x900000) && (address <= 0x9001ff)) { WRITE_WORD(nv_ram, address - 0x900000, value); return; }
	else
	if ((address >= 0x950000) && (address <= 0x95001f)) { quantum_colorram_w(address, value); return; }
	else
	if ((address >= 0x958000) && (address <= 0x958001)) { quantum_led_write(address, value); return; }
	else
	if (address == 0x958000) { printf("LED WRITE\n"); return; }
	else
	if (address == 0x960000) { printf("Ignored Write\n"); return; }
	else
	if (address == 0x968000) { printf("Vector Reset\n"); return; }
	else
		if ((address == 0x970000) || (address == 0x970001)) 
		{
			wrlog("<------------- AVG Go called, at address %x with data %x", address, value);
			//avgdvg_goQ(address, value);	
		return; 
		}
	else
	if ((address == 0x978000) || (address == 0x978001)) 
	{
		//printf("WATCHDOG WRITE: %x\n", value); 
		//avgdvg_goQ(address, value);
		return; 
	}
	
	wrlog("Unhandled write 16: %x ", address);
	//switch((address >> 21) & 7)
	// {
	// case 0: break; /* ROM */
	//}
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	//wrlog("Reading Memory 32, addr: %x", address);

	/* Split into 2 reads */
	return (m68k_read_memory_16(address + 0) << 16 | m68k_read_memory_16(address + 2));
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	//wrlog("Write Memory 32, addr: %x, data %x\n", address, value);
	/* Split into 2 writes */
	m68k_write_memory_16(address, (value >> 16) & 0xFFFF);
	m68k_write_memory_16(address + 2, value & 0xFFFF);
}

void run_quantum()
{
	avg_clear();
	avg_go();
	
	watchdog_reset_w(0, 0, 0);
	pokey_sh_update();
}

int init_quantum()
{
	wrlog("Starting Quantum Init");
	memset(main_ram, 0x00, 0x4fff);
	memset(vec_ram, 0x00, 0x1fff);
	memset(program_rom, 0x00, 0x13fff);
	memset(nv_ram, 0xff, 0x200);

	memcpy(program_rom, Machine->memory_region[CPU0], 0x14000);//0x14000
	byteswap(program_rom, 0x14000);

	init68k();
	avg_init();

	//timer_set(TIME_IN_HZ(246), 0, quantum_interrupt);
	pokey_sh_start(&pokey_interface);
	QLoadScore();
	wrlog("End Quantum Init");
	return 0;
}

void end_quantum()
{
	QSaveScore();
	pokey_sh_stop();
}