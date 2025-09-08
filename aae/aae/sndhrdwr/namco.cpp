// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
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
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module (Modernized)
//
// Namco 3-voice PROM-based WSG (Pac/Pengo-era) — API-compatible modernization.
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
#include "wav_resample.h"      // linear_interpolation_16 / cubic_interpolation_16

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

// Resampler-managed stream buffer (new[]/delete[] are done inside resampler)
static int16_t* g_stream_buffer = nullptr;
static int      g_stream_buffer_len = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Saturate 32-bit to signed 16-bit
static inline int16_t sat16(int32_t v) noexcept
{
	if (v < -32768) return -32768;
	if (v > 32767) return  32767;
	return static_cast<int16_t>(v);
}

// -----------------------------------------------------------------------------
// Mixer core (emulation-rate): mix `len` samples into `buffer` (int16)
// -----------------------------------------------------------------------------
// Sums 3 (or iface->voices) voices with wave[-8..+7] * vol[0..15],
// scales per old gain contract: out ~= (sum * gain * 256) / (voices * 16)
void namco_update(short* out, int len)
{
	// Scratch int32 staging (to allow filters pre-clamp)
	static std::vector<int32_t> acc32;
	if (static_cast<int>(acc32.size()) < len) acc32.resize(len);
	std::fill(acc32.begin(), acc32.begin() + len, 0);

	const int voices = std::clamp(g_iface ? g_iface->voices : 3, 1, MAX_VOICES);

	for (int v = 0; v < voices; ++v)
	{
		const int32_t f = g_freq[v];
		const int32_t vol = g_volume[v]; // 0..15
		if (vol == 0 || f == 0) continue;

		const int8_t* __restrict w = g_wave[v]; // 32 samples in -8..+7
		int32_t c = g_counter[v];

		// Phase step / 32-sample wavetable.
		int32_t* __restrict dst = acc32.data();

		for (int i = 0; i < len; ++i)
		{
			c += f;
			const int idx = (c >> 15) & 0x1f; // 0..31
			// wave in -8..+7; multiply by volume 0..15
			dst[i] += static_cast<int32_t>(w[idx]) * vol;
		}
		g_counter[v] = c;
	}

	// Scale to 16-bit with original gain concept:
	// old code roughly mapped |sum| -> 8-bit via table: val = i * gain / (voices*16), << 8
	// We approximate this linearly (no table) and saturate properly.
	const int gain = (g_iface ? g_iface->gain : 16);
	const int denom = std::max(voices * 16, 1);
	const int32_t scale = (static_cast<int32_t>(gain) << 8); // *256

	for (int i = 0; i < len; ++i)
	{
		const int64_t s = static_cast<int64_t>(acc32[i]) * scale / denom;
		out[i] = sat16(static_cast<int32_t>(s));
	}
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
int namco_sh_start(struct namco_interface* intf)
{
	g_iface = intf;
	if (!g_iface) return 1;

	// FPS: integer-only flow (per your request)
	const int fps_rounded = Machine->drv->fps;
	g_buffer_len = (g_iface->samplerate > 0) ? (g_iface->samplerate / fps_rounded) : 0;
	g_emulation_rate = g_buffer_len * fps_rounded;

	if (memory_region(REGION_SOUND1)) {
		g_sound_prom_data = memory_region(REGION_SOUND1);
	}
	else {
		g_sound_prom_data = nullptr;
		LOG_ERROR("Namco WSG: REGION_SOUND1 missing — wave ROM will be silent.");
	}

	// Fallback: silence waveforms if PROM missing (still safe)
	for (int w = 0; w < 8; ++w) {
		for (int i = 0; i < 32; ++i) g_wave_rom[w][i] = 0;
	}

	// Decode 4-bit PROM (8 waves × 32 samples) to signed -8..+7
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

	// Output buffer at emulation rate
	g_output_buffer.assign(std::max(g_buffer_len, 0), 0);

	// Resampler stream buffer is managed by the resampler (new[]/delete[])
	g_stream_buffer = nullptr;
	g_stream_buffer_len = 0;

	g_sample_pos = 0;

	// Start your stream (unchanged)
	stream_start(11, 0, 16, fps_rounded);
	return 0;
}

// -----------------------------------------------------------------------------
// Stop
// -----------------------------------------------------------------------------
void namco_sh_stop(void)
{
	// Free resampler-owned buffer
	if (g_stream_buffer) { delete[] g_stream_buffer; g_stream_buffer = nullptr; }
	g_stream_buffer_len = 0;

	// Release other state
	g_output_buffer.clear();
	g_sound_prom_data = nullptr;
	g_iface = nullptr;

	stream_stop(11, 0);
}

// -----------------------------------------------------------------------------
// Per-frame update: render one emulation frame and push one device frame
// -----------------------------------------------------------------------------
void namco_sh_update(void)
{
	// Render remaining space in this emu-frame
	if (g_buffer_len > g_sample_pos) {
		namco_update(g_output_buffer.data() + g_sample_pos, g_buffer_len - g_sample_pos);
	}
	g_sample_pos = 0;

	// Resample to device rate
	const float ratio = (g_emulation_rate > 0)
		? static_cast<float>(config.samplerate) / static_cast<float>(g_emulation_rate)
		: 1.0f;

#if NAMCO_USE_CUBIC_RESAMPLER
	cubic_interpolation_16(
		g_output_buffer.data(), g_buffer_len,
		&g_stream_buffer, &g_stream_buffer_len,
		ratio
	);
#else
	linear_interpolation_16(
		g_output_buffer.data(), g_buffer_len,
		&g_stream_buffer, &g_stream_buffer_len,
		ratio
	);
#endif

	// Ship to mixer
	stream_update(11, g_stream_buffer);
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