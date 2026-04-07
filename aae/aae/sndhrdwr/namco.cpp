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
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include "namco.h"
#include "aae_mame_driver.h"
#include "mixer.h"
#include "framework.h"
#include "sys_log.h"

#include <cstring>
#include <cstdlib>
#include <cstdint>

/* 8 voices max */
#define MAX_VOICES 8

// -----------------------------------------------------------------------------
// Globals visible to drivers
// -----------------------------------------------------------------------------
unsigned char* namco_soundregs = nullptr;
unsigned char* namco_wavedata = nullptr;

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static unsigned char* sound_prom_data = nullptr;

static int emulation_rate = 0;
static int buffer_len = 0;
static int sample_pos = 0;

static short* output_buffer = nullptr;

/* stream-rate buffer managed by resampler */
static short* stream_buffer = nullptr;
static int    stream_buffer_len = 0;

/* mixer tables and internal buffers */
static int16_t* mixer_table = nullptr;
static int16_t* mixer_lookup = nullptr;
static int16_t* mixer_buffer = nullptr;

static struct namco_interface* soundinterface = nullptr;

static int num_voices = 0;
static int sound_enable = 1;
static int samples_per_byte = 1;
static int stereo_enabled = 0;

/* per-voice state */
static int freq[MAX_VOICES];
static int volume[MAX_VOICES];
static const unsigned char* wave[MAX_VOICES];
static int counter[MAX_VOICES];

//extern int soundpos;

// -----------------------------------------------------------------------------
// make_mixer_table
// -----------------------------------------------------------------------------

static int make_mixer_table(int voices)
{
	int count = voices * 128;
	int i;
	int gain = 16;

	/* allocate memory */
	mixer_table = (short*)malloc(256 * voices * sizeof(INT16));
	if (!mixer_table)
		return 1;

	/* find the middle of the table */
	mixer_lookup = mixer_table + (128 * voices);

	/* fill in the table - 16 bit case */
	for (i = 0; i < count; i++)
	{
		int val = i * gain * 16 / voices;
		if (val > 32767) val = 32767;
		mixer_lookup[i] = val;
		mixer_lookup[-i] = -val;
	}

	return 0;
}

// -----------------------------------------------------------------------------
// reset_voices
// -----------------------------------------------------------------------------
static void reset_voices()
{
	int i;

	for (i = 0; i < MAX_VOICES; i++)
	{
		freq[i] = 0;
		volume[i] = 0;
		counter[i] = 0;
		wave[i] = sound_prom_data ? &sound_prom_data[0] : nullptr;
	}
}

// -----------------------------------------------------------------------------
// decode_wave_sample
// Handles both ROM-backed wave data (low nibble only) and RAM-backed waveform
// data (high nibble then low nibble), matching the newer interface behavior.
// -----------------------------------------------------------------------------
static inline int decode_wave_sample(const unsigned char* w, int offs)
{
	if (!w)
		return 0;

	if (samples_per_byte == 1)
	{
		return (w[offs] & 0x0f) - 8;
	}
	else
	{
		if (offs & 1)
			return (w[offs >> 1] & 0x0f) - 8;
		else
			return ((w[offs >> 1] >> 4) & 0x0f) - 8;
	}
}

// -----------------------------------------------------------------------------
// namco_update
// Newer mono mix behavior adapted to the AAE streaming/resampler path.
// -----------------------------------------------------------------------------
void namco_update(short* buffer, int len)
{
	int i;
	int voice;
	int16_t* mix;

	if (!buffer || len <= 0)
		return;

	if (sound_enable == 0)
	{
		std::memset(buffer, 0, len * sizeof(short));
		return;
	}

	if (!mixer_buffer || !mixer_lookup)
	{
		std::memset(buffer, 0, len * sizeof(short));
		return;
	}

	std::memset(mixer_buffer, 0, len * sizeof(int16_t));

	for (voice = 0; voice < num_voices; voice++)
	{
		const int f = freq[voice];
		const int v = volume[voice];

		if (v && f && wave[voice])
		{
			const unsigned char* w = wave[voice];
			int c = counter[voice];

			mix = mixer_buffer;
			for (i = 0; i < len; i++)
			{
				int offs;

				c += f;
				offs = (c >> 15) & 0x1f;
				*mix++ += (int16_t)(decode_wave_sample(w, offs) * v);
			}

			counter[voice] = c;
		}
	}

	mix = mixer_buffer;
	for (i = 0; i < len; i++)
	{
		*buffer++ = mixer_lookup[*mix++];
	}
}

// -----------------------------------------------------------------------------
// namco_sh_start
// Keeps the AAE streaming path, but moves to the newer interface fields.
// -----------------------------------------------------------------------------
int namco_sh_start(struct namco_interface* intf)
{
	int regs_size;
	int fps;

	if (!intf)
		return 1;

	LOG_INFO("Calling Namco SH Start at samplerate %d voices %d region %d stereo %d",
		intf->samplerate, intf->voices, intf->region, intf->stereo);

	soundinterface = intf;
	num_voices = intf->voices;
	stereo_enabled = intf->stereo ? 1 : 0;
	sound_enable = 1;

	if (num_voices <= 0 || num_voices > MAX_VOICES)
	{
		LOG_ERROR("namco_sh_start: invalid voice count %d", num_voices);
		return 1;
	}

	fps = Machine->drv->fps;
	if (intf->samplerate <= 0 || fps <= 0)
	{
		LOG_ERROR("namco_sh_start: invalid samplerate=%d fps=%d", intf->samplerate, fps);
		return 1;
	}

	buffer_len = intf->samplerate / fps;
	emulation_rate = buffer_len * fps;

	if (buffer_len <= 0 || emulation_rate <= 0)
	{
		LOG_ERROR("namco_sh_start: invalid buffer_len=%d emulation_rate=%d", buffer_len, emulation_rate);
		return 1;
	}

	/* pick waveform source */
	if (intf->region == -1)
	{
		sound_prom_data = namco_wavedata;
		samples_per_byte = 2;
		LOG_INFO("Namco using RAM waveform data");
	}
	else
	{
		sound_prom_data = memory_region(intf->region);
		samples_per_byte = 1;
		LOG_INFO("Namco using memory region %d waveform data", intf->region);
	}

	/* fallback for older callers that still expect REGION_SOUND1 */
	if (!sound_prom_data && memory_region(REGION_SOUND1))
	{
		sound_prom_data = memory_region(REGION_SOUND1);
		samples_per_byte = 1;
		LOG_INFO("Namco fallback: using REGION_SOUND1 waveform data");
	}

	if (!sound_prom_data)
	{
		LOG_ERROR("namco_sh_start: no waveform data available");
		return 1;
	}

	/* Pengo uses 0x20 bytes, Mappy uses more; allocate enough for both */
	regs_size = 0x40;
	namco_soundregs = (unsigned char*)std::malloc(regs_size);
	if (!namco_soundregs)
	{
		LOG_ERROR("namco_sh_start: failed to allocate namco_soundregs");
		return 1;
	}
	std::memset(namco_soundregs, 0, regs_size);

	output_buffer = (short*)std::malloc(buffer_len * sizeof(short));
	if (!output_buffer)
	{
		std::free(namco_soundregs);
		namco_soundregs = nullptr;
		return 1;
	}

	mixer_buffer = (int16_t*)std::malloc(buffer_len * sizeof(int16_t));
	if (!mixer_buffer)
	{
		std::free(output_buffer);
		output_buffer = nullptr;

		std::free(namco_soundregs);
		namco_soundregs = nullptr;
		return 1;
	}

	if (make_mixer_table(num_voices))
	{
		std::free(mixer_buffer);
		mixer_buffer = nullptr;

		std::free(output_buffer);
		output_buffer = nullptr;

		std::free(namco_soundregs);
		namco_soundregs = nullptr;
		return 1;
	}

	sample_pos = 0;
	reset_voices();

	std::memset(output_buffer, 0, buffer_len * sizeof(short));

	stream_buffer = nullptr;
	stream_buffer_len = 0;

	/* current AAE path is still mono */
	stream_start(11, 0, 16, fps);

	if (stereo_enabled)
	{
		LOG_INFO("namco_sh_start: stereo requested but current AAE stream path remains mono");
	}

	LOG_INFO("Finished Calling Namco SH Start");
	return 0;
}

// -----------------------------------------------------------------------------
// namco_sh_stop
// -----------------------------------------------------------------------------
void namco_sh_stop(void)
{
	if (mixer_table)
	{
		std::free(mixer_table);
		mixer_table = nullptr;
		mixer_lookup = nullptr;
	}

	if (mixer_buffer)
	{
		std::free(mixer_buffer);
		mixer_buffer = nullptr;
	}

	if (output_buffer)
	{
		std::free(output_buffer);
		output_buffer = nullptr;
	}

	if (stream_buffer)
	{
		delete[] stream_buffer;
		stream_buffer = nullptr;
	}
	stream_buffer_len = 0;

	if (namco_soundregs)
	{
		std::free(namco_soundregs);
		namco_soundregs = nullptr;
	}

	stream_stop(11, 0);

	soundinterface = nullptr;
	sound_prom_data = nullptr;
	num_voices = 0;
	sound_enable = 1;
	samples_per_byte = 1;
	stereo_enabled = 0;
	buffer_len = 0;
	emulation_rate = 0;
	sample_pos = 0;
}

// -----------------------------------------------------------------------------
// namco_sh_update
// Keeps your existing frame render + resample + stream_update path.
// -----------------------------------------------------------------------------
void namco_sh_update(void)
{
	const float ratio = (float)config.samplerate / (float)emulation_rate;

	namco_update(&output_buffer[sample_pos], buffer_len - sample_pos);
	sample_pos = 0;

	linear_interpolation_16(
		output_buffer,
		buffer_len,
		&stream_buffer,
		&stream_buffer_len,
		ratio
	);

	stream_update(11, stream_buffer);
}

// -----------------------------------------------------------------------------
// doupdate
// Used by register writes to keep generated audio in sync.
// -----------------------------------------------------------------------------
void doupdate(void)
{
	int newpos;
	int delta;

	if (!output_buffer || buffer_len <= 0)
		return;

	newpos = cpu_scale_by_cycles(buffer_len, Machine->gamedrv->cpu[0].cpu_freq);
	delta = newpos - sample_pos;

	if (delta > 0)
	{
		namco_update(&output_buffer[sample_pos], delta);
		sample_pos = newpos;
	}
}

// -----------------------------------------------------------------------------
// pengo_sound_enable_w
// -----------------------------------------------------------------------------
void pengo_sound_enable_w(int offset, int data)
{
	(void)offset;
	sound_enable = data ? 1 : 0;
}

// -----------------------------------------------------------------------------
// pengo_sound_w
// Pengo/Pac-Man style 3-voice register decoding.
// -----------------------------------------------------------------------------
void pengo_sound_w(int offset, int data)
{
	int voice;
	int base;

	if (!namco_soundregs)
		return;

	doupdate();

	namco_soundregs[offset & 0x3f] = (unsigned char)(data & 0x0f);

	for (base = 0, voice = 0; voice < 3 && voice < num_voices; voice++, base += 5)
	{
		freq[voice] = namco_soundregs[0x14 + base];
		freq[voice] = freq[voice] * 16 + namco_soundregs[0x13 + base];
		freq[voice] = freq[voice] * 16 + namco_soundregs[0x12 + base];
		freq[voice] = freq[voice] * 16 + namco_soundregs[0x11 + base];

		if (base == 0)
			freq[voice] = freq[voice] * 16 + namco_soundregs[0x10 + base];
		else
			freq[voice] = freq[voice] * 16;

		volume[voice] = namco_soundregs[0x15 + base] & 0x0f;
		wave[voice] = &sound_prom_data[32 * (namco_soundregs[0x05 + base] & 0x07)];
	}
}

// -----------------------------------------------------------------------------
// namco_sound_w
// Compatibility wrapper for older code that assumed the Pengo register layout.
// -----------------------------------------------------------------------------
void namco_sound_w(int offset, int data)
{
	pengo_sound_w(offset, data);
}

// -----------------------------------------------------------------------------
// mappy_sound_enable_w
// Matches the MAME code you uploaded.
// -----------------------------------------------------------------------------
void mappy_sound_enable_w(int offset, int data)
{
	(void)data;
	sound_enable = offset ? 1 : 0;
}

// -----------------------------------------------------------------------------
// mappy_sound_w
// Mappy/Dig Dug 2 style 8-voice register decoding.
// -----------------------------------------------------------------------------
void mappy_sound_w(int offset, int data)
{
	int voice;
	int base;

	if (!namco_soundregs)
		return;

	doupdate();

	namco_soundregs[offset & 0x3f] = (unsigned char)data;

	for (base = 0, voice = 0; voice < num_voices && voice < MAX_VOICES; voice++, base += 8)
	{
		freq[voice] = namco_soundregs[0x06 + base] & 0x0f;
		freq[voice] = freq[voice] * 256 + namco_soundregs[0x05 + base];
		freq[voice] = freq[voice] * 256 + namco_soundregs[0x04 + base];

		volume[voice] = namco_soundregs[0x03 + base] & 0x0f;
		wave[voice] = &sound_prom_data[32 * ((namco_soundregs[0x06 + base] >> 4) & 0x07)];
	}
}