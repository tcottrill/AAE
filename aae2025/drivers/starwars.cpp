
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
#include "pokey.h"
#include "pokyintf.h"
#include "earom.h"
#include "rand.h"
#include "5220intf.h"
#include "loaders.h"
#include "sndhrdw/starwars_snd.h"
#include "swmathbx.h"



#include "glcode.h"

#define MASTER_CLOCK (12096000)
#define CLOCK_3KHZ  (MASTER_CLOCK / 4096)


static int bankaddress = 0; // base address of banked ROM

/*   If 1 then log functions called */
#define MACHDEBUG 0
#define SNDDEBUG 0

static unsigned char ADC_VAL = 0;

/* control select values for ADC_R */
#define kPitch	0
#define kYaw	1
#define kThrust	2

static unsigned char control_num = kPitch;


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

/* machine/slapstic.c */
void slapstic_init(int chip);
int slapstic_bank(void);
int slapstic_tweak(int offset);

extern unsigned char* atarigen_slapstic;
unsigned char* slapstic_area;

static int empire_setopbase(int pc)
{
	int prevpc = cpu_getppc();
	int bank;

	/*
	 *		This is a slightly ugly kludge for Indiana Jones & the Temple of Doom because it jumps
	 *		directly to code in the slapstic.  The general order of things is this:
	 *
	 *			jump to $3A, which turns off interrupts and jumps to $00 (the reset address)
	 *			look up the request in a table and jump there
	 *			(under some circumstances, tweak the special addresses)
	 *			return via an RTS at the real bankswitch address
	 *
	 *		To simulate this, we tweak the slapstic reset address on entry into slapstic code; then
	 *		we let the system tweak whatever other addresses it wishes.  On exit, we tweak the
	 *		address of the previous PC, which is the RTS instruction, thereby completing the
	 *		bankswitch sequence.
	 *
	 *		Fortunately for us, all 4 banks have exactly the same code at this point in their
	 *		ROM, so it doesn't matter which version we're actually executing.
	 */

	if ((pc & 0xe000) == 0x8000)
	{
		wrlog(" new pc inside of slapstic region: %04x (prev = %04x)\n", pc, prevpc);
		bank = slapstic_tweak((pc) & 0x1fff);
	}
	else if ((prevpc & 0xe000) == 0x8000)
	{
		wrlog(" old pc inside of slapstic region: %04x (new = %04x)\n", prevpc, pc);
		bank = slapstic_tweak((prevpc) & 0x1fff);
	}
	return pc;
}

extern int m6809_slapstic;
void empire_init_machine(void)
{
	/* Set up the slapstic */
	slapstic_init(101);
	m6809_slapstic = 1;
	//cpu_setOPbaseoverride(empire_setopbase);

	/* Reset all the banks */
	//starwars_out_w(4, 0);

//	init_swmathbox();
}

/*************************************
 *
 *		Slapstic ROM read/write.
 *
 *************************************/

READ_HANDLER(empire_slapstic_r)
{
	int val;

	int bank = (slapstic_tweak(address) * 0x2000);
	val = atarigen_slapstic[bank + (address & 0x1fff)];
	wrlog("slapstic_r, %04x: %02x", 0x8000 + address, val);
	return val;
}


WRITE_HANDLER(empire_slapstic_w)
{
	wrlog("esb slapstic tweak via write");
	slapstic_tweak(address);
}
/// ////////////////////////////////////////////////////////////////////////

WRITE_HANDLER(avgdvg_go_w)
{
	avg_go();
}

/********************************************************/

READ_HANDLER(starwars_control_r)
{
	if (control_num == kPitch)
		return readinputport(4);
	else if (control_num == kYaw)
		return readinputport(5);
	/* default to unused thrust */
	else return 0;
}

/* Read from ROM 0. Use bankaddress as base address */
READ_HANDLER(bank1_rom_r)
{	// Ugh, change this later. This was only for testing.
	//int bank[8] = { 0x6000, 0x0a000, 0x0c000, 0x0e000, 0x10000, 0x1c000, 0x1e000, 0x12000 };
	int bank[8] = { 0x6000, 0x10000};

	//RAM = Machine->memory_region[0];
	//return RAM[bank[bankaddress] + address];

	RAM = &Machine->memory_region[0][bank[bankaddress]];
	return RAM[address];

}

READ_HANDLER(bank2_rom_r)
{	// Ugh, change this later. This was only for testing.
	int bank[8] = {0x0a000, 0x1c000 };

	//RAM = Machine->memory_region[0];
	//return RAM[bank[bankaddress] + address];
	//RAM = &Machine->memory_region[0][0];
	//wrlog("Returning %x", RAM[(address + bank[bankaddress])]);
	//return RAM[(address + bank[bankaddress])];

	RAM = &Machine->memory_region[0][bank[bankaddress]];
	return RAM[address];
}

READ_HANDLER(bank3_rom_r)
{	// Ugh, change this later. This was only for testing.
	int bank[8] = { 0x0c000, 0x1e000 };

	//RAM = Machine->memory_region[0];
	//return RAM[bank[bankaddress] + address];
	//RAM = &Machine->memory_region[0][0];
	//wrlog("Returning %x", RAM[(address + bank[bankaddress])]);
	//return RAM[(address + bank[bankaddress])];

	RAM = &Machine->memory_region[0][bank[bankaddress]];
	return RAM[address];
}

READ_HANDLER(bank4_rom_r)
{	// Ugh, change this later. This was only for testing.
//int bank[8] = { 0x6000, 0x0a000, 0x0c000, 0x0e000, 0x10000, 0x1c000, 0x1e000, 0x12000 };
	int bank[8] = { 0x0e000, 0x12000 };
//	wrlog("Reading Bank 4 Ram, %x bankaddress %x", address, bankaddress);
	//RAM = Machine->memory_region[0];
	//return RAM[bank[bankaddress] + address];
//	RAM = &Machine->memory_region[0][0];
	//wrlog("Returning %x", RAM[(address + bank[bankaddress])]);
	//return RAM[(address + bank[bankaddress])];

	RAM = &Machine->memory_region[0][bank[bankaddress]];
	return RAM[address];
}

//********/
READ_HANDLER(input_bank_0_r)
{
	int x = input_port_0_r(0);
	return x;
}
/********************************************************/
READ_HANDLER(input_bank_1_r)
{
	int x;

	x = input_port_1_r(0); // Read memory mapped port 2
	x = x & 0x34; /* Clear out bit 3 (SPARE 2), and 0 and 1 (UNUSED) */
			  /* MATH_RUN (bit 7) set to 0 */

// Kludge to enable Starwars & Empire Mathbox Self-test
// The mathbox looks like it's running, from this address... :)
	if (cpu_getpc() == 0xf978 || cpu_getpc() == 0xf655)
		x |= 0x80;

	//wrlog("CPU Get PC here %d", cpu_getpc());
	/* set the AVG done flag */
	if (avg_check())
	//if (avgdvg_done())
	//if (get_elapsed_ticks(0) > 33000)
	{
		//x |= 0x40;
	}
	else
	{
	//	x &= ~0x40;
	}
	//wrlog("Returning %x Input 1", x);
	x |= 0x40;
	return x;
}
/*********************************************************/

/* Dip switch bank zero */
READ_HANDLER(opt_0_r)
{
	int x;
	x = input_port_2_r(0);
	return x;
}

/********************************************************/

/* Dip switch bank 1 */
READ_HANDLER(opt_1_r)
{
	int x;
	x = input_port_3_r(0);
	return x;
}

/********************************************************/
READ_HANDLER(adc_r)
{
	return ADC_VAL;
}

/**********************************************************/
/************** Write Handlers ****************************/
/**********************************************************/

WRITE_HANDLER(irqclr)
{}
WRITE_HANDLER(starwars_out_w)
{
	//unsigned char* RAM = memory_region(REGION_CPU1);

	switch (address)
	{
	case 0:		/* Coin counter 1 */
		//coin_counter_w(0, data);
		break;

	case 1:		/* Coin counter 2 */
		//coin_counter_w(1, data);
		break;

	case 2:		/* LED 3 */
		set_led_status(2, ~data & 0x80);
		break;

	case 3:		/* LED 2 */
		set_led_status(1, ~data & 0x80);
		break;

	case 4:		/* bank switch */
		wrlog( "bank_switch_w, %02x", data);
		if ((data & 0x80) == 0)
			bankaddress = 0; /* First half of ROM */
		else
			bankaddress = 1; /* Second half of ROM */
		break;

	case 5:		/* reset PRNG */
		prngclr();
		break;

	case 6:		/* LED 1 */
		set_led_status(0, ~data & 0x80);
		break;

	case 7:
		wrlog("sw recall"); /* what's that? */
		break;
	}

}


WRITE_HANDLER(nstore)
{}

/********************************************************/


WRITE_HANDLER(starwars_control_w)
{
	control_num = address;
}

void starwars_interrupt()
{

	wrlog("Starwars Interrupt Taken");
	if (cpu_getiloops() == 5) { avg_clear(); }
	//cpu_set_pending_interrupt(INT_TYPE_INT, 0);
	cpu_do_int_imm(0, INT_TYPE_INT);
	//cpu_do_interrupt(INT_TYPE_INT, 0);
	
}

void end_starwars()
{
	starwars_sh_stop();
	tms5220_sh_stop();
}


WRITE_HANDLER(avgdvg_reset_w)
{
	wrlog("---------------------------AVGDVG RESET ------------------------"); total_length = 0;
}

void run_starwars()
{
	wrlog("Made it to run");
	starwars_sh_update();
}

/////////////////////////////////////////////////

// Star Wars READ memory map 
MEM_READ(starwars_readmem)

//	   { 0x0000, 0x2fff, MRA_RAM, &vectorram },   // vector_ram 
//	   { 0x3000, 0x3fff, MRA_ROM },  // vector_rom 
//     { 0x4800, 0x4fff, MRA_RAM },   //cpu_ram   
//     { 0x5000, 0x5fff, MRA_RAM },    (math_ram_r) math_ram 
//     { 0x0000, 0x3fff, MRA_RAM, &vectorram}, // Vector RAM and ROM 
//		{ 0x4800, 0x5fff, MRA_RAM },  // CPU and Math RAM 
//		{ 0x6000, 0x7fff, banked_rom_r }, // banked ROM 

MEM_ADDR(0x4300, 0x431f, input_bank_0_r) // Memory mapped input port 0 
MEM_ADDR(0x4320, 0x433f, input_bank_1_r) // Memory mapped input port 1 
MEM_ADDR(0x4340, 0x435f, opt_0_r) // DIP switches bank 0 
MEM_ADDR(0x4360, 0x437f, opt_1_r) // DIP switches bank 1 
MEM_ADDR(0x4380, 0x439f, starwars_control_r)   // ADC read 
MEM_ADDR(0x4400, 0x4400, main_read_r) //Sound
MEM_ADDR(0x4401, 0x4401, main_ready_flag_r) //Sound
//		{ 0x4500, 0x45ff, MRA_RAM }, // nv_ram 
MEM_ADDR(0x4700, 0x4700, reh)
MEM_ADDR(0x4701, 0x4701, rel)
MEM_ADDR(0x4703, 0x4703, prng) // pseudo random number generator 
MEM_ADDR(0x6000, 0x7fff, bank1_rom_r)
//MEM_ADDR(0x8000, 0xffff, MRA_ROM)  // rest of main_rom 
MEM_END


// Star Wars WRITE memory map 
MEM_WRITE(starwars_writemem)
//MEM_ADDR( 0x0000, 0x2fff, MWA_RAM, &vectorram ) // vector_ram 
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)  // vector_rom 
//  { 0x4800, 0x4fff, MWA_RAM },   cpu_ram 
//  { 0x5000, 0x5fff, MWA_RAM },  (math_ram_w) math_ram 
//	{ 0x4800, 0x5fff, MWA_RAM }, // CPU and Math RAM 
//MEM_ADDR( 0x6000, 0xffff, MWA_ROM ) // main_rom 
MEM_ADDR(0x4400, 0x4400, main_wr_w)  //Sound
//	{ 0x4500, 0x45ff, MWA_RAM }, // nov_ram 
MEM_ADDR(0x4600, 0x461f, avgdvg_go_w)  // evggo(mine) or vg2_go 
MEM_ADDR(0x4620, 0x463f, avgdvg_reset_w) // evgres(mine) or vg_reset 
MEM_ADDR(0x4640, 0x465f, watchdog_reset_w) //  (wdclr) Watchdog clear 
MEM_ADDR(0x4660, 0x467f, irqclr)  // clear periodic interrupt 
//  { 0x4680, 0x4680, MWA_NOP },  (coin_ctr2) Coin counter 1 
//  { 0x4681, 0x4681, MWA_NOP },  (coin_ctr1) Coin counter 2 
//MEM_ADDR(0x4680, 0x4681, MWA_NOP)  //  Coin counters 
//MEM_ADDR(0x4682, 0x4682, led3) // led3 
//MEM_ADDR(0x4683, 0x4683, led2) // led2 
//MEM_ADDR(0x4684, 0x4684, mpage)  // Page select for ROM0 
//MEM_ADDR(0x4685, 0x4685, prngclr) // Reset PRNG 
//MEM_ADDR(0x4686, 0x4686, led1)    // led1 
//MEM_ADDR(0x4687, 0x4687, recall)
MEM_ADDR(0x4680, 0x4687, starwars_out_w)
MEM_ADDR(0x46a0, 0x46bf, nstore)
MEM_ADDR(0x46c0, 0x46c2, starwars_control_w)	// Selects which a-d control port (0-3) will be read 
MEM_ADDR(0x46e0, 0x46e0, soundrst) //Sound
MEM_ADDR(0x4700, 0x4707, swmathbx)
MEM_END


MEM_READ(empire_readmem)
	//{ 0x6000, 0x1dfff, MRA_ROM },		/* vector_rom */
	//{ 0x0000, 0x2fff, MRA_RAM, &vectorram, &vectorram_size },   /* vector_ram */
	//{ 0x3000, 0x3fff, MRA_ROM },		/* vector_rom */
	{ 0x4300, 0x431f, input_bank_0_r }, /* Memory mapped input port 0 */
	{ 0x4320, 0x433f, input_bank_1_r }, /* Memory mapped input port 1 */
	{ 0x4340, 0x435f, opt_0_r },	/* DIP switches bank 0 */
	{ 0x4360, 0x437f, opt_1_r },	/* DIP switches bank 1 */
	{ 0x4380, 0x439f, starwars_control_r }, /* a-d control result */
	{ 0x4400, 0x4400, main_read_r },
	{ 0x4401, 0x4401, main_ready_flag_r },
	{ 0x4500, 0x45ff, MRA_RAM },		/* nov_ram */
	{ 0x4700, 0x4700, reh },
	{ 0x4701, 0x4701, rel },
	{ 0x4703, 0x4703, prng },			/* pseudo random number generator */
	{ 0x4800, 0x5fff, MRA_RAM },		/* CPU and Math RAM */
/*	{ 0x4800, 0x4fff, MRA_RAM }, */		/* cpu_ram */
/*	{ 0x5000, 0x5fff, MRA_RAM }, */		/* (math_ram_r) math_ram */
	{ 0x6000, 0x7fff, bank1_rom_r },	    /* banked ROM */
	{ 0x8000, 0x9fff, empire_slapstic_r },//, &slapstic_area },
	{ 0xa000, 0xbfff, bank2_rom_r },		/* banked ROM */
	{ 0xc000, 0xdfff, bank3_rom_r },		/* rest of main_rom */
	{ 0xe000, 0xffff, bank4_rom_r },		/* banked ROM */
	/* Dummy entry to set up the slapstic */
	//{ 0x14000, 0x1bfff, MRA_NOP, &atarigen_slapstic },
	MEM_END

	MEM_WRITE(empire_writemem)
	//{ 0x0000, 0x2fff, MWA_RAM, &vectorram }, /* vector_ram */
	{ 0x3000, 0x3fff, MWA_ROM },		/* vector_rom */
/*	{ 0x4800, 0x4fff, MWA_RAM }, */		/* cpu_ram */
/*	{ 0x5000, 0x5fff, MWA_RAM }, */		/* (math_ram_w) math_ram */
	{ 0x4800, 0x5fff, MWA_RAM },		/* CPU and Math RAM */
	{ 0x8000, 0x9fff, empire_slapstic_w },		/* slapstic write */
//	{ 0x6000, 0xffff, MWA_ROM },		/* main_rom */
	{ 0x4400, 0x4400, main_wr_w },
	{ 0x4500, 0x45ff, MWA_RAM },		/* nov_ram */
	{ 0x4600, 0x461f, avgdvg_go_w },
	{ 0x4620, 0x463f, avgdvg_reset_w },
	{ 0x4640, 0x465f, MWA_NOP },		/* (wdclr) Watchdog clear */
	{ 0x4660, 0x467f, MWA_NOP },        /* irqclr: clear periodic interrupt */
	{ 0x4680, 0x4687, starwars_out_w },
	{ 0x46a0, 0x46bf, MWA_NOP },		/* nstore */
	{ 0x46c0, 0x46c2, starwars_control_w },	/* Selects which a-d control port (0-3) will be read */
	{ 0x46e0, 0x46e0, soundrst },
	{ 0x4700, 0x4707, swmathbx },
MEM_END



// Star Wars Sound READ memory map 
MEM_READ(starwars_audio_readmem)
MEM_ADDR( 0x0800, 0x0fff, sin_r ) // SIN Read 
//{ 0x1000, 0x107f, MRA_RAM },  // 6532 RAM 
MEM_ADDR(0x1080, 0x109f, m6532_r )
//{ 0x4000, 0x7fff, MRA_ROM }, // sound roms 
//{ 0xc000, 0xffff, MRA_ROM }, // load last rom twice 
MEM_END

// Star Wars sound WRITE memory map 
MEM_WRITE(starwars_audio_writemem)
MEM_ADDR( 0x0000, 0x07ff, sout_w )
//{ 0x1000, 0x107f, MWA_RAM }, // 6532 ram 
MEM_ADDR( 0x1080, 0x109f, m6532_w )
MEM_ADDR( 0x1800, 0x183f, quadpokey_w )//starwars_pokey_sound_w },
//{ 0x2000, 0x27ff, MWA_RAM }, // program RAM 
//{ 0x4000, 0x7fff, MWA_ROM }, // sound rom 
//{ 0xc000, 0xffff, MWA_ROM }, // sound rom again, for intvecs 
MEM_END

int init_esb()
{
	atarigen_slapstic = &Machine->memory_region[0][0x14000];
	slapstic_area = &Machine->memory_region[0][0x8000];
	bankaddress = 0;
	
	slapstic_init(101);
	m6809_slapstic = 1;
	cpu_setOPbaseoverride(empire_setopbase);
	starwars_sh_start();

	init6809(empire_readmem, empire_writemem, 0);
	init6809(starwars_audio_readmem, starwars_audio_writemem, 1);
	translate_proms();
	avg_start_starwars();

	

	
	return 0;
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_starwars(void)
{
	bankaddress = 0;
	starwars_sh_start();
	init6809(starwars_readmem, starwars_writemem, 0);
	init6809(starwars_audio_readmem, starwars_audio_writemem, 1);
	translate_proms();
	avg_start_starwars();
	return 0;
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////