//============================================================================
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
//============================================================================

#include "starwars.h"
#include "aae_mame_driver.h"
#include "aae_avg.h"
#include "aae_pokey.h"
#include "tms5220.h"
#include "loaders.h"
#include "starwars_machine.h"
#include "starwars_snd.h"
#include "slapstic.h"
#include "glcode.h"

#define MASTER_CLOCK (12096000)
#define CLOCK_3KHZ  (MASTER_CLOCK / 4096)

int catch_nextBranch;

extern int m6809_slapstic;

static UINT8 nvram[0x100];


int bank1 = 0x06000;
int bank2 = 0x0a000;

enum slapstic_states
{
	DISABLED,
	ENABLED,
	ALTERNATE1,
	ALTERNATE2,
	ALTERNATE3,
	BITWISE1,
	BITWISE2,
	BITWISE3,
	ADDITIVE1,
	ADDITIVE2,
	ADDITIVE3
};

/*************************************
 *
 *	NVRAM handler
 *
 *************************************/

void starwars_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
		osd_fwrite(file, nvram, 0x100);
	else if (file)
		osd_fread(file, nvram, 0x100);
	else
		memset(nvram, 0, 0x100);
}

WRITE_HANDLER(nvram_w)
{
	nvram[address] = data;
}

READ_HANDLER(nvram_r)
{
	return nvram[address];
}

static struct TMS5220interface tms5220_interface =
{
	640000,     // clock speed (80*samplerate) */
	255,        // volume */
	0           // IRQ handler */
};

static struct POKEYinterface pokey_interface =
{
	4,			/* 4 chips */
	1512000,	/* 1.5 MHz? */
	255,    /* volume */
	6,//POKEY_DEFAULT_GAIN/4,
	USE_CLIP,
	/* The 8 pot handlers */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	/* The allpot handler */
	{ 0, 0, 0, 0 },
};

////////////////////////////////////////////////////////////// SLAPSTIC
unsigned char* RAM;

static UINT8* slapstic_source;
static UINT8* slapstic_base;
static UINT8 current_bank;

static UINT8 is_esb;
int slapstic_en = 0;

// The slapstic encryption took me forever to figure out, even using a custom compiled MAME version. I guess I am just dense, it took me a week to
// understand that only the last memory read and the change_cpu function should be sent to these routines. Not the opcode reads, not any imtermediate reads,
// Just the final memory reads for each opcode Thank you RTS (0x39) for helping drive this home.
// Not understanding how MAME does it's banking really hurt me here.

void bank_switch_read(int address, int result)
{
	int new_bank = slapstic_tweak(address);

	/* update for the new bank */
	if (new_bank != current_bank)
	{
		current_bank = new_bank;
		//LOG_INFO("Bank switch read to %d bank Result %04x, new bank %d OPCODE: %02X", current_bank, result, new_bank, m_cpu_6809[0]->get_last_ireg());
	//	LOG_INFO("Bank switch read to %d bank", current_bank);
		memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);
	}
	//else
	//	LOG_INFO("Slapstic Read without Bank Switch %d bank, offset %04x, result %04x OPCODE: %0x", current_bank, address, result, m_cpu_6809[0]->get_last_ireg());
		//LOG_INFO("Slapstic Read without Bank Switch %d bank, offset %04x, result %04x\n", current_bank, address, result);
}

READ_HANDLER(esb_slapstic_r)
{
	int result = slapstic_base[address];

	//LOG_INFO("Slapstic Read Called, CPU %x, PC: %04x, PPC %04x, OPCODE: %02X", get_active_cpu(),m_cpu_6809[0]->get_pc(), m_cpu_6809[0]->ppc, this_opcode);

	if (slapstic_en) bank_switch_read(address, result);

	return result;
}

WRITE_HANDLER(esb_slapstic_w)
{
	int new_bank = 0;

	if (slapstic_en)
		new_bank = slapstic_tweak(address);

	/* update for the new bank */
	if (new_bank != current_bank)
	{
		current_bank = new_bank;
		//LOG_INFO("Bank switch write to %d bank", current_bank);
		memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);
	}
	//else LOG_INFO("Slapstic Write without Bank Switch %d bank\n", current_bank);
}

static int esb_setopbase(int address)
{
	static int counter = 0;
	static int last_opcode;
	int prevpc = cpu_getppc();

	/*
	 *	This is a slightly ugly kludge for Empire Strikes Back because it jumps
	 *	directly to code in the slapstic.
	 */

	 /* if we're jumping into the slapstic region, tweak the new PC */
	if ((address & 0xe000) == 0x8000)
	{
		esb_slapstic_r(address & 0x1fff, 0);

		//LOG_INFO("Slapstick in: PrevPC: %04x, address: %04x address adj: %04x, bank %d opcode %x", prevpc, address, (address & 0x1fff), current_bank, m_cpu_6809[0]->get_last_ireg());
		/* make sure we catch the next branch as well */
		//catch_nextBranch = 1;
		return -1;
	}

	/* if we're jumping out of the slapstic region, tweak the previous PC */
	else if ((prevpc & 0xe000) == 0x8000)
	{
		//LOG_INFO("Slapstick exit: PrevPC: %04x, PrevPC Adj: %04x address: %04x, bank %d OPcode %x", prevpc, (prevpc & 0xe000), address, current_bank , m_cpu_6809[0]->get_last_ireg());
		if (prevpc != 0x8080 && prevpc != 0x8090 && prevpc != 0x80a0 && prevpc != 0x80b0)
		{
			//LOG_INFO("Calling esb_slapstic_read from esb_setopbase jump out");
			esb_slapstic_r(prevpc & 0x1fff, 0);
		}
	}

	return address;
}

void esb_init_machine(void)
{
  /* Reset all the banks */
  //starwars_out_w(4, 0);
  //init_swmathbox();
}

/// ////////////////////////////////////////////////////////////////////////

WRITE_HANDLER(avgdvg_go_w)
{
	avg_go();
}

/* Read from ROM 0. Use bankaddress as base address */
READ_HANDLER(BANK1_R)
{
	return Machine->memory_region[0][bank1 + address];
}

READ_HANDLER(BANK2_R)
{
	return Machine->memory_region[0][bank2 + address];
}

/**********************************************************/
/************** Write Handlers ****************************/
/**********************************************************/

WRITE_HANDLER(irqclr)
{
}

/********************************************************/

void starwars_interrupt()
{
	cpu_do_int_imm(0, INT_TYPE_INT);
}

void end_starwars()
{
	starwars_sh_stop();
}

void run_starwars()
{
	starwars_sh_update();
}

/////////////////////////////////////////////////

// Star Wars READ memory map
MEM_READ(starwars_readmem)
MEM_ADDR(0x4300, 0x431f, ip_port_0_r) // Memory mapped input port 0
MEM_ADDR(0x4320, 0x433f, starwars_input_1_r) // Memory mapped input port 1
MEM_ADDR(0x4340, 0x435f, ip_port_2_r) // DIP switches bank 0
MEM_ADDR(0x4360, 0x437f, ip_port_3_r) // DIP switches bank 1
MEM_ADDR(0x4380, 0x439f, starwars_adc_r)   // ADC read
MEM_ADDR(0x4400, 0x4400, main_read_r) //Sound
MEM_ADDR(0x4401, 0x4401, main_ready_flag_r) //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_r) // nv_ram
MEM_ADDR(0x4700, 0x4700, swmathbx_reh_r)
MEM_ADDR(0x4701, 0x4701, swmathbx_rel_r)
MEM_ADDR(0x4703, 0x4703, swmathbx_prng_r) // pseudo random number generator
MEM_ADDR(0x4800, 0x5fff, MRA_RAM)
MEM_ADDR(0x6000, 0x7fff, BANK1_R)
//MEM_ADDR(0x8000, 0xffff, MRA_ROM)  // rest of main_rom
MEM_END

// Star Wars WRITE memory map
MEM_WRITE(starwars_writemem)
MEM_ADDR(0x0000, 0x2fff, MWA_RAM) // &vectorram
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)  // vector_rom
MEM_ADDR(0x4400, 0x4400, main_wr_w)  //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_w) // nv_ram
MEM_ADDR(0x4600, 0x461f, avgdvg_go_w)  // evggo(mine) or vg2_go
MEM_ADDR(0x4620, 0x463f, avgdvg_reset_w) // evgres(mine) or vg_reset
MEM_ADDR(0x4640, 0x465f, watchdog_reset_w) //  (wdclr) Watchdog clear
MEM_ADDR(0x4660, 0x467f, irqclr)  // clear periodic interrupt
MEM_ADDR(0x4680, 0x4687, starwars_out_w)
MEM_ADDR(0x46a0, 0x46bf, MWA_NOP)
MEM_ADDR(0x46c0, 0x46c2, starwars_adc_select_w)	// Selects which a-d control port (0-3) will be read
MEM_ADDR(0x46e0, 0x46e0, soundrst) //Sound
MEM_ADDR(0x4700, 0x4707, swmathbx_w)
MEM_ADDR(0x4800, 0x5fff, MWA_RAM) 	/* CPU and Math RAM */
MEM_ADDR(0x6000, 0xffff, MWA_ROM)		/* main_rom */
MEM_END

MEM_READ(empire_readmem)
MEM_ADDR(0x4300, 0x431f, ip_port_0_r) // Memory mapped input port 0
MEM_ADDR(0x4320, 0x433f, starwars_input_1_r) // Memory mapped input port 1
MEM_ADDR(0x4340, 0x435f, ip_port_2_r) // DIP switches bank 0
MEM_ADDR(0x4360, 0x437f, ip_port_3_r) // DIP switches bank 1
MEM_ADDR(0x4380, 0x439f, starwars_adc_r)   // ADC read
MEM_ADDR(0x4400, 0x4400, main_read_r) //Sound
MEM_ADDR(0x4401, 0x4401, main_ready_flag_r) //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_r) // nv_ram
MEM_ADDR(0x4700, 0x4700, swmathbx_reh_r)
MEM_ADDR(0x4701, 0x4701, swmathbx_rel_r)
MEM_ADDR(0x4703, 0x4703, swmathbx_prng_r) // pseudo random number generator
MEM_ADDR(0x4800, 0x5fff, MRA_RAM)		/* CPU and Math RAM */
MEM_ADDR(0x6000, 0x7fff, BANK1_R)	    /* banked ROM */
MEM_ADDR(0x8000, 0x9fff, esb_slapstic_r)//, &slapstic_area },
MEM_ADDR(0xa000, 0xffff, BANK2_R)		/* banked ROM */
/* Dummy entry to set up the slapstic */
MEM_ADDR(0x14000, 0x1bfff, MRA_NOP) //, &atarigen_slapstic },
MEM_END

MEM_WRITE(empire_writemem)
MEM_ADDR(0x0000, 0x2fff, MWA_RAM)    //&vectorram
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)    //vector_rom
MEM_ADDR(0x4400, 0x4400, main_wr_w)  //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_w)    //nv_ram
MEM_ADDR(0x4600, 0x461f, avgdvg_go_w)      // evggo(mine) or vg2_go
MEM_ADDR(0x4620, 0x463f, avgdvg_reset_w)   // evgres(mine) or vg_reset
MEM_ADDR(0x4640, 0x465f, watchdog_reset_w) //  (wdclr) Watchdog clear
MEM_ADDR(0x4660, 0x467f, irqclr)  // clear periodic interrupt
MEM_ADDR(0x4680, 0x4687, starwars_out_w)
MEM_ADDR(0x46a0, 0x46bf, MWA_NOP)
MEM_ADDR(0x46c0, 0x46c2, starwars_adc_select_w)	// Selects which a-d control port (0-3) will be read{ 0x46e0, 0x46e0, soundrst },
MEM_ADDR(0x4700, 0x4707, swmathbx_w)
MEM_ADDR(0x8000, 0x9fff, esb_slapstic_w)		/* slapstic write */
MEM_END

// Star Wars Sound READ memory map
MEM_READ(starwars_audio_readmem)
MEM_ADDR(0x0800, 0x0fff, sin_r) // SIN Read
MEM_ADDR(0x1000, 0x107f, MRA_RAM)  // 6532 RAM
MEM_ADDR(0x1080, 0x109f, m6532_r)
MEM_ADDR(0x4000, 0xbfff, MRA_ROM) // sound roms
MEM_ADDR(0xc000, 0xffff, MRA_ROM) // load last rom twice
MEM_END

// Star Wars sound WRITE memory map
MEM_WRITE(starwars_audio_writemem)
MEM_ADDR(0x0000, 0x07ff, sout_w)
MEM_ADDR(0x1000, 0x107f, MWA_RAM) // 6532 ram
MEM_ADDR(0x1080, 0x109f, m6532_w)
MEM_ADDR(0x1800, 0x183f, quadpokey_w)//starwars_pokey_sound_w },
MEM_ADDR(0x2000, 0x27ff, MWA_RAM) // program RAM
MEM_ADDR(0x4000, 0xbfff, MWA_ROM) // sound rom
MEM_ADDR(0xc000, 0xffff, MWA_ROM) // sound rom again, for intvecs
MEM_END

int init_esb()
{
	/* Set up the slapstic */
	slapstic_init(101);
	slapstic_source = &memory_region(REGION_CPU1)[0x14000];
	slapstic_base = &memory_region(REGION_CPU1)[0x08000];
	m6809_slapstic = 1;
	cpu_setOPbaseoverride(esb_setopbase);

	slapstic_reset();
	current_bank = slapstic_bank();
	memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);

	starwars_sh_start();

	init6809(empire_readmem, empire_writemem, 0);
	init6809(starwars_audio_readmem, starwars_audio_writemem, 1);
	swmathbox_init();
	avg_start_starwars();
	// TODO: This is temporary, figure it out.
	config.gain = 0;

	return 0;
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_starwars(void)
{
	starwars_sh_start();
	init6809(starwars_readmem, starwars_writemem, 0);
	init6809(starwars_audio_readmem, starwars_audio_writemem, 1);
	swmathbox_init();
	avg_start_starwars();
	return 0;
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////