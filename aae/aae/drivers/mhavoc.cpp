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

#define NOMINMAX
#include "mhavoc.h"
#include "aae_mame_driver.h"
#include "driver_registry.h" 
#include "aae_pokey.h"
#include "earom.h"
#include "tms5220.h"
#include "timer.h"
#include "mhavoc_custom_video.h"
#include "glcode.h"
#include "mixer.h"
#include "wav_resample.h"
#include "okim6295_loader.h"

#pragma warning( disable :  4244)

#define mh_debug 0
#define MHAVOC_CLOCK		10000000
#define MHAVOC_CLOCK_2_5M	(MHAVOC_CLOCK/4)
#define MHAVOC_CLOCK_1_25M	(MHAVOC_CLOCK/8)
#define OKI_CLOCK           1056000 // Courtesy of HBMAME

static struct POKEYinterface pokey_interface_alphaone =
{
	2,			/* 4 chips */
	1250000,
	200,	/* volume */
	POKEY_DEFAULT_GAIN / 2,
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

static struct POKEYinterface pokey_interface =
{
	4,			/* 4 chips */
	1250000,
	200,	/* volume */
	POKEY_DEFAULT_GAIN / 4,
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
		osd_fwrite(file, mhavoc_nvram, 0x200);
	}
	else
	{
		if (file)
		{
			osd_fread(file, mhavoc_nvram, 0x200);
		}
		else
		{
			memset(mhavoc_nvram, 0, 0x200);
		}
	}
}

/*
static const char* mhavocpe_samples[] = {
	"mhavocpe.zip",
	"00_speech_chip_test.wav",
	"Char - Take 1 - One.wav",
	"Char - Take 1 - Two.wav",
	"Char - Take 1 - Three.wav",
	"Char - Take 1 - Four.wav",
	"Char - Take 1 - Five.wav",
	"Char - Take 1 - Six.wav",
	"Char - Take 1 - Seven.wav",
	"Char - Take 1 - Eight.wav",
	"Char - Take 1 - Nine.wav",
	"Char - Take 1 - Ten.wav",
	"jess_max_pitchdown_85_mechanized__intruder.wav",
	"jess_max_pitchdown_85_mechanized__destroy_intruder.wav",
	"jess_max_pitchdown_85_mechanized__destroyed.wav",
	"jess_max_pitchdown_85_mechanized__hahahaha_1.wav",
	"jess_max_pitchdown_85_mechanized__hahahaha_2.wav",
	"jess_max_pitchdown_85_mechanized__hahahaha_3.wav",
	"jess_max_pitchdown_85_mechanized__we_win.wav",
	"jess_max_pitchdown_85_mechanized__you_lose.wav",
	"jess_max2_pitchdown_83_mechanized__activated.wav",
	"jess_max2_pitchdown_83_mechanized__attack.wav",
	"jess_max2_pitchdown_83_mechanized__you_cannot_win.wav",
	"jess_max2_pitchdown_83_mechanized__you_will_not_win.wav",
	"jess_max3_pitchdown_85_mechanized__ill_be_back.wav",
	"jess_max3_pitchdown_85_mechanized__ahhhh2.wav",
	"jess_max3_pitchdown_85_mechanized__ohhhh.wav",
	"jess_max3_pitchdown_85_mechanized__eeewww.wav",
	"jess_rex_mechanized__better_hurry.wav",
	"jess_rex_mechanized__come_on.wav",
	"jess_rex_mechanized__im_outta_here.wav",
	"jess_rex_mechanized__ooof.wav",
	"jess_rex_mechanized__ooom.wav",
	"jess_rex_mechanized__ouch.wav",
	"jess_rex_mechanized__uggh.wav",
	"jess_rex_mechanized__uh_oh.wav",
	"jess_rex_mechanized__um_hmm.wav",
	"jess_rex_mechanized__uumm.wav",
	"jess_rex_mechanized__whoa.wav",
	"jess_rex_mechanized__woo_hoo.wav",
	"benk_18_force will be with you, always.wav",
	"boy_01_look at the size of that thing.wav",
	 0 };
*/

static UINT8 alpha_data = 0;
static UINT8 alpha_rcvd = 0;
static UINT8 alpha_xmtd = 0;

static UINT8 gamma_data = 0;
static UINT8 gamma_rcvd = 0;
static UINT8 gamma_xmtd = 0;

static UINT8 speech_write_buffer;
static UINT8 player_1;

// Version specific variables 
int is_mhavocpe = 0;
int has_gamma_cpu = 0;
int has_tms5220 = 0;

// Clock Variables
static UINT8 alpha_irq_clock;
static UINT8 alpha_irq_clock_enable;
static UINT8 gamma_irq_clock;

// Ram and Rom Backing Variables
static int ram_bank[2] = { 0x20200, 0x20800 };
static int ram_bank_sel = 0;
static int rom_bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
static int rom_bank_sel = 0;
unsigned char* cur_bank;

static int MHAVGDONE = 1;
float sweep;

/*************************************
*
*	Alpha One: Dual Pokey.
*
*************************************/

READ_HANDLER(dual_pokey_r)
{
	int offset = address;
	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	int pokey_reg = (offset % 8) | control;

	if (pokey_num == 0)
		return pokey1_r(pokey_reg);
	else
		return pokey2_r(pokey_reg);
}

WRITE_HANDLER(dual_pokey_w)
{
	int offset = address;
	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	int pokey_reg = (offset % 8) | control;

	if (pokey_num == 0)
		pokey_1_w(pokey_reg, data, 0);
	else
		pokey_2_w(pokey_reg, data, 0);
}

static int last_sample = 0;

WRITE_HANDLER(speech_strobe_w)
{
	if (has_tms5220)
	{
		tms5220_data_w(0, speech_write_buffer);
	}
	else
	{
		sample_start(3,data - 0x80 , 0);
	}
}

int mhavoc_sh_start(void)
{
	int rv;
	
	if (has_gamma_cpu) rv = pokey_sh_start(&pokey_interface);
	else rv = pokey_sh_start(&pokey_interface_alphaone);
	
	if (has_tms5220) { tms5220_sh_start(&tms5220_interface); }
	
	unsigned char* RAM = memory_region(REGION_CPU4);
	size_t ROM_SIZE = Machine->memory_region_length[REGION_CPU4];

	const int oki_clock_hz = OKI_CLOCK;
	const bool pin7_high = true;     // pin 7 tied high → divisor 132

	// Mixer/system output rate
	const int out_rate = config.samplerate;
		
	const uint32_t max_scan = (uint32_t)std::min<size_t>(ROM_SIZE / 8, 4096);

	const int count = load_okim6295_from_region(
		RAM, ROM_SIZE, out_rate, oki_clock_hz, pin7_high, max_scan
	);

	LOG_INFO("OKI loader: registered %d samples", count);
	return rv;
}

void mhavoc_sh_stop(void)
{
	pokey_sh_stop();
	if (has_tms5220)
	tms5220_sh_stop();
}

void mhavoc_sh_update()
{
	pokey_sh_update();
	if (has_tms5220)
	 tms5220_sh_update();
}

void end_mhavoc()
{
	LOG_INFO("End Major Havoc Called");

	mhavoc_sh_stop();
	if (!has_gamma_cpu) { save_hi_aae(0x1800, 0x100, 0); }
	//Reset all game specific variables.
	is_mhavocpe = 0;
	has_gamma_cpu = 0;
	has_tms5220 = 0;
}

void run_reset()
{
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
	cache_clear();
	//cpu_reset(0);

	if (has_gamma_cpu)
	{
		//cpu_reset(1);
	}
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
		if ((alpha_irq_clock & (0x0c)) == (0x0c)) //0x0c //0x30
		{
			//LOG_INFO("IRQ ALPHA CPU");
			cpu_do_int_imm(CPU0, INT_TYPE_INT);
			alpha_irq_clock_enable = 0;
			alpha_irq_clock = 0;
		}
	}

	/* clock the LS161 driving the gamma CPU IRQ */
	if (has_gamma_cpu)
	{
		gamma_irq_clock++;
		if ((gamma_irq_clock & (0x08)) == (0x08))//08 //0x20
		{
			//LOG_INFO("IRQ GAMMA CPU");
			cpu_do_int_imm(CPU1, INT_TYPE_INT);
		}
	}
}

WRITE_HANDLER(mhavoc_alpha_irq_ack_w)
{
	/* clear the line and reset the clock */
	m_cpu_6502[CPU0]->m6502clearpendingint();
	cpu_clear_pending_int(INT_TYPE_INT, CPU0);

	//LOG_INFO("Alpha IRQ ACK!", data);
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
}

WRITE_HANDLER(mhavoc_gamma_irq_ack_w)
{
	/* clear the line and reset the clock */
	//LOG_INFO("Gamma IRQ ACK!", data);
	m_cpu_6502[CPU1]->m6502clearpendingint();
	cpu_clear_pending_int(INT_TYPE_INT, CPU1);
	gamma_irq_clock = 0;
}

static void mhavoc_clr_busy(int dummy)
{
    MHAVGDONE = 1;
}

WRITE_HANDLER(avgdvg_reset_w)
{
	LOG_INFO("---------------------------AVGDVG RESET ------------------------");
	total_length = 0;
}

WRITE_HANDLER(avg_mgo)
{
	if (!MHAVGDONE) { return; }

	mhavoc_video_update();

	if (total_length > 10)
	{
		MHAVGDONE = 0;
		// Clear the video tick count.
		get_video_ticks(0xff);
	
		// There is a method to this madness, the time for the sweep is what it should be if the game was running 30FPS instead of 50. 
		//That's why the multiplication by 1.666
		sweep = (float)(TIME_IN_NSEC(1500) * total_length) * Machine->gamedrv->cpu[CPU0].cpu_freq; // This is the rough time for 50fps.
		if (has_gamma_cpu)
		sweep = sweep * 1.666; // Alpha One is slower, it actually seems to run 30fps.
		
		if (config.debug_profile_code) {
			LOG_INFO("Sweep Timer %f", sweep);
		}
	}
	else { MHAVGDONE = 1; }
}

WRITE_HANDLER(mhavoc_out_0_w)
{
	if (is_mhavocpe)
	{
		avg_set_flip_x_mh(data & 0x40);
		avg_set_flip_y_mh(data & 0x80);
	}

	if (!(data & 0x08))
	{
		//LOG_INFO ("\t\t\t\t*** resetting gamma processor. ***\n");
		alpha_rcvd = 0;
		alpha_xmtd = 0;
		gamma_rcvd = 0;
		gamma_xmtd = 0;
		cpu_reset(1);   //RESET GAMMA CPU
	}
	player_1 = (data >> 5) & 1;
	
	// Bit 0 = Roller light (Blinks on fatal errors) //
	set_led_status(0, data & 0x01);
}

WRITE_HANDLER(mhavoc_out_1_w)
{
	// LOG_INFO("LED OUT WRITE");
	set_led_status(0, data & 0x01);
	set_led_status(1, (data & 0x02) >> 1);
}

// Simulates frequency and vector halt
READ_HANDLER(mhavoc_port_0_r)
{
	UINT8 res;
	
	if (!MHAVGDONE)
	{
		if (get_video_ticks(0) > sweep)
		{
			mhavoc_clr_busy(0);
			//LOG_INFO("Mhavoc DONE Set HERE %x at Frame %d Cycles %d", MHAVGDONE, cpu_getcurrentframe(), cpu_getcycles_cpu(0));
		}
	}
	
	// Bits 7-6 = selected based on Player 1
		// Bits 5-4 = common
	if (player_1)
		res = (readinputport(0) & 0x30) | (readinputport(5) & 0xc0);
	else
		res = readinputport(0) & 0xf0;

	// Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024)  (EVERY 120 CYCLES)
	// Note: Bigticks that was being used previously may actually be better and help the timing.
	if (!(get_eterna_ticks(0) & 0x400)) 
	 res |= 0x02; 

	if (MHAVGDONE)
		res |= 0x01;

	if (gamma_rcvd)
		res |= 0x08;

	if (gamma_xmtd)
		res |= 0x04;

	return res;
}

READ_HANDLER(mhavoc_port_1_r)
{
	int res;
	// Bits 7 - 2 = input switches
	res = readinputport(1) & 0xfc;

	// Bit 2 = TMS5220 ready flag
	if (has_tms5220)
	{
		if (!tms5220_ready_r())	res |= 0x04;
		else
			res &= ~0x04;
		//LOG_INFO("TMS Ready Data %d", tms5220_ready_r());
	}
	/* Bit 1 = Alpha rcvd flag */
	if (has_gamma_cpu && alpha_rcvd)
		res |= 0x02;

	/* Bit 0 = Alpha xmtd flag */
	if (has_gamma_cpu && alpha_xmtd)
		res |= 0x01;

	return res;
}

READ_HANDLER(alphaone_port_0_r)
{
	int res;

	res = readinputport(0) & 0xfc;

	if (!MHAVGDONE)
	{
		if (get_video_ticks(0) > sweep) 
		{
			MHAVGDONE = 1;
			total_length = 0;
		}
	}
	
	/* Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024) */
	if (!(get_eterna_ticks(0) & 0x400)) 
	 res |= 0x02; 

	if (MHAVGDONE)
		res |= 0x01;
	

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
	if (is_mhavocpe)
	{
		data = ((data & 1) | ((data & 2) << 1) | ((data & 4) >> 1));
	}
	else { data = data & 0x03; }
	cur_bank = &Machine->memory_region[CPU0][bank[data]];
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

	Machine->memory_region[CPU0][bank] = data;
}

READ_HANDLER(MRA_BANK1_R)
{
	int bank;

	if (ram_bank_sel == 0) { bank = 0x20200; }
	else { bank = 0x20800; }
	bank = bank + (address);

	return Machine->memory_region[CPU0][bank];
}

/* Read from the gamma processor */
READ_HANDLER(mhavoc_gamma_r)
{
	alpha_rcvd = 1;
	gamma_xmtd = 0;
	return gamma_data;
}

/* Read from the alpha processor */
READ_HANDLER(mhavoc_alpha_r)
{
	gamma_rcvd = 1;
	alpha_xmtd = 0;
	return alpha_data;
}
/* Write to the alpha processor */
WRITE_HANDLER(mhavoc_alpha_w)
{
	alpha_rcvd = 0;
	gamma_xmtd = 1;
	gamma_data = data;
}

/* Write to the gamma processor */
WRITE_HANDLER(mhavoc_gamma_w)
{
	gamma_rcvd = 0;
	alpha_xmtd = 1;
	alpha_data = data;
	cpu_do_int_imm(CPU1, INT_TYPE_NMI);
}

READ_HANDLER(mhavoc_gammaram_r)
{
	return Machine->memory_region[CPU1][address & 0x7ff];
}

WRITE_HANDLER(mhavoc_gammaram_w)
{
	//LOG_INFO("Ram write from Gamma CPU address:%x data:%x",address,data);
	// Note: Writing to both addresses seems to cure the nvram saving issue.
	Machine->memory_region[CPU1][address & 0x7ff] = data;
	Machine->memory_region[CPU1][address] = data;
}

WRITE_HANDLER(nvram_w)
{
	mhavoc_nvram[address] = data;
}

READ_HANDLER(nvram_r)
{
	return mhavoc_nvram[address];
}

WRITE_HANDLER(speech_data_w)
{
	speech_write_buffer = data;
}

void run_mhavoc()
{
	//m_cpu_6502[0]->log_instruction_usage();
	//m_cpu_6502[0]->reset_instruction_counts();

	if (!has_gamma_cpu) 
	{
		watchdog_reset_w(0, 0, 0);	
	}
	mhavoc_sh_update();
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
MEM_ADDR(0x1200, 0x1200, MWA_ROM)			     /* don't care */
MEM_ADDR(0x1400, 0x141f, mhavoc_colorram_w)		 /* ColorRAM */
MEM_ADDR(0x1600, 0x1600, mhavoc_out_0_w)		 /* Control Signals */
MEM_ADDR(0x1640, 0x1640, avg_mgo)			     /* Vector Generator GO */
MEM_ADDR(0x1680, 0x1680, watchdog_reset_w)			 /* Watchdog Clear */
MEM_ADDR(0x16c0, 0x16c0, avgdvg_reset_w)		 /* Vector Generator Reset */
MEM_ADDR(0x1700, 0x1700, mhavoc_alpha_irq_ack_w) /* IRQ ack */
MEM_ADDR(0x1740, 0x1740, mhavoc_rom_banksel_w)	 /* Program ROM Page Select */
MEM_ADDR(0x1780, 0x1780, mhavoc_ram_banksel_w)	 /* Program RAM Page Select */
MEM_ADDR(0x17c0, 0x17c0, mhavoc_gamma_w)		 /* Gamma Communication Write Port */
//MEM_ADDR( 0x1800, 0x1fff, MWA_RAM )			 /* Shared Beta Ram (Not Used)*/
MEM_ADDR(0x2000, 0x3fff, MWA_ROM)			     /* (ROM)Major Havoc writes here.*/
//MEM_ADDR( 0x4000, 0x4fff, MWA_RAM,             /* Vector Generator RAM	*/
MEM_ADDR(0x6000, 0x7fff, MWA_ROM)				 /* ROM */
MEM_END

MEM_READ(GammaRead)
//MEM_ADDR(0x0000, 0x07ff, MRA_RAM)				/* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_r)		/* wraps to 0x000-0x7ff */
MEM_ADDR(0x2000, 0x203f, quadpokey_r)	/* Quad Pokey read	*/
MEM_ADDR(0x2800, 0x2800, mhavoc_port_1_r)	    /* Gamma Input Port	*/
MEM_ADDR(0x3000, 0x3000, mhavoc_alpha_r)		/* Alpha Comm. Read Port*/
MEM_ADDR(0x3800, 0x3803, ip_port_2_r)			/* Roller Controller Input*/
MEM_ADDR(0x4000, 0x4000, ip_port_4_r)			/* DSW at 8S */
MEM_ADDR(0x6000, 0x61ff, nvram_r)				/* EEROM	*/
//MEM_ADDR(0x8000, 0xffff, MRA_ROM)			    /* Program ROM (16K)	*/
MEM_END

MEM_WRITE(GammaWrite)
//MEM_ADDR(0x0000, 0x07ff, MWA_RAM )			 /* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_w)		 /* wraps to 0x000-0x7ff */
MEM_ADDR(0x2000, 0x203f, quadpokey_w)	 /* Quad Pokey write	*/
MEM_ADDR(0x4000, 0x4000, mhavoc_gamma_irq_ack_w)/* IRQ Acknowledge	*/
MEM_ADDR(0x4800, 0x4800, mhavoc_out_1_w)		 /* Coin Counters 	*/
MEM_ADDR(0x5000, 0x5000, mhavoc_alpha_w)		 /* Alpha Comm. Write Port */
MEM_ADDR(0x5800, 0x5800, speech_data_w)		     /* Alpha Comm. Write Port */
MEM_ADDR(0x5900, 0x5900, speech_strobe_w)		 /* Alpha Comm. Write Port */
MEM_ADDR(0x6000, 0x61ff, nvram_w)   	         /* EEROM		*/
MEM_ADDR(0x8000, 0xffff, MWA_ROM)
MEM_END
//----------------------------ALPHAONE DEFINITIONS-----------------------------------------------------------------------
MEM_READ(AlphaOneRead)
MEM_ADDR(0x0200, 0x07ff, MRA_BANK1_R)				/* 3K Paged Program RAM	*/
MEM_ADDR(0x1020, 0x103f, dual_pokey_r)
MEM_ADDR(0x1040, 0x1040, alphaone_port_0_r)         /* Alpha Input Port 0 */
MEM_ADDR(0x1060, 0x1060, ip_port_1_r)				/* Gamma Input Port	*/
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

/////////////////// MAIN() for program ///////////////////////////////////////////////////


int init_mhavocpe()
{
	is_mhavocpe = 1;
	init_mhavoc();
	return 0;
}

int init_mhavoccrv()
{
	has_tms5220 = 1;
	init_mhavoc();
	return 0;
}

int init_alphone()
{
	has_gamma_cpu = 0;

	//init6502(AlphaOneRead, AlphaOneWrite, 0xffff, CPU0);
	memset(Machine->memory_region[CPU0] + 0x1800, 0xff, 0xff);
	//Load High Score table for Alpha One
	load_hi_aae(0x1800, 0x100, 0);
	// Reset app variables to start
	run_reset();
	//Init the Sound
	mhavoc_sh_start();
	//Init the video
	mhavoc_video_init(2);
	return 0;
}

int init_mhavoc(void)
{
	has_gamma_cpu = 1;

	//init6502(AlphaRead, AlphaWrite, 0xffff, CPU0);
	//init6502(GammaRead, GammaWrite, 0xbfff, CPU1);

	run_reset();
	//Init the Sound
	mhavoc_sh_start();
	//Init the video
	mhavoc_video_init(3);
	
	LOG_INFO("MHAVOC Init complete");
	return 0;
}


INPUT_PORTS_START(mhavoc)
PORT_START("IN0")	/* IN0 - alpha (player_1 = 0) */
PORT_BIT(0x03, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diag Step/Coin C", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN1)	/* Left Coin Switch  */
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)	/* Right Coin */

PORT_START("IN1")	/* IN1 - gamma */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN2")	/* IN2 - gamma */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_REVERSE, 100, 40, 0, 0, 0)

PORT_START("DIP1") /* DIP Switch at position 13/14S */
PORT_DIPNAME(0x01, 0x00, "Adaptive Difficulty")
PORT_DIPSETTING(0x01, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x02, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x02, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x0c, "50000")
PORT_DIPSETTING(0x00, "100000")
PORT_DIPSETTING(0x04, "200000")
PORT_DIPSETTING(0x08, "None")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x10, "Easy")
PORT_DIPSETTING(0x00, "Medium")
PORT_DIPSETTING(0x30, "Hard")
PORT_DIPSETTING(0x20, "Demo")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3 (2 in Free Play)")
PORT_DIPSETTING(0xc0, "4 (3 in Free Play)")
PORT_DIPSETTING(0x80, "5 (4 in Free Play)")
PORT_DIPSETTING(0x40, "6 (5 in Free Play)")

PORT_START("DIP2") /* DIP Switch at position 8S */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x02, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x01, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x0c, "Right Coin Mech")
PORT_DIPSETTING(0x0c, "x1")
PORT_DIPSETTING(0x08, "x4")
PORT_DIPSETTING(0x04, "x5")
PORT_DIPSETTING(0x00, "x6")
PORT_DIPNAME(0x10, 0x10, "Left Coin Mech")
PORT_DIPSETTING(0x10, "x1")
PORT_DIPSETTING(0x00, "x2")
PORT_DIPNAME(0xe0, 0xe0, "Bonus Credits")
PORT_DIPSETTING(0x80, "2 each 4")
PORT_DIPSETTING(0x40, "1 each 3")
PORT_DIPSETTING(0xa0, "1 each 4")
PORT_DIPSETTING(0x60, "1 each 5")
PORT_DIPSETTING(0xe0, "None")

PORT_START("IN5")	/* IN5 - dummy for player_1 = 1 on alpha */
PORT_BIT(0x3f, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_DIPNAME(0x40, 0x40, "Credit to start")
PORT_DIPSETTING(0x40, "1")
PORT_DIPSETTING(0x00, "2")
PORT_BITX(0x80, 0x80, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPSETTING(0x80, DEF_STR(Off))

INPUT_PORTS_END

INPUT_PORTS_START(mhavocp)
PORT_START("IN0")	/* IN0 - alpha (player_1 = 0) */
PORT_BIT(0x0f, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/* PORT_BIT ( 0x10, IP_ACTIVE_LOW, IPT_SERVICE, "Diag Step", OSD_KEY_T, IP_JOY_NONE ) */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diag Step/Coin C", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN1)	/* Left Coin Switch  */
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN2)	/* Right Coin */

PORT_START("IN1")	/* IN1 - gamma */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN2")	/* IN2 - gamma */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_REVERSE, 100, 10, 0, 0, 0)

PORT_START("DIP1") /* DIP Switch at position 13/14S */
PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x02, "3")
PORT_DIPSETTING(0x03, "4")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x0c, "50000")
PORT_DIPSETTING(0x00, "100000")
PORT_DIPSETTING(0x04, "200000")
PORT_DIPSETTING(0x08, "None")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x10, "Easy")
PORT_DIPSETTING(0x00, "Medium")
PORT_DIPSETTING(0x30, "Hard")
PORT_DIPSETTING(0x20, "Demo")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "3 (2 in Free Play)")
PORT_DIPSETTING(0xc0, "4 (3 in Free Play)")
PORT_DIPSETTING(0x80, "5 (4 in Free Play)")
PORT_DIPSETTING(0x40, "6 (5 in Free Play)")

PORT_START("DIP2") /* DIP Switch at position 8S */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Coinage))
PORT_DIPSETTING(0x02, DEF_STR(2C_1C))
PORT_DIPSETTING(0x03, DEF_STR(1C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x01, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x0c, "Right Coin Mechanism")
PORT_DIPSETTING(0x0c, "x1")
PORT_DIPSETTING(0x08, "x4")
PORT_DIPSETTING(0x04, "x5")
PORT_DIPSETTING(0x00, "x6")
PORT_DIPNAME(0x10, 0x10, "Left Coin Mechanism")
PORT_DIPSETTING(0x10, "x1")
PORT_DIPSETTING(0x00, "x2")
PORT_DIPNAME(0xe0, 0xe0, "Bonus Credits")
PORT_DIPSETTING(0x80, "2 each 4")
PORT_DIPSETTING(0x40, "1 each 3")
PORT_DIPSETTING(0xa0, "1 each 4")
PORT_DIPSETTING(0x60, "1 each 5")
PORT_DIPSETTING(0xe0, "None")

PORT_START("IN5")	/* IN5 - dummy for player_1 = 1 on alpha */
PORT_BIT(0x3f, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_DIPNAME(0x40, 0x40, "Credit to start")
PORT_DIPSETTING(0x40, "1")
PORT_DIPSETTING(0x00, "2")
PORT_BITX(0x80, 0x80, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPSETTING(0x80, DEF_STR(Off))
INPUT_PORTS_END

INPUT_PORTS_START(alphaone)
PORT_START("IN0")	/* IN0 - alpha (player_1 = 0) */
PORT_BIT(0x03, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BIT(0x7c, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN1")	/* IN1 - gamma */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_COIN1)

PORT_START("IN2")	/* IN2 - gamma */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_REVERSE, 100, 40, 0, 0, 0)
INPUT_PORTS_END


ROM_START(alphaone)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("vec5000.tw", 0x5000, 0x1000, CRC(2a4c149f) SHA1(b60a0b29958bee9b5f7c1d88163680b626bb76dd))
ROM_LOAD("8000.tw", 0x8000, 0x2000, CRC(962d4da2) SHA1(2299f850aed7470a80a21526143f7b412a879cb1))
ROM_LOAD("a000.tw", 0xa000, 0x2000, CRC(f739a791) SHA1(1e70e446fc7dd27683ad71e768ebb2bc1d4fedd3))
ROM_LOAD("twjk1.bin", 0xc000, 0x2000, CRC(1ead0b34) SHA1(085e05526d029bcff7c8ae050cde73f52ee13846))
ROM_LOAD("e000.tw", 0xe000, 0x1000, CRC(6b1d7d2b) SHA1(36ac8b53e2fe01ed281c94afec02484ef676ddad))
ROM_RELOAD(0x0f000, 0x1000)
ROM_LOAD("page01.tw", 0x10000, 0x4000, CRC(cbf3b05a) SHA1(1dfaf9300a252c9c921f06167160a59cdf329726))

ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("vec_pg01.tw", 0x0000, 0x4000, CRC(e392a94d) SHA1(b5843da97d7aa5767c87c29660115efc5ad9ad54))
ROM_LOAD("vec_pg23.tw", 0x4000, 0x4000, CRC(1ff74292) SHA1(90e61c48544c62d905e207bba5c67ae7694e86a5))

// AVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(alphaonea)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("vec5000.tw", 0x5000, 0x1000, CRC(2a4c149f) SHA1(b60a0b29958bee9b5f7c1d88163680b626bb76dd))
ROM_LOAD("8000.tw", 0x8000, 0x2000, CRC(962d4da2) SHA1(2299f850aed7470a80a21526143f7b412a879cb1))
ROM_LOAD("a000.tw", 0xa000, 0x2000, CRC(f739a791) SHA1(1e70e446fc7dd27683ad71e768ebb2bc1d4fedd3))
ROM_LOAD("c000.tw", 0xc000, 0x2000, CRC(f21fb1ac) SHA1(2590147e75611a3f87397e7b0baa7020e7528ac8))
ROM_LOAD("e000.tw", 0xe000, 0x1000, CRC(6b1d7d2b) SHA1(36ac8b53e2fe01ed281c94afec02484ef676ddad))
ROM_RELOAD(0x0f000, 0x1000)
ROM_LOAD("page01.tw", 0x10000, 0x4000, CRC(cbf3b05a) SHA1(1dfaf9300a252c9c921f06167160a59cdf329726))

ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("vec_pg01.tw", 0x0000, 0x4000, CRC(e392a94d) SHA1(b5843da97d7aa5767c87c29660115efc5ad9ad54))
ROM_LOAD("vec_pg23.tw", 0x4000, 0x4000, CRC(1ff74292) SHA1(90e61c48544c62d905e207bba5c67ae7694e86a5))
// AVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END


ROM_START(mhavoc2)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("136025.110", 0x5000, 0x2000, CRC(16eef583) SHA1(277252bd716dd96d5b98ec5e33a3a6a3bc1a9abf))
ROM_LOAD("136025.103", 0x8000, 0x4000, CRC(bf192284) SHA1(4c2dc3ba75122e521ebf2c42f89b31737613c2df))
ROM_LOAD("136025.104", 0xc000, 0x4000, CRC(833c5d4e) SHA1(932861b2a329172247c1a5d0a6498a00a1fce814))
ROM_LOAD("136025.101", 0x10000, 0x4000, CRC(2b3b591f) SHA1(39fd6fdd14367906bc0102bde15d509d3289206b))
ROM_LOAD("136025.109", 0x14000, 0x4000, CRC(4d766827) SHA1(7697bf6f92bff0e62850ed75ff66008a08583ef7))
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("136025.106", 0x0000, 0x4000, CRC(2ca83c76) SHA1(cc1adca32f70af30c4590e9fd6b056b051ccdb38))
ROM_LOAD("136025.107", 0x4000, 0x4000, CRC(5f81c5f3) SHA1(be4055727a2d4536e37ec20150deffdb5af5b01f))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136025.108", 0x8000, 0x4000, CRC(93faf210) SHA1(7744368a1d520f986d1c4246113a7e24fcdd6d04))
ROM_RELOAD(0x0c000, 0x4000) /* reset+interrupt vectors */
// AVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(mhavocrv)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("136025.210", 0x5000, 0x2000, CRC(c67284ca) SHA1(d9adad80c266d36429444f483cac4ebcf1fec7b8))
ROM_LOAD("136025.916", 0x8000, 0x4000, CRC(1255bd7f) SHA1(e277fe7b23ce8cf1294b6bfa5548b24a6c8952ce))
ROM_LOAD("136025.917", 0xc000, 0x4000, CRC(21889079) SHA1(d1ad6d9fa1432912e376bca50ceeefac2bfd6ac3))
ROM_LOAD("136025.915", 0x10000, 0x4000, CRC(4c7235dc) SHA1(67cafc2ce438ec389550efb46c554a7fe7b45efc))
ROM_LOAD("136025.918", 0x14000, 0x4000, CRC(84735445) SHA1(21aacd862ce8911d257c6f48ead119ee5bb0b60d))
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("136025.106", 0x0000, 0x4000, CRC(2ca83c76) SHA1(cc1adca32f70af30c4590e9fd6b056b051ccdb38))
ROM_LOAD("136025.907", 0x4000, 0x4000, CRC(4deea2c9) SHA1(c4107581748a3f2d2084de2a4f120abd67a52189))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136025.908", 0x8000, 0x4000, CRC(c52ec664) SHA1(08120a385f71b17ec02a3c2ef856ff835a91773e))
ROM_RELOAD(0x0c000, 0x4000) /* reset+interrupt vectors */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END


ROM_START(mhavocp)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("136025.010", 0x5000, 0x2000, CRC(3050c0e6) SHA1(f19a9538996d949cdca7e6abd4f04e8ff6e0e2c1))
ROM_LOAD("136025.016", 0x8000, 0x4000, CRC(94caf6c0) SHA1(8734411280bd0484c99a59231b97ad64d6e787e8))
ROM_LOAD("136025.017", 0xc000, 0x4000, CRC(5cba70a) SHA1(c069e6dec3e5bc278103156d0908ab93f3784be1))
ROM_LOAD("136025.015", 0x10000, 0x4000, CRC(c567c11b) SHA1(23b89389f59bb6a040342adfe583818a91ce5bff))
ROM_LOAD("136025.018", 0x14000, 0x4000, CRC(a8c35ccd) SHA1(c243a5407557390a64c6560d857f5031f839973f))
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("136025.006", 0x0000, 0x4000, CRC(e272ed41) SHA1(0de395d1c4300a64da7f45746d7b550779e36a21))
ROM_LOAD("136025.007", 0x4000, 0x4000, CRC(e152c9d8) SHA1(79d0938fa9ad262c7f28c5a8ad21004a4dec9ed8))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136025.008", 0x8000, 0x4000, CRC(22ea7399) SHA1(eeda8cc40089506063835a62c3273e7dd3918fd5))
ROM_RELOAD(0x0c000, 0x4000) /* reset+interrupt vectors */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(mhavoc)
ROM_REGION(0x22000, REGION_CPU1, 0)
/* Vector Generator ROM */
ROM_LOAD("136025.210", 0x5000, 0x2000, CRC(c67284ca) SHA1(d9adad80c266d36429444f483cac4ebcf1fec7b8))
/* Program ROM */
ROM_LOAD("136025.216", 0x8000, 0x4000, CRC(522a9cc0) SHA1(bbd75e01c45220e1c87bd1e013cf2c2fb9f376b2))
ROM_LOAD("136025.217", 0xc000, 0x4000, CRC(ea3d6877) SHA1(27823c1b546c073b37ff11a8cb25312ea71673c2))
/* Paged Program ROM */
ROM_LOAD("136025.215", 0x10000, 0x4000, CRC(a4d380ca) SHA1(c3cdc76054be2f904b1fb6f28c3c027eba5c3a70))
ROM_LOAD("136025.318", 0x14000, 0x4000, CRC(ba935067) SHA1(05ad81e7a1982b9d8fddb48502546f48b5dc21b7))
/* Paged Vector Generator ROM */
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("136025.106", 0x0000, 0x4000, CRC(2ca83c76) SHA1(cc1adca32f70af30c4590e9fd6b056b051ccdb38))
ROM_LOAD("136025.107", 0x4000, 0x4000, CRC(5f81c5f3) SHA1(be4055727a2d4536e37ec20150deffdb5af5b01f))
//GAMMA CPU
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136025.108", 0x8000, 0x4000, CRC(93faf210) SHA1(7744368a1d520f986d1c4246113a7e24fcdd6d04))
//ROM_LOAD("136025.108", 0x0c000, 0x4000) /* reset+interrupt vectors */
 ROM_RELOAD(               0x0c000, 0x4000 ) /* reset+interrupt vectors */
// AVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(mhavocpe)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("mhpe101.6kl", 0x5000, 0x2000, CRC(ddcaab40) SHA1(d70b91137204ebbf2685fe22a3498cb6014a7bef))
ROM_LOAD("mhpe101.1mn", 0x8000, 0x4000, CRC(8b4b1c7c) SHA1(9840532b78f5ca7e9785d35883f191b46d0e1436))
ROM_LOAD("mhpe101.1l", 0xc000, 0x4000, CRC(90348169) SHA1(58227bd56d123aa76c8f287f75b83a3a7cc6d1d2))
ROM_LOAD("mhpe101.1q", 0x10000, 0x8000, CRC(d5d86868) SHA1(ddc6330a55106dadefa356b55f1562affb9ebc00))
ROM_LOAD("mhpe101.1np", 0x18000, 0x8000, CRC(6f7b38a9) SHA1(0107cc88a54780c6bca97afbf0e99adf1f4ceba1))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("mhpe101.9s", 0x8000, 0x4000, CRC(46cdd0fa) SHA1(f6c75333311279b15cb42ce3ed8a40e54a508869))
ROM_RELOAD(0x0c000, 0x4000) /* reset+interrupt vectors */
ROM_REGION(0x10000, REGION_CPU3, 0)
ROM_LOAD("mhpe101.6h", 0x0000, 0x4000, CRC(6b380183) SHA1(2fad9dc301aa5622195e3acac2865339406ccc38))
ROM_LOAD("mhpe101.6jk", 0x4000, 0x4000, CRC(7ecfd43b) SHA1(7f99fde09062bc4ca4bbbf06f1b11dc3f5203541))
ROM_REGION(0x40000, REGION_CPU4, 0) // Speech Rom
ROM_LOAD("mhpe101.x1", 0x0000, 0x40000, CRC(aade65d1) SHA1(7e379ee84ee36395095ce68d1c1da4fcf907a07e))
// AVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6c", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END


// Major Havoc (Revision 3)
AAE_DRIVER_BEGIN(drv_mhavoc, "mhavoc", "Major Havoc (Revision 3)")
AAE_DRIVER_ROM(rom_mhavoc)
AAE_DRIVER_FUNCS(&init_mhavoc, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_mhavoc)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Alpha 6502 @ 2.5 MHz with interrupts
	AAE_CPU_ENTRY(
		CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
		AlphaRead, AlphaWrite, nullptr, nullptr, nullptr, nullptr
	),
	// CPU1: Gamma 6502 @ 1.25 MHz, no interrupts
	AAE_CPU_ENTRY(
		CPU_M6502, 1250000, 400, 100, INT_TYPE_NONE, nullptr,
		GammaRead, GammaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 341, 0, 260)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()


// Major Havoc (Revision 2)
AAE_DRIVER_BEGIN(drv_mhavoc2, "mhavoc2", "Major Havoc (Revision 2)")
AAE_DRIVER_ROM(rom_mhavoc2)
AAE_DRIVER_FUNCS(&init_mhavoc, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_mhavoc)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
		AlphaRead, AlphaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_ENTRY(
		CPU_M6502, 1250000, 400, 100, INT_TYPE_NONE, nullptr,
		GammaRead, GammaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 341, 0, 260)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()

// Major Havoc (Return To VAX - Mod by Jeff Askey)
AAE_DRIVER_BEGIN(drv_mhavocrv, "mhavocrv", "Major Havoc (Return To VAX - Mod by Jeff Askey)")
AAE_DRIVER_ROM(rom_mhavocrv)
AAE_DRIVER_FUNCS(&init_mhavoccrv, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_mhavoc)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
		AlphaRead, AlphaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_ENTRY(
		CPU_M6502, 1250000, 400, 100, INT_TYPE_NONE, nullptr,
		GammaRead, GammaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 341, 0, 260)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()

// Major Havoc (The Promised End 1.01 adpcm)
AAE_DRIVER_BEGIN(drv_mhavocpe, "mhavocpe", "Major Havoc (The Promised End 1.01 adpcm)")
AAE_DRIVER_ROM(rom_mhavocpe)
AAE_DRIVER_FUNCS(&init_mhavocpe, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_mhavoc)
//AAE_DRIVER_SAMPLES(mhavocpe_samples)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
		AlphaRead, AlphaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_ENTRY(
		CPU_M6502, 1250000, 400, 100, INT_TYPE_NONE, nullptr,
		GammaRead, GammaWrite, nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 335, 0, 260)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()

// Major Havoc (Prototype)
AAE_DRIVER_BEGIN(drv_mhavocp, "mhavocp", "Major Havoc (Prototype)")
AAE_DRIVER_ROM(rom_mhavocp)
AAE_DRIVER_FUNCS(&init_mhavoc, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_mhavocp)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPUS(
AAE_CPU_ENTRY(
CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
AlphaRead, AlphaWrite, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_ENTRY(
CPU_M6502, 1250000, 400, 100, INT_TYPE_NONE, nullptr,
GammaRead, GammaWrite, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)
AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 341, 0, 260)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()


// Alpha One (Major Havoc Prototype - 3 Lives)
AAE_DRIVER_BEGIN(drv_alphaone, "alphaone", "Alpha One (Major Havoc Prototype - 3 Lives)")
AAE_DRIVER_ROM(rom_alphaone)
AAE_DRIVER_FUNCS(&init_alphone, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_alphaone)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
// Single 6502 using Alpha One maps
AAE_CPU_ENTRY(
CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
AlphaOneRead, AlphaOneWrite, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 512, 2, 384)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()


// Alpha One (Major Havoc Prototype - 5 Lives)
AAE_DRIVER_BEGIN(drv_alphaonea, "alphaonea", "Alpha One (Major Havoc Prototype - 5 Lives)")
AAE_DRIVER_ROM(rom_alphaonea)
AAE_DRIVER_FUNCS(&init_alphone, &run_mhavoc, &end_mhavoc)
AAE_DRIVER_INPUT(input_ports_alphaone)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
AAE_CPU_ENTRY(
CPU_M6502, 2500000, 400, 100, INT_TYPE_INT, &mhavoc_interrupt,
AlphaOneRead, AlphaOneWrite, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(50, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 512, 2, 384)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x4000, 0x1000)
AAE_DRIVER_NVRAM(mhavoc_nvram_handler)
AAE_DRIVER_END()


AAE_REGISTER_DRIVER(drv_mhavoc)
AAE_REGISTER_DRIVER(drv_mhavoc2)
AAE_REGISTER_DRIVER(drv_mhavocrv)
AAE_REGISTER_DRIVER(drv_mhavocpe)
AAE_REGISTER_DRIVER(drv_mhavocp)
AAE_REGISTER_DRIVER(drv_alphaone)
AAE_REGISTER_DRIVER(drv_alphaonea)

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
