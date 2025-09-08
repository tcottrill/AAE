#define NOMINMAX
#include "bosco.h"
//#include "bosco_vid.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "namco.h"
#include "timer.h"

// audio + resampling
#include "mixer.h"
#include "wav_resample.h"
#include <vector>
#include <string>


static const char* bosco_samples[] =
{
	"bosco.zip",
	"midbang.wav",
	"bigbang.wav",
	"shot.wav",
	0	/* end of array */
};


// Prebuilt sample IDs
static int s_bosco_blastOff = -1;
static int s_bosco_alertAlert = -1;
static int s_bosco_battleStation = -1;
static int s_bosco_spyShipSighted = -1;
static int s_bosco_conditionRed = -1;

static UINT8 interrupt_enable_1 = 1;
static UINT8 interrupt_enable_2 = 0;
static UINT8 interrupt_enable_3 = 0;
int bosco_irq_clock = 0;
static int nmi_timer = -1;
static int flipscreen = 0;

//Video Pointers
#define MAX_STARS 250
#define STARS_COLOR_BASE 32

struct starb
{
	int x, y, col, set;
};

static struct starb stars[MAX_STARS];
static int total_stars;

unsigned char* bosco_sharedram;
unsigned char* bosco_videoram2;
unsigned char* bosco_colorram2;

unsigned char* bosco_staronoff;
unsigned char* bosco_starblink;
static unsigned int bstars_scrollx;
static unsigned int bstars_scrolly;
static unsigned char bosco_scrollx;
static unsigned char bosco_scrolly;
static unsigned char bosco_starcontrol;
static int displacement;

unsigned char* bosco_radarx;
unsigned char* bosco_radary;
unsigned char* bosco_radarattr;

int bosco_radarram_size = 0x0b;
static int		HiScore = 0;
int		Score = 0;
int Score1 = 0;
int Score2 = 0;
int		NextBonus, NextBonus1, NextBonus2;
int		FirstBonus, IntervalBonus;

static int nmi_timer_1 = -1;
static int nmi_timer_2 = -1;

static struct rectangle spritevisiblearea =
{
	0 * 8 + 3, 28 * 8 - 1,
	0 * 8, 28 * 8 - 1
};

static struct rectangle spritevisibleareaflip =
{
	8 * 8, 36 * 8 - 1 - 3,
	0 * 8, 28 * 8 - 1
};

static struct rectangle radarvisiblearea =
{
	28 * 8, 36 * 8 - 1,
	0 * 8, 28 * 8 - 1
};

static struct rectangle radarvisibleareaflip =
{
	0 * 8, 8 * 8 - 1,
	0 * 8, 28 * 8 - 1
};

const rectangle visible_area =
{
 0,
 224,
 0,
 288
};

//This bosco Uses the NAMCO Interface
static struct namco_interface bosco_namco_interface =
{
	3072000 / 32,	// sample rate
	4,			// number of voices
	32,			// gain adjustment
	245			// playback volume
};


static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 characters */
	256,	/* 256 characters */
	2,	/* 2 bits per pixel */
	{ 0, 4 },      /* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },   /* bits are packed in groups of four */
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },   /* characters are rotated 90 degrees */
	16 * 8	       /* every char takes 16 bytes */
};

static struct GfxLayout spritelayout =
{
	16,16,		/* 16*16 sprites */
	64,		/* 128 sprites */
	2,		/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
			24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3, 0, 1, 2, 3  },
	{ 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
			32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8 },
	64 * 8	/* every sprite takes 64 bytes */
};

static struct GfxLayout dotlayout =
{
	4,4,	/* 4*4 characters */
	8,	/* 8 characters */
	2,	/* 2 bits per pixel */
	{ 6, 7 },
	{ 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
	{ 3 * 32, 2 * 32, 1 * 32, 0 * 32 },
	16 * 8	/* every char takes 16 consecutive bytes */
};

struct GfxDecodeInfo bosco_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &charlayout,	        0, 64 },
	{ REGION_GFX2, 0, &spritelayout,	 64 * 4, 64 },
	{ REGION_GFX3, 0, &dotlayout,    64 * 4 + 64 * 4,	1 },
	{ -1 } /* end of array */
};

static inline uint8_t expand4to8(uint8_t nibble)
{
	nibble &= 0x0F;
	return static_cast<uint8_t>((nibble << 4) | nibble);
}

// Build one 16-bit/44.1 kHz sample from a slice of decoded 8-bit unsigned PCM.
static int build_resampled_sample_from_u8(const uint8_t* u8, int srcFrames,
	int srcHz, const std::string& name)
{
	if (!u8 || srcFrames <= 0 || srcHz <= 0) return -1;

	// 1) Convert u8 (0..255, mid=128) to s16 mono at srcHz
	std::vector<int16_t> s16_src(srcFrames);
	for (int i = 0; i < srcFrames; ++i)
		s16_src[i] = Make16bit(u8[i]); // declared in mixer.h

	// 2) Compute output length for 44.1 kHz
	const int dstHz = 44100;
	const int dstFrames = static_cast<int>((int64_t)srcFrames * dstHz + srcHz / 2) / srcHz;

	// 3) Create an empty mono 16-bit sample at 44.1k.
	//    NOTE: create_sample returns a "sound id". Convert it to the registry
	//    index (samplenum) with snumlookup(); that's what sample_start() expects.
	const int sound_id = create_sample(/*bits*/16, /*stereo*/false, dstHz, dstFrames, name);
	if (sound_id < 0) return -1;
	const int samplenum = snumlookup(sound_id);
	if (samplenum < 0) return -1;
	
	// 4) Cubic-resample into a temporary buffer, then upload via mixer_upload_sample16.
	std::vector<int16_t> s16_dst(dstFrames);
	cubic_interpolation_16_into(s16_src.data(), (int32_t)s16_src.size(),
	s16_dst.data(), (int32_t)dstFrames);
	
	if (mixer_upload_sample16(samplenum, s16_dst.data(),
			(uint32_t)dstFrames, dstHz, /*stereo=*/false) != 0)
		 return -1;
	
		// Return the samplenum (registry index) so callers can pass it to sample_start(...)
		return samplenum;
}

// Decode ROM, slice phrases, create & resample to 44.1 kHz; run once at init.
static void bosco_build_voice_samples()
{
	const uint8_t* rom = memory_region(REGION_SOUND2);
	const int      rlen = (int)0x3000;

	if (!rom || rlen <= 0) return;

	// 1) nibble-decode to unsigned 8-bit mono (length doubles)
	std::vector<uint8_t> speech8(rlen * 2);
	for (int i = 0; i < rlen; ++i) {
		const uint8_t b = rom[i];
		speech8[2 * i + 0] = expand4to8(b & 0x0F);
		speech8[2 * i + 1] = expand4to8((b >> 4) & 0x0F);
	}

	// 2) slice table - note: (values already *2); each entry in FRAMES
	struct Slice { int start; int length; const char* name; };
	static const Slice ss[] = {
		{ 0x0020 * 2, 0x08D7 * 2, "bosco_blast_off"      }, // Blast Off
		{ 0x08F7 * 2, 0x0906 * 2, "bosco_alert_alert"    }, // Alert, Alert
		{ 0x11FD * 2, 0x07DD * 2, "bosco_battle_station" }, // Battle Station
		{ 0x19DA * 2, 0x07DE * 2, "bosco_spy_ship"       }, // Spy Ship Sighted
		{ 0x21B8 * 2, 0x079F * 2, "bosco_condition_red"  }  // Condition Red
	};

	// 3) build five samples (source rate -> 44.1k)
	for (const auto& sl : ss) {
		const int s0 = std::max(0, std::min((int)speech8.size(), sl.start));
		const int n = std::max(0, std::min((int)speech8.size() - s0, sl.length));
		if (n <= 0) continue;

		const uint8_t* u8 = &speech8[s0];
		const int sn = build_resampled_sample_from_u8(u8, n, 4000, sl.name);

		if (std::string(sl.name) == "bosco_blast_off")       s_bosco_blastOff = sn;
		else if (std::string(sl.name) == "bosco_alert_alert")     s_bosco_alertAlert = sn;
		else if (std::string(sl.name) == "bosco_battle_station")  s_bosco_battleStation = sn;
		else if (std::string(sl.name) == "bosco_spy_ship")        s_bosco_spyShipSighted = sn;
		else if (std::string(sl.name) == "bosco_condition_red")   s_bosco_conditionRed = sn;
	}
}



void bosco_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	int i;
#define TOTAL_COLORS(gfxn) (Machine->gfx[gfxn]->total_colors * Machine->gfx[gfxn]->color_granularity)
#define COLOR(gfxn,offs) (colortable[Machine->drv->gfxdecodeinfo[gfxn].color_codes_start + offs])


	for (i = 0; i < 32; i++)
	{
		int bit0, bit1, bit2;


		bit0 = (color_prom[31 - i] >> 0) & 0x01;
		bit1 = (color_prom[31 - i] >> 1) & 0x01;
		bit2 = (color_prom[31 - i] >> 2) & 0x01;
		palette[3 * i] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = (color_prom[31 - i] >> 3) & 0x01;
		bit1 = (color_prom[31 - i] >> 4) & 0x01;
		bit2 = (color_prom[31 - i] >> 5) & 0x01;
		palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		bit0 = 0;
		bit1 = (color_prom[31 - i] >> 6) & 0x01;
		bit2 = (color_prom[31 - i] >> 7) & 0x01;
		palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
	}

	/* characters / sprites */
	for (i = 0; i < 64 * 4; i++)
	{
		colortable[i] = 15 - (color_prom[i + 32] & 0x0f);	/* chars */
		colortable[i + 64 * 4] = 15 - (color_prom[i + 32] & 0x0f) + 0x10;	/* sprites */
		if (colortable[i + 64 * 4] == 0x10) colortable[i + 64 * 4] = 0;	/* preserve transparency */
	}

	/* radar dots lookup table */
	/* they use colors 0-3, I think */
	for (i = 0; i < 4; i++)
		COLOR(2, i) = i;

	/* now the stars */
	for (i = 32; i < 32 + 64; i++)
	{
		int bits;
		int map[4] = { 0x00, 0x47, 0x97 ,0xde };//{ 0x00, 0x88, 0xcc, 0xff };

		bits = ((i - 32) >> 0) & 0x03;
		palette[3 * i] = map[bits];
		bits = ((i - 32) >> 2) & 0x03;
		palette[3 * i + 1] = map[bits];
		bits = ((i - 32) >> 4) & 0x03;
		palette[3 * i + 2] = map[bits];
	}

}

//////////////////////////////////////////////////////////////
//MAIN bosco HANDLERS
//////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////
//MAIN bosco HANDLERS
//////////////////////////////////////////////////////////////


WRITE_HANDLER(bosco_flipscreen_w)
{
	if (flipscreen != (~data & 1))
	{
		flipscreen = ~data & 1;
	}
}

WRITE_HANDLER(bosco_sound_w)
{
	namco_sound_w(address & 0x1f, data);
}

WRITE_HANDLER(bosco_interrupt_enable_1_w)
{
	interrupt_enable_1 = data & 1;
}

WRITE_HANDLER(bosco_interrupt_enable_2_w)
{
	interrupt_enable_2 = data & 1;
}

WRITE_HANDLER(bosco_interrupt_enable_3_w)
{
	interrupt_enable_3 = !(data & 1);
}



WRITE_HANDLER(bosco_halt_w)
{
	if (data & 1)
	{
		//cpu_set_reset_line(1, CLEAR_LINE);
		//cpu_set_reset_line(2, CLEAR_LINE);
	}
	else
	{
		cpu_needs_reset(1);
		cpu_needs_reset(2);
	}
}

WRITE_HANDLER(boscohaltw)
{
	static int reset23;

	data &= 1;
	if (data && !reset23)
	{
		cpu_needs_reset(1);
		cpu_needs_reset(2);
		cpu_enable(CPU1, 1);
		cpu_enable(CPU2, 1);

		//LOG_INFO("CPUS ENABLED");

	}
	else if (!data)
	{
		cpu_enable(CPU1, 0);
		cpu_enable(CPU2, 0);
		//LOG_INFO("CPUS ENABLED without reset");
	}

	reset23 = data;
}


void bosco_nmi_generate_1(int param)
{
	cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	//LOG_INFO("NMI CPU0");
}


void bosco_nmi_generate_2(int param)
{
	cpu_do_int_imm(CPU1, INT_TYPE_NMI);
	//LOG_INFO("NMI CPU1");
}


READ_HANDLER(bosco_dsw_r)
{
	int bit0, bit1;
	int offset = address;

	bit0 = (input_port_0_r(0) >> offset) & 1;
	bit1 = (input_port_1_r(0) >> offset) & 1;

	return bit0 | (bit1 << 1);
}


WRITE_HANDLER(bosco_scrollx_w)
{
	bosco_scrollx = data;
}

WRITE_HANDLER(bosco_scrolly_w)
{
	bosco_scrolly = data;
}

WRITE_HANDLER(bosco_starcontrol_w)
{
	bosco_starcontrol = data;
}

WRITE_HANDLER(bosco_sharedram_w)
{
	bosco_sharedram[address] = data;
}

READ_HANDLER(bosco_sharedram_r)
{
	return bosco_sharedram[address];
}


/***************************************************************************

Emulate the custom IO chip.

***************************************************************************/
static int customio_command_1 = 0;
static int customio_command_2 = 0;
static int mode, credits;
static int coinpercred, credpercoin;
static unsigned char customio_1[16] = { 0 };
static unsigned char customio_2[16] = { 0 };;

WRITE_HANDLER(bosco_customio_data_1_w)
{
	int offset = address & 0x0f;
	customio_1[offset] = data;

	//LOG_INFO("Customio_1 Write offset %d, data %x customio_command_1 is %x", offset, data, customio_command_1);

	switch (customio_command_1)
	{
	case 0x48:
		if (offset == 1)
		{
			switch (customio_1[0])
			{
			case 0x20:	 //		Mid Bang
				sample_start(0, 0, 0);
				break;
			case 0x10:	 //		Big Bang
				sample_start(1, 1, 0);
				break;
			case 0x50:	 //		Shot
				sample_start(2, 2, 0);
				break;
			}
		}
		break;

	case 0x64:
		if (offset == 0)
		{
			switch (customio_1[0])
			{
			case 0x60:	/* 1P Score */
				Score2 = Score;
				Score = Score1;
				NextBonus2 = NextBonus;
				NextBonus = NextBonus1;
				break;
			case 0x68:	/* 2P Score */
				Score1 = Score;
				Score = Score2;
				NextBonus1 = NextBonus;
				NextBonus = NextBonus2;
				break;
			case 0x81:
				Score += 10;
				break;
			case 0x83:
				Score += 20;
				break;
			case 0x87:
				Score += 50;
				break;
			case 0x88:
				Score += 60;
				break;
			case 0x89:
				Score += 70;
				break;
			case 0x8D:
				Score += 200;
				break;
			case 0x93:
				Score += 200;
				break;
			case 0x95:
				Score += 300;
				break;
			case 0x96:
				Score += 400;
				break;
			case 0x98:
				Score += 600;
				break;
			case 0x9A:
				Score += 800;
				break;
			case 0xA0:
				Score += 500;
				break;
			case 0xA1:
				Score += 1000;
				break;
			case 0xA2:
				Score += 1500;
				break;
			case 0xA3:
				Score += 2000;
				break;
			case 0xA5:
				Score += 3000;
				break;
			case 0xA6:
				Score += 4000;
				break;
			case 0xA7:
				Score += 5000;
				break;
			case 0xA8:
				Score += 6000;
				break;
			case 0xA9:
				Score += 7000;
				break;
			case 0xB7:
				Score += 100;
				break;
			case 0xB8:
				Score += 120;
				break;
			case 0xB9:
				Score += 140;
				break;
			default:
				LOG_INFO("unknown score: %02x", customio_1[0]);
				break;
			}
		}
		break;
		
	case 0x84:
		if (offset == 2)
		{
			int hi = (data / 16);
			int mid = (data % 16);
			if (customio_1[1] == 0x20)
				FirstBonus = (hi * 100000) + (mid * 10000);
			if (customio_1[1] == 0x30)
				IntervalBonus = (hi * 100000) + (mid * 10000);
		}
		else if (offset == 3)
		{
			int lo = (data / 16);
			if (customio_1[1] == 0x20)
				FirstBonus = FirstBonus + (lo * 1000);
			if (customio_1[1] == 0x30)
				IntervalBonus = IntervalBonus + (lo * 1000);
		}
		break;
	}
}

READ_HANDLER(bosco_customio_data_1_r)
{
	int offset = address & 0x0f;
	switch (customio_command_1)
	{
	case 0x71:
		if (offset == 0)
		{
			int p4 = readinputport(4);

			/* check if the user inserted a coin */
			if ((p4 & 0x10) == 0 && credits < 99)
				credits++;

			/* check if the user inserted a coin */
			if ((p4 & 0x20) == 0 && credits < 99)
				credits++;

			/* check if the user inserted a coin */
			if ((p4 & 0x40) == 0 && credits < 99)
				credits++;

			/* check for 1 player start button */
			if ((p4 & 0x04) == 0 && credits >= 1)
				credits--;

			/* check for 2 players start button */
			if ((p4 & 0x08) == 0 && credits >= 2)
				credits -= 2;

			if (mode)	/* switch mode */
				return (p4 & 0x80);
			else	/* credits mode: return number of credits in BCD format */
				return (credits / 10) * 16 + credits % 10;
		}
		else if (offset == 1)
		{
			int in = readinputport(2), dir;

			/*
				  Direction is returned as shown below:
								0
							7		1
						6				2
							5		3
								4
				  For the previous direction return 8.
			 */
			dir = 8;
			if ((in & 0x01) == 0)		/* up */
			{
				if ((in & 0x02) == 0)	/* right */
					dir = 1;
				else if ((in & 0x08) == 0) /* left */
					dir = 7;
				else
					dir = 0;
			}
			else if ((in & 0x04) == 0)	/* down */
			{
				if ((in & 0x02) == 0)	/* right */
					dir = 3;
				else if ((in & 0x08) == 0) /* left */
					dir = 5;
				else
					dir = 4;
			}
			else if ((in & 0x02) == 0)	/* right */
				dir = 2;
			else if ((in & 0x08) == 0) /* left */
				dir = 6;

			/* check fire (both impulse and hold, boscomd2 has autofire) */
			dir |= (in & 0x30);

			return dir;
		}
		break;

	case 0x94:
		if (offset == 0)
		{
			int flags = 0;
			int lo = (Score / 1000000) % 10;
			if (Score >= HiScore)
			{
				HiScore = Score;
				flags |= 0x80;
			}
			if (Score >= NextBonus)
			{
				if (NextBonus == FirstBonus)
				{
					NextBonus = IntervalBonus;
					flags |= 0x40;
				}
				else
				{
					NextBonus += IntervalBonus;
					flags |= 0x20;
				}
			}
			return lo | flags;
		}
		else if (offset == 1)
		{
			int hi = (Score / 100000) % 10;
			int lo = (Score / 10000) % 10;
			return (hi * 16) + lo;
		}
		else if (offset == 2)
		{
			int hi = (Score / 1000) % 10;
			int lo = (Score / 100) % 10;
			return (hi * 16) + lo;
		}

		else if (offset == 3)
		{
			int hi = (Score / 10) % 10;
			int lo = Score % 10;
			return (hi * 16) + lo;
		}
		break;

	case 0x91:
		if (offset <= 2)
			return 0;
		break;
	}

	return -1;
}

READ_HANDLER(bosco_customio_1_r)
{
	return customio_command_1;
}

WRITE_HANDLER(bosco_customio_1_w)
{
	//if (data != 0x10)
		//LOG_INFO("%04x: custom IO 1 command %02x", 0, data);

	customio_command_1 = data;

	//if (data == 0x48) { LOG_INFO("48 written to customio_command_1"); }

	switch (data)
	{
	case 0x10:
		if (nmi_timer_1 > -1)
		{
			timer_remove(nmi_timer_1);
			nmi_timer_1 = -1;
		}
		return;

	case 0x61:
		mode = 1;
		break;

	case 0xC1:
		Score = 0;
		Score1 = 0;
		Score2 = 0;
		NextBonus = FirstBonus;
		NextBonus1 = FirstBonus;
		NextBonus2 = FirstBonus;
		break;

	case 0xC8:
		break;

	case 0x84:
		break;

	case 0x91:
		mode = 0;
		break;

	case 0xa1:
		mode = 1;
		break;
	}
	//LOG_INFO("NMI TIMER 1 SET ");
	nmi_timer_1 = timer_set(TIME_IN_USEC(50), CPU0, bosco_nmi_generate_1);
}

WRITE_HANDLER(bosco_customio_data_2_w)
{
	int offset = address;// &0x0f;

	//LOG_INFO("OFFSET for CUSTOMIO  2 is %x", offset);

	customio_2[offset] = data;

	//LOG_INFO("%04x: custom IO 2 offset %02x data %02x\n", 0, offset, data);
	switch (customio_command_2)
	{
	case 0x82:
		if (offset == 2)
		{
			// customio_2[0] selects the phrase 1..5
			sample_set_volume(4,200);
			switch (customio_2[0])
			{
			case 1: // Blast Off
				if (s_bosco_blastOff >= 0)       sample_start(4, s_bosco_blastOff, 0);
				break;
			case 2: // Alert, Alert
				if (s_bosco_alertAlert >= 0)     sample_start(4, s_bosco_alertAlert, 0);
				break;
			case 3: // Battle Station
				if (s_bosco_battleStation >= 0)  sample_start(4, s_bosco_battleStation, 0);
				break;
			case 4: // Spy Ship Sighted
				if (s_bosco_spyShipSighted >= 0) sample_start(4, s_bosco_spyShipSighted, 0);
				break;
			case 5: // Condition Red
				if (s_bosco_conditionRed >= 0)   sample_start(4, s_bosco_conditionRed, 0);
				break;
			}
		}
		break;
	}
}

READ_HANDLER(bosco_customio_data_2_r)
{
	int offset = address & 0x0f;
	//LOG_INFO("OFFSET for CUSTOMIO COMMAND 2 is %x command is %x", offset, customio_command_2);
	switch (customio_command_2)
	{
	case 0x91:
		if (offset == 2)
		{
			//LOG_INFO("WARNING Returning weird mem location %x", GI[CPU0][0x89cd]);
			Machine->memory_region[CPU0][0x89cc];//program_read_byte(0x89cc /cd);  cpu_readmem16??
		}
		else if (offset <= 3)
			return 0;
		break;
	}

	return -1;
}

READ_HANDLER(bosco_customio_2_r)
{
	return customio_command_2;
}

WRITE_HANDLER(bosco_customio_2_w)
{
	customio_command_2 = data;

	switch (data)
	{
	case 0x10:
		if (nmi_timer_2 > -1)
		{
			timer_remove(nmi_timer_2);
			nmi_timer_2 = -1;
		}
		return;
		
			
		break;
	}
	//LOG_INFO("NMI TIMER 2 SET ");
	nmi_timer_2 = timer_set(TIME_IN_USEC(50), CPU1, bosco_nmi_generate_2);
}

void bosco_vh_screenrefresh()
{
	//clearbitmap(myscreen);

	int offs = 0;
	int sx, sy;

	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int flipx, flipy;

		sx = offs % 32;
		sy = offs / 32;
		flipx = ~bosco_colorram2[offs] & 0x40;
		flipy = bosco_colorram2[offs] & 0x80;

		drawgfx(tmpbitmap1, Machine->gfx[0],
			bosco_videoram2[offs],
			bosco_colorram2[offs] & 0x3f,
			flipx, flipy,
			8 * sx, 8 * sy,
			0, TRANSPARENCY_NONE, 0);
		//	}
	}

	/* update radar */
	for (offs = videoram_size - 1; offs >= 0; offs--)
	{
		int flipx, flipy;

		dirtybuffer[offs] = 0;

		sx = (offs % 32) ^ 4;
		sy = offs / 32 - 2;
		flipx = ~colorram[offs] & 0x40;
		flipy = colorram[offs] & 0x80;
		if (flipscreen)
		{
			sx = 7 - sx;
			sy = 27 - sy;
			flipx = !flipx;
			flipy = !flipy;
		}

		drawgfx(tmpbitmap, Machine->gfx[0],
			videoram[offs],
			colorram[offs] & 0x3f,
			flipx, flipy,
			8 * sx, 8 * sy,
			&radarvisibleareaflip, TRANSPARENCY_NONE, 0);

	}
	/* copy the temporary bitmap to the screen */
	{
		int scrollx, scrolly;

		if (flipscreen)
		{
			scrollx = (bosco_scrollx + 32);//- 3*displacement) + 32;
			scrolly = (bosco_scrolly + 16) - 32;
		}
		else
		{
			scrollx = -(bosco_scrollx);
			scrolly = -(bosco_scrolly + 16);
		}

		copyscrollbitmap(main_bitmap, tmpbitmap1, 1, &scrollx, 1, &scrolly, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	}

	/* radar */
	if (flipscreen)
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &radarvisibleareaflip, TRANSPARENCY_NONE, 0);
	else
		copybitmap(main_bitmap, tmpbitmap, 0, 0, 28 * 8, 0, &radarvisiblearea, TRANSPARENCY_NONE, 0);

	/* draw the sprites */
	for (offs = 0; offs < spriteram_size; offs += 2)
	{
		drawgfx(main_bitmap, Machine->gfx[1],
			(spriteram[offs] & 0xfc) >> 2,
			spriteram_2[offs + 1],
			spriteram[offs] & 1, spriteram[offs] & 2,
			spriteram[offs + 1] - 1, 224 - spriteram_2[offs],
			&spritevisiblearea, TRANSPARENCY_THROUGH, 0);
	}

	/* draw the dots on the radar and the bullets */
	for (offs = 0; offs < bosco_radarram_size; offs++)
	{
		int x, y;


		x = bosco_radarx[offs] + ((~bosco_radarattr[offs] & 0x01) << 8) - 2;
		y = 235 - bosco_radary[offs];
		if (flipscreen)
		{
			x -= 1;
			y += 2;
		}

		drawgfx(main_bitmap, Machine->gfx[2],
			((bosco_radarattr[offs] & 0x0e) >> 1) ^ 0x07,
			0,
			flipscreen, flipscreen,
			x, y,
			&Machine->drv->visible_area, TRANSPARENCY_PEN, 3);
	}

	// draw the stars 

	if ((*bosco_staronoff & 1) == 0)
	{
		for (offs = 0; offs < total_stars; offs++)
		{
			int x, y;
			int set;
			int starset[4][2] = { {0,3},{0,1},{2,3},{2,1} };

			x = (stars[offs].x + bstars_scrollx) % 224;
			y = (stars[offs].y + bstars_scrolly) % 224;

			set = (bosco_starblink[0] & 1) + ((bosco_starblink[1] & 1) << 1);

			if (((stars[offs].set == starset[set][0]) ||
				(stars[offs].set == starset[set][1])))
			{
				if (read_pixel(main_bitmap, x, y) == 0) //bpen
				{
					plot_pixel(main_bitmap, x, y, stars[offs].col);
				}
			}
		}
	}

}

int bosco_vh_start(void)
{
	int generator;
	int x, y;
	int set = 0;

	/* precalculate the star background */
	/* this comes from the Galaxian hardware, Bosconian is probably different */
	total_stars = 0;
	generator = 0;

	for (x = 255; x >= 0; x--)
	{
		for (y = 511; y >= 0; y--)
		{
			int bit1, bit2;

			generator <<= 1;
			bit1 = (~generator >> 17) & 1;
			bit2 = (generator >> 5) & 1;

			if (bit1 ^ bit2) generator |= 1;

			if (x >= visible_area.min_x &&
				x <= visible_area.max_x &&
				((~generator >> 16) & 1) &&
				(generator & 0xff) == 0xff)
			{
				int color;

				color = (~(generator >> 8)) & 0x3f;
				if (color && total_stars < MAX_STARS)
				{
					stars[total_stars].x = x;
					stars[total_stars].y = y;
					stars[total_stars].col = Machine->gfx[0]->colortable[color + STARS_COLOR_BASE];
					stars[total_stars].set = set;
					if (++set > 3)
						set = 0;

					total_stars++;
				}
			}
		}
	}
	*bosco_staronoff = 1;

	LOG_INFO("TEMP BITMAP CREATED");
	if ((tmpbitmap = osd_create_bitmap(32 * 8, 32 * 8)) == 0)

		LOG_INFO("TEMP BITMAP 1 CREATED");
	if ((tmpbitmap1 = osd_create_bitmap(32 * 8, 32 * 8)) == 0)
		return 1;

	return generic_vh_start();
}


/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void bosco_vh_stop(void)
{
	osd_free_bitmap(tmpbitmap1);
}

void bosco_vh_interrupt(void)
{
	int speedsx[8] = { -1, -2, -3, 0, 3, 2, 1, 0 };
	int speedsy[8] = { 0, -1, -2, -3, 0, 3, 2, 1 };

	bstars_scrollx += speedsx[bosco_starcontrol & 7];
	bstars_scrolly += speedsy[(bosco_starcontrol & 56) >> 3];
}


void boscoint1()
{
	bosco_vh_interrupt();	/* update the background stars position */
	if (interrupt_enable_1)
	{
		//LOG_INFO("bosco interrupt CPU0 called");
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
	}

}

void boscoint2()
{
	if (interrupt_enable_2)
	{
		//LOG_INFO("bosco interrupt CPU1 called?");
		cpu_do_int_imm(CPU1, INT_TYPE_INT);
	}
}

void boscoint3()
{
	//LOG_INFO("Iterrupt 3 status here is %d", inten3);
	if (interrupt_enable_3)
	{
		cpu_do_int_imm(CPU2, INT_TYPE_NMI);
		//LOG_INFO("bosco interrupt CPU2 called?");
	}
}

void run_bosco()
{
	watchdog_reset_w(0, 0, 0);
	bosco_vh_screenrefresh();
	namco_sh_update();
}

PORT_READ(boscoPortRead)
PORT_END
PORT_WRITE(boscoPortWrite)
PORT_END

MEM_READ(boscoCPU1_Read)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_r)
MEM_ADDR(0x6800, 0x6807, bosco_dsw_r)
MEM_ADDR(0x7000, 0x700f, bosco_customio_data_1_r)
MEM_ADDR(0x7100, 0x7100, bosco_customio_1_r)
MEM_END

MEM_WRITE(boscoCPU1_Write)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_w)
MEM_ADDR(0x7000, 0x700f, bosco_customio_data_1_w)
MEM_ADDR(0x7100, 0x7100, bosco_customio_1_w)
MEM_ADDR(0x6820, 0x6820, bosco_interrupt_enable_1_w)
MEM_ADDR(0x6822, 0x6822, bosco_interrupt_enable_3_w)
MEM_ADDR(0x6823, 0x6823, bosco_halt_w)
MEM_ADDR(0x6830, 0x6830, MWA_ROM)

//MEM_ADDR(0x8000, 0x83ff, videoram_w)
//MEM_ADDR(0x8400, 0x87ff, bosco_videoram2_w)
//MEM_ADDR(0x8800, 0x8bff, colorram_w)
//MEM_ADDR(0x8c00, 0x8fff, bosco_colorram2_w)

MEM_ADDR(0x9810, 0x9810, bosco_scrollx_w)
MEM_ADDR(0x9820, 0x9820, bosco_scrolly_w)
MEM_ADDR(0x9830, 0x9830, bosco_starcontrol_w)
MEM_ADDR(0x9870, 0x9870, bosco_flipscreen_w)

//MEM_ADDR(0xa000, 0xa005, galaga_starcontrol_w)
//MEM_ADDR(0xa007, 0xa007, galaga_flipscreen_w)
MEM_END

MEM_READ(boscoCPU2_Read)
MEM_ADDR(0x6800, 0x6807, bosco_dsw_r)
MEM_ADDR(0x9000, 0x900f, bosco_customio_data_2_r)
//MEM_ADDR(0x9010, 0x90ff, bosco_sharedram_r)
MEM_ADDR(0x9100, 0x9100, bosco_customio_2_r)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_r)  //0x8fff
//MEM_ADDR(0x9101, 0x97ff, bosco_sharedram_r)
MEM_END

MEM_WRITE(boscoCPU2_Write)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x6821, 0x6821, bosco_interrupt_enable_2_w)
//MEM_ADDR(0x7800, 0x8fff, bosco_sharedram_w)
//MEM_ADDR(0x9000, 0x900f, bosco_customio_data_2_w)
//MEM_ADDR(0x9010, 0x90ff, bosco_sharedram_w)
//MEM_ADDR(0x9100, 0x9100, bosco_customio_2_w)
//MEM_ADDR(0x9101, 0x97ff, bosco_sharedram_w)

//MEM_ADDR(0x8000, 0x83ff, videoram_w)
//MEM_ADDR(0x8400, 0x87ff, bosco_videoram2_w)
//MEM_ADDR(0x8800, 0x8bff, colorram_w)
//MEM_ADDR(0x8c00, 0x8fff, bosco_colorram2_w)
MEM_ADDR(0x9000, 0x900f, bosco_customio_data_2_w)
MEM_ADDR(0x9100, 0x9100, bosco_customio_2_w)
MEM_ADDR(0x9830, 0x9830, bosco_starcontrol_w)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_w)
MEM_END

MEM_READ(boscoCPU3_Read)
MEM_ADDR(0x6800, 0x6807, bosco_dsw_r)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_r)
MEM_END

MEM_WRITE(boscoCPU3_Write)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x6800, 0x681f, bosco_sound_w)
MEM_ADDR(0x6822, 0x6822, bosco_interrupt_enable_3_w)
MEM_ADDR(0x7800, 0x97ff, bosco_sharedram_w)
//MEM_ADDR(0x8000, 0x83ff, videoram_w)
//MEM_ADDR(0x8400, 0x87ff, bosco_videoram2_w)
//MEM_ADDR(0x8800, 0x8bff, colorram_w)
//MEM_ADDR(0x8c00, 0x8fff, bosco_colorram2_w)
MEM_END


int init_bosco()
{
		
	//Init CPU's
	//Init CPU's
	init_z80(boscoCPU1_Read, boscoCPU1_Write, boscoPortRead, boscoPortWrite, CPU0);
	init_z80(boscoCPU2_Read, boscoCPU2_Write, boscoPortRead, boscoPortWrite, CPU1);
	init_z80(boscoCPU3_Read, boscoCPU3_Write, boscoPortRead, boscoPortWrite, CPU2);

	credits = 0;
	HiScore = 20000;

	bosco_halt_w(0, 0, 0);
	//Ram Pointers for video routines

	LOG_INFO("bosco Init called");

	memory_region(REGION_CPU1)[0x8c00] = 1;
	memory_region(REGION_CPU1)[0x8c01] = 1;

	//FOR RASTER, VIDEORAM POINTER, SPRITERAM POINTER NEED TO BE SET MANUALLY
	videoram = &Machine->memory_region[CPU0][0x8000];
	videoram_size = 0x400;

	bosco_videoram2 = &Machine->memory_region[CPU0][0x8400];
	bosco_colorram2 = &Machine->memory_region[CPU0][0x8C00];

	colorram = &Machine->memory_region[CPU0][0x8800];
	spriteram = &Machine->memory_region[CPU0][0x83D4];
	spriteram_size = 0x0b;
	spriteram_2 = &Machine->memory_region[CPU0][0x8bD4];
	spriteram_2_size = 0x0b;

	bosco_radarx = &Machine->memory_region[CPU0][0x83f4];
	bosco_radary = &Machine->memory_region[CPU0][0x8bf4];
	bosco_radarattr = &Machine->memory_region[CPU0][0x9804];

	bosco_sharedram = &Machine->memory_region[CPU0][0x7800];
	bosco_staronoff = &Machine->memory_region[CPU0][0x9840];
	bosco_starblink = &Machine->memory_region[CPU1][0x9874];

	bosco_vh_start();
	//Start Namco Sound interface
	namco_sh_start(&bosco_namco_interface);
	bosco_build_voice_samples();

	return 0;
}

void end_bosco()
{

}


ROM_START(bosco)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code for the first CPU  */
ROM_LOAD("bos3_1.3n", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("bos1_2.3m", 0x1000, 0x1000, CRC(2d8f3ebe)SHA1(75de1cba7531ae4bf7fbbef7b8e37b9fec4ed0d0))
ROM_LOAD("bos1_3.3l", 0x2000, 0x1000, CRC(c80ccfa5)SHA1(f2bbec2ea9846d4601f06c0b4242744447a88fda))
ROM_LOAD("bos1_4b.3k", 0x3000, 0x1000, CRC(a3f7f4ab)SHA1(eb26184311bae0767c7a5593926e6eadcbcb680e))

ROM_REGION(0x10000, REGION_CPU2, 0)	/* 64k for the second CPU */
ROM_LOAD("bos1_5c.3j", 0x0000, 0x1000, CRC(a7c8e432)SHA1(3607be75daa10f1f98dbfd9e600c5ba513130d44))
ROM_LOAD("bos3_6.3h", 0x1000, 0x1000, CRC(4543cf82)SHA1(50ad7d1ab6694eb8fab88d0fa79ee04f6984f3ca))

ROM_REGION(0x10000, REGION_CPU3, 0)	/* 64k for the third CPU  */
ROM_LOAD("bos1_7.3e", 0x0000, 0x1000, CRC(d45a4911)SHA1(547236adca9174f5cc0ec05b9649618bb92ba630))

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("bos1_14.5d", 0x0000, 0x1000, CRC(a956d3c5)SHA1(c5a9d7b1f9b4acda8fb9762414e085cb5fb80c9e))

ROM_REGION(0x1000, REGION_GFX2, 0)
ROM_LOAD("bos1_13.5e", 0x0000, 0x1000, CRC(e869219c)SHA1(425614cd0642743a82ef9c1aada29774a92203ea))

ROM_REGION(0x0100, REGION_GFX3, 0)
ROM_LOAD("bos1-4.2r", 0x0000, 0x0100, CRC(9b69b543)SHA1(47af3f67e50794e839b74fe61197af2228084efd))	/* dots */

ROM_REGION(0x0260, REGION_PROMS, 0)
ROM_LOAD("bos1-6.6b", 0x0000, 0x0020, CRC(d2b96fb0)SHA1(54c100ec9d173d7dd48a453ebed5f625053cb6e0))    /* palette */
ROM_LOAD("bos1-5.4m", 0x0020, 0x0100, CRC(4e15d59c)SHA1(3542ead6421d169c3569e121ec2be304e108787c))    /* lookup table */
//ROM_LOAD("bos1-3.2d", 0x0120, 0x0020, CRC(b88d5ba9)SHA1(7b97a38a540b7ca4b7d9ae338ec38b9b1a337846))    /* video layout (not used) */
//ROM_LOAD("bos1-7.7h", 0x0140, 0x0020, CRC(87d61353)SHA1(c7493e52662c921625676a4a4e8cf4371bd938b7))    /* video timing (not used) */


ROM_REGION(0x0200, REGION_SOUND1, 0)	/* sound prom */
ROM_LOAD("bos1-1.1d", 0x0000, 0x0100, CRC(de2316c6) SHA1(0e55c56046331888d1d3f0d9823d2ceb203e7d3f))
//ROM_LOAD("bos1-2.5c", 0x0100, 0x0100, CRC(77245b66) ,SHA1(0c4d0bee858b97632411c440bea6948a74759746))    /* timing - not used */

ROM_REGION(0x3000, REGION_SOUND2, 0)	/* ROMs for digitised speech */
ROM_LOAD("bos1_9.5n", 0x0000, 0x1000, CRC(09acc978) SHA1(2b264aaeb6eba70ad91593413dca733990e5467b))
ROM_LOAD("bos1_10.5m", 0x1000, 0x1000, CRC(e571e959) SHA1(9c81d7bec73bc605f7dd9a089171b0f34c4bb09a))
ROM_LOAD("bos1_11.5k", 0x2000, 0x1000, CRC(17ac9511) SHA1(266f3fae90d2fe38d109096d352863a52b379899))
ROM_END

ROM_START(boscomd)
ROM_REGION(0x10000, REGION_CPU1, 0)	/* 64k for code for the first CPU  */
ROM_LOAD("3n", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("3m", 0x1000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("3l", 0x2000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("3k", 0x3000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x10000, REGION_CPU2, 0)	/* 64k for the second CPU */
ROM_LOAD("3j", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("3h", 0x1000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x10000, REGION_CPU3, 0)	/* 64k for the third CPU  */
ROM_LOAD("2900.3e", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("5300.5d", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x1000, REGION_GFX2, 0)
ROM_LOAD("5200.5e", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x0100, REGION_GFX3, 0)
ROM_LOAD("prom.2d", 0x0000, 0x0100, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x0260, REGION_PROMS, 0)
ROM_LOAD("bosco.6b", 0x0000, 0x0020, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("bosco.4m", 0x0020, 0x0100, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
//ROM_LOAD("prom.1d", 0x0120, 0x0100, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
//ROM_LOAD("prom.2r", 0x0220, 0x0020, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
//ROM_LOAD("prom.7h", 0x0240, 0x0020, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x0100, REGION_SOUND1, 0)	/* sound prom */
ROM_LOAD("bosco.spr", 0x0000, 0x0100, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
//ROM_LOAD("prom.5c", 0x0100, 0x0100, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))

ROM_REGION(0x3000, REGION_SOUND2, 0)	/* ROMs for digitised speech */
ROM_LOAD("4900.5n", 0x0000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("5000.5m", 0x1000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_LOAD("5100.5l", 0x2000, 0x1000, CRC(96021267)SHA1(bd49b0caabcccf9df45a272d767456a4fc8a7c07))
ROM_END


INPUT_PORTS_START(bosco)
PORT_START("DSW0")  /* DSW0 */
PORT_DIPNAME(0x07, 0x07, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(4C_1C))
PORT_DIPSETTING(0x02, DEF_STR(3C_1C))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
PORT_DIPSETTING(0x04, DEF_STR(2C_3C))
PORT_DIPSETTING(0x06, DEF_STR(1C_2C))
PORT_DIPSETTING(0x05, DEF_STR(1C_3C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
/* TODO: bonus scores are different for 5 lives */
PORT_DIPNAME(0x38, 0x08, "Bonus Fighter")
PORT_DIPSETTING(0x30, "15K 50K")
PORT_DIPSETTING(0x38, "20K 70K")
PORT_DIPSETTING(0x08, "10K 50K 50K")
PORT_DIPSETTING(0x10, "15K 50K 50K")
PORT_DIPSETTING(0x18, "15K 70K 70K")
PORT_DIPSETTING(0x20, "20K 70K 70K")
PORT_DIPSETTING(0x28, "30K 100K 100K")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x40, "2")
PORT_DIPSETTING(0x80, "3")
PORT_DIPSETTING(0xc0, "5")

PORT_START("DSW1")      /* DSW1 */
PORT_DIPNAME(0x03, 0x03, DEF_STR(Difficulty))
PORT_DIPSETTING(0x01, "Easy")
PORT_DIPSETTING(0x03, "Medium")
PORT_DIPSETTING(0x02, "Hardest")
PORT_DIPSETTING(0x00, "Auto")
PORT_DIPNAME(0x04, 0x04, "2 Credits Game")
PORT_DIPSETTING(0x00, "1 Player")
PORT_DIPSETTING(0x04, "2 Players")
PORT_DIPNAME(0x08, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x08, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x10, 0x10, "Freeze")
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, "Allow Continue")
PORT_DIPSETTING(0x00, DEF_STR(No))
PORT_DIPSETTING(0x20, DEF_STR(Yes))
PORT_DIPNAME(0x40, 0x40, "Test ????")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IO1")      /* FAKE */
/* The player inputs are not memory mapped, they are handled by an I/O chip. */
/* These fake input ports are read by galaga_customio_data_r() */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO2")      /* FAKE */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO3")      /* FAKE */
/* the button here is used to trigger the sound in the test screen */
PORT_BITX(0x03, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_START1, 1)
PORT_BIT_IMPULSE(0x08, IP_ACTIVE_LOW, IPT_START2, 1)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN2, 1)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN3, 1)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)
INPUT_PORTS_END


INPUT_PORTS_START(boscomd)
PORT_START("DSW0")  /* DSW0 */
PORT_DIPNAME(0x07, 0x07, DEF_STR(Coinage))
PORT_DIPSETTING(0x01, DEF_STR(4C_1C))
PORT_DIPSETTING(0x02, DEF_STR(3C_1C))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
PORT_DIPSETTING(0x04, DEF_STR(2C_3C))
PORT_DIPSETTING(0x06, DEF_STR(1C_2C))
PORT_DIPSETTING(0x05, DEF_STR(1C_3C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
/* TODO: bonus scores are different for 5 lives */
PORT_DIPNAME(0x38, 0x08, "Bonus Fighter")
PORT_DIPSETTING(0x30, "15K 50K")
PORT_DIPSETTING(0x38, "20K 70K")
PORT_DIPSETTING(0x08, "10K 50K 50K")
PORT_DIPSETTING(0x10, "15K 50K 50K")
PORT_DIPSETTING(0x18, "15K 70K 70K")
PORT_DIPSETTING(0x20, "20K 70K 70K")
PORT_DIPSETTING(0x28, "30K 100K 100K")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x40, "2")
PORT_DIPSETTING(0x80, "3")
PORT_DIPSETTING(0xc0, "5")

PORT_START("DSW1")      /* DSW1 */
PORT_DIPNAME(0x01, 0x01, "2 Credits Game")
PORT_DIPSETTING(0x00, "1 Player")
PORT_DIPSETTING(0x01, "2 Players")
PORT_DIPNAME(0x06, 0x06, DEF_STR(Difficulty))
PORT_DIPSETTING(0x02, "Easy")
PORT_DIPSETTING(0x06, "Medium")
PORT_DIPSETTING(0x04, "Hardest")
PORT_DIPSETTING(0x00, "Auto")
PORT_DIPNAME(0x08, 0x08, "Allow Continue")
PORT_DIPSETTING(0x00, DEF_STR(No))
PORT_DIPSETTING(0x08, DEF_STR(Yes))
PORT_DIPNAME(0x10, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x10, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x20, 0x20, "Freeze")
PORT_DIPSETTING(0x20, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x40, 0x40, "Test ????")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IO1")      /* FAKE */
/* The player inputs are not memory mapped, they are handled by an I/O chip. */
/* These fake input ports are read by galaga_customio_data_r() */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO2")      /* FAKE */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 1)
PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IO3")      /* FAKE */
/* the button here is used to trigger the sound in the test screen */
PORT_BITX(0x03, IP_ACTIVE_LOW, IPT_BUTTON1, 0, IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_LOW, IPT_START1, 1)
PORT_BIT_IMPULSE(0x08, IP_ACTIVE_LOW, IPT_START2, 1)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_LOW, IPT_COIN1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN2, 1)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN3, 1)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)
INPUT_PORTS_END

// Bosconian Driver
AAE_DRIVER_BEGIN(drv_bosco, "bosco", "Bosconian")
AAE_DRIVER_ROM(rom_bosco)
AAE_DRIVER_FUNCS(&init_bosco, &run_bosco, &end_bosco)
AAE_DRIVER_INPUT(input_ports_bosco)
AAE_DRIVER_SAMPLES(bosco_samples)
AAE_DRIVER_ART_NONE()
AAE_DRIVER_CPU3(CPU_MZ80, 3072000, 400, 1, INT_TYPE_INT, &boscoint1, CPU_MZ80, 3072000, 400, 1, INT_TYPE_INT, boscoint2, CPU_MZ80, 3072000, 400, 2, INT_TYPE_INT, boscoint3)
AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(36 * 8, 28 * 8, 0 * 8, 36 * 8 - 1, 0 * 8, 28 * 8 - 1)
AAE_DRIVER_RASTER(bosco_gfxdecodeinfo, 32 + 64, 64 * 4 + 64 * 4 + 4, bosco_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_bosco)