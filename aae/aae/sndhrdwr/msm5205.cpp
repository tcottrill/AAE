/*****************************************************************************
  MSM5205 ADPCM decoder emulation
  Ported from MAME 0.57 sound/msm5205.c to AAE. See msm5205.h for the notes
  on what changed in the port (per-frame stream push, AAE timers).

  Original MAME credits: Aaron Giles (streaming ADPCM driver), decode core by
  Mirko Buffoni, MSM5205 separation 01/06/99. THE DECODE TABLES AND LOGIC ARE
  FROM MAME and COPYRIGHT the MAME TEAM.
 *****************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "msm5205.h"
#include "mixer.h"           /* stream_start, stream_update, mixer_alloc_channel */
#include "aae_mame_driver.h" /* Machine */
#include "cpu_control.h"     /* cpu_scale_by_cycles */
#include "timer.h"           /* timer_pulse / timer_remove / TIME_IN_HZ */
#include "sys_log.h"

/* step size index shift table */
static const int index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* lookup table for the precomputed difference */
static int diff_lookup[49 * 16];

static void ComputeTables(void)
{
	/* nibble to bit map */
	static const int nbl2bit[16][4] =
	{
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};

	for (int step = 0; step <= 48; step++)
	{
		int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));

		for (int nib = 0; nib < 16; nib++)
		{
			diff_lookup[step * 16 + nib] = nbl2bit[nib][0] *
				(stepval * nbl2bit[nib][1] +
				 stepval / 2 * nbl2bit[nib][2] +
				 stepval / 4 * nbl2bit[nib][3] +
				 stepval / 8);
		}
	}
}

struct MSM5205Voice
{
	int channel;      /* AAE mixer channel                 */
	int timer_id;     /* VCLK timer id, -1 if none         */
	int data;         /* next adpcm data                   */
	int vclk;         /* vclk signal (external mode)       */
	int reset;        /* reset pin signal                  */
	int prescaler;    /* prescaler selector S1 and S2      */
	int bitwidth;     /* bit width selector -3B/4B         */
	int signal;       /* current ADPCM signal (-2048..2047)*/
	int step;         /* current ADPCM step                */

	int16_t* frame_buf;   /* current frame's reconstructed waveform */
	int write_pos;        /* fill cursor within frame_buf           */
};

static const struct MSM5205interface* msm5205_intf = nullptr;
static struct MSM5205Voice msm5205[MAX_MSM5205];
static int msm_frame_len = 0;

/* -------------------------------------------------------------------------
   catch_up - sample-and-hold the CURRENT signal into the frame buffer up to
   the executing CPU's position within the frame (same idiom as dac.cpp).
   ------------------------------------------------------------------------- */
static void msm_catch_up(struct MSM5205Voice* voice)
{
	if (!voice->frame_buf || msm_frame_len <= 0)
		return;

	int pos = cpu_scale_by_cycles(msm_frame_len, 0);
	if (pos > msm_frame_len) pos = msm_frame_len;
	if (pos < voice->write_pos) pos = voice->write_pos;   /* monotonic */

	int16_t level = (int16_t)(voice->signal * 16);
	for (int s = voice->write_pos; s < pos; s++)
		voice->frame_buf[s] = level;
	voice->write_pos = pos;
}

/* timer callback at VCLK low edge */
static void MSM5205_vclk_callback(int num)
{
	struct MSM5205Voice* voice = &msm5205[num];
	int new_signal;

	/* callback user handler and latch next data */
	if (msm5205_intf->vclk_interrupt[num])
		(*msm5205_intf->vclk_interrupt[num])(num);

	/* reset check at last high edge of VCLK */
	if (voice->reset)
	{
		new_signal = 0;
		voice->step = 0;
	}
	else
	{
		/* the MSM5205 has internal 12bit decoding, signal width -2048..2047 */
		int val = voice->data;
		new_signal = voice->signal + diff_lookup[voice->step * 16 + (val & 15)];
		if (new_signal > 2047) new_signal = 2047;
		else if (new_signal < -2048) new_signal = -2048;
		voice->step += index_shift[val & 7];
		if (voice->step > 48) voice->step = 48;
		else if (voice->step < 0) voice->step = 0;
	}

	/* update the frame buffer when the signal changes */
	if (voice->signal != new_signal)
	{
		msm_catch_up(voice);
		voice->signal = new_signal;
	}
}

int MSM5205_sh_start(const struct MSM5205interface* intf)
{
	msm5205_intf = intf;

	ComputeTables();

	memset(msm5205, 0, sizeof(msm5205));

	int fps = Machine->gamedrv->fps;
	if (fps <= 0) fps = 60;
	msm_frame_len = config.samplerate / fps;

	for (int i = 0; i < msm5205_intf->num; i++)
	{
		struct MSM5205Voice* voice = &msm5205[i];

		voice->timer_id = -1;

		voice->channel = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
		if (voice->channel < 0) {
			LOG_DEBUG("MSM5205 #%d: no free mixer channel", i);
			return 1;
		}

		voice->frame_buf = (int16_t*)malloc(msm_frame_len * sizeof(int16_t));
		if (!voice->frame_buf) {
			LOG_DEBUG("MSM5205 #%d: frame buffer malloc failed", i);
			return 1;
		}
		memset(voice->frame_buf, 0, msm_frame_len * sizeof(int16_t));

		stream_start(voice->channel, 0, 16, fps, /*stereo=*/false);
		sample_set_volume_mixer(voice->channel, msm5205_intf->mixing_level[i]);
	}

	MSM5205_sh_reset();
	return 0;
}

void MSM5205_sh_stop(void)
{
	if (!msm5205_intf)
		return;

	for (int i = 0; i < msm5205_intf->num; i++)
	{
		struct MSM5205Voice* voice = &msm5205[i];

		if (voice->timer_id >= 0) {
			timer_remove(voice->timer_id);
			voice->timer_id = -1;
		}
		if (voice->channel >= 0) {
			stream_stop(voice->channel, 0);
			voice->channel = -1;
		}
		if (voice->frame_buf) {
			free(voice->frame_buf);
			voice->frame_buf = nullptr;
		}
	}

	msm5205_intf = nullptr;
}

/* push one frame of audio per chip; call once per emulated video frame */
void MSM5205_sh_update(void)
{
	if (!msm5205_intf)
		return;

	for (int i = 0; i < msm5205_intf->num; i++)
	{
		struct MSM5205Voice* voice = &msm5205[i];
		if (!voice->frame_buf || voice->channel < 0)
			continue;

		int16_t level = (int16_t)(voice->signal * 16);
		for (int s = voice->write_pos; s < msm_frame_len; s++)
			voice->frame_buf[s] = level;

		stream_update(voice->channel, voice->frame_buf);
		voice->write_pos = 0;
	}
}

void MSM5205_sh_reset(void)
{
	if (!msm5205_intf)
		return;

	for (int i = 0; i < msm5205_intf->num; i++)
	{
		struct MSM5205Voice* voice = &msm5205[i];

		voice->data   = 0;
		voice->vclk   = 0;
		voice->reset  = 0;
		voice->signal = 0;
		voice->step   = 0;

		/* apply the default timer / bitwidth selection */
		MSM5205_playmode_w(i, msm5205_intf->select[i]);
	}
}

/*
 *    External VCLK input (slave/SEX mode only): decode on the falling edge.
 */
void MSM5205_vclk_w(int num, int vclk)
{
	if (!msm5205_intf || num >= msm5205_intf->num)
		return;

	if (msm5205[num].prescaler != 0)
	{
		LOG_DEBUG("MSM5205_vclk_w() chip %d is in master (prescaler) mode", num);
		return;
	}

	if (msm5205[num].vclk != vclk)
	{
		msm5205[num].vclk = vclk;
		if (!vclk)
			MSM5205_vclk_callback(num);
	}
}

void MSM5205_reset_w(int num, int reset)
{
	if (!msm5205_intf || num >= msm5205_intf->num)
		return;
	msm5205[num].reset = reset;
}

void MSM5205_data_w(int num, int data)
{
	if (!msm5205_intf || num >= msm5205_intf->num)
		return;

	if (msm5205[num].bitwidth == 4)
		msm5205[num].data = data & 0x0f;
	else
		msm5205[num].data = (data & 0x07) << 1; /* unknown */
}

void MSM5205_playmode_w(int num, int select)
{
	static const int prescaler_table[4] = { 96, 48, 64, 0 };

	if (!msm5205_intf || num >= msm5205_intf->num)
		return;

	struct MSM5205Voice* voice = &msm5205[num];
	int prescaler = prescaler_table[select & 3];
	int bitwidth = (select & 4) ? 4 : 3;

	if (voice->prescaler != prescaler)
	{
		msm_catch_up(voice);
		voice->prescaler = prescaler;

		/* master mode: self-clocked at baseclock / prescaler.
		   NOTE: AAE's timer_pulse() is a one-shot despite the name; the
		   MAME-style periodic API is timer_alloc + timer_adjust with a
		   repeat period, so that is what we use. */
		if (prescaler)
		{
			double per = TIME_IN_HZ(msm5205_intf->baseclock / prescaler);
			if (voice->timer_id < 0)
				voice->timer_id = timer_alloc(MSM5205_vclk_callback);
			timer_adjust(voice->timer_id, per, num, per);
		}
		else if (voice->timer_id >= 0)
		{
			/* slave (external VCLK) mode: stop the self-clock */
			timer_enable(voice->timer_id, 0);
		}
	}

	if (voice->bitwidth != bitwidth)
	{
		msm_catch_up(voice);
		voice->bitwidth = bitwidth;
	}
}
