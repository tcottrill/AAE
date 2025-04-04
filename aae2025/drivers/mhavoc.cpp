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


#include "mhavoc.h"
#include "aae_mame_driver.h"
#include "aae_avg.h"
#include "pokyintf.h"
#include "earom.h"
#include "5220intf.h"
#include "loaders.h"
#include "timer.h"
#include "mhavoc_custom_video.h"
#include "glcode.h"

#pragma warning( disable :  4244)

#define mh_debug 0
#define MHAVOC_CLOCK		10000000
#define MHAVOC_CLOCK_2_5M	(MHAVOC_CLOCK/4)
#define MHAVOC_CLOCK_1_25M	(MHAVOC_CLOCK/8)

static struct POKEYinterface pokey_interface_alphaone =
{
	2,			/* 4 chips */
	1250000,
	255,	/* volume */
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
	255,	/* volume */
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

static UINT8 alpha_data = 0;
static UINT8 alpha_rcvd = 0;
static UINT8 alpha_xmtd = 0;

static UINT8 gamma_data = 0;
static UINT8 gamma_rcvd = 0;
static UINT8 gamma_xmtd = 0;

static UINT8 speech_write_buffer;
static UINT8 player_1;
int has_gamma_cpu = 1;

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
		pokey1_w(pokey_reg, data);
	else
		pokey2_w(pokey_reg, data);
}

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

int mhavoc_sh_start(void)
{
	int rv;
	
	if (has_gamma_cpu) rv = pokey_sh_start(&pokey_interface);
	else rv = pokey_sh_start(&pokey_interface_alphaone);
	
	if (gamenum == MHAVOCRV) { tms5220_sh_start(&tms5220_interface); }
	
	return rv;
}

void mhavoc_sh_stop(void)
{
	pokey_sh_stop();
	if (gamenum == MHAVOCRV)
	tms5220_sh_stop();
}

void mhavoc_sh_update()
{
	pokey_sh_update();
	if (gamenum == MHAVOCRV) 
	 tms5220_sh_update();
}

void end_mhavoc()
{
	wrlog("End Major Havoc Called");
	if (has_gamma_cpu == 0) { save_hi_aae(0x1800, 0x100, 0); }
	mhavoc_sh_stop();
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
	cpu_reset(0);

	if (has_gamma_cpu)
	{
		cpu_reset(1);
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
			//wrlog("IRQ ALPHA CPU");
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
			//wrlog("IRQ GAMMA CPU");
			cpu_do_int_imm(CPU1, INT_TYPE_INT);
		}
	}
}

WRITE_HANDLER(mhavoc_alpha_irq_ack_w)
{
	/* clear the line and reset the clock */
	m_cpu_6502[CPU0]->m6502clearpendingint();
	cpu_clear_pending_int(INT_TYPE_INT, CPU0);

	//wrlog("Alpha IRQ ACK!", data);
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
}

WRITE_HANDLER(mhavoc_gamma_irq_ack_w)
{
	/* clear the line and reset the clock */
	//wrlog("Gamma IRQ ACK!", data);
	m_cpu_6502[CPU1]->m6502clearpendingint();
	cpu_clear_pending_int(INT_TYPE_INT, CPU1);
	gamma_irq_clock = 0;
}

WRITE_HANDLER(avgdvg_reset_w)
{
	wrlog("---------------------------AVGDVG RESET ------------------------");
	total_length = 0;
}

WRITE_HANDLER(avg_mgo)
{
	if (MHAVGDONE == 0) { return; }

	mhavoc_video_update();

	if (total_length > 10)
	{
		MHAVGDONE = 0;
		// Clear the video tick count.
		get_video_ticks(0xff);
		// sweep = 3.75 * total_length;
		sweep = 2.268 * total_length;
		//sweep = (float)(TIME_IN_NSEC(1500) * total_length) * 1250000;//1512000;// driver[gamenum].cpu_freq[CPU0];
		//wrlog("Total Time in cycles for video  %f, total_length %d", sweep, total_length);
		
		if (config.debug_profile_code) {
			wrlog("Sweep Timer %f", sweep);
		}
	}
	else { MHAVGDONE = 1; }
}

WRITE_HANDLER(mhavoc_out_0_w)
{
	if (gamenum == MHAVOCPE)
	{
		avg_set_flip_x(data & 0x40);
		avg_set_flip_y(data & 0x80);
	}

	if (!(data & 0x08))
	{
		//wrlog ("\t\t\t\t*** resetting gamma processor. ***\n");
		alpha_rcvd = 0;
		alpha_xmtd = 0;
		gamma_rcvd = 0;
		gamma_xmtd = 0;
		cpu_reset(1);   //RESET GAMMA CPU
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

// Simulates frequency and vector halt
READ_HANDLER(mhavoc_port_0_r)
{
	UINT8 res;

	if (!MHAVGDONE)
	{
		if ((get_video_ticks(0) > sweep) && MHAVGDONE == 0)
		{
			MHAVGDONE = 1;
			total_length = 0;
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
	if (gamenum == MHAVOCRV || gamenum == MHAVOCPE)
	{
		if (!tms5220_ready_r())	res |= 0x04;
		else
			res &= ~0x04;
		//wrlog("TMS Ready Data %d", tms5220_ready_r());
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
		if ((get_video_ticks(0) > sweep) && MHAVGDONE == 0)
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
	if (gamenum == MHAVOCPE)
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
	//wrlog("Ram write from Gamma CPU address:%x data:%x",address,data);
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
	if (!has_gamma_cpu) 
	{
		watchdog_reset_w(0, 0, 0);	
	}
		
	mhavoc_sh_update();
	
	if (gamenum == MHAVOCRV)  tms5220_sh_update(); 
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

int init_mhavoc(void)
{
	if (gamenum == ALPHAONE || gamenum == ALPHAONA) has_gamma_cpu = 0; else has_gamma_cpu = 1;

	total_length = 1;
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
	
	if (has_gamma_cpu) 
	{
		init6502(AlphaRead, AlphaWrite, 0xffff, CPU0);
		init6502(GammaRead, GammaWrite, 0xbfff, CPU1);

	}
	else 
	{
		init6502(AlphaOneRead, AlphaOneWrite, 0xffff, CPU0);
		memset(Machine->memory_region[CPU0] + 0x1800, 0xff, 0xff);
		//Load High Score table for Alpha One
		if (has_gamma_cpu == 0) { load_hi_aae(0x1800, 0x100, 0); }
	}
	mhavoc_sh_start();
	if (has_gamma_cpu)
		mhavoc_video_init(3);
	else
		mhavoc_video_init(2);
	wrlog("MHAVOC CPU init complete");
	return 0;
}

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
