// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.(TM) Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E.(TM) Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module (Modernized)
//
// Namco 3-voice PROM-based WSG (Pac/Pengo-era) - API-compatible modernization.
// - Same API and external integration points intact.
// - Improved mixing quality (32-bit accumulate -> int16_t saturate).
// - Predecodes 4-bit PROM waveforms to signed amplitudes (-8..+7).
//
// Notes:
// * Stream/resampler contract is unchanged 
//   allocate/free stream_buffer via new[] and delete[]).
// * Register write timing via doupdate() is preserved.
//
// -----------------------------------------------------------------------------
#define NOMINMAX
#include "namco.h"
#include "aae_mame_driver.h"   // Machine, memory_region, cpu_scale_by_cycles, etc.
#include "mixer.h"
#include "framework.h"         // config.samplerate
//#include "wav_resample.h"      // linear_interpolation_16 / cubic_interpolation_16

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

// -----------------------------------------------------------------------------
// Configuration (tweakable)
// -----------------------------------------------------------------------------

// Use cubic resampling by default (comment to use linear if preferred)
#define NAMCO_USE_CUBIC_RESAMPLER 1

// -----------------------------------------------------------------------------
// Globals (internal)
// -----------------------------------------------------------------------------

// PROM source (MAME region pointer) and predecoded waveform ROM
static const uint8_t* g_sound_prom_data = nullptr;
static std::array<std::array<int8_t, 32>, 8> g_wave_rom{}; // [8 waves][32 samples] in -8..+7

// Driver interface and framing
static namco_interface* g_iface = nullptr;
static int g_buffer_len = 0;       // samples per emulation frame
static int g_emulation_rate = 0;   // g_buffer_len * fps_rounded
static int g_sample_pos = 0;       // write head within frame (for doupdate())

// Output frame buffer at emulation rate (mono 16-bit)
static std::vector<int16_t> g_output_buffer;

// Per-voice state (support up to 8 voices; we only use iface->voices)
static constexpr int MAX_VOICES = 8;
static std::array<int32_t, MAX_VOICES> g_freq{};
static std::array<int32_t, MAX_VOICES> g_counter{};
static std::array<int32_t, MAX_VOICES> g_volume{};
static std::array<const int8_t*, MAX_VOICES> g_wave{};

// Register shadow (Pengo/Pac WSG layout uses 0x1f useful nibbles)
static std::array<uint8_t, 0x20> g_regs{}; // a touch bigger for safety

// No persistent resampler buffer needed -- namco_sh_update allocates and frees
// per frame via the cubic/linear allocating wrappers (new[]/delete[]).

// -----------------------------------------------------------------------------
// Mixer table 
//
// Builds a centre-biased lookup table mapping accumulated voice sums to output
// amplitude using the original MAME gain formula:
//
//   val = i * gain / (voices * 16),  clamped to 127
//
// g_mixer_table_storage holds 256*voices bytes.
// g_mixer_lookup points to the centre of that block so negative indices work,
// matching the original:  mixer_lookup = mixer_table + (voices * 128)
//
// The table produces values in -127..+127 (8-bit signed range).
// Because this file outputs int16_t to the resampler, we scale up by 256
// (left shift 8) when writing to the output buffer so the full 16-bit range
// is used and volume matches what the resampler and mixer expect.
//
// gain is specified as gain*16 in namco_interface (typical: 16 = unity).
// -----------------------------------------------------------------------------
static std::vector<int8_t> g_mixer_table_storage;
static int8_t* g_mixer_lookup = nullptr; // points to centre of storage

// Build (or rebuild) the mixer lookup table.
// Called from namco_sh_start(). Returns true on success.
static bool make_mixer_table(int voices, int gain)
{
	const int count = voices * 128; // entries in each half (+/-)

	g_mixer_table_storage.assign(256 * voices, int8_t(0));
	if (g_mixer_table_storage.empty()) return false;

	// Centre pointer: valid index range is -count .. +(count-1).
	g_mixer_lookup = g_mixer_table_storage.data() + (voices * 128);

	for (int i = 0; i < count; ++i)
	{
		int val = i * gain / (voices * 16);
		if (val > 127) val = 127;

		g_mixer_lookup[i] = static_cast<int8_t>(val);
		g_mixer_lookup[-i] = static_cast<int8_t>(-val);
	}
	return true;
}

// -----------------------------------------------------------------------------
// Frame boundary tail buffer for the resampler.
//
// cubic_into_core uses a Catmull-Rom spline that needs p0 = sample[idx-1] and
// p3 = sample[idx+2] for each output point.  At the very start of a frame,
// idx-1 is out of bounds, so sample_at_safe_i16 clamps it to in[0] instead of
// the true last sample of the previous frame.  That slope discontinuity at
// every frame boundary is audible as a periodic crackle.
//
// Fix: keep the last 2 samples of each rendered frame in g_resamp_tail[], and
// prepend them to a stitched buffer before each resampler call.  The resampler
// then sees correct history at the seam and the discontinuity disappears.
//
// 2 samples of history is enough: Catmull-Rom only looks back 1 (idx-1) and
// forward 2 (idx+1, idx+2), so a 2-sample prefix fully covers the look-back.
// -----------------------------------------------------------------------------
static constexpr int RESAMP_TAIL = 2;
static int16_t g_resamp_tail[RESAMP_TAIL] = { 0, 0 };

// Stitched input buffer: [tail0, tail1, frame...] rebuilt each update.
static std::vector<int16_t> g_stitch_buf;

// -----------------------------------------------------------------------------
// namco_update
//
// Renders `len` samples into `out` (int16_t) at the emulation rate.
//
// Signal path (restored from good_namco.cpp):
//   1. Zero a short scratch buffer.
//   2. For each active voice: accumulate pre-decoded wave sample * volume
//      into scratch, advancing the phase counter.
//   3. Map each scratch value through g_mixer_lookup[] (MAME gain table)
//      to get a value in -127..+127.
//   4. Scale that 8-bit-range value up by 256 (left shift 8) before writing
//      to the int16_t output so the full 16-bit range is used.
//      Without this shift the output sits at ~0.4% of full scale.
// -----------------------------------------------------------------------------
void namco_update(short* out, int len)
{
	// Scratch buffer for voice accumulation -- same role as the original short[].
	// Static so it only grows, never reallocates on steady-state frames.
	static std::vector<int16_t> mix;
	if (static_cast<int>(mix.size()) < len) mix.resize(len);
	std::fill(mix.begin(), mix.begin() + len, int16_t(0));

	const int voices = std::clamp(g_iface ? g_iface->voices : 3, 1, MAX_VOICES);

	for (int v = 0; v < voices; ++v)
	{
		const int32_t f = g_freq[v];
		const int32_t vol = g_volume[v]; // 0..15

		// Skip silent or inactive voices.
		if (vol == 0 || f == 0) continue;

		const int8_t* __restrict w = g_wave[v]; // pre-decoded -8..+7
		int32_t c = g_counter[v];

		for (int i = 0; i < len; ++i)
		{
			c += f;
			// Phase index: upper bits of counter wrapped to 32 entries.
			// Identical to (c >> 15) & 0x1f in the original code.
			mix[i] += static_cast<int16_t>(w[(c >> 15) & 0x1f] * vol);
		}

		g_counter[v] = c;
	}

	// Map through the MAME mixer table, then scale to 16-bit.
	// The table returns -127..+127; shift left 8 to fill the int16_t range.
	const int half = voices * 128;
	for (int i = 0; i < len; ++i)
	{
		const int s = std::clamp(static_cast<int>(mix[i]), -half, half - 1);
		out[i] = static_cast<int16_t>(g_mixer_lookup[s] << 8);
	}
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
int namco_sh_start(struct namco_interface* intf)
{
	g_iface = intf;
	if (!g_iface) return 1;

	// FPS: integer-only flow 
	const int fps_rounded = Machine->drv->fps;
	g_buffer_len = (g_iface->samplerate > 0) ? (g_iface->samplerate / fps_rounded) : 0;
	g_emulation_rate = g_buffer_len * fps_rounded;

	if (memory_region(REGION_SOUND1)) {
		g_sound_prom_data = memory_region(REGION_SOUND1);
	}
	else {
		g_sound_prom_data = nullptr;
		LOG_ERROR("Namco WSG: REGION_SOUND1 missing - wave ROM will be silent.");
	}

	// Fallback: silence waveforms if PROM missing (still safe)
	for (int w = 0; w < 8; ++w) {
		for (int i = 0; i < 32; ++i) g_wave_rom[w][i] = 0;
	}

	// Decode 4-bit PROM (8 waves x 32 samples) to signed -8..+7
	if (g_sound_prom_data) {
		for (int w = 0; w < 8; ++w) {
			const uint8_t* base = &g_sound_prom_data[32 * w];
			for (int i = 0; i < 32; ++i) {
				g_wave_rom[w][i] = static_cast<int8_t>((base[i] & 0x0f) - 8);
			}
		}
	}

	// Voice defaults
	const int voices = std::clamp(g_iface->voices, 1, MAX_VOICES);
	for (int v = 0; v < voices; ++v) {
		g_freq[v] = 0;
		g_volume[v] = 0;
		g_counter[v] = 0;
		// Start on wave 0 safely (will be set by writes)
		g_wave[v] = g_wave_rom[0].data();
	}

	// Build the mixer lookup table using the original MAME gain formula.
	// intf->gain is gain*16 units (typical value: 16 = unity gain).
	if (!make_mixer_table(voices, g_iface->gain)) {
		LOG_ERROR("Namco WSG: failed to allocate mixer table.");
		return 1;
	}

	// Clear the resampler tail history so no stale data bleeds into the first frame.
	g_resamp_tail[0] = 0;
	g_resamp_tail[1] = 0;

	// Output buffer at emulation rate
	g_output_buffer.assign(std::max(g_buffer_len, 0), 0);

	g_sample_pos = 0;

	// Start the stream 
	stream_start(11, 0, 16, fps_rounded);
	return 0;
}

// -----------------------------------------------------------------------------
// Stop
// -----------------------------------------------------------------------------
void namco_sh_stop(void)
{
	// Free resampler-owned buffer
	// (per-frame alloc/free is done inside namco_sh_update; nothing to free here)

	// Release other state
	g_output_buffer.clear();
	g_stitch_buf.clear();
	g_mixer_table_storage.clear();
	g_mixer_lookup = nullptr;
	g_resamp_tail[0] = 0;
	g_resamp_tail[1] = 0;
	g_sound_prom_data = nullptr;
	g_iface = nullptr;

	stream_stop(11, 0);
}

// -----------------------------------------------------------------------------
// Per-frame update: render one emulation frame and push one device frame.
//
// To fix frame-boundary crackle in the Catmull-Rom resampler:
//   We prepend the last RESAMP_TAIL samples of the previous frame to the
//   current frame before resampling.  This gives the resampler correct history
//   at the seam (p0 = idx-1 is now valid instead of clamping to in[0]).
//
// The stitched buffer is [tail0, tail1, frame_0 .. frame_N-1], length N+2.
// We ask the resampler for (output_samples + tail_output) samples and discard
// the first tail_output samples, keeping only the true frame output.
//
// tail_output = floor(RESAMP_TAIL * ratio) -- the number of output samples
// that correspond to the 2 prepended input samples at the current ratio.
// Those leading output samples are thrown away; the rest go to stream_update.
// -----------------------------------------------------------------------------
void namco_sh_update(void)
{
	// Render remaining space in this emulation frame.
	if (g_buffer_len > g_sample_pos) {
		namco_update(g_output_buffer.data() + g_sample_pos,
			g_buffer_len - g_sample_pos);
	}
	g_sample_pos = 0;

	const float ratio = (g_emulation_rate > 0)
		? static_cast<float>(config.samplerate) / static_cast<float>(g_emulation_rate)
		: 1.0f;

	// Build the stitched input: [prev_tail | current_frame]
	const int stitch_in_len = RESAMP_TAIL + g_buffer_len;
	if (static_cast<int>(g_stitch_buf.size()) < stitch_in_len)
		g_stitch_buf.resize(stitch_in_len);

	g_stitch_buf[0] = g_resamp_tail[0];
	g_stitch_buf[1] = g_resamp_tail[1];
	std::memcpy(g_stitch_buf.data() + RESAMP_TAIL,
		g_output_buffer.data(),
		g_buffer_len * sizeof(int16_t));

	// Save the tail of the current frame for next time before we resample.
	if (g_buffer_len >= RESAMP_TAIL) {
		g_resamp_tail[0] = g_output_buffer[g_buffer_len - 2];
		g_resamp_tail[1] = g_output_buffer[g_buffer_len - 1];
	}

	// Resample the full stitched buffer.
	// The resampler will produce floor(stitch_in_len * ratio) output samples.
	int16_t* stitch_out = nullptr;
	int      stitch_out_len = 0;

#if NAMCO_USE_CUBIC_RESAMPLER
	cubic_interpolation_16(
		g_stitch_buf.data(), stitch_in_len,
		&stitch_out, &stitch_out_len,
		ratio
	);
#else
	linear_interpolation_16(
		g_stitch_buf.data(), stitch_in_len,
		&stitch_out, &stitch_out_len,
		ratio
	);
#endif

	// Discard the leading output samples that correspond to the prepended tail.
	// tail_output = how many output samples the RESAMP_TAIL input samples produced.
	const int tail_output = static_cast<int>(std::floor(RESAMP_TAIL * ratio));
	const int true_start = std::min(tail_output, stitch_out_len);
	const int true_len = stitch_out_len - true_start;

	// Ship only the true frame output to the mixer stream.
	// stream_update expects exactly (SYS_FREQ / fps) samples; true_len should
	// match that.  If there is a 1-sample rounding difference it is harmless
	// because stream_update memcpy's only sample->dataSize bytes.
	if (stitch_out && true_len > 0)
		stream_update(11, stitch_out + true_start);

	// Free the resampler-allocated buffer (allocated via new[] inside the wrapper).
	delete[] stitch_out;
}

// -----------------------------------------------------------------------------
// doupdate(): advance mix head to "now" based on CPU cycles
// -----------------------------------------------------------------------------
void doupdate()
{
	if (g_buffer_len <= 0) return;

	const int cpu0_hz = Machine->gamedrv->cpu[0].cpu_freq;
	const int newpos = cpu_scale_by_cycles(g_buffer_len, cpu0_hz);
	const int delta = newpos - g_sample_pos;
	if (delta > 0) {
		namco_update(g_output_buffer.data() + g_sample_pos, delta);
		g_sample_pos = newpos;
	}
}

// -----------------------------------------------------------------------------
// Register writes (Pengo/Pac WSG mapping): recompute freq/vol/wave
// -----------------------------------------------------------------------------
void namco_sound_w(int offset, int data)
{
	if (offset < 0 || offset >= static_cast<int>(g_regs.size())) return;

	doupdate(); // keep audio timing correct

	// 4-bit register write
	g_regs[static_cast<size_t>(offset)] = static_cast<uint8_t>(data) & 0x0f;

	// Recompute voice params (3 voices, base+=5 mapping)
	int base = 0;
	for (int voice = 0; voice < 3; ++voice, base += 5)
	{
		// Frequency accumulator (20 bits for voice 0, else 16 + 4 pad)
		// Original code:
		//   freq = [0x14+base]*16 + 0x13 ... + 0x11, then for voice 0:
		//   freq = freq*16 + 0x10; else freq *= 16
		int32_t f = g_regs[0x14 + base];    // always 0
		f = f * 16 + g_regs[0x13 + base];
		f = f * 16 + g_regs[0x12 + base];
		f = f * 16 + g_regs[0x11 + base];

		if (base == 0)  f = f * 16 + g_regs[0x10 + base];
		else            f = f * 16;

		g_freq[voice] = f;

		// Volume (0..15) and waveform select (3 bits: 0..7)
		g_volume[voice] = (g_regs[0x15 + base] & 0x0f);
		const int wsel = (g_regs[0x05 + base] & 7);
		g_wave[voice] = g_wave_rom[wsel].data();
	}
}