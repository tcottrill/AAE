/*****************************************************************************/
/*                                                                           */
/* Module:  POKEY Chip Emulator, V2.3                                       */
/* Purpose: To emulate the sound generation hardware of the Atari POKEY chip.*/
/* Author:  Ron Fries                                                        */
/*                                                                           */
/* Revision History:                                                         */
/*                                                                           */
/* 09/22/96 - Ron Fries - Initial Release                                    */
/* 01/14/97 - Ron Fries - Corrected a minor problem to improve sound quality */
/*                        Also changed names from POKEY11.x to POKEY.x       */
/* 01/17/97 - Ron Fries - Added support for multiple POKEY chips.            */
/* 03/31/97 - Ron Fries - Made some minor mods for MAME (changed to signed   */
/*                        8-bit sample, increased gain range, removed        */
/*                        _disable() and _enable().)                         */
/* 04/06/97 - Brad Oliver - Some cross-platform modifications. Added         */
/*                          big/little endian #defines, removed <dos.h>,     */
/*                          conditional defines for TRUE/FALSE               */
/* 01/19/98 - Ron Fries - Changed signed/unsigned sample support to a        */
/*                        compile-time option.  Defaults to unsigned -       */
/*                        define SIGNED_SAMPLES to create signed.            */
/*                                                                           */
/* V2.0 Detailed Changes                                                     */
/* ---------------------                                                     */
/*                                                                           */
/* Now maintains both a POLY9 and POLY17 counter.  Though this slows the     */
/* emulator in general, it was required to support mutiple POKEYs since      */
/* each chip can individually select POLY9 or POLY17 operation.  Also,       */
/* eliminated the Poly17_size variable.                                      */
/*                                                                           */
/* Changed address of POKEY chip.  In the original, the chip was fixed at    */
/* location D200 for compatibility with the Atari 800 line of 8-bit          */
/* computers. The update function now only examines the lower four bits, so  */
/* the location for all emulated chips is effectively xxx0 - xxx8.           */
/*                                                                           */
/* The Update_pokey_sound function has two additional parameters which       */
/* selects the desired chip and selects the desired gain.                    */
/*                                                                           */
/* Added clipping to reduce distortion, configurable at compile-time.        */
/*                                                                           */
/* The Pokey_sound_init function has an additional parameter which selects   */
/* the number of pokey chips to emulate.                                     */
/*                                                                           */
/* The output will be amplified by gain/16.  If the output exceeds the       */
/* maximum value after the gain, it will be limited to reduce distortion.    */
/* The best value for the gain depends on the number of POKEYs emulated      */
/* and the maximum volume used.  The maximum possible output for each        */
/* channel is 15, making the maximum possible output for a single chip to    */
/* be 60.  Assuming all four channels on the chip are used at full volume,   */
/* a gain of 64 can be used without distortion.  If 4 POKEY chips are        */
/* emulated and all 16 channels are used at full volume, the gain must be    */
/* no more than 16 to prevent distortion.  Of course, if only a few of the   */
/* 16 channels are used or not all channels are used at full volume, a       */
/* larger gain can be used.                                                  */
/*                                                                           */
/* The Pokey_process routine automatically processes and mixes all selected  */
/* chips/channels.  No additional calls or functions are required.           */
/*                                                                           */
/* The unoptimized Pokey_process2() function has been removed.               */
/*                                                                           */
/*****************************************************************************/
/*                                                                           */
/*                 License Information and Copyright Notice                  */
/*                 ========================================                  */
/*                                                                           */
/* PokeySound is Copyright(c) 1996-1998 by Ron Fries                         */
/*                                                                           */
/* This library is free software; you can redistribute it and/or modify it   */
/* under the terms of version 2 of the GNU Library General Public License    */
/* as published by the Free Software Foundation.                             */
/*                                                                           */
/* This library is distributed in the hope that it will be useful, but       */
/* WITHOUT ANY WARRANTY; without even the implied warranty of                */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library */
/* General Public License for more details.                                  */
/* To obtain a copy of the GNU Library General Public License, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/* Any permitted reproduction of these routines, in whole or in part, must   */
/* bear this legend.                                                         */
/*                                                                           */
/*****************************************************************************/

// This code has been modified. See original code for more details
// It does not include Serial Ports, Timers, Key or POT handling.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>

#include "aae_pokey.h"
#include "sys_log.h"
#include "mixer.h"
#include "aae_mame_driver.h"

// --- Core constants for audio generation ---
#define NOTPOLY5    0x80
#define POLY4       0x40
#define PURE        0x20
#define VOL_ONLY    0x10
#define VOLUME_MASK 0x0F

#define POLY9       0x80
#define CH1_179     0x40
#define CH3_179     0x20
#define CH1_CH2     0x10
#define CH3_CH4     0x08
#define CH1_FILTER  0x04
#define CH2_FILTER  0x02
#define CLOCK_15    0x01

#define DIV_64      28
#define DIV_15      114

#define CHAN1       0
#define CHAN2       1
#define CHAN3       2
#define CHAN4       3
#define SAMPLE      127

// Core state
static uint8_t  Num_pokeys;
static uint8_t  AUDF[4 * MAXPOKEYS];
static uint8_t  AUDC[4 * MAXPOKEYS];
static uint8_t  AUDCTL[MAXPOKEYS];
static uint8_t  AUDV[4 * MAXPOKEYS];
static uint8_t  Outbit[4 * MAXPOKEYS];
static uint8_t  Outvol[4 * MAXPOKEYS];

static uint8_t* poly9;
static uint8_t* poly17;
static uint8_t  poly4[0x0F];
static uint8_t  poly5[0x1F];

static uint8_t* rand9;
static uint8_t* rand17;
// Random tracking variables
static uint64_t last_rng_cycle[MAXPOKEYS] = {};
static uint32_t random_pos[MAXPOKEYS] = {};
static uint8_t  pokey_random[MAXPOKEYS] = {};
uint8_t rng[MAXPOKEYS];  // controls random enable

static uint32_t Poly_adjust;
static uint32_t P4 = 0, P5 = 0, P9 = 0, P17 = 0;

static uint32_t Div_n_cnt[4 * MAXPOKEYS];
static uint32_t Div_n_max[4 * MAXPOKEYS];
static uint32_t Samp_n_max;
static uint32_t Samp_n_cnt[2];
static uint32_t Base_mult[MAXPOKEYS];

void test_tempest_rng_pattern()
{
	const int start = 0; // can be adjusted for broader testing
	const int mask = 0x1FFFF;

	bool passed = false;

	for (int offset = 0; offset < 256; ++offset) {
		int p0 = (start + offset + 0) & mask;
		int p4 = (start + offset + 4) & mask;

		uint8_t v0 = rand17[p0] ^ 0xFF;
		uint8_t v4 = rand17[p4] ^ 0xFF;

		uint8_t upper4 = (v0 >> 4) & 0x0F;
		uint8_t lower4 = v4 & 0x0F;

		if (upper4 == lower4) {
			LOG_DEBUG("Tempest RNG match at offset %d: upper nibble 0x%X == lower nibble 0x%X",
				offset, upper4, lower4);
			passed = true;
			break;
		}
	}

	if (!passed)
		LOG_DEBUG("Tempest RNG pattern test FAILED   upper nibble did not propagate.");
}

void dump_rand_sequence(uint8_t chip, bool use_poly9, int start_pos, int count) {
	const uint8_t* table = use_poly9 ? rand9 : rand17;
	const int mask = use_poly9 ? 0x1FF : 0x1FFFF;

	LOG_DEBUG("POKEY[%d] RNG (%s) sequence starting at %d:",
		chip, use_poly9 ? "rand9" : "rand17", start_pos);

	for (int i = 0; i < count; ++i) {
		int pos = (start_pos + i) & mask;
		uint8_t value = table[pos] ^ 0xFF;  // same as returned from RANDOM_C
		LOG_DEBUG("%04X: %02X\n", pos, value);
	}
}

// ----------------------------------------------------------------------------
// Polynomial & random initialization
// ----------------------------------------------------------------------------

static void poly_init(uint8_t* poly, int size, int left, int right, int add) {
	int mask = (1 << size) - 1;
	int x = 0;
	for (int i = 0; i < mask; ++i) {
		*poly++ = x & 1;
		x = ((x << left) + (x >> right) + add) & mask;
	}
}

static void rand_init(uint8_t* rng_arr, int size, int left, int right, int add) {
	int mask = (1 << size) - 1;
	int x = 0;
	for (int i = 0; i < mask; ++i) {
		*rng_arr++ = (size == 17) ? (x >> 6) : x;
		x = ((x << left) + (x >> right) + add) & mask;
	}
}

// ----------------------------------------------------------------------------
// Pokey_sound_init
// ----------------------------------------------------------------------------

int Pokey_sound_init(uint32_t freq17, uint16_t playback_freq, uint8_t num_pokeys) {
	// allocate polynomial tables
	poly9 = (uint8_t*)std::malloc(0x1FF + 1);
	poly17 = (uint8_t*)std::malloc(0x1FFFF + 1);
	rand9 = (uint8_t*)std::malloc(0x1FF + 1);
	rand17 = (uint8_t*)std::malloc(0x1FFFF + 1);
	if (!poly9 || !poly17 || !rand9 || !rand17) {
		pokey_sound_stop();
		return 1;
	}

	// init
	poly_init(poly4, 4, 3, 1, 0x04);
	poly_init(poly5, 5, 3, 2, 0x08);
	poly_init(poly9, 9, 8, 1, 0x180);
	poly_init(poly17, 17, 16, 1, 0x1C000);

	rand_init(rand9, 9, 8, 1, 0x180);
	rand_init(rand17, 17, 16, 1, 0x1C000);

	Poly_adjust = 0;
	P4 = P5 = P9 = P17 = 0;

	Samp_n_max = (freq17 << 8) / playback_freq;
	Samp_n_cnt[0] = Samp_n_cnt[1] = 0;

	for (int i = 0; i < 4 * MAXPOKEYS; ++i) {
		Outvol[i] = Outbit[i] = 0;
		Div_n_cnt[i] = 0;
		Div_n_max[i] = 0x7FFFFFFF;
		AUDF[i] = AUDC[i] = AUDV[i] = 0;
	}
	for (int c = 0; c < MAXPOKEYS; ++c) {
		AUDCTL[c] = 0;
		Base_mult[c] = DIV_64;
		rng[c] = 3;
	}
	// Init our attempt at pokey cycle counting
	// for the pseudo-random number generator
	for (int i = 0; i < MAXPOKEYS; ++i) {
		random_pos[i] = 0;
		last_rng_cycle[i] = 0;
		pokey_random[i] = 0x00; // shift reg = 0, so ^0xFF = 0xFF
	}

	Num_pokeys = num_pokeys;
	return 0;
}

/*****************************************************************************/
/* Module:  Update_pokey_sound()                                             */
/* Purpose: To process the latest control values stored in the AUDF, AUDC,   */
/*          and AUDCTL registers.  It pre-calculates as much information as  */
/*          possible for better performance.  This routine has not been      */
/*          optimized.                                                       */
/*                                                                           */
/* Author:  Ron Fries                                                        */
/* Date:    January 1, 1997                                                  */
/*                                                                           */
/* Inputs:  addr - the address of the parameter to be changed                */
/*          val - the new value to be placed in the specified address        */
/*          gain - specified as an 8-bit fixed point number - use 1 for no   */
/*                 amplification (output is multiplied by gain)              */
/*                                                                           */
/* Outputs: Adjusts local globals - no return value                          */
/*                                                                           */
/*****************************************************************************/

void Update_pokey_sound(uint16_t addr, uint8_t val, uint8_t chip, uint8_t gain)
{
	uint32_t new_val = 0;
	uint8_t chan;
	uint8_t chan_mask = 0;
	uint8_t chip_offs = chip << 2;

	switch (addr & 0x0f)
	{
	case AUDF1_C:
		AUDF[CHAN1 + chip_offs] = val;
		chan_mask = 1 << CHAN1;
		if (AUDCTL[chip] & CH1_CH2) chan_mask |= 1 << CHAN2;
		break;

	case AUDC1_C:
		AUDC[CHAN1 + chip_offs] = val;
		AUDV[CHAN1 + chip_offs] = (val & VOLUME_MASK) * gain;
		chan_mask = 1 << CHAN1;
		break;

	case AUDF2_C:
		AUDF[CHAN2 + chip_offs] = val;
		chan_mask = 1 << CHAN2;
		break;

	case AUDC2_C:
		AUDC[CHAN2 + chip_offs] = val;
		AUDV[CHAN2 + chip_offs] = (val & VOLUME_MASK) * gain;
		chan_mask = 1 << CHAN2;
		break;

	case AUDF3_C:
		AUDF[CHAN3 + chip_offs] = val;
		chan_mask = 1 << CHAN3;
		if (AUDCTL[chip] & CH3_CH4) chan_mask |= 1 << CHAN4;
		break;

	case AUDC3_C:
		AUDC[CHAN3 + chip_offs] = val;
		AUDV[CHAN3 + chip_offs] = (val & VOLUME_MASK) * gain;
		chan_mask = 1 << CHAN3;
		break;

	case AUDF4_C:
		AUDF[CHAN4 + chip_offs] = val;
		chan_mask = 1 << CHAN4;
		break;

	case AUDC4_C:
		AUDC[CHAN4 + chip_offs] = val;
		AUDV[CHAN4 + chip_offs] = (val & VOLUME_MASK) * gain;
		chan_mask = 1 << CHAN4;
		break;

	case AUDCTL_C:
		AUDCTL[chip] = val;
		chan_mask = 0x0F;
		Base_mult[chip] = (val & CLOCK_15) ? DIV_15 : DIV_64;
		break;

	case SKCTL_C:
	{
		// Enable RNG if any of the lower 2 bits of SKCTL are set
		rng[chip] = (val & 0x03) != 0;

		if (!rng[chip])
		{
			// When SKCTL is reset, clear the RNG shift register
			// This causes RANDOM_C to return 0xFF (due to XOR)
			pokey_random[chip] = 0x00;
			random_pos[chip] = 0;
			last_rng_cycle[chip] = get_exact_cyclecount(get_active_cpu());
		}
		return;
	}
	default:
		return;
	}

	auto update_channel_freq = [&](int ch) {
		uint8_t c = ch + chip_offs;

		if (ch == CHAN1) {
			new_val = (AUDCTL[chip] & CH1_179)
				? AUDF[c] + 4
				: (AUDF[c] + 1) * Base_mult[chip];
		}
		else if (ch == CHAN2 && (AUDCTL[chip] & CH1_CH2)) {
			new_val = (AUDCTL[chip] & CH1_179)
				? AUDF[CHAN2 + chip_offs] * 256 + AUDF[CHAN1 + chip_offs] + 7
				: (AUDF[CHAN2 + chip_offs] * 256 + AUDF[CHAN1 + chip_offs] + 1) * Base_mult[chip];
		}
		else if (ch == CHAN3) {
			new_val = (AUDCTL[chip] & CH3_179)
				? AUDF[c] + 4
				: (AUDF[c] + 1) * Base_mult[chip];
		}
		else if (ch == CHAN4 && (AUDCTL[chip] & CH3_CH4)) {
			new_val = (AUDCTL[chip] & CH3_179)
				? AUDF[CHAN4 + chip_offs] * 256 + AUDF[CHAN3 + chip_offs] + 7
				: (AUDF[CHAN4 + chip_offs] * 256 + AUDF[CHAN3 + chip_offs] + 1) * Base_mult[chip];
		}
		else {
			new_val = (AUDF[c] + 1) * Base_mult[chip];
		}

		if (new_val != Div_n_max[c]) {
			Div_n_max[c] = new_val;
			if (Div_n_cnt[c] > new_val)
				Div_n_cnt[c] = new_val;
		}
		};

	for (chan = CHAN1; chan <= CHAN4; chan++) {
		if (chan_mask & (1 << chan)) {
			update_channel_freq(chan);
		}
	}

	for (chan = CHAN1; chan <= CHAN4; chan++) {
		if (chan_mask & (1 << chan)) {
			uint8_t c = chan + chip_offs;
			uint8_t audc = AUDC[c];

			if ((audc & VOL_ONLY) || !(audc & VOLUME_MASK) || (Div_n_max[c] < (Samp_n_max >> 8))) {
				Outvol[c] = 1;

				bool disable = (chan == CHAN3 && !(AUDCTL[chip] & CH1_FILTER)) ||
					(chan == CHAN4 && !(AUDCTL[chip] & CH2_FILTER)) ||
					(chan == CHAN1 || chan == CHAN2) ||
					(Div_n_max[c] < (Samp_n_max >> 8));

				if (disable) {
					Div_n_max[c] = Div_n_cnt[c] = 0x7fffffff;
				}
			}
		}
	}
}

/*****************************************************************************/
/* Module:  Pokey_process()                                                  */
/* Purpose: To fill the output buffer with the sound output based on the     */
/*          pokey chip parameters.                                           */
/*                                                                           */
/* Author:  Ron Fries                                                        */
/* Date:    January 1, 1997                                                  */
/*                                                                           */
/* Inputs:  *buffer - pointer to the buffer where the audio output will      */
/*                    be placed                                              */
/*          n - size of the playback buffer                                  */
/*          num_pokeys - number of currently active pokeys to process        */
/*                                                                           */
/* Outputs: the buffer will be filled with n bytes of audio - no return val  */
/*                                                                           */
/*****************************************************************************/

void Pokey_process(short* buffer, uint16_t n)
{
	uint32_t* samp_cnt_w_ptr = (uint32_t*)((uint8_t*)&Samp_n_cnt[0] + 1);
	int16_t cur_val = 0;

	// Initial output summation (subtract + add each active Outvol)
	for (int i = 0; i < Num_pokeys * 4; ++i) {
		cur_val -= AUDV[i] / 2;
		if (Outvol[i]) cur_val += AUDV[i];
	}

	while (n)
	{
		uint8_t next_event = SAMPLE;
		uint32_t event_min = *samp_cnt_w_ptr;

		// Find next event (minimum Div_n_cnt or sample count)
		for (int i = 0; i < Num_pokeys * 4; ++i) {
			if (Div_n_cnt[i] <= event_min) {
				event_min = Div_n_cnt[i];
				next_event = static_cast<uint8_t>(i);
			}
		}
		// Advance all counters by event_min
		for (int i = 0; i < Num_pokeys * 4; ++i) {
			Div_n_cnt[i] -= event_min;
		}
		*samp_cnt_w_ptr -= event_min;
		Poly_adjust += event_min;

		if (next_event != SAMPLE)
		{
			// Update polynomial positions
			P4 = (P4 + Poly_adjust) % 0x0000f;
			P5 = (P5 + Poly_adjust) % 0x0001f;
			P9 = (P9 + Poly_adjust) % 0x001ff;
			P17 = (P17 + Poly_adjust) % 0x1ffff;
			Poly_adjust = 0;

			Div_n_cnt[next_event] += Div_n_max[next_event];
			uint8_t audc = AUDC[next_event];
			uint8_t* outp = &Outvol[next_event];
			uint8_t chip = next_event >> 2;
			uint8_t chan = next_event & 0x03;
			bool toggle = false;

			// Toggle logic
			if (!(audc & VOL_ONLY)) {
				if ((audc & NOTPOLY5) || poly5[P5]) {
					if (audc & PURE) {
						toggle = true;
					}
					else if (audc & POLY4) {
						toggle = (poly4[P4] == !(*outp));
					}
					else if (AUDCTL[chip] & POLY9) {
						toggle = (poly9[P9] == !(*outp));
					}
					else {
						toggle = (poly17[P17] == !(*outp));
					}
				}
			}

			// Filter suppressions
			auto suppress_filter = [&](uint8_t filter_bit, uint8_t trigger_chan, uint8_t target_chan) {
				if ((AUDCTL[chip] & filter_bit) && chan == trigger_chan) {
					uint8_t index = (chip << 2) | target_chan;
					if (Outvol[index]) {
						Outvol[index] = 0;
						cur_val -= AUDV[index];
					}
				}
				};

			suppress_filter(CH1_FILTER, CHAN3, CHAN1);
			suppress_filter(CH2_FILTER, CHAN4, CHAN2);

			// Toggle output
			if (toggle) {
				if (*outp) {
					cur_val -= AUDV[next_event];
					*outp = 0;
				}
				else {
					cur_val += AUDV[next_event];
					*outp = 1;
				}
			}
		}
		else
		{
			*Samp_n_cnt += Samp_n_max;

			if (cur_val > 127)
				*buffer++ = static_cast<int16_t>(0x7fff);
			else if (cur_val < -128)
				*buffer++ = static_cast<int16_t>(-32768);
			else
				*buffer++ = static_cast<int16_t>(cur_val << 8);

			--n;
		}
	}
}

// ----------------------------------------------------------------------------
// pokey_sound_stop
// ----------------------------------------------------------------------------

void pokey_sound_stop() {
	if (rand17) std::free(rand17), rand17 = nullptr;
	if (poly17) std::free(poly17), poly17 = nullptr;
	if (rand9) std::free(rand9), rand9 = nullptr;
	if (poly9) std::free(poly9), poly9 = nullptr;
}

// ----------------------------------------------------------------------------
// Interface layer (merged from pokyintf.cpp)
// ----------------------------------------------------------------------------

static int    buffer_len;
static int    emulation_rate;
static int    sample_pos;
static int    channel;
static POKEYinterface* intf_ptr;
static int16_t* buffer_ptr;

int pokey_sh_start(POKEYinterface* intfa) {
	intf_ptr = intfa;
	buffer_len = config.samplerate / Machine->gamedrv->fps;
	emulation_rate = buffer_len * Machine->gamedrv->fps;
	sample_pos = 0;

	LOG_INFO("Pokey stream size %d", (16 / 8) * buffer_len);
	buffer_ptr = (int16_t*)std::malloc(buffer_len * sizeof(int16_t));
	if (!buffer_ptr) {
		LOG_ERROR("Pokey buffer allocation failed");
		return 1;
	}
	std::memset(buffer_ptr, 0, buffer_len * sizeof(int16_t));

	if (Pokey_sound_init(intf_ptr->clock, emulation_rate, intf_ptr->num)) {
		std::free(buffer_ptr);
		return 1;
	}

	stream_start(0, 0, 16, Machine->gamedrv->fps);
	return 0;
}

void pokey_sh_stop(void) {
	pokey_sound_stop();
	stream_stop(0, 0);
	std::free(buffer_ptr);
	LOG_INFO("Pokey buffer freed");
}

static void update_pokeys(void) {
	int newpos = cpu_scale_by_cycles(buffer_len, intf_ptr->clock);
	if (newpos - sample_pos < 10)	{ return; }
	Pokey_process(buffer_ptr + sample_pos, newpos - sample_pos);
	sample_pos = newpos;
}

int Read_pokey_regs(uint16_t addr, uint8_t chip) {
	switch (addr & 0x0F) {
	case POT0_C:
		return intf_ptr->pot0_r[chip] ? intf_ptr->pot0_r[chip](addr) : 0;
	case POT1_C:
		return intf_ptr->pot1_r[chip] ? intf_ptr->pot1_r[chip](addr) : 0;
	case POT2_C:
		return intf_ptr->pot2_r[chip] ? intf_ptr->pot2_r[chip](addr) : 0;
	case POT3_C:
		return intf_ptr->pot3_r[chip] ? intf_ptr->pot3_r[chip](addr) : 0;
	case POT4_C:
		return intf_ptr->pot4_r[chip] ? intf_ptr->pot4_r[chip](addr) : 0;
	case POT5_C:
		return intf_ptr->pot5_r[chip] ? intf_ptr->pot5_r[chip](addr) : 0;
	case POT6_C:
		return intf_ptr->pot6_r[chip] ? intf_ptr->pot6_r[chip](addr) : 0;
	case POT7_C:
		return intf_ptr->pot7_r[chip] ? intf_ptr->pot7_r[chip](addr) : 0;
	case ALLPOT_C:
		return intf_ptr->allpot_r[chip] ? intf_ptr->allpot_r[chip](addr) : 0;

	case RANDOM_C:
	{
		if (!rng[chip])
			return pokey_random[chip] ^ 0xFF;  // SKCTL reset: returns 0xFF

		uint64_t now = get_exact_cyclecount(get_active_cpu());
		uint64_t last = last_rng_cycle[chip];
		last_rng_cycle[chip] = now;

		uint64_t pokey_clocks = 0;

		if (now > last) {
			uint64_t elapsed_cycles = now - last;

			// Convert CPU cycles to POKEY clocks
			pokey_clocks = elapsed_cycles * intf_ptr->clock / Machine->drv->cpu_freq[0];
		}

		if (pokey_clocks > 0) {
			if (AUDCTL[chip] & POLY9) {
				random_pos[chip] = (random_pos[chip] + pokey_clocks) & 0x1FF;
				pokey_random[chip] = rand9[random_pos[chip]];
			}
			else {
				random_pos[chip] = (random_pos[chip] + pokey_clocks) & 0x1FFFF;
				pokey_random[chip] = rand17[random_pos[chip]];
			}
		}

		return pokey_random[chip] ^ 0xFF;
	}
	default:
		return 0;
	}
}

int pokey1_r(int offset) { update_pokeys(); return Read_pokey_regs(offset, 0); }
int pokey2_r(int offset) { update_pokeys(); return Read_pokey_regs(offset, 1); }
int pokey3_r(int offset) { update_pokeys(); return Read_pokey_regs(offset, 2); }
int pokey4_r(int offset) { update_pokeys(); return Read_pokey_regs(offset, 3); }

int quad_pokey_r(int offset) {
	int chip = (offset >> 3) & ~0x04;
	int ctrl = (offset & 0x20) >> 2;
	int reg = (offset % 8) | ctrl;
	return Read_pokey_regs(reg, chip);
}

void pokey1_w(int offset, int data) { update_pokeys(); Update_pokey_sound(offset, data, 0, intf_ptr->gain); }
void pokey2_w(int offset, int data) { update_pokeys(); Update_pokey_sound(offset, data, 1, intf_ptr->gain); }
void pokey3_w(int offset, int data) { update_pokeys(); Update_pokey_sound(offset, data, 2, intf_ptr->gain); }
void pokey4_w(int offset, int data) { update_pokeys(); Update_pokey_sound(offset, data, 3, intf_ptr->gain); }

void quad_pokey_w(int offset, int data) {
	int chip = (offset >> 3) & ~0x04;
	int ctrl = (offset & 0x20) >> 2;
	int reg = (offset % 8) | ctrl;
	switch (chip) {
	case 0: pokey1_w(reg, data); break;
	case 1: pokey2_w(reg, data); break;
	case 2: pokey3_w(reg, data); break;
	case 3: pokey4_w(reg, data); break;
	}
}

// AAE read/write callbacks
uint8_t pokey_1_r(uint32_t address, MemoryReadByte*) {
	return pokey1_r(address);
}
uint8_t pokey_2_r(uint32_t address, MemoryReadByte*) {
	return pokey2_r(address);
}
uint8_t pokey_3_r(uint32_t address, MemoryReadByte*) {
	return pokey3_r(address);
}
uint8_t pokey_4_r(uint32_t address, MemoryReadByte*) {
	return pokey4_r(address);
}
uint8_t quadpokey_r(uint32_t address, MemoryReadByte*) {
	return quad_pokey_r(address);
}

void pokey_1_w(uint32_t address, uint8_t data, MemoryWriteByte*) {
	pokey1_w(address & 0x0F, data);
}
void pokey_2_w(uint32_t address, uint8_t data, MemoryWriteByte*) {
	pokey2_w(address & 0x0F, data);
}
void pokey_3_w(uint32_t address, uint8_t data, MemoryWriteByte*) {
	pokey3_w(address & 0x0F, data);
}
void pokey_4_w(uint32_t address, uint8_t data, MemoryWriteByte*) {
	pokey4_w(address & 0x0F, data);
}
void quadpokey_w(uint32_t address, uint8_t data, MemoryWriteByte*) {
	quad_pokey_w(address, data);
}

void pokey_sh_update(void) {
	if (sample_pos < buffer_len)
		Pokey_process(buffer_ptr + sample_pos, buffer_len - sample_pos);
	sample_pos = 0;
	stream_update(0, buffer_ptr);
}