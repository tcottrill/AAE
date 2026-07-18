/***********************************************************

     Astrocade custom 'IO' chip sound chip driver
     Frank Palazzolo

     Portions copied from the Pokey emulator by Ron Fries

     First Release:
        09/20/98
     Updated 11/2004
        Fixed noise generator bug
        Changed to stream system
        Fixed out of bounds memory access bug

     Ported from MAME 0.90 to AAE. See astrocade.h for the
     list of differences from the MAME original.

 ***********************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "astrocade_sound.h"
#include "mixer.h"           /* stream_start, stream_update, stream_stop */
#include "aae_mame_driver.h" /* Machine, config */
#include "cpu_z80.h"         /* cpu_z80::GetBC - reg 8 block transfer */
#include "cpu_control.h"     /* m_cpu_z80, get_active_cpu, cpu_scale_by_cycles */
#include "sys_log.h"

/* -------------------------------------------------------------------------
   Module-level state
   ------------------------------------------------------------------------- */
static const struct astrocade_interface* intf = nullptr;

static int div_by_N_factor = 0;
static int buffer_len = 0;       /* samples per video frame */
static int emulation_rate = 0;   /* buffer_len * fps */

/* Per-chip mixer stream channel id returned by mixer_alloc_channel() */
static int chip_channel[MAX_ASTROCADE_CHIPS];

/* Per-chip frame buffer and fill cursor (mid-frame catch-up position) */
static int16_t* chip_buffer[MAX_ASTROCADE_CHIPS];
static int sample_pos[MAX_ASTROCADE_CHIPS];

static int current_count_A[MAX_ASTROCADE_CHIPS];
static int current_count_B[MAX_ASTROCADE_CHIPS];
static int current_count_C[MAX_ASTROCADE_CHIPS];
static int current_count_V[MAX_ASTROCADE_CHIPS];
static int current_count_N[MAX_ASTROCADE_CHIPS];

static int current_state_A[MAX_ASTROCADE_CHIPS];
static int current_state_B[MAX_ASTROCADE_CHIPS];
static int current_state_C[MAX_ASTROCADE_CHIPS];
static int current_state_V[MAX_ASTROCADE_CHIPS];

static int current_size_A[MAX_ASTROCADE_CHIPS];
static int current_size_B[MAX_ASTROCADE_CHIPS];
static int current_size_C[MAX_ASTROCADE_CHIPS];
static int current_size_V[MAX_ASTROCADE_CHIPS];
static int current_size_N[MAX_ASTROCADE_CHIPS];

/* Registers */

static int master_osc[MAX_ASTROCADE_CHIPS];
static int freq_A[MAX_ASTROCADE_CHIPS];
static int freq_B[MAX_ASTROCADE_CHIPS];
static int freq_C[MAX_ASTROCADE_CHIPS];
static int vol_A[MAX_ASTROCADE_CHIPS];
static int vol_B[MAX_ASTROCADE_CHIPS];
static int vol_C[MAX_ASTROCADE_CHIPS];
static int vibrato[MAX_ASTROCADE_CHIPS];
static int vibrato_speed[MAX_ASTROCADE_CHIPS];
static int mux[MAX_ASTROCADE_CHIPS];
static int noise_am[MAX_ASTROCADE_CHIPS];
static int vol_noise4[MAX_ASTROCADE_CHIPS];
static int vol_noise8[MAX_ASTROCADE_CHIPS];

static int randbyte[MAX_ASTROCADE_CHIPS];
static int randbit[MAX_ASTROCADE_CHIPS];

/* -------------------------------------------------------------------------
   astrocade_update - generate samples for one chip from its current fill
   cursor up to newpos. This is the MAME 0.90 inner loop, generating into
   the chip's frame buffer instead of a stream callback buffer.
   ------------------------------------------------------------------------- */
static void astrocade_update(int num, int newpos)
{
	int16_t* buffer = chip_buffer[num];
	int pos = sample_pos[num];
	int data, noise_plus_osc, vib_plus_osc;

	while (pos < newpos)
	{
		/* Update current output value */

		if (current_count_N[num] == 0)
		{
			randbyte[num] = rand() & 0xff;
		}

		current_size_V[num] = 32768 * vibrato_speed[num] / div_by_N_factor;

		if (!mux[num])
		{
			if (current_state_V[num] == -1)
				vib_plus_osc = (master_osc[num] - vibrato[num]) & 0xff;
			else
				vib_plus_osc = master_osc[num];
			current_size_A[num] = vib_plus_osc * freq_A[num] / div_by_N_factor;
			current_size_B[num] = vib_plus_osc * freq_B[num] / div_by_N_factor;
			current_size_C[num] = vib_plus_osc * freq_C[num] / div_by_N_factor;
		}
		else
		{
			noise_plus_osc = ((master_osc[num] - (vol_noise8[num] & randbyte[num]))) & 0xff;
			current_size_A[num] = noise_plus_osc * freq_A[num] / div_by_N_factor;
			current_size_B[num] = noise_plus_osc * freq_B[num] / div_by_N_factor;
			current_size_C[num] = noise_plus_osc * freq_C[num] / div_by_N_factor;
			current_size_N[num] = 2 * noise_plus_osc / div_by_N_factor;
		}

		data = (current_state_A[num] * vol_A[num] +
				current_state_B[num] * vol_B[num] +
				current_state_C[num] * vol_C[num]);

		if (noise_am[num])
		{
			randbit[num] = rand() & 1;
			data = data + randbit[num] * vol_noise4[num];
		}

		/* Put it in the buffer */

		buffer[pos++] = (int16_t)(data << 8);

		/* Update the state of the chip */

		if (current_count_A[num] >= current_size_A[num])
		{
			current_state_A[num] = -current_state_A[num];
			current_count_A[num] = 0;
		}
		else
			current_count_A[num]++;

		if (current_count_B[num] >= current_size_B[num])
		{
			current_state_B[num] = -current_state_B[num];
			current_count_B[num] = 0;
		}
		else
			current_count_B[num]++;

		if (current_count_C[num] >= current_size_C[num])
		{
			current_state_C[num] = -current_state_C[num];
			current_count_C[num] = 0;
		}
		else
			current_count_C[num]++;

		if (current_count_V[num] >= current_size_V[num])
		{
			current_state_V[num] = -current_state_V[num];
			current_count_V[num] = 0;
		}
		else
			current_count_V[num]++;

		if (current_count_N[num] >= current_size_N[num])
		{
			current_count_N[num] = 0;
		}
		else
			current_count_N[num]++;
	}
	sample_pos[num] = pos;
}

/* -------------------------------------------------------------------------
   astrocade_doupdate - AAE equivalent of MAME's stream_update() before a
   register change: generate with the OLD register values up to the CPU's
   current position within the frame, so mid-frame writes land at the right
   sample instead of being decimated to one change per frame.
   ------------------------------------------------------------------------- */
static void astrocade_doupdate(int num)
{
	if (!chip_buffer[num] || buffer_len <= 0)
		return;

	/* clock=0 derives the position from the active CPU's own clock */
	int newpos = cpu_scale_by_cycles(buffer_len, 0);
	if (newpos > buffer_len) newpos = buffer_len;

	if (newpos > sample_pos[num])
		astrocade_update(num, newpos);
}

/* =========================================================================
   Register writes
   ========================================================================= */
void astrocade_sound_w(int num, int offset, int data)
{
	int i, temp_vib;

	if (!intf || num >= intf->num)
		return;

	/* update with the old register values first */
	astrocade_doupdate(num);

	switch (offset)
	{
		case 0:  /* Master Oscillator */
			master_osc[num] = data + 1;
		break;

		case 1:  /* Tone A Frequency */
			freq_A[num] = data + 1;
		break;

		case 2:  /* Tone B Frequency */
			freq_B[num] = data + 1;
		break;

		case 3:  /* Tone C Frequency */
			freq_C[num] = data + 1;
		break;

		case 4:  /* Vibrato Register */
			vibrato[num] = data & 0x3f;

			temp_vib = (data >> 6) & 0x03;
			vibrato_speed[num] = 1;
			for (i = 0; i < temp_vib; i++)
				vibrato_speed[num] <<= 1;
		break;

		case 5:  /* Tone C Volume, Noise Modulation Control */
			vol_C[num] = data & 0x0f;
			mux[num] = (data >> 4) & 0x01;
			noise_am[num] = (data >> 5) & 0x01;
		break;

		case 6:  /* Tone A & B Volume */
			vol_B[num] = (data >> 4) & 0x0f;
			vol_A[num] = data & 0x0f;
		break;

		case 7:  /* Noise Volume Register */
			vol_noise8[num] = data;
			vol_noise4[num] = (data >> 4) & 0x0f;
		break;

		case 8:  /* Sound Block Transfer */
		{
			/* OTIR to port 0x18: real hardware latches the Z80 B register
			   (riding the upper address bus) to select the destination
			   register. AAE dispatches 8-bit port addresses, so read the
			   live B register from the active Z80 instead (MAME 0.57
			   handled it the same way via cpu_get_reg(Z80_BC)). */
			const int cpunum = get_active_cpu();
			if (m_cpu_z80[cpunum])
			{
				int bvalue = (m_cpu_z80[cpunum]->GetBC() >> 8) & 0x07;
				astrocade_sound_w(num, bvalue, data);
			}
		}
		break;
	}
}

/* Z80 port write handlers. Ports 0x10-0x18 / 0x50-0x58: the low nibble is
   the register offset (0x18 & 0x0f = 8 = block transfer). */
void astrocade_sound1_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW)
{
	(void)pPW;
	astrocade_sound_w(0, port & 0x0f, data);
}

void astrocade_sound2_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW)
{
	(void)pPW;
	astrocade_sound_w(1, port & 0x0f, data);
}

/* =========================================================================
   Lifecycle
   ========================================================================= */

/*
 * astrocade_sh_start - initialise all chips declared in the interface.
 * Call once from the driver's init after mixer_init().
 *
 * Returns 0 on success, 1 on any failure.
 */
int astrocade_sh_start(const struct astrocade_interface* intf_in)
{
	int i;

	intf = intf_in;

	int fps = Machine->gamedrv->fps;
	if (fps <= 0) fps = 60;

	buffer_len = config.samplerate / fps;
	emulation_rate = buffer_len * fps;

	if (buffer_len <= 0 || intf->baseclock <= 0)
	{
		LOG_ERROR("astrocade_sh_start: invalid buffer_len=%d baseclock=%d", buffer_len, intf->baseclock);
		return 1;
	}

	div_by_N_factor = intf->baseclock / emulation_rate;
	if (div_by_N_factor <= 0) div_by_N_factor = 1;

	for (i = 0; i < intf->num; i++)
	{
		/* Allocate a mixer channel out of the chip-stream range. */
		chip_channel[i] = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
		if (chip_channel[i] < 0)
		{
			LOG_ERROR("astrocade #%d: no free mixer channel in chip stream range", i);
			return 1;
		}

		chip_buffer[i] = (int16_t*)malloc(buffer_len * sizeof(int16_t));
		if (!chip_buffer[i])
		{
			LOG_ERROR("astrocade #%d: frame buffer malloc failed", i);
			return 1;
		}
		memset(chip_buffer[i], 0, buffer_len * sizeof(int16_t));

		/* Register mono 16-bit stream at the emulation rate; the mixer
		   resamples to the output rate inline. */
		stream_start(chip_channel[i], 0, 16, fps);
		stream_set_native_rate(chip_channel[i], emulation_rate);
		sample_set_volume(chip_channel[i], std::clamp(intf->mixing_level[i], 0, 255));

		/* reset state */
		sample_pos[i] = 0;
		current_count_A[i] = 0;
		current_count_B[i] = 0;
		current_count_C[i] = 0;
		current_count_V[i] = 0;
		current_count_N[i] = 0;
		current_state_A[i] = 1;
		current_state_B[i] = 1;
		current_state_C[i] = 1;
		current_state_V[i] = 1;
		current_size_A[i] = 0;
		current_size_B[i] = 0;
		current_size_C[i] = 0;
		current_size_V[i] = 0;
		current_size_N[i] = 0;
		randbyte[i] = 0;
		randbit[i] = 1;

		/* registers */
		master_osc[i] = 0;
		freq_A[i] = 0;
		freq_B[i] = 0;
		freq_C[i] = 0;
		vol_A[i] = 0;
		vol_B[i] = 0;
		vol_C[i] = 0;
		vibrato[i] = 0;
		vibrato_speed[i] = 0;
		mux[i] = 0;
		noise_am[i] = 0;
		vol_noise4[i] = 0;
		vol_noise8[i] = 0;
	}

	LOG_INFO("astrocade_sh_start: %d chip(s), baseclock %d, div_by_N %d, buffer_len %d",
		intf->num, intf->baseclock, div_by_N_factor, buffer_len);

	return 0;
}

/*
 * astrocade_sh_stop - free per-chip state and stop the streaming channels.
 */
void astrocade_sh_stop(void)
{
	if (!intf)
		return;

	for (int i = 0; i < intf->num; i++)
	{
		if (chip_channel[i] >= 0)
		{
			stream_stop(chip_channel[i], 0);
			chip_channel[i] = -1;
		}

		if (chip_buffer[i])
		{
			free(chip_buffer[i]);
			chip_buffer[i] = nullptr;
		}
	}

	intf = nullptr;
}

/*
 * astrocade_sh_update - fill the rest of the frame and push one frame of
 * audio to the mixer for every chip. Call once per emulated video frame.
 */
void astrocade_sh_update(void)
{
	if (!intf)
		return;

	for (int num = 0; num < intf->num; num++)
	{
		if (!chip_buffer[num] || chip_channel[num] < 0)
			continue;

		astrocade_update(num, buffer_len);
		sample_pos[num] = 0;

		stream_update(chip_channel[num], chip_buffer[num]);
	}
}
