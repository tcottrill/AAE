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
#include "driver_registry.h"
#include "aae_avg.h"
#include "aae_pokey.h"
#include "tms5220.h"
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
	LOG_INFO("Calling ESB Init");
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
	swmathbox_init();
	avg_start_starwars();
	return 0;
}


INPUT_PORTS_START(starwars)
PORT_START("IN0")//// IN0 
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_TILT)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_TOGGLE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x10, "Off")
PORT_DIPSETTING(0x00, "On")
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START1)

PORT_START("IN1")	// IN1 
PORT_BIT(0x03, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
// Bit 6 is MATH_RUN - see machine/starwars.c 
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
// Bit 7 is VG_HALT - see machine/starwars.c 
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW")	// DSW0 
PORT_DIPNAME(0x03, 0x00, "Shields")
PORT_DIPSETTING(0x00, "6")
PORT_DIPSETTING(0x01, "7")
PORT_DIPSETTING(0x02, "8")
PORT_DIPSETTING(0x03, "9")
PORT_DIPNAME(0x0c, 0x04, "Difficulty")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x04, "Moderate")
PORT_DIPSETTING(0x08, "Hard")
PORT_DIPSETTING(0x0c, "Hardest")
PORT_DIPNAME(0x30, 0x00, "Bonus Shields")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x10, "1")
PORT_DIPSETTING(0x20, "2")
PORT_DIPSETTING(0x30, "3")
PORT_DIPNAME(0x40, 0x00, "Attract Music")
PORT_DIPSETTING(0x00, "On")
PORT_DIPSETTING(0x40, "Off")
PORT_DIPNAME(0x80, 0x80, "Game Mode")
PORT_DIPSETTING(0x00, "Freeze")
PORT_DIPSETTING(0x80, "Normal")

PORT_START("DS1")// DSW1 
PORT_DIPNAME(0x03, 0x02, "Credits/Coin")
PORT_DIPSETTING(0x00, "Free Play")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x02, "1")
PORT_DIPSETTING(0x03, "1/2")
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN4")	// IN4 
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 70, 30, 0, 0, 255)

PORT_START("IN5")	// IN5 
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 50, 30, 0, 0, 255)
INPUT_PORTS_END


INPUT_PORTS_START(esb)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_TILT)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON4)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON3)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
/* Bit 6 is MATH_RUN - see machine/starwars.c */
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
/* Bit 7 is VG_HALT - see machine/starwars.c */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW0")	/* DSW0 */
PORT_DIPNAME(0x03, 0x03, "Starting Shields")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x03, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Difficulty))
PORT_DIPSETTING(0x08, "Easy")
PORT_DIPSETTING(0x0c, "Moderate")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPSETTING(0x04, "Hardest")
PORT_DIPNAME(0x30, 0x30, "Jedi-Letter Mode")
PORT_DIPSETTING(0x00, "Level Only")
PORT_DIPSETTING(0x10, "Level")
PORT_DIPSETTING(0x20, "Increment Only")
PORT_DIPSETTING(0x30, "Increment")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, "Freeze")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")	/* DSW1 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0xe0, 0xe0, "Bonus Coinage")
PORT_DIPSETTING(0x20, "2 gives 1")
PORT_DIPSETTING(0x60, "4 gives 2")
PORT_DIPSETTING(0xa0, "3 gives 1")
PORT_DIPSETTING(0x40, "4 gives 1")
PORT_DIPSETTING(0x80, "5 gives 1")
PORT_DIPSETTING(0xe0, "None")
/* 0xc0 and 0x00 None */
PORT_START("IN4")	/* IN4 */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 70, 30, 0, 0, 255)

PORT_START("IN5")	// IN5 
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 50, 30, 0, 0, 255)
INPUT_PORTS_END



ROM_START(starwars)
ROM_REGION(0x14000, REGION_CPU1, 0)
// Vector Rom
ROM_LOAD("136021-105.1l", 0x3000, 0x1000, CRC(538e7d2f) SHA1(032c933fd94a6b0b294beee29159a24494ae969b))
// Banked Roms

ROM_LOAD("136021.214.1f", 0x6000, 0x2000, CRC(4f1876e) SHA1(c1d3637cb31ece0890c25f6122d6bcd27e6ffe0c))
ROM_CONTINUE(0x10000, 0x2000)
ROM_LOAD("136021.102.1hj", 0x8000, 0x2000, CRC(f725e344) SHA1(f8943b67f2ea032ab9538084756ba86f892be5ca))
ROM_LOAD("136021.203.1jk", 0xa000, 0x2000, CRC(f6da0a00) SHA1(dd53b643be856787bbc4da63e5eb132f98f623c3))
ROM_LOAD("136021.104.1kl", 0xc000, 0x2000, CRC(7e406703) SHA1(981b505d6e06d7149f8bcb3e81e4d0c790f2fc86))
ROM_LOAD("136021.206.1m", 0xe000, 0x2000, CRC(c7e51237) SHA1(4960f4446271316e3f730eeb2531dbc702947395))

ROM_REGION(0x10000, REGION_CPU2, 0) // Audio
ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0))
ROM_RELOAD(0xc000, 0x2000) /* Copied again for */
ROM_LOAD("136021-208.1h", 0x6000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))
ROM_RELOAD(0xe000, 0x2000) /* proper int vecs */

ROM_REGION(0x1000, REGION_GFX1, 0)
// Mathbox
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4))
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977))
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c))
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c))

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END

ROM_START(starwars1)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD("136021-105.1l", 0x3000, 0x1000, CRC(538e7d2f) SHA1(032c933fd94a6b0b294beee29159a24494ae969b))  /* 3000-3fff is 4k vector rom */
ROM_LOAD("136021.214.1f", 0x6000, 0x2000, CRC(4f1876e) SHA1(c1d3637cb31ece0890c25f6122d6bcd27e6ffe0c))
ROM_CONTINUE(0x10000, 0x2000)
ROM_LOAD("136021.102.1hj", 0x8000, 0x2000, CRC(f725e344) SHA1(f8943b67f2ea032ab9538084756ba86f892be5ca)) /*  8k ROM 1 bank */
ROM_LOAD("136021.203.1jk", 0xa000, 0x2000, CRC(f6da0a00) SHA1(dd53b643be856787bbc4da63e5eb132f98f623c3)) /*  8k ROM 2 bank */
ROM_LOAD("136021.104.1kl", 0xc000, 0x2000, CRC(7e406703) SHA1(981b505d6e06d7149f8bcb3e81e4d0c790f2fc86)) /*  8k ROM 3 bank */
ROM_LOAD("136021.206.1m", 0xe000, 0x2000, CRC(c7e51237) SHA1(4960f4446271316e3f730eeb2531dbc702947395))  /*  8k ROM 4 bank */

ROM_REGION(0x10000, REGION_CPU2, 0) /* Really only 32k, but it looks like 64K */
ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0)) /* Sound ROM 0 */
ROM_LOAD("136021-107.1jk", 0xc000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0))
ROM_LOAD("136021-208.1h", 0x6000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))  /* Sound ROM 1 */
ROM_LOAD("136021-208.1h", 0xe000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))

ROM_REGION(0x1000, REGION_GFX1, 0)
// Mathbox
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4))
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977))
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c))
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c))

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END


ROM_START(esb)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("136031.111", 0x03000, 0x1000, CRC(b1f9bd12) SHA1(76f15395c9fdcd80dd241307a377031a1f44e150))    // 3000-3fff is 4k vector rom
ROM_LOAD("136031.101", 0x06000, 0x2000, CRC(ef1e3ae5) SHA1(d228ff076faa7f9605badeee3b827adb62593e0a))
ROM_CONTINUE(0x10000, 0x2000)
// $8000 - $9fff : slapstic page
ROM_LOAD("136031.102", 0x0a000, 0x2000, CRC(62ce5c12) SHA1(976256acf4499dc396542a117910009a8808f448))
ROM_CONTINUE(0x1c000, 0x2000)
ROM_LOAD("136031.203", 0x0c000, 0x2000, CRC(27b0889b) SHA1(a13074e83f0f57d65096d7f49ae78f33ab00c479))
ROM_CONTINUE(0x1e000, 0x2000)
ROM_LOAD("136031.104", 0x0e000, 0x2000, CRC(fd5c725e) SHA1(541cfd004b1736b6cec13836dfa813f00eedeed0))
ROM_CONTINUE(0x20000, 0x2000)

ROM_LOAD("136031.105", 0x14000, 0x4000, CRC(ea9e4dce) SHA1(9363fd5b1fce62c2306b448a7766eaf7ec97cdf5)) // slapstic 0, 1
ROM_LOAD("136031.106", 0x18000, 0x4000, CRC(76d07f59) SHA1(44dd018b406f95e1512ce92923c2c87f1458844f)) // slapstic 2, 3

// Sound ROMS
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136031.113", 0x4000, 0x2000, CRC(24ae3815) SHA1(b1a93af76de79b902317eebbc50b400b1f8c1e3c)) // Sound ROM 0
ROM_CONTINUE(0xc000, 0x2000) // Copied again for
ROM_LOAD("136031.112", 0x6000, 0x2000, CRC(ca72d341) SHA1(52de5b82bb85d7c9caad2047e540d0748aa93ba5)) // Sound ROM 1
ROM_CONTINUE(0xe000, 0x2000) // proper int vecs

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136031.110", 0x0000, 0x0400, CRC(b8d0f69d) SHA1(c196f1a592bd1ac482a81e23efa224d9dfaefc0a)) /* PROM 0 */
ROM_LOAD("136031.109", 0x0400, 0x0400, CRC(6a2a4d98) SHA1(cefca71f025f92a193c5a7d8b5ab8be10db2fd44)) /* PROM 1 */
ROM_LOAD("136031.108", 0x0800, 0x0400, CRC(6a76138f) SHA1(9ef7af898a3e29d03f35045901023615a6a55205)) /* PROM 2 */
ROM_LOAD("136031.107", 0x0c00, 0x0400, CRC(afbf6e01) SHA1(0a6438e6c106d98e5d67a019751e1584324f5e5c)) /* PROM 3 */

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END


// Star Wars (Revision 2)
AAE_DRIVER_BEGIN(drv_starwars, "starwars", "Star Wars (Revision 2)")
AAE_DRIVER_ROM(rom_starwars)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 @ 1.512 MHz, 6 INT passes/frame
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      6,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &starwars_interrupt,
		/*r8*/       starwars_readmem,
		/*w8*/       starwars_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: Audio 6809 @ 1.512 MHz, 24 INT passes/frame
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      24,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &starwars_snd_interrupt,
		/*r8*/       starwars_audio_readmem,
		/*w8*/       starwars_audio_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 240, 0, 280)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x000, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_END()

// Star Wars (Revision 1)
AAE_DRIVER_BEGIN(drv_starwars1, "starwars1", "Star Wars (Revision 1)")
AAE_DRIVER_ROM(rom_starwars1)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 6, INT_TYPE_INT, &starwars_interrupt,
		starwars_readmem, starwars_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	// CPU1: Audio 6809
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 24, INT_TYPE_INT, &starwars_snd_interrupt,
		starwars_audio_readmem, starwars_audio_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 240, 0, 280)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_END()

// Star Wars: Empire Strike Back
AAE_DRIVER_BEGIN(drv_esb, "esb", "Star Wars: Empire Strike Back")
AAE_DRIVER_ROM(rom_esb)
AAE_DRIVER_FUNCS(&init_esb, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_esb)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 (hardware-compatible with Star Wars main)
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 6, INT_TYPE_INT, &starwars_interrupt,
		empire_readmem, empire_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	// CPU1: Audio 6809
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 24, INT_TYPE_INT, &starwars_snd_interrupt,
		starwars_audio_readmem, starwars_audio_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 240, 0, 280)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x000, 0x3000)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_starwars)
AAE_REGISTER_DRIVER(drv_starwars1)
AAE_REGISTER_DRIVER(drv_esb)

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////