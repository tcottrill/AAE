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

// Note: This is pretty much a self contained emulator for Major Havoc. This will eventually get merged into using the proper cpu and video code.

// license:BSD - 3 - Clause
// copyright-holders:Mike Appolo
/***************************************************************************

	Atari Major Havoc hardware

	driver by Mike Appolo

	Modified 10/08/2006 by Jess M. Askey to include support for Speech which was not stuffed on production
	Major Havoc PCB's. However, the hardware if stuffed is functional. Speech is used in Major Havoc Return
	to Vaxx.

	Games supported:
		* Alpha One
		* Major Havoc
		* Major Havoc: Return to Vax (including speech) - This version is a hack that includes 3 new levels
														  near the end of the game. Level 19 is incomplete.

	Known bugs:
		* none at this time

****************************************************************************
/

/*
	 Memory Map for Major Havoc

	Alpha Processor
					 D  D  D  D  D  D  D  D
	Hex Address      7  6  5  4  3  2  1  0                    Function
	--------------------------------------------------------------------------------
	0000-01FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (1/2K)
	0200-07FF     |  D  D  D  D  D  D  D  D   | R/W  | Paged Program RAM (3K)
	0800-09FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (1/2K)
				  |                           |      |
	1000          |  D  D  D  D  D  D  D  D   |  R   | Gamma Commuication Read Port
				  |                           |      |
	1200          |  D                        |  R   | Right Coin (Player 1=0)
	1200          |     D                     |  R   | Left Coin  (Player 1=0)
	1200          |        D                  |  R   | Aux. Coin  (Player 1=0)
	1200          |           D               |  R   | Diagnostic Step
	1200          |  D                        |  R   | Self Test Switch (Player 1=1)
	1200          |     D                     |  R   | Cabinet Switch (Player 1=1)
	1200          |        D                  |  R   | Aux. Coin Switch (Player 1=1)
	1200          |           D               |  R   | Diagnostic Step
	1200          |              D            |  R   | Gammma Rcvd Flag
	1200          |                 D         |  R   | Gamma Xmtd Flag
	1200          |                    D      |  R   | 2.4 KHz
	1200          |                       D   |  R   | Vector Generator Halt Flag
				  |                           |      |
	1400-141F     |              D  D  D  D   |  W   | ColorRAM
				  |                           |      |
	1600          |  D                        |  W   | Invert X
	1600          |     D                     |  W   | Invert Y
	1600          |        D                  |  W   | Player 1
	1600          |           D               |  W   | Not Used
	1600          |              D            |  W   | Gamma Proc. Reset
	1600          |                 D         |  W   | Beta Proc. Reset
	1600          |                    D      |  W   | Not Used
	1600          |                       D   |  W   | Roller Controller Light
				  |                           |      |
	1640          |                           |  W   | Vector Generator Go
	1680          |                           |  W   | Watchdog Clear
	16C0          |                           |  W   | Vector Generator Reset
				  |                           |      |
	1700          |                           |  W   | IRQ Acknowledge
	1740          |                    D  D   |  W   | Program ROM Page Select
	1780          |                       D   |  W   | Program RAM Page Select
	17C0          |  D  D  D  D  D  D  D  D   |  W   | Gamma Comm. Write Port
				  |                           |      |
	1800-1FFF     |  D  D  D  D  D  D  D  D   | R/W  | Shared Beta RAM(not used)
				  |                           |      |
	2000-3FFF     |  D  D  D  D  D  D  D  D   |  R   | Paged Program ROM (32K)
	4000-4FFF     |  D  D  D  D  D  D  D  D   | R/W  | Vector Generator RAM (4K)
	5000-5FFF     |  D  D  D  D  D  D  D  D   |  R   | Vector Generator ROM (4K)
	6000-7FFF     |  D  D  D  D  D  D  D  D   |  R   | Paged Vector ROM (32K)
	8000-FFFF     |  D  D  D  D  D  D  D  D   |  R   | Program ROM (32K)
	-------------------------------------------------------------------------------

	Gamma Processor

					 D  D  D  D  D  D  D  D
	Hex Address      7  6  5  4  3  2  1  0                    Function
	--------------------------------------------------------------------------------
	0000-07FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (2K)
	2000-203F     |  D  D  D  D  D  D  D  D   | R/W  | Quad-Pokey I/O
				  |                           |      |
	2800          |  D                        |  R   | Fire 1 Switch
	2800          |     D                     |  R   | Shield 1 Switch
	2800          |        D                  |  R   | Fire 2 Switch
	2800          |           D               |  R   | Shield 2 Switch
	2800          |              D            |  R   | Not Used
	2800          |                 D         |  R   | Speech Chip Ready
	2800          |                    D      |  R   | Alpha Rcvd Flag
	2800          |                       D   |  R   | Alpha Xmtd Flag
				  |                           |      |
	3000          |  D  D  D  D  D  D  D  D   |  R   | Alpha Comm. Read Port
				  |                           |      |
	3800-3803     |  D  D  D  D  D  D  D  D   |  R   | Roller Controller Input
				  |                           |      |
	4000          |                           |  W   | IRQ Acknowledge
	4800          |                    D      |  W   | Left Coin Counter
	4800          |                       D   |  W   | Right Coin Counter
				  |                           |      |
	5000          |  D  D  D  D  D  D  D  D   |  W   | Alpha Comm. Write Port
				  |                           |      |
	5800          |  D  D  D  D  D  D  D  D   |  W   | Speech Data Write / Write Strobe Clear
	5900          |                           |  W   | Speech Write Strobe Set
					|                           |      |
	6000-61FF     |  D  D  D  D  D  D  D  D   | R/W  | EEROM
	8000-BFFF     |  D  D  D  D  D  D  D  D   |  R   | Program ROM (16K)
	-----------------------------------------------------------------------------

	MAJOR HAVOC DIP SWITCH SETTINGS

	$=Default

	DIP Switch at position 13/14S

									  1    2    3    4    5    6    7    8
	STARTING LIVES                  _________________________________________
	Free Play   1 Coin   2 Coin     |    |    |    |    |    |    |    |    |
		2         3         5      $|Off |Off |    |    |    |    |    |    |
		3         4         4       | On | On |    |    |    |    |    |    |
		4         5         6       | On |Off |    |    |    |    |    |    |
		5         6         7       |Off | On |    |    |    |    |    |    |
	GAME DIFFICULTY                 |    |    |    |    |    |    |    |    |
	Hard                            |    |    | On | On |    |    |    |    |
	Medium                         $|    |    |Off |Off |    |    |    |    |
	Easy                            |    |    |Off | On |    |    |    |    |
	Demo                            |    |    | On |Off |    |    |    |    |
	BONUS LIFE                      |    |    |    |    |    |    |    |    |
	50,000                          |    |    |    |    | On | On |    |    |
	100,000                        $|    |    |    |    |Off |Off |    |    |
	200,000                         |    |    |    |    |Off | On |    |    |
	No Bonus Life                   |    |    |    |    | On |Off |    |    |
	ATTRACT MODE SOUND              |    |    |    |    |    |    |    |    |
	Silence                         |    |    |    |    |    |    | On |    |
	Sound                          $|    |    |    |    |    |    |Off |    |
	ADAPTIVE DIFFICULTY             |    |    |    |    |    |    |    |    |
	No                              |    |    |    |    |    |    |    | On |
	Yes                            $|    |    |    |    |    |    |    |Off |
									-----------------------------------------

		DIP Switch at position 8S

									  1    2    3    4    5    6    7    8
									_________________________________________
	Free Play                       |    |    |    |    |    |    | On |Off |
	1 Coin for 1 Game               |    |    |    |    |    |    |Off |Off |
	1 Coin for 2 Games              |    |    |    |    |    |    | On | On |
	2 Coins for 1 Game             $|    |    |    |    |    |    |Off | On |
	RIGHT COIN MECHANISM            |    |    |    |    |    |    |    |    |
	x1                             $|    |    |    |    |Off |Off |    |    |
	x4                              |    |    |    |    |Off | On |    |    |
	x5                              |    |    |    |    | On |Off |    |    |
	x6                              |    |    |    |    | On | On |    |    |
	LEFT COIN MECHANISM             |    |    |    |    |    |    |    |    |
	x1                             $|    |    |    |Off |    |    |    |    |
	x2                              |    |    |    | On |    |    |    |    |
	BONUS COIN ADDER                |    |    |    |    |    |    |    |    |
	No Bonus Coins                 $|Off |Off |Off |    |    |    |    |    |
	Every 4, add 1                  |Off | On |Off |    |    |    |    |    |
	Every 4, add 2                  |Off | On | On |    |    |    |    |    |
	Every 5, add 1                  | On |Off |Off |    |    |    |    |    |
	Every 3, add 1                  | On |Off | On |    |    |    |    |    |
									-----------------------------------------

		2 COIN MINIMUM OPTION: Short pin 6 @13N to ground.

***************************************************************************/

#include "mhavoc.h"
#include "aae_mame_driver.h"
#include "samples.h"
#include "vector.h"
#include "aae_avg.h"
#include "pokyintf.h"
#include "earom.h"
#include "rand.h"
#include "5220intf.h"
#include "loaders.h"

#define MHAVOC_CLOCK		10000000
#define MHAVOC_CLOCK_2_5M	(MHAVOC_CLOCK/4)
#define MHAVOC_CLOCK_1_25M	(MHAVOC_CLOCK/8)

static struct POKEYinterface pokey_interface =
{
	4,			/* 4 chips */
	1,			/* 1 update per video frame (low quality) */
	1250000,
	128,	/* volume */
	NO_CLIP,
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

static struct TMS5220interface tms5220_interface =
{
	555555,     /* clock speed */
	255,        /* volume */
	0           /* IRQ handler */
};

static unsigned char mhavoc_nvram[0x200];

void mhavoc_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		wrlog("Writing NVRAM File");
		osd_fwrite(file, mhavoc_nvram, 0x200);
	}
	else
	{
		if (file)
		{
			wrlog("Reading NVRAM File");
			osd_fread(file, mhavoc_nvram, 0x200);
		}
		else
		{
			wrlog("Creating NVRAM File");
			memset(mhavoc_nvram, 0, 0x200);
		}
	}
}

// Video Variables
static int width, height;
static int xcenter, ycenter;
static int xmin, xmax;
static int ymin, ymax;
static int flip_x, flip_y, swap_xy;

static UINT8 alpha_data = 0;
static UINT8 alpha_rcvd = 0;
static UINT8 alpha_xmtd = 0;

static UINT8 gamma_data = 0;
static UINT8 gamma_rcvd = 0;
static UINT8 gamma_xmtd = 0;
static UINT8 speech_write_buffer;
static UINT8 player_1;
int has_gamma_cpu = 1;

int nmicpu1 = 0;
int delayed_data = 0;

static UINT8 alpha_irq_clock;
static UINT8 alpha_irq_clock_enable;
static UINT8 gamma_irq_clock;

static int ram_bank[2] = { 0x20200, 0x20800 };
static int ram_bank_sel = 0;
static int rom_bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
static int rom_bank_sel = 0;
unsigned char* cur_bank;

static int reset1 = 0;
static int MHAVGDONE = 1;
static int vidticks;
static int totalticks;
static int bigticks;
static int twokticks = 0;
static int bitflip = 0;

// Pokey Variables.
int pkenable[4];
static int sample_pos = 0;
int BUFFER_SIZE;  //FOR POKEY
AUDIOSTREAM* stream1;
unsigned char* soundbuffer;
int gammaticks = 0;
static int last_sample = 0;

WRITE_HANDLER(speech_strobe_w)
{
	if (gamenum == MHAVOCRV)
	{
		tms5220_data_w(0, speech_write_buffer);
	}
	else
	{
		if (data < 0x80) return;

		if (last_sample != data)
		{
			last_sample = data;

			switch (data)
			{
			case 0x81: sample_start(3, 0, 0); break;  //Speech Chip Test
			case 0x82: sample_start(3, 1, 0); break;  //Char - Take 1 - One.wav
			case 0x83: sample_start(3, 2, 0); break;  //Char - Take 1 - Two.wav
			case 0x84: sample_start(3, 3, 0); break;
			case 0x85: sample_start(3, 4, 0); break;
			case 0x86: sample_start(3, 5, 0); break;
			case 0x87: sample_start(3, 6, 0); break;
			case 0x88: sample_start(3, 7, 0); break;
			case 0x89: sample_start(3, 8, 0); break;
			case 0x8a: sample_start(3, 9, 0); break;
			case 0x8b: sample_start(3, 10, 0); break;
			case 0x8c: sample_start(3, 11, 0); break;
			case 0x8d: sample_start(3, 12, 0); break;
			case 0x8e: sample_start(3, 13, 0); break;
			case 0x8f: sample_start(3, 14, 0); break;
			case 0x90: sample_start(3, 15, 0); break;
			case 0x91: sample_start(3, 16, 0); break;
			case 0x92: sample_start(3, 17, 0); break;
			case 0x93: sample_start(3, 18, 0); break;
			case 0x94: sample_start(3, 19, 0); break;
			case 0x95: sample_start(3, 20, 0); break;
			case 0x96: sample_start(3, 21, 0); break;
			case 0x97: sample_start(3, 22, 0); break;
			case 0x98: sample_start(3, 23, 0); break;
			case 0x99: sample_start(3, 24, 0); break;
			case 0x9a: sample_start(3, 25, 0); break;
			case 0x9b: sample_start(3, 26, 0); break;
			case 0x9c: sample_start(3, 27, 0); break;
			case 0x9d: sample_start(3, 28, 0); break;
			case 0x9e: sample_start(3, 29, 0); break;
			case 0x9f: sample_start(3, 30, 0); break;
			case 0xa0: sample_start(3, 31, 0); break;
			case 0xa1: sample_start(3, 32, 0); break;
			case 0xa2: sample_start(3, 33, 0); break;
			case 0xa3: sample_start(3, 34, 0); break;
			case 0xa4: sample_start(3, 35, 0); break;
			case 0xa5: sample_start(3, 36, 0); break;
			case 0xa6: sample_start(3, 37, 0); break;
			case 0xa7: sample_start(3, 38, 0); break;
			case 0xa8: sample_start(3, 39, 0); break;
			case 0xa9: sample_start(3, 40, 0); break;
			case 0xaa: sample_start(3, 41, 0); break;

			default: wrlog("!!!! Unhandled sound effect!!!! %x ", data);
			}
		}
	}
}

int get_vidticks(int reset)
{
	int v = 0;
	int temp;

	if (reset == 0xff) //Reset Tickcount;
	{
		vidticks = 0;
		vidticks -= m_cpu_6502[CPU0]->get6502ticks(0);  //Make vid_tickcount a negative number to check for reset later;
		return 0;
	}

	v = vidticks;
	temp = m_cpu_6502[CPU0]->get6502ticks(0);
	//wrlog("Vid ticks here is ------------------------- %d", temp);
	//if (temp <= cyclecount[running_cpu]) temp = cyclecount[running_cpu]-m6502zpGetElapsedTicks(0);
	//else wrlog("Video CYCLE Count ERROR occured, check code.");
	v += temp;
	return v;
}

void playstreamedsample_mhavoc(int channel, unsigned char* data, int len, int vol)
{
	unsigned char* p;

	p = (unsigned char*)get_audio_stream_buffer(stream1);

	if (p)
	{
		for (int i = 0; i < len; i++)
		{
			p[i] = data[i];
			p[i] ^= 0x80;
		}

		free_audio_stream_buffer(stream1);
	}
}

int cpu_scale_by_cycles_mh(int val)
{
	int k;
	int current = 0;
	int max;
	// Have to use Alpha CPU here.
	current = m_cpu_6502[CPU1]->get6502ticks(0);
	current += gammaticks;
	max = 1250000 / 50;

	k = val * (float)((float)current / (float)max); //BUFFER_SIZE  *

	return k;
}

static void mh_pokey_update()
{
	int newpos = 0;

	newpos = cpu_scale_by_cycles_mh(BUFFER_SIZE);

	if (newpos - sample_pos < 10) 	return;
	Pokey_process(soundbuffer + sample_pos, newpos - sample_pos);
	sample_pos = newpos;
}

static void pokey_do_update(void)
{
	if (sample_pos < BUFFER_SIZE) { Pokey_process(soundbuffer + sample_pos, BUFFER_SIZE - sample_pos); }
	playstreamedsample_mhavoc(0, soundbuffer, BUFFER_SIZE, 200);
	sample_pos = 0;
}

void end_mhavoc()
{
	wrlog("End Major Havoc Called");
	free_audio_stream_buffer(stream1);
	stop_audio_stream(stream1);
	free(soundbuffer);
	if (gamenum == MHAVOCRV || gamenum == MHAVOCPE) { tms5220_sh_stop(); }
	if (has_gamma_cpu == 0) { save_hi_aae(0x1800, 0x100, 0); }
}

void run_reset()
{
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
	gamma_irq_clock = 0;
	MHAVGDONE = 0;
	alpha_data = 0;
	alpha_rcvd = 0;
	alpha_xmtd = 0;
	gamma_data = 0;
	gamma_rcvd = 0;
	gamma_xmtd = 0;
	player_1 = 0;
	cache_clear();
	m_cpu_6502[CPU0]->reset6502();

	if (has_gamma_cpu)
	{
		m_cpu_6502[CPU1]->reset6502();
		Pokey_sound_init(1250000, 44100, 4, 6);
	}
	else { Pokey_sound_init(1250000, 44100, 2, 6); }
}

/*************************************
 *
 *  Interrupt handling
 *
 *************************************/
 //We are running this at 250mhz/4, so each of the clock number have to be multiplied by 4. (400 passes, 125 cycles)
void mhavoc_interrupt()
{
	/* clock the LS161 driving the alpha CPU IRQ */
	if (alpha_irq_clock_enable)
	{
		alpha_irq_clock++;
		if ((alpha_irq_clock & (0x30)) == (0x30)) //0x0c //0x30
		{
			//wrlog("IRQ ALPHA CPU");
			m_cpu_6502[CPU0]->irq6502();
			alpha_irq_clock_enable = 0;
		}
	}

	/* clock the LS161 driving the gamma CPU IRQ */
	if (has_gamma_cpu)
	{
		gamma_irq_clock++;
		if ((gamma_irq_clock & (0x20)) == (0x20))//08 //0x20
		{
			//wrlog("IRQ GAMMA CPU");
			m_cpu_6502[CPU1]->irq6502();
		}
	}
}

WRITE_HANDLER(mhavoc_alpha_irq_ack_w)
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(0, 0, CLEAR_LINE);
	//m_cpu_6502[CPU0]->m6502clearpendingint();
	//wrlog("Alpha IRQ ACK!", data);
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
}

WRITE_HANDLER(mhavoc_gamma_irq_ack_w)
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(1, 0, CLEAR_LINE);
	//wrlog("Gamma IRQ ACK!", data);
	//m_cpu_6502[CPU1]->m6502clearpendingint();
	gamma_irq_clock = 0;
}

READ_HANDLER(mh_quad_pokey_read)
{
	static int rng = 0;
	int pokey_reg[4];

	int offset = address;
	int pokey_num = (offset >> 3) & ~0x04;
	int control = (offset & 0x20) >> 2;
	pokey_reg[pokey_num] = (offset % 8) | control;
	//wrlog("Pokey # Read # %x  Reg %x", pokey_num, pokey_reg[pokey_num]);
	if (pokey_reg[pokey_num] == 0x08) return input_port_3_r(0); //Dipswitch Read

	if (pokey_reg[pokey_num] == 0x0a)
	{
		if (pkenable[pokey_num] != 0x00)
		{
			rng = randintm(0xff);
			return rng;
		}
		else return rng ^ 0xff;
	}

	return 0;
}

WRITE_HANDLER(mh_quad_pokey_write)
{
	int pokey_reg[4];
	int offset = address & 0xff;
	int pokey_num = (offset >> 3) & ~0x04;
	int control = (offset & 0x20) >> 2;
	pokey_reg[pokey_num] = (offset % 8) | control;
	//wrlog("Pokey # Write # %x  Reg %x, data %x", pokey_num, pokey_reg[pokey_num], data);
	if (pokey_reg[pokey_num] == 0x0f) { pkenable[pokey_num] = data; }
	switch (pokey_num) {
	case 0:
		mh_pokey_update();
		Update_pokey_sound(pokey_reg[0], data, 0, 6);
		break;
	case 1:
		mh_pokey_update();
		Update_pokey_sound(pokey_reg[1], data, 1, 6);
		break;
	case 2:
		mh_pokey_update();
		Update_pokey_sound(pokey_reg[2], data, 2, 6);
		break;
	case 3:
		mh_pokey_update();
		Update_pokey_sound(pokey_reg[3], data, 3, 6);
		break;
	}
}

READ_HANDLER(dual_pokey_r)
{
	int offset = address;
	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	int pokey_reg = (offset % 8) | control;

	if (pokey_num == 0) {
		if (pokey_reg == 0x0a) return rand();
		return 0;
	}
	else { return 0; }
}

WRITE_HANDLER(dual_pokey_w)
{
	int offset = address;
	int pokey_reg[4];
	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	pokey_reg[pokey_num] = (offset % 8) | control;

	if (pokey_reg[pokey_num] == 0x0f) { pkenable[pokey_num] = data; }
	switch (pokey_num) {
	case 0:	mh_pokey_update(); Update_pokey_sound(pokey_reg[0], data, 0, 6); break;
	case 1:	mh_pokey_update(); Update_pokey_sound(pokey_reg[1], data, 1, 6); break;
	}
}

int mhavoc_sh_start(void)
{
	int rv;
	rv = pokey_sh_start(&pokey_interface);
	return rv;
	//return(tms5220_sh_start (&tms5220_interface));
}

void mhavoc_sh_stop(void)
{
	pokey_sh_stop();
	// tms5220_sh_stop ();
}

void mhavoc_sh_update()
{
	pokey_sh_update();
	// tms5220_sh_update();
}

WRITE_HANDLER(avgdvg_reset_w)
{
	wrlog("---------------------------AVGDVG RESET ------------------------"); total_length = 0;
}

WRITE_HANDLER(avg_mgo)
{
	if (MHAVGDONE == 0) { return; }

	MH_generate_vector_list();
	//wrlog("Total Frame list length %d" ,total_length );
	if (total_length > 1) {
		MHAVGDONE = 0; get_vidticks(0xff);
		//wrlog("Total LENGTH HERE %d ", total_length);
	}
	else { MHAVGDONE = 1; }
	//wrlog("AVG RUN CALLED.");
}

WRITE_HANDLER(mhavoc_out_0_w)
{
	if (gamenum == MHAVOCPE)
	{
		flip_x = (data & 0x40);
		flip_y = (data & 0x80);
	}

	if (!(data & 0x08))
	{
		//wrlog ("\t\t\t\t*** resetting gamma processor. ***\n");
		alpha_rcvd = 0;
		alpha_xmtd = 0;
		gamma_rcvd = 0;
		gamma_xmtd = 0;
		reset1 = 1;   //RESET GAMMA CPU
	}
	player_1 = (data >> 5) & 1;
	// Emulate the roller light (Blinks on fatal errors) //
	set_aae_leds(data & 0x01, -1, -1);
}

WRITE_HANDLER(mhavoc_out_1_w)
{
	// wrlog("LED OUT WRITE");
	set_aae_leds(-1, data & 0x01, -1);
	set_aae_leds(-1, -1, (data & 0x02) >> 1);
}

/* Simulates frequency and vector halt */
READ_HANDLER(mhavoc_port_0_r)
{
	int res;
	int ticks;
	float me;

	me = 3.75 * total_length;//(((1780 * total_length)/ 1000000) * 1512);//* 1512);
	ticks = m_cpu_6502[CPU0]->get6502ticks(0);
	//if (MHAVGDONE == 0)wrlog("Total LENGTH HERE %f and TOTAL TICKS %d", me, ticks);
	if (get_vidticks(0) > me && MHAVGDONE == 0) { MHAVGDONE = 1; total_length = 0; }

	/* Bits 7-6 = selected based on Player 1 */
		/* Bits 5-4 = common */
	if (player_1)
		res = (readinputport(0) & 0x30) | (readinputport(5) & 0xc0);
	else
		res = readinputport(0) & 0xf0;

	// Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024)  (EVERY 120 CYCLES)
	 // if (bigticks & 0x400) //0x400
	if (bitflip) { res &= ~0x02; }
	else { res |= 0x02; }

	if (MHAVGDONE)
		res |= 0x01;
	else
		res &= ~0x01;

	if (gamma_rcvd == 1)
		res |= 0x08;
	else
		res &= ~0x08;

	if (gamma_xmtd == 1)
		res |= 0x04;
	else
		res &= ~0x04;

	return (res & 0xff);
}

READ_HANDLER(mhavoc_port_1_r)
{
	int res;
	// Bits 7 - 2 = input switches
	res = readinputport(1) & 0xfc;

	// Bit 2 = TMS5220 ready flag
	if (gamenum == MHAVOCRV || gamenum == MHAVOCPE)
	{
		if (!tms5220_ready_r())	res |= 0x04;
		else
			res &= ~0x04;
		//wrlog("TMS Ready Data %d", tms5220_ready_r());
	}
	if (alpha_rcvd == 1)	res |= 0x02;	else res &= ~0x02;
	if (alpha_xmtd == 1)	res |= 0x01; else	res &= ~0x01;

	return (res & 0xff);
}

READ_HANDLER(alphaone_port_0_r)
{
	int res;
	//    int ticks;
	float me;
	static int service = 0;
	res = readinputport(0) & 0xfc;

	me = (((1780 * total_length) / 1000000) * 1512);
	//ticks=m6502zpGetElapsedTicks(0);
	if (get_vidticks(0) > me && MHAVGDONE == 0) { MHAVGDONE = 1; total_length = 0; }

	//if (player_1) res = 0xff;
	//else res = 0xf0;

	/* Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024) */
	if (bitflip) { res &= ~0x02; }
	else { res |= 0x02; }

	if (MHAVGDONE)
		res |= 0x01;
	else
		res &= ~0x01;

	return (res & 0xff);
}

READ_HANDLER(alphaone_port_1_r)
{
	static int service = 0;
	int res;

	res = readinputportbytag("IN1");

	if (service)     bitclr(res, 0x10);

	return res;
}

WRITE_HANDLER(mhavoc_ram_banksel_w)
{
	data &= 0x01;
	ram_bank_sel = data;
}

WRITE_HANDLER(mhavoc_rom_banksel_w)
{
	int bank[8] = { 0x10000, 0x12000, 0x14000, 0x16000, 0x18000, 0x1A000, 0x1C000, 0x1E000 };
	if (gamenum == MHAVOCPE)
	{
		data = ((data & 1) | ((data & 2) << 1) | ((data & 4) >> 1));
	}
	else { data = data & 0x03; }
	cur_bank = &GI[CPU0][bank[data]];
}

READ_HANDLER(MRA_BANK2_R)
{
	return cur_bank[address];
}
////////////////////////////////////////////////////////////////////////////////////////////////

WRITE_HANDLER(MWA_BANK1_W)
{
	int bank;

	if (ram_bank_sel == 0) { bank = 0x20200; }
	else { bank = 0x20800; }
	bank = bank + address;

	GI[CPU0][bank] = data;
}

READ_HANDLER(MRA_BANK1_R)
{
	int bank;

	if (ram_bank_sel == 0) { bank = 0x20200; }
	else { bank = 0x20800; }
	bank = bank + (address);

	return GI[CPU0][bank];
}

WRITE_HANDLER(mhavoc_colorram_w)
{
	int i = (data & 4) ? 0x0f : 0x08;
	int r = (data & 8) ? 0x00 : i;
	int g = (data & 2) ? 0x00 : i;
	int b = (data & 1) ? 0x00 : i;

	vec_colors[address].r = r;
	vec_colors[address].g = g;
	vec_colors[address].b = b;
}

void avg_set_flip_x(int flip)
{
	if (flip)
		flip_x = 1;
}

void avg_set_flip_y(int flip)
{
	if (flip)
		flip_y = 1;
}

void avg_apply_flipping_and_swapping(int* x, int* y)
{
	if (flip_x)
		*x += (xcenter - *x) << 1;
	if (flip_y)
		*y += (ycenter - *y) << 1;

	if (swap_xy)
	{
		int temp = *x;
		*x = *y - ycenter + xcenter;
		*y = temp - xcenter + ycenter;
	}
}

void avg_add_point(int x, int y, int ex, int ey, int r, int g, int b, int p)
{
	avg_apply_flipping_and_swapping(&x, &y);
	if (p)
	{
		avg_apply_flipping_and_swapping(&ex, &ey);
		add_color_line(x, y, ex, ey, r, g, b);
	}
	else add_color_point(x, y, r, g, b);
}

/////////////////////////////VECTOR GENERATOR//////////////////////////////////
static void MH_generate_vector_list(void)
{
	int pc = 0x4000;
	int sp;
	int stack[8];
	int flipword = 0;
	int scale = 0;
	int statz = 0;
	int sparkle = 0;
	int xflip = 0;
	int color = 0;
	int myx = 0;
	int myy = 0;
	int currentx, currenty = 0;
	int done = 0;
	int firstwd, secondwd;
	int opcode;

	int x, y, z = 0, b, l, d, a;
	int deltax, deltay = 0;

	int vectorbank = 0;
	static int lastbank = 0;
	int red, green, blue;
	int ywindow = 1;
	int clip = 0;
	float sy = 0;
	float ey = 0;
	float sx = 0;
	float ex = 0;
	int nocache = 0;
	int draw = 1;
	int scalef = 0;
	int one = 0;
	int two = 0;
	static int spkl_shift = 0;
	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;

	firstwd = memrdwd(pc);
	pc++; pc++;
	secondwd = memrdwd(pc);
	total_length = 0;
	if ((firstwd == 0) && (secondwd == 0))
	{
		//wrlog("VGO with zeroed vector memory at %x\n",pc);
		return;
	}
	if (firstwd == 0xafe2) {
		//wrlog("EMPTY FRAME");
		total_length = 1; return;
	}
	pc = 0x4000;
	cache_clear();
	while (!done)
	{
		firstwd = memrdwd(pc);
		opcode = firstwd >> 13;
		pc++; pc++;

		if (opcode == VCTR) //Get the second word if it's a draw command
		{
			secondwd = memrdwd(pc); pc++; pc++;
		}

		if ((opcode == STAT) && ((firstwd & 0x1000) != 0))
			opcode = SCAL;

		switch (opcode)
		{
		case VCTR:

			x = twos_comp_val(secondwd, 13);
			y = twos_comp_val(firstwd, 13);
			z = (secondwd >> 12) & 0x0e;
			goto DRAWCODE;

			break;

		case SVEC:
			x = twos_comp_val(firstwd, 5) << 1;
			y = twos_comp_val(firstwd >> 8, 5) << 1;
			z = ((firstwd >> 4) & 0x0e);

		DRAWCODE:	if (z == 2) { z = statz; }if (z) { z = (z << 4) | 0x1f; }

			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;
			myx = x * scalef;///2
			myy = y * scalef;///2
			total_length += vector_timer(myx, myy);

			if (z > 0)
			{
				if (sparkle) {
					color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4)
						| ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));
				}
				if (vec_colors[color].r) red = z;   else red = 0;
				if (vec_colors[color].g) green = z; else green = 0;
				if (vec_colors[color].b) blue = z;  else blue = 0;

				ey = ((currenty - deltay) >> VEC_SHIFT);
				sy = currenty >> VEC_SHIFT;
				sx = (currentx >> VEC_SHIFT);
				ex = (currentx + deltax) >> VEC_SHIFT;

				if (sparkle)
				{
					one = 0;
					two = 0;
					// Get direction
					if (ex > sx) one = 1;
					if (ey > sy) two = 1;

					//add_color_point(ex, ey, red, green, blue);
					avg_add_point(ex, ey, 0, 0, red, green, blue, 0);

					color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4)
						| ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));

					if (vec_colors[color].r) red = z; else red = 0;
					if (vec_colors[color].g) green = z; else green = 0;
					if (vec_colors[color].b) blue = z; else blue = 0;

					//add_color_point(sx, sy, red, green, blue);
					avg_add_point(sx, sy, 0, 0, red, green, blue, 0);

					while (ex != sx || ey != sy)
					{
						color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4)
							| ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));
						if (vec_colors[color].r) red = z; else red = 0;
						if (vec_colors[color].g) green = z; else green = 0;
						if (vec_colors[color].b) blue = z; else blue = 0;

						if (sx != ex) {
							if (one) { sx += 1; }
							else { sx -= 1; }
						}

						if (sy != ey) {
							if (two) { sy += 1; }
							else { sy -= 1; }
						}

						//add_color_point(sx, sy, red, green, blue);
						avg_add_point(sx, sy, 0, 0, red, green, blue, 0);

						spkl_shift = (((spkl_shift & 0x40) >> 6) ^ ((spkl_shift & 0x20) >> 5) ^ 1) | (spkl_shift << 1);

						if ((spkl_shift & 0x7f) == 0x7f) spkl_shift = 0;
					}
					//add_color_line(sx, sy, ex, ey, red, green, blue);
					avg_add_point(sx, sy, ex, ey, red, green, blue, 1);
				}
				if (ywindow == 1)
				{
					//Y-Window clipping
					if (sy < clip && ey < clip) { draw = 0; }
					else { draw = 1; }
					if (ey < clip && ey < sy) { ex = ((clip - ey) * ((ex - sx) / (ey - sy))) + ex; ey = clip; }
					if (sy < clip && sy < ey) { sx = ((clip - sy) * ((sx - ex) / (sy - ey))) + sx; sy = clip; }
				}

				if (draw && sparkle == 0)
				{
					//add_color_line(sx, sy, ex, ey, red, green, blue);
					//add_color_point(sx, sy, red, green, blue);
					//add_color_point(ex, ey, red, green, blue);
					avg_add_point(sx, sy, ex, ey, red, green, blue, 1);
					avg_add_point(sx, sy, 0, 0, red, green, blue, 0);
					avg_add_point(ex, ey, 0, 0, red, green, blue, 0);
				}
			}
			currentx += deltax;
			currenty -= deltay;
			total_length += vector_timer(myx, myy);
			break;

		case STAT:
			color = firstwd & 0x0f;
			statz = (firstwd >> 4) & 0x0f;
			sparkle = firstwd & 0x0800;
			xflip = firstwd & 0x0400;
			vectorbank = ((firstwd >> 8) & 3) * 0x2000;

			if (lastbank != vectorbank) {
				lastbank = vectorbank;
				//wrlog("Vector Bank Switch %x", ((firstwd >> 8) & 3) * 0x2000);
				memcpy(GI[CPU0] + 0x6000, GI[CPU2] + vectorbank, 0x2000);
			}
			break;

		case SCAL:
			b = ((firstwd >> 8) & 0x07) + 8;
			l = (~firstwd) & 0xff;
			scale = (l << VEC_SHIFT) >> b;
			scalef = scale;
			//Triple the scale for 1024x768 resolution
			if (has_gamma_cpu) { scale = scale * 3; }
			else { scale = scale * 2; }
			if (firstwd & 0x0800)
			{
				if (ywindow == 0)
				{
					ywindow = 1;
					clip = currenty >> VEC_SHIFT;
				}
				else
				{
					ywindow = 0;
				}
			}

			break;

		case CNTR:
			d = firstwd & 0xff;
			currentx = 500 << VEC_SHIFT;
			currenty = 512 << VEC_SHIFT;

			break;

		case RTSL:

			if (sp == 0)
			{
				wrlog("*** Vector generator stack underflow! ***");
				done = 1;
				sp = MAXSTACK - 1;
			}
			else
			{
				sp--;
				pc = stack[sp];
			}
			break;

		case HALT:
			done = 1;
			break;

		case JMPL:
			a = 0x4000 + ((firstwd & 0x1fff) << 1);
			/* if a = 0x0000, treat as HALT */
			if (a == 0x4000)
			{
				done = 1;
			}
			else
			{
				pc = a;
			}
			break;

		case JSRL:
			a = 0x4000 + ((firstwd & 0x1fff) << 1);
			/* if a = 0x0000, treat as HALT */
			if (a == 0x4000)
			{
				done = 1;
			}
			else
			{
				stack[sp] = pc;
				if (sp == (MAXSTACK - 1))
				{
					wrlog("--- Passed MAX STACK (BAD!!) ---");
					done = 1;
					sp = 0;
				}
				else
				{
					sp++;
					pc = a;
				}
			}
			break;

		default: wrlog("Error in AVG engine");
		}
	}
}

/////////////////////////////////////////GAMMA CPU ///////////////////////////////////////////

/* Read from the gamma processor */
READ_HANDLER(mhavoc_gamma_r)
{
	//wrlog(  "Gamma: Now reading Alpha Data: %02x", gamma_data);
	alpha_rcvd = 1;
	gamma_xmtd = 0;
	return gamma_data;
}

/* Read from the alpha processor */
READ_HANDLER(mhavoc_alpha_r)
{
	//wrlog(  "Now Reading from data from Gamma Processor: %02x", alpha_data);
	gamma_rcvd = 1;
	alpha_xmtd = 0;
	return alpha_data;
}
/* Write to the alpha processor */
WRITE_HANDLER(mhavoc_alpha_w)
{
	//wrlog( "Now Writing to Gamma Processor: %02x", data);
	alpha_rcvd = 0;
	gamma_xmtd = 1;
	gamma_data = data;
}

/* Write to the gamma processor */
WRITE_HANDLER(mhavoc_gamma_w)
{
	//wrlog(  "Gamma: Writing to Alpha: %02x", data);
	delayed_data = data;
	nmicpu1 = 1;
}

READ_HANDLER(mhavoc_gammaram_r)
{
	return GI[CPU1][address & 0x7ff];
}

WRITE_HANDLER(mhavoc_gammaram_w)
{
	//wrlog("Ram write from Gamma CPU address:%x data:%x",address,data);
	// Note: Writing to both addresses seems to cure the nvram saving issue.
	GI[CPU1][address & 0x7ff] = data;
	GI[CPU1][address] = data;
}

WRITE_HANDLER(watchdog_w)
{
	//if (data !=0x50 && data !=0) wrlog("WatchDog Barking!!!!!!!!!!!!!!!! ------x data:%x",data);
}

WRITE_HANDLER(nvram_w)
{
	mhavoc_nvram[address] = data;
}

READ_HANDLER(nvram_r)
{
	return mhavoc_nvram[address];
}

// Read Rom
UINT8 MHAVOC_MRA_ROM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//wrlog("Address here is %x Lowaddr %x data %x", address, psMemRead->lowAddr, Machine->memory_region[active_cpu][address + psMemRead->lowAddr]);
	return GI[CPU1][address + psMemRead->lowAddr];
}

// Write Rom
void MHAVOC_MWA_ROM(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	wrlog("Attempted ROM Write Address here is %x Lowaddr %x data %x", address, psMemWrite->lowAddr, data);
	//If logging add here
}

WRITE_HANDLER(speech_data_w)
{
	if (data != 255) {
		wrlog("Speech Data Write %x", data);
	}
	speech_write_buffer = data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

MEM_READ(AlphaRead)
//MEM_ADDR(0x0000, 0x01ff, MRA_RAM)   		    /* 0.5K Program Ram */
MEM_ADDR(0x0200, 0x07ff, MRA_BANK1_R)			/* 3K Paged Program RAM	*/
//MEM_ADDR( 0x0800, 0x09ff, MRA_RAM)			/* 0.5K Program RAM */
MEM_ADDR(0x1000, 0x1000, mhavoc_gamma_r)		/* Gamma Read Port */
MEM_ADDR(0x1200, 0x1200, mhavoc_port_0_r)	    /* Alpha Input Port 0 */
//MEM_ADDR(0x1800, 0x1FFF, MRA_RAM)				/* Shared Beta Ram */
MEM_ADDR(0x2000, 0x3fff, MRA_BANK2_R)			/* Paged Program ROM (32K) */
//MEM_ADDR(0x4000, 0x4fff, MRA_RAM )			/* Vector RAM	(4K) */
// MEM_ADDR(0x5000, 0x5fff, VectorRom )			/* Vector ROM (4K) */
//MEM_ADDR(0x6000, 0x7fff, VectorRom )			/* Paged Vector ROM (32K) */
//MEM_ADDR( 0x8000, 0xffff, MRA_ROM )			/* Program ROM (32K) */
MEM_END

/* Main Board Writemem */
MEM_WRITE(AlphaWrite)
//MEM_ADDR(0x0000, 0x01ff, MWA_RAM )			 /* 0.5K Program Ram */
MEM_ADDR(0x0200, 0x07ff, MWA_BANK1_W)			 /* 3K Paged Program RAM */
//MEM_ADDR(0x0800, 0x09ff, MWA_RAM )			 /* 0.5K Program RAM */
MEM_ADDR(0x1200, 0x1200, NoWrite)			     /* don't care */
MEM_ADDR(0x1400, 0x141f, mhavoc_colorram_w)		 /* ColorRAM */
MEM_ADDR(0x1600, 0x1600, mhavoc_out_0_w)		 /* Control Signals */
MEM_ADDR(0x1640, 0x1640, avg_mgo)			     /* Vector Generator GO */
MEM_ADDR(0x1680, 0x1680, watchdog_w)			 /* Watchdog Clear */
MEM_ADDR(0x16c0, 0x16c0, avgdvg_reset_w)		 /* Vector Generator Reset */
MEM_ADDR(0x1700, 0x1700, mhavoc_alpha_irq_ack_w) /* IRQ ack */
MEM_ADDR(0x1740, 0x1740, mhavoc_rom_banksel_w)	 /* Program ROM Page Select */
MEM_ADDR(0x1780, 0x1780, mhavoc_ram_banksel_w)	 /* Program RAM Page Select */
MEM_ADDR(0x17c0, 0x17c0, mhavoc_gamma_w)		 /* Gamma Communication Write Port */
//MEM_ADDR( 0x1800, 0x1fff, MWA_RAM )			 /* Shared Beta Ram (Not Used)*/
MEM_ADDR(0x2000, 0x3fff, NoWrite)			     /* (ROM)Major Havoc writes here.*/
//MEM_ADDR( 0x4000, 0x4fff, MWA_RAM,             /* Vector Generator RAM	*/
MEM_ADDR(0x6000, 0x7fff, NoWrite)				 /* ROM */
MEM_END

MEM_READ(GammaRead)
//MEM_ADDR(0x0000, 0x07ff, MRA_RAM)				/* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_r)		/* wraps to 0x000-0x7ff */
MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_read)	/* Quad Pokey read	*/
MEM_ADDR(0x2800, 0x2800, mhavoc_port_1_r)	    /* Gamma Input Port	*/
MEM_ADDR(0x3000, 0x3000, mhavoc_alpha_r)		/* Alpha Comm. Read Port*/
MEM_ADDR(0x3800, 0x3803, ip_port_2_r)			/* Roller Controller Input*/
MEM_ADDR(0x4000, 0x4000, ip_port_4_r)			/* DSW at 8S */
MEM_ADDR(0x6000, 0x61ff, nvram_r)				/* EEROM	*/
MEM_ADDR(0x8000, 0xffff, MHAVOC_MRA_ROM)			    /* Program ROM (16K)	*/
MEM_END

MEM_WRITE(GammaWrite)
//MEM_ADDR(0x0000, 0x07ff, MWA_RAM )			 /* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_w)		 /* wraps to 0x000-0x7ff */
MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_write)	 /* Quad Pokey write	*/
MEM_ADDR(0x4000, 0x4000, mhavoc_gamma_irq_ack_w)/* IRQ Acknowledge	*/
MEM_ADDR(0x4800, 0x4800, mhavoc_out_1_w)		 /* Coin Counters 	*/
MEM_ADDR(0x5000, 0x5000, mhavoc_alpha_w)		 /* Alpha Comm. Write Port */
MEM_ADDR(0x5800, 0x5800, speech_data_w)		     /* Alpha Comm. Write Port */
MEM_ADDR(0x5900, 0x5900, speech_strobe_w)		 /* Alpha Comm. Write Port */
MEM_ADDR(0x6000, 0x61ff, nvram_w)   	         /* EEROM		*/
MEM_ADDR(0x8000, 0xffff, MHAVOC_MWA_ROM)
MEM_END
//----------------------------ALPHAONE DEFINITIONS-----------------------------------------------------------------------
MEM_READ(AlphaOneRead)
MEM_ADDR(0x0200, 0x07ff, MRA_BANK1_R)				/* 3K Paged Program RAM	*/
MEM_ADDR(0x1020, 0x103f, dual_pokey_r)
MEM_ADDR(0x1040, 0x1040, alphaone_port_0_r)         /* Alpha Input Port 0 */
MEM_ADDR(0x1060, 0x1060, alphaone_port_1_r)     	/* Gamma Input Port	*/
MEM_ADDR(0x1080, 0x1080, ip_port_2_r)				/* Roller Controller Input*/
MEM_ADDR(0x2000, 0x3fff, MRA_BANK2_R)			    /* Paged Program ROM (32K) */
MEM_END

MEM_WRITE(AlphaOneWrite)
MEM_ADDR(0x0200, 0x07ff, MWA_BANK1_W)				/* 3K Paged Program RAM */
MEM_ADDR(0x1020, 0x103f, dual_pokey_w)
MEM_ADDR(0x1040, 0x1040, MWA_NOP)					/* don't care */
MEM_ADDR(0x10a0, 0x10a0, mhavoc_out_0_w)			/* Control Signals */
MEM_ADDR(0x10a4, 0x10a4, avg_mgo)					/* Vector Generator GO */
MEM_ADDR(0x10a8, 0x10a8, MWA_NOP)					/* Watchdog Clear */
MEM_ADDR(0x10ac, 0x10ac, avgdvg_reset_w)			/* Vector Generator Reset */
MEM_ADDR(0x10b0, 0x10b0, mhavoc_alpha_irq_ack_w)	/* IRQ ack */
MEM_ADDR(0x10b4, 0x10b4, mhavoc_rom_banksel_w)		/* Program ROM Page Select */
MEM_ADDR(0x10b8, 0x10b8, mhavoc_ram_banksel_w)		/* Program RAM Page Select */
MEM_ADDR(0x10e0, 0x10ff, mhavoc_colorram_w)			/* ColorRAM */
MEM_ADDR(0x2000, 0x3fff, MWA_ROM)					/* Major Havoc writes here.*/
MEM_ADDR(0x6000, 0xffff, MWA_ROM)
MEM_END

void run_mhavoc()
{
	int x;
	int cycles1 = (MHAVOC_CLOCK_2_5M / 50);
	int cycles2 = (MHAVOC_CLOCK_1_25M / 50);
	int passes = 400;
	int cpunum = 0;
	int cycles = 0;
	int cyclesgamma = 0;
	int cpu1ticks = 0;
	int cpu2ticks = 0;

	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	dwElapsedTicks = m_cpu_6502[CPU0]->get6502ticks(0xff);

	gammaticks = 0;

	//wrlog("------------FRAME START --------------");
	update_input_ports();

	for (x = 0; x < (passes + 1); x++)
	{
		cycles = m_cpu_6502[CPU0]->get6502ticks(0xff);

		dwResult = m_cpu_6502[CPU0]->exec6502(125);
		if (0x80000000 != dwResult)
		{
			x = 3000; done = 1;
			allegro_message("Invalid instruction at %.2x on CPU 0", m_cpu_6502[CPU0]->get_pc());
		}

		cycles = m_cpu_6502[CPU0]->get6502ticks(0xff);

		cpu1ticks += cycles;
		bigticks += cycles;

		if (bigticks > 0xffff) { bigticks = 0; }

		if (vidticks < 1) vidticks = cycles - vidticks; //Play catchup after reset
		else 	 vidticks += cycles;

		twokticks += cycles;
		//wrlog("HRTZ Counter %d",hrzcounter);
		if (twokticks > 120) { twokticks -= 120; bitflip ^= 1; }

		if (has_gamma_cpu)
		{
			//----------------------------------------------------------------------------------------------------------------------

			if (reset1) { m_cpu_6502[CPU1]->reset6502(); reset1 = 0; wrlog("Reset, Gamma CPU"); }

			// This really isn't a 250ms delay, but it seems to work ok?
			// ToDo: Correct with more accurate timing.
			if (nmicpu1)
			{
				m_cpu_6502[CPU1]->nmi6502();
				nmicpu1 = 0;
				gamma_rcvd = 0;
				alpha_xmtd = 1;
				alpha_data = delayed_data;
				//wrlog("NMI Taken, Gamma CPU");
			}

			cyclesgamma = m_cpu_6502[CPU1]->get6502ticks(0xff);
			cyclesgamma = 0;
			dwResult = m_cpu_6502[CPU1]->exec6502(63); //(cycles2 / passes) This should be 64 cycles with 400 passes.
			if (0x80000000 != dwResult)
			{
				x = 3000; done = 1; end_mhavoc();
				allegro_message("Invalid instruction at %.2x on CPU 2", m_cpu_6502[CPU1]->get_pc());
			}

			cyclesgamma = m_cpu_6502[CPU1]->get6502ticks(0xff);
			gammaticks += cyclesgamma;
		}
		mhavoc_interrupt();
	}
	pokey_do_update();
	if (gamenum == MHAVOCRV || gamenum == MHAVOCPE) { tms5220_sh_update(); }

	//wrlog("Alpha cycles ran this frame %d - Wanted to run %d", cpu1ticks, cycles1);
	//wrlog("Gamma cycles ran this frame %d - wanted to run %d", gammaticks, cycles2);
}
/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_mhavoc(void)
{
	if (gamenum == ALPHAONE || gamenum == ALPHAONA) has_gamma_cpu = 0; else has_gamma_cpu = 1;

	total_length = 1;
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
	gamma_irq_clock = 0;
	MHAVGDONE = 1;
	alpha_data = 0;
	alpha_rcvd = 0;
	alpha_xmtd = 0;
	gamma_data = 0;
	gamma_rcvd = 0;
	gamma_xmtd = 0;
	player_1 = 0;
	// Start with all pokeys enabled.
	pkenable[0] = 1;
	pkenable[1] = 1;
	pkenable[2] = 1;
	pkenable[3] = 1;
	sample_pos = 0;
	gammaticks = 0;

	if (has_gamma_cpu) {
		init6502(AlphaRead, AlphaWrite, 0);
		init6502(GammaRead, GammaWrite, 1);
		Pokey_sound_init(1250000, 44100, 4, 6);
	}
	else {
		init6502(AlphaOneRead, AlphaOneWrite, 0);
		Pokey_sound_init(1250000, 44100, 2, 6);
		memset(GI[CPU0] + 0x1800, 0xff, 0xff);
	}

	// Copy the vector rom to the paged rom area
	memcpy(GI[CPU0] + 0x6000, GI[CPU2], 0x2000);
	wrlog("MHAVOC CPU init complete");

	//Load High Score table for Alpha One
	if (has_gamma_cpu == 0) { load_hi_aae(0x1800, 0x100, 0); }

	// Sound Config to move.
	BUFFER_SIZE = 44100 / driver[gamenum].fps;// / 2;
	soundbuffer = (unsigned char*)malloc(BUFFER_SIZE);
	memset(soundbuffer, 0, BUFFER_SIZE);
	stream1 = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, 125, 128); //config.pokeyvol
	//mhavoc_sh_start();
	if (gamenum == MHAVOCRV || gamenum == MHAVOCPE) { tms5220_sh_start(&tms5220_interface); }
	// Start with clear video cache
	cache_clear();

	/* compute the min/max values */
	xmin = 0;
	ymin = 0;
	xmax = 1024;
	ymax = 812;
	width = xmax - xmin;
	height = ymax - ymin;

	/* determine the center points */
	xcenter = ((xmax + xmin) / 2);// << 16;
	ycenter = ((ymax + ymin) / 2);// << 16;

	/* initialize to no avg flipping */
	flip_x = flip_y = 0;
	swap_xy = 0;

	return 0;
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////