#include "namco.h"

#include "aae_mame_driver.h"
#include "mixer.h"


/* note: we work with signed samples here, unlike other drivers */

#define SIGNED_SAMPLES 1

#ifdef SIGNED_SAMPLES
#define AUDIO_CONV(A) (A)
#else
#define AUDIO_CONV(A) (A+0x80)
#endif


unsigned char *sound_prom_data;

static int emulation_rate;
static struct namco_interface *soundinterface;
static int buffer_len;
static int sample_pos;
static short *output_buffer;

static int channel;

unsigned char *pengo_soundregs;
#define MAX_VOICES 8

static int freq[MAX_VOICES], volume[MAX_VOICES];
static const unsigned char *wave[MAX_VOICES];
static int counter[MAX_VOICES];


static signed char  *mixer_table;
static signed char  *mixer_lookup;
static signed short *mixer_buffer;

extern int soundpos;


/* note: gain is specified as gain*16 */
static int make_mixer_table(int voices, int gain)
{
	int count = voices * 128;
	int i;

	/* allocate memory */
	mixer_table = (signed char *)malloc(256 * voices);
	if (!mixer_table)
		return 1;

	/* find the middle of the table */
	mixer_lookup = mixer_table + (voices * 128);

	/* fill in the table */
	for (i = 0; i < count; i++)
	{
		int val = i * gain / (voices * 16);
		if (val > 127) val = 127;
		mixer_lookup[i] = AUDIO_CONV(val);
		mixer_lookup[-i] = AUDIO_CONV(-val);
	}

	return 0;
}


void namco_update(short *buffer, int len)
{
	int i;
	int voice;

	signed short *mix;

	mix = mixer_buffer;
	for (i = 0; i < len; i++)
		*mix++ = 0;

	for (voice = 0; voice < soundinterface->voices; voice++)
	{
		int f = freq[voice];
		int v = volume[voice];

		if (v && f)
		{
			const unsigned char *w = wave[voice];
			int c = counter[voice];

			mix = mixer_buffer;
			for (i = 0; i < len; i++)
			{
				c += f;
				*mix++ += ((w[(c >> 15) & 0x1f] & 0x0f) - 8) * v;
			}

			counter[voice] = c;
		}
	}
	mix = mixer_buffer;
	for (i = 0; i < len; i++)
		*buffer++ = (mixer_lookup[*mix++])<<8;
}


int namco_sh_start(struct namco_interface *intf)
{
	int voice;

	LOG_INFO("Calling Namco SH Start at samplerate %d", intf->samplerate);
	buffer_len = (intf->samplerate / 60);
	emulation_rate = buffer_len * 60;

	if (memory_region(REGION_SOUND1)) 
	{
		LOG_INFO("Assigning Sound Prom Data");
		sound_prom_data = memory_region(REGION_SOUND1);
	}

	pengo_soundregs = (unsigned char *)malloc(0x1f); 

	if ((output_buffer = (short *)malloc(buffer_len * 2)) == 0)
		return 1;

	if ((mixer_buffer = (signed short *)malloc(sizeof(short) * buffer_len)) == 0)
	{
		free (output_buffer);
		return 1;
	}

	if (make_mixer_table(intf->voices, intf->gain))
	{
		free(mixer_buffer);
		free (output_buffer);
		return 1;
	}

	sample_pos = 0;
	soundinterface = intf;

	for (voice = 0; voice < soundinterface->voices; voice++)
	{
		freq[voice] = 0;
		volume[voice] = 0;
		wave[voice] = &sound_prom_data[0];
		counter[voice] = 0;
	}

	
	memset(output_buffer, 0, buffer_len);

	stream_start(11, 0, 16, Machine->drv->fps);
	LOG_INFO("Finished Calling Namco SH Start");
	return 0;
}


void namco_sh_stop(void)
{
	free(mixer_table);
	free(mixer_buffer);
	free (output_buffer);
	free(pengo_soundregs);
	stream_stop(11, 0);
}


void namco_sh_update(void)
{
    //if (Machine->sample_rate == 0) return;

	//LOG_INFO("Namco SH Update");
	namco_update(&output_buffer[sample_pos], buffer_len - sample_pos);
	sample_pos = 0;

	stream_update(11, output_buffer);
	
}


/********************************************************************************/


void doupdate()
{
	int newpos;

	//LOG_INFO("Calling scale by cycles");
	newpos = cpu_scale_by_cycles(buffer_len, Machine->gamedrv->cpu_freq[0]);	// get current position based on the timer

	namco_update(&output_buffer[sample_pos], newpos - sample_pos);
	sample_pos = newpos;
}


/********************************************************************************/



void namco_sound_w(int offset, int data)
{
	int voice;
	int base;


	doupdate();	/* update output buffer before changing the registers */


	/* set the register */
	pengo_soundregs[offset] = data & 0x0f;

	/* recompute all the voice parameters */
	for (base = 0, voice = 0; voice < 3; voice++, base += 5)
	{
		freq[voice] = pengo_soundregs[0x14 + base];	/* always 0 */
		freq[voice] = freq[voice] * 16 + pengo_soundregs[0x13 + base];
		freq[voice] = freq[voice] * 16 + pengo_soundregs[0x12 + base];
		freq[voice] = freq[voice] * 16 + pengo_soundregs[0x11 + base];
		if (base == 0)	/* the first voice has extra frequency bits */
		{
			freq[voice] = (freq[voice] * 16) + pengo_soundregs[(0x10 + base)];
		}
		else { freq[voice] = freq[voice] * 16; }

		volume[voice] = pengo_soundregs[0x15 + base] & 0x0f;
		wave[voice] = &sound_prom_data[32 * (pengo_soundregs[0x05 + base] & 7)];
	}

}
