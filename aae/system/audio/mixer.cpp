/* =============================================================================
 * File: mixer.cpp
 * Component: Audio mixer + playback (XAudio2 backend)
 *
 * Overview
 * --------
 * Hybrid audio system providing two playback paths:
 *   1) Per-voice playback (one IXAudio2SourceVoice per channel) for simple,
 *      low-latency one-shot or looping samples.
 *   2) Lightweight software mixer that mixes MONO/STEREO 8/16-bit PCM into an
 *      interleaved S16 buffer submitted to XAudio2 each frame (no callback).
 *
 * The mixer supports fractional update rates (e.g., 29.97/59.94 fps) via a
 * 32.32 fixed-point samples-per-frame accumulator to eliminate long-term drift.
 * Optional VU meters provide smooth left/right peak levels for UI display.
 *
 * Key Types (see mixer.h)
 * -----------------------
 *   CHANNEL : Runtime state of a logical channel (voice, buffer, loop, vol/pan/pitch).
 *   SAMPLE  : Loaded audio (8/16-bit PCM, mono/stereo) + format metadata.
 *
 * Runtime Model
 * -------------
 *   - Call mixer_init(outputRateHz, fpsExact) once at startup.
 *   - Call mixer_update() once per game/app tick; it fills the next output buffer
 *     and hands it to the streaming backend.
 *   - Use the voice path (sample_start/stop/end) for direct XAudio2 playback,
 *     or use the software-mixer path (sample_start_mixer / stream_*) for mixed or
 *     generated audio that you update every frame.
 *
 * Panning & Volume
 * ----------------
 *   - Pan uses a constant-power style mapping from 0..255 (128 = center) to L/R gains.
 *   - Volume accepts 0..255 (or 0..100 via helpers) and is mapped to a perceptual
 *     (dB-tapered) linear amplitude for consistent loudness control.
 *
 * Threading
 * ---------
 *   - A small audio worker is signaled once per frame to run mixer_update_internal().
 *     Synchronization uses a mutex + condition variable. Optional watchdog logging
 *     helps detect stalls or starvation.
 *
 * Most Important Functions
 * ------------------------
 *   // Initialization & lifecycle
 *   void mixer_init(int rate, double fps);
 *       Initializes mixer state and the XAudio2 streaming backend; computes the
 *       frame buffer capacity from (rate / fps).
 *
 *   void mixer_update();
 *       Mixes all active software-mixer voices into the next interleaved S16 buffer
 *       and submits it to the backend.
 *
 *   void mixer_end();
 *       Shuts down playback, releases voices/buffers, and resets mixer state.
 *
 *   // Sample management (loading/saving)
 *   int  load_sample(const char* archname, const char* filename, bool force_resample = true);
 *       Loads audio into a SAMPLE and registers it; optionally resamples to the
 *       system output rate on load.
 *   void save_sample(int samplenum);
 *       Writes a loaded SAMPLE back to disk as a standard WAV.
 *
 *   // Voice path (direct XAudio2 per-channel playback)
 *   void sample_start(int chanid, int samplenum, int loop);
 *   void sample_stop(int chanid);
 *   void sample_end(int chanid);
 *   int  sample_playing(int chanid);
 *       Create/queue/stop source-voice playback for a channel; query playing state.
 *
 *   void sample_set_volume(int chanid, int volume /0..255/);
 *   void sample_set_freq(int chanid, int freq /Hz/);
 *   void sample_set_pan(int chanid, int pan /0..255, 128=center/);
 *   int  sample_get_volume(int chanid);
 *   int  sample_get_freq(int chanid);
 *   int  sample_get_pan(int chanid);
 *       Per-channel volume/pitch/pan control.
 *
 *   // Software-mixer path (mixed in mixer_update)
 *   void sample_start_mixer(int chanid, int samplenum, int loop);
 *   void sample_stop_mixer(int chanid);
 *   void sample_end_mixer(int chanid);
 *       Start/stop a SAMPLE that will be mixed by the software mixer each frame.
 *
 *   // Simple streaming (producer overwrites a fixed buffer each update)
 *   void stream_start(int chanid, int stream, int bits, int frame_rate);
 *   void stream_start(int chanid, int stream, int bits, int frame_rate, bool stereo);
 *   void stream_update(int chanid, short* data);
 *   void stream_update(int chanid, unsigned char* data);
 *   void stream_stop(int chanid, int stream);
 *       Treat a channel as a ring-updated SAMPLE for live/generated audio.
 *
 *   // Utilities & helpers
 *   int  create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name);
 *       Allocate a SAMPLE buffer for generated/streamed audio and register it.
 *   void resample_wav_8(SAMPLE* s, int new_freq);
 *   void resample_wav_16(SAMPLE* s, int new_freq, bool use_cubic = true);
 *       Load-time/utility resamplers used to conform assets to the output rate.
 *   float mixer_get_master_volume();
 *   void  mixer_set_master_volume(int volumePercent);
 *       Master output gain (implemented by the XAudio2 streaming backend).
 *
 * Dependencies
 * ------------
 *   - XAudio2 streaming backend (XAudio2Stream.*) for buffer submission.
 *   - WAV/MP3/OGG loader (wav_file.*) feeding the SAMPLE structure.
 *   - mixer.h for public API/types; mixer_volume.h for dB/linear conversions.
 *
 * Build Notes
 * -----------
 *   - Windows + XAudio2 (xaudio2redist). Link XAudio2 and include the Windows SDK.
 *
 * Limitations
 * -----------
 *   - Default software path mixes to interleaved S16 stereo.
 *   - Pitch changes on the voice path use XAudio2 FrequencyRatio.
 *
 * ---------------------------------------------------------------------------
 * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2022-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */
// This code was updated with assistance from chatgpt
// Note for ME: This is the FULL INTEGER version of this code, for AAE only: 8/15/25
#define NOMINMAX
#include "mixer.h"
#include "framework.h"
#include "wav_file.h"
#include "aae_fileio.h"
#include "XAudio2Stream.h"
#include "wav_resample.h"
#include "helper_functions.h"
#include "mixer_volume.h"
#include "error_wav.h"

#include <mutex>
#include <vector>
#include <list>
#include <atomic>
#include <cmath>
#include <memory>
#include <cstring>
#include <thread>
#include <condition_variable>
#include <chrono>

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

extern IXAudio2* pXAudio2;
static int SYS_FREQ = 44100;
static int BUFFER_SIZE = 0;

constexpr int MAX_CHANNELS = 20;
constexpr int MAX_SOUNDS = 255;

static std::atomic<bool> sound_paused{ false };
static int sound_id = -1;
static float last_master_vol = 1.0f;

static std::mutex audioMutex;
static std::list<int> audio_list;
static std::vector<std::shared_ptr<SAMPLE>> lsamples;

static CHANNEL channel[MAX_CHANNELS];

#ifdef USE_VUMETER
// VU METER ONLY
// Smooth peak meters (0..1), written by audio thread, read by main thread.
static std::atomic<float> g_vuL{ 0.0f };
static std::atomic<float> g_vuR{ 0.0f };

// Simple peak meter ballistics: fast attack (take peak immediately) and slow decay.
static inline float vu_decay_step(float prev, float target, float decayFactor)
{
	// Attack: jump up immediately to new peak
	if (target > prev) return target;
	// Release: multiply by decayFactor (~0.90..0.98 per update @60Hz)
	return prev * decayFactor;
}
#endif

// ----------------------
// Thread management
// ----------------------
static std::thread audioThread;
static std::condition_variable audioCV;
static std::mutex audioCVMutex;
static std::atomic<bool> audioThreadExit{ false };
static std::atomic<bool> audioThreadRun{ false };

// Safeguards
static std::atomic<int> queuedFrames{ 0 };
static std::chrono::steady_clock::time_point lastSignalTime;

// Forward declaration
static void mixer_update_internal();

// constant power panning helper: CHANNEL.pan (0..255, 128=center) -> L/R gains.
static inline void mixer_pan_gains(int panByte, float& gainL, float& gainR)
{
	panByte = std::clamp(panByte, 0, 255);
	const float p = panByte / 255.0f; // 0..1
	if (p <= 0.5f) {         // tilt left->center
		gainL = 1.0f;
		gainR = p * 2.0f;    // 0..1
	}
	else {                  // center->right
		gainL = (1.0f - p) * 2.0f; // 1..0
		gainR = 1.0f;
	}
}

// -----------------------------------------------------------------------------
// Audio Thread Function
// Waits for a signal each frame, then runs mixer_update_internal().
// Includes timing and watchdog logging.
// -----------------------------------------------------------------------------
static void audio_thread_func() {
	LOG_INFO("Audio thread: started");
	std::unique_lock<std::mutex> lock(audioCVMutex);

	while (!audioThreadExit) {
		// Wait with timeout to detect long idle times
		bool woke = audioCV.wait_for(lock, std::chrono::seconds(1), [] {
			return audioThreadExit || audioThreadRun.load();
			});
		if (!woke) {
			LOG_INFO("Audio thread: waited >1s without signal (emulator paused?)");
			continue;
		}
		if (audioThreadExit) break;

		// Check if frames are piling up
		if (queuedFrames > 1) {
			LOG_INFO("Audio thread: %d frames queued (main thread may be signaling too fast)", queuedFrames.load());
		}

		// Check how long since last signal
		auto now = std::chrono::steady_clock::now();
		auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSignalTime).count();
		if (delta > 50) {
			LOG_INFO("Audio thread: WARNING - late signal detected (%lld ms since last frame)", (long long)delta);
		}

		// Measure execution time
		auto start = std::chrono::high_resolution_clock::now();
		mixer_update_internal();
		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

		if (elapsed > 2000) { // log if >2ms
			LOG_INFO("Audio thread: mixer_update_internal() took %lld microseconds", (long long)elapsed);
		}

		queuedFrames = 0;
		audioThreadRun = false;
	}

	LOG_INFO("Audio thread: exiting");
}

unsigned char Make8bit(int16_t sample)
{
	sample >>= 8;
	sample ^= 0x80;
	return static_cast<uint8_t>(sample & 0xFF);
}

short Make16bit(uint8_t sample)
{
	return static_cast<int16_t>(sample - 0x80) << 8;
}

// -----------------------  RESAMPLING CODE BELOW --------------------------------------------------

// Helper: deinterleave / interleave
template<typename T>
static void deinterleave(const T* src, int frames, int ch, std::vector<std::vector<T>>& chans)
{
	chans.assign(ch, std::vector<T>(frames));
	for (int i = 0; i < frames; ++i)
		for (int c = 0; c < ch; ++c)
			chans[c][i] = src[i * ch + c];
}

template<typename T>
static void interleave(const std::vector<std::vector<T>>& chans, int frames, int ch, T* dst)
{
	for (int i = 0; i < frames; ++i)
		for (int c = 0; c < ch; ++c)
			dst[i * ch + c] = chans[c][i];
}

// 8-bit (unsigned) resample: linear only (robust and cheap)
void resample_wav_8(SAMPLE* sample, int new_freq)
{
	if (!sample || !sample->data8) return;

	const int ch = std::max<int>(1, sample->fx.nChannels);
	const int in_samples = static_cast<int>(sample->sampleCount);      // total samples (includes both channels)
	const int in_frames = in_samples / ch;
	if (in_frames <= 0 || sample->fx.nSamplesPerSec <= 0) return;

	const int out_frames = static_cast<int>((int64_t)in_frames * new_freq / sample->fx.nSamplesPerSec);
	if (out_frames <= 0) return;

	// Deinterleave
	std::vector<std::vector<uint8_t>> chans;
	deinterleave(sample->data8.get(), in_frames, ch, chans);

	// Resample each channel
	std::vector<std::vector<uint8_t>> chans_out(ch, std::vector<uint8_t>(out_frames));
	for (int c = 0; c < ch; ++c) {
		linear_interpolation_8(chans[c].data(), chans_out[c].data(), in_frames, out_frames);
	}

	// Interleave to a new owned buffer
	auto out = std::make_unique<uint8_t[]>(out_frames * ch);
	interleave(chans_out, out_frames, ch, out.get());

	// Publish
	sample->data8 = std::move(out);
	sample->data16.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = out_frames * ch;                   // bytes (8-bit)
	sample->sampleCount = out_frames * ch;               // total samples
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data8.get();

	LOG_INFO("Resampled 8-bit Sample #%d (%s): %d ch, %d -> %d Hz, %d -> %d frames",
		sample->num, sample->name.c_str(), ch,
		(int)sample->fx.nSamplesPerSec, new_freq, in_frames, out_frames);
}

// 16-bit resample: cubic by default for better quality (falls back easy)
void resample_wav_16(SAMPLE* sample, int new_freq, bool use_cubic /*= true*/)
{
	if (!sample || !sample->data16) return;

	const int ch = std::max<int>(1, sample->fx.nChannels);
	const int in_samples = static_cast<int>(sample->sampleCount);      // total samples (includes both channels)
	const int in_frames = in_samples / ch;
	if (in_frames <= 0 || sample->fx.nSamplesPerSec <= 0) return;

	const int out_frames = static_cast<int>((int64_t)in_frames * new_freq / sample->fx.nSamplesPerSec);
	if (out_frames <= 0) return;

	// Deinterleave
	std::vector<std::vector<int16_t>> chans;
	deinterleave(sample->data16.get(), in_frames, ch, chans);

	// Resample each channel into owned storage
	std::vector<std::vector<int16_t>> chans_out(ch, std::vector<int16_t>(out_frames));
	for (int c = 0; c < ch; ++c) {
		if (use_cubic)
			cubic_interpolation_16_into(chans[c].data(), in_frames, chans_out[c].data(), out_frames);
		else
			linear_interpolation_16_into(chans[c].data(), in_frames, chans_out[c].data(), out_frames);
	}

	// Interleave to a new owned buffer
	auto out = std::make_unique<int16_t[]>(out_frames * ch);
	interleave(chans_out, out_frames, ch, out.get());

	// Publish
	sample->data16 = std::move(out);
	sample->data8.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = out_frames * ch * sizeof(int16_t);
	sample->sampleCount = out_frames * ch;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data16.get();

	LOG_INFO("Resampled 16-bit Sample #%d (%s): %d ch, %s, %d -> %d Hz, %d -> %d frames",
		sample->num, sample->name.c_str(), ch, (use_cubic ? "cubic" : "linear"),
		(int)sample->fx.nSamplesPerSec, new_freq, in_frames, out_frames);
}

// -----------------------  END RESAMPLING CODE --------------------------------------------------

int load_sample(const char* archname, const char* filename, bool force_resample)
{
	auto sample = std::make_shared<SAMPLE>();
	std::unique_ptr<uint8_t[]> sample_data;
	int sample_size = 0;

	if (archname) {
		sample_data.reset(load_zip_file(archname, filename));
		sample_size = get_last_zip_file_size();
	}
	else {
		sample_data.reset(load_file(filename));
		sample_size = get_last_file_size();
	}

	HRESULT result = S_OK;
	if (!sample_data) {
		LOG_ERROR("Error: File not found %s, loading fallback error.wav", filename);
		result = WavLoadFileInternal(error_wav, 10008, sample.get());
	}
	else {
		result = WavLoadFileInternal(sample_data.get(), sample_size, sample.get());
	}

	if (FAILED(result)) {
		LOG_ERROR("Error loading WAV file: %s", filename);
		return -1;
	}

	sample->name = base_name(remove_extension(filename));
	sample->state = SoundState::Loaded;
	sample->num = ++sound_id;

	if (force_resample && sample->fx.nSamplesPerSec != SYS_FREQ) {
		if (sample->fx.wBitsPerSample == 8) resample_wav_8(sample.get(), SYS_FREQ);
		else                                resample_wav_16(sample.get(), SYS_FREQ, /*use_cubic=*/true);
	}

	LOG_INFO("Loaded file %s with ID %d", filename, sample->num);
	{
		std::scoped_lock lock(audioMutex);
		lsamples.push_back(sample);
	}
	LOG_INFO("Wav File Load completed");
	return sample->num;
}

// -----------------------------------------------------------------------------
// mixer_init
// Initializes the mixer with an exact (possibly fractional) frames-per-second
// clock and starts the audio worker thread. Audio generation remains frame-locked:
// one call to mixer_update() per emulation/video frame produces and submits that
// frame s audio. Variable samples-per-frame are handled via a 32.32 fixed-point
// accumulator to ensure zero long-term drift.
//
// Parameters:
//   rate - output sample rate in Hz (e.g., 44100 or 48000)
//   fps  - exact emulation/video frame rate (supports fractional,
//          e.g., 60000.0/1001.0 for 59.94, 24000.0/1001.0 for 23.976)
//
// Behavior:
//   - BUFFER_SIZE is kept as a nominal integer size using a rounded FPS; it s
//     used only for logging and initial scratch sizing. The actual per-frame
//     mix length is computed each frame from  fps  and may be N or N+1 samples.
//   - XAudio2 is initialized with the rounded (nominal) FPS for sizing/logging.
// -----------------------------------------------------------------------------
void mixer_init(int rate, int fps)  // <<< integer FPS
{
	// Fixed buffer size for each frame/update
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;
		
	LOG_INFO("Mixer init, BUFFER SIZE = %d, freq %d framerate %d", BUFFER_SIZE, rate, fps);


	// Initialize the xaudio2 backend at the correct rate.
	xaudio2_init(rate, fps);

	// Reset channel state
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		channel[i] = CHANNEL();
	}

	sound_paused = false;

	// Start audio worker
	audioThreadExit = false;
	audioThreadRun = false;
	queuedFrames = 0;
	audioThread = std::thread(audio_thread_func);
}

// -----------------------------------------------------------------------------
// mixer_update_internal
// Software mix to an interleaved 16-bit stereo ring buffer for XAudio2.
// - Runs once per emulation/video frame (frame-locked).
// - Supports fractional FPS by mixing a variable number of samples per frame,
//   computed via a 32.32 fixed-point accumulator (g_spf_fp/g_spf_accum).
// - Mono sources: pan is IGNORED (centered). We duplicate to L/R pre-pan.
// - Stereo sources: pan is applied as a post-balance using equal-power gains.
//
// Notes:
//   - Uses per-call variable length: xaudio2_update(soundbuffer, frames*4).
//   - Scratch accumulators auto-grow to the largest frame seen (no shrink),
//     avoiding reallocs on alternating N / N+1 frames.
// -----------------------------------------------------------------------------
static void mixer_update_internal()
{
	BYTE* soundbuffer = GetNextBuffer();
	int16_t* out = reinterpret_cast<int16_t*>(soundbuffer);

	std::scoped_lock lock(audioMutex);

	// Fixed number of samples each frame (integer FPS path)
	const int samplesThisFrame = BUFFER_SIZE;
	if (samplesThisFrame <= 0) {
		xaudio2_update(soundbuffer, 0);
		return;
	}

	// ----- one-time scratch buffers for the per-sample accumulators (no per-call alloc) -----
	static std::unique_ptr<int32_t[]> accumL;
	static std::unique_ptr<int32_t[]> accumR;
	static int scratchSize = 0;
	if (samplesThisFrame > scratchSize) {
		accumL.reset(new int32_t[samplesThisFrame]);
		accumR.reset(new int32_t[samplesThisFrame]);
		scratchSize = samplesThisFrame;
	}

	// ---- mix and track peaks in the same pass ----
	int32_t peak = 0; // max |L| or |R|
	int32_t peakL = 0; // max |L|
	int32_t peakR = 0; // max |R|

	for (int i = 0; i < samplesThisFrame; ++i)
	{
		int32_t fmixL = 0;
		int32_t fmixR = 0;

		if (!sound_paused)
		{
			for (auto it = audio_list.begin(); it != audio_list.end(); )
			{
				int chan = *it;
				auto& ch = channel[chan];
				auto& sample = lsamples[ch.loaded_sample_num];

				// End-of-sample handling
				if (ch.pos >= sample->sampleCount)
				{
					if (!ch.looping) {
						ch.state = SoundState::Stopped;
						it = audio_list.erase(it);
						continue;
					}
					ch.pos = 0;
				}

				// Decode one frame (mono or stereo)
				int32_t sL = 0, sR = 0;
				const int chCount = sample->fx.nChannels;     // 1 or 2
				const int bits = sample->fx.wBitsPerSample; // 8 or 16

				if (bits == 16 && sample->data16)
				{
					if (chCount == 2) {
						sL = static_cast<int32_t>(sample->data16[ch.pos + 0]);
						sR = static_cast<int32_t>(sample->data16[ch.pos + 1]);
					}
					else {
						const int32_t s = static_cast<int32_t>(sample->data16[ch.pos]);
						sL = sR = s;
					}
				}
				else if (bits == 8 && sample->data8)
				{
					if (chCount == 2) {
						sL = static_cast<int32_t>((sample->data8[ch.pos + 0] - 128) << 8);
						sR = static_cast<int32_t>((sample->data8[ch.pos + 1] - 128) << 8);
					}
					else {
						const int32_t s = static_cast<int32_t>((sample->data8[ch.pos] - 128) << 8);
						sL = sR = s;
					}
				}

				// Channel gain
				const float vol = static_cast<float>(ch.vol);

				// Pan gains:
				// - Mono: ignore pan entirely (center) -> gL=gR=1
				// - Stereo: equal-power balance
				float gL = 1.0f, gR = 1.0f;
				if (chCount == 2) {
					mixer_pan_gains(ch.pan, gL, gR);
				}

				fmixL += static_cast<int32_t>(sL * vol * gL);
				fmixR += static_cast<int32_t>(sR * vol * gR);

				ch.pos += chCount;
				++it;
			}
		}

		accumL[i] = fmixL;
		accumR[i] = fmixR;

		// Track peaks on-the-fly
		const int32_t a = std::abs(fmixL);
		const int32_t b = std::abs(fmixR);
		if (a > peakL) peakL = a;
		if (b > peakR) peakR = b;
		const int32_t p = (a > b) ? a : b;
		if (p > peak) peak = p;
	}

#ifdef USE_VUMETER
	// ---- VU calculation  ----
	constexpr float kInvFullScale = 1.0f / 32767.0f;
	constexpr float kDecay = 0.94f;
	float curL = std::min(1.0f, peakL * kInvFullScale);
	float curR = std::min(1.0f, peakR * kInvFullScale);

	float prevL = g_vuL.load(std::memory_order_relaxed);
	float prevR = g_vuR.load(std::memory_order_relaxed);
	g_vuL.store(vu_decay_step(prevL, curL, kDecay), std::memory_order_relaxed);
	g_vuR.store(vu_decay_step(prevR, curR, kDecay), std::memory_order_relaxed);
#endif

	// ---- write out with limiter (one pass) ----
	if (peak > INT16_MAX) {
		const float g = 32767.0f / static_cast<float>(peak);
		for (int i = 0; i < samplesThisFrame; ++i) {
			out[2 * i + 0] = static_cast<int16_t>(std::lrintf(accumL[i] * g));
			out[2 * i + 1] = static_cast<int16_t>(std::lrintf(accumR[i] * g));
		}
	}
	else {
		// No limiting needed; clamp just in case
		for (int i = 0; i < samplesThisFrame; ++i) {
			int32_t L = std::clamp(accumL[i], static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
			int32_t R = std::clamp(accumR[i], static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
			out[2 * i + 0] = static_cast<int16_t>(L);
			out[2 * i + 1] = static_cast<int16_t>(R);
		}
	}

	// Submit variable-length frame (stereo 16-bit = 4 bytes per frame)
	xaudio2_update(soundbuffer, static_cast<DWORD>(samplesThisFrame * 4));
}



// -----------------------------------------------------------------------------
// mixer_update
// Now only signals the audio thread to run mixer_update_internal().
// -----------------------------------------------------------------------------
void mixer_update()
{
	lastSignalTime = std::chrono::steady_clock::now();
	queuedFrames++;
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		audioThreadRun = true;
	}
	audioCV.notify_one();
}

void mixer_end()
{
	// Stop the audio thread
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		audioThreadExit = true;
	}
	audioCV.notify_one();
	if (audioThread.joinable()) audioThread.join();

	xaudio2_stop();
	std::scoped_lock lock(audioMutex);
	for (auto& sample : lsamples) {
		if (sample->buffer)
			LOG_INFO("Freeing sample #%d named %s", sample->num, sample->name.c_str());
	}
	lsamples.clear();
	audio_list.clear();
}

void sample_stop(int chanid)
{
	auto& ch = channel[chanid];
	if (ch.isPlaying && ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.looping = 0;
		ch.pos = 0;
	}
}

// -----------------------------------------------------------------------------
// SetPan
// Applies panning/balance on a source voice's output matrix.
// Behavior:
//   - Mono source (1->2): pan is ignored; always centered (1.0, 1.0).
//   - Stereo source (2->2): equal-power balance using mixer_pan_gains().
// -----------------------------------------------------------------------------
static void SetPan(IXAudio2SourceVoice* voice, int panByte)
{
	if (!voice) return;

	XAUDIO2_VOICE_DETAILS details{};
	voice->GetVoiceDetails(&details);
	const UINT32 srcCh = details.InputChannels;
	const UINT32 dstCh = 2; // mastering voice is stereo now

	if (srcCh == 1 && dstCh == 2)
	{
		// Mono -> Stereo: center (ignore pan)
		const float m[2] = { 1.0f, 1.0f };
		voice->SetOutputMatrix(nullptr, 1, 2, m);
	}
	else if (srcCh == 2 && dstCh == 2)
	{
		// Stereo balance with equal-power gains
		float gL = 1.0f, gR = 1.0f;
		mixer_pan_gains(panByte, gL, gR);

		// 2x2 matrix (row-major): [L->L, L->R, R->L, R->R]
		const float m[4] = {
			gL, 0.0f,
			0.0f, gR
		};
		voice->SetOutputMatrix(nullptr, 2, 2, m);
	}
	else
	{
		// Fallback: identity routing (no pan)
		std::vector<float> m(srcCh * dstCh, 0.0f);
		const UINT32 minCh = (srcCh < dstCh) ? srcCh : dstCh;
		for (UINT32 c = 0; c < minCh; ++c) m[c * dstCh + c] = 1.0f;
		voice->SetOutputMatrix(nullptr, srcCh, dstCh, m.data());
	}
}

void sample_remove(int samplenum)
{
}

void sample_start(int chanid, int samplenum, int loop)
{
	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	auto& ch = channel[chanid];

	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}

	auto& sample = lsamples[samplenum];

	// the 8.0f is really important here and required for the StarCastle drone.
	if (FAILED(pXAudio2->CreateSourceVoice(&ch.voice, &sample->fx, 0, 8.0f)))
	{
		LOG_ERROR("Failed to create voice for sample %d", sample->num);
		return;
	}

	ch.isAllocated = true;
	ch.isReleased = false;
	ch.isPlaying = true;
	ch.looping = loop;
	ch.volume = 255;
	ch.pan = 128;
	ch.frequency = sample->fx.nSamplesPerSec;
	ch.loaded_sample_num = samplenum;

	ch.buffer.AudioBytes = sample->dataSize;
	ch.buffer.pAudioData = static_cast<BYTE*>(sample->buffer);
	ch.buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
	ch.voice->SubmitSourceBuffer(&ch.buffer);
	// We have to set the volume manually here to avoid a scoped_lock recursive error.
	const float gain = static_cast<float>(VolumeByteToLinear(ch.volume)); // avoid double->float warning
	ch.vol = gain;
	if (ch.voice) {
		ch.voice->SetVolume(static_cast<float>(ch.vol));
	}
	SetPan(ch.voice, ch.pan);

	HR(ch.voice->Start());
}

int sample_get_position(int chanid)
{
	return channel[chanid].pos;
}

// -----------------------------------------------------------------------------
// sample_set_volume
// Accepts 0..255 from older code; internally converts to 0..100 and uses
// the shared curve so both sample voices and mixer path feel identical.
// -----------------------------------------------------------------------------
void sample_set_volume(int chanid, int volume)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_volume: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);  

	auto& ch = channel[chanid];

	// Convert 0..255 byte to linear gain using your existing taper
	const float gain = static_cast<float>(VolumeByteToLinear(volume)); // avoid double->float warning
	ch.vol = gain;

	// Update the live XAudio2 source voice if this channel uses one
	if (ch.voice) {
		ch.voice->SetVolume(static_cast<float>(ch.vol));
	}
}

int sample_get_volume(int chanid)
{ // returns 0..255
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 0;
	double g = std::clamp(channel[chanid].vol, 0.0, 1.0);
	return static_cast<int>(std::lround(g * 255.0));
}

void sample_set_position(int chanid, int pos)
{
	// Placeholder: currently unused
}

int sample_get_freq(int chanid)
{
	if (channel[chanid].isPlaying)
	{
		return channel[chanid].frequency;
	}
	return 0;
}

void sample_set_freq(int chanid, int freq)
{
	auto& ch = channel[chanid];
	if (ch.isPlaying && ch.voice) {
		float ratio = static_cast<float>(freq) / static_cast<float>(ch.frequency);
		ch.voice->SetFrequencyRatio(ratio);
	}
}

// -----------------------------------------------------------------------------
// sample_set_pan
// Set panning (0..255; 128=center). Mono sources ignore pan in both the
// software mixer and XAudio2 voice path (always centered).
//
// Parameters:
//   chanid - channel index
//   pan    - 0..255, where 0=left, 128=center, 255=right
// -----------------------------------------------------------------------------
void sample_set_pan(int chanid, int pan)
{
	std::scoped_lock lock(audioMutex);  

	pan = std::clamp(pan, 0, 255);
	auto& ch = channel[chanid];
	ch.pan = pan; // software mixer reads this each frame

	// If this channel uses a direct XAudio2 source voice, update its matrix too.
	if (ch.voice) {
		SetPan(ch.voice, ch.pan);
	}
}

// -----------------------------------------------------------------------------
// sample_get_pan
// Get current pan value (0..255; 128=center) for a given channel.
//
// Parameters:
//   chanid - channel index
//
// Returns:
//   0..255 pan value (0=left, 128=center, 255=right). Returns 128 on bad id.
// -----------------------------------------------------------------------------
int sample_get_pan(int chanid)
{
	std::scoped_lock lock(audioMutex);

	if (chanid < 0 || chanid >= MAX_CHANNELS)
		return 128; // safe default

	return channel[chanid].pan;
}

int sample_playing(int chanid)
{
	auto& ch = channel[chanid];
	if (ch.voice) {
		XAUDIO2_VOICE_STATE state;
		ch.voice->GetState(&state);
		return (state.BuffersQueued > 0) ? 1 : 0;
	}
	return 0;
}

void samples_stop_all()
{
	std::scoped_lock lock(audioMutex);

	// Stop all channels (direct voices and software-mixed)
	for (int i = 0; i < MAX_CHANNELS; ++i)
	{
		auto& ch = channel[i];

		// Stop and flush any XAudio2 source voice
		if (ch.voice) {
			ch.voice->Stop(0);
			ch.voice->FlushSourceBuffers();
		}

		// Reset channel state (mirror of sample_stop() behavior)
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.looping = 0;
		ch.pos = 0;
		ch.loaded_sample_num = -1;
		ch.stream_type = 0;
	}

	// Clear the software-mixer active list
	audio_list.clear();
#ifdef USE_VUMETER
	mixer_reset_vu();
#endif

	LOG_INFO("All samples stopped.");
}

void sample_end(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_start_mixer(int chanid, int samplenum, int loop)
{
	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	if (channel[chanid].state == SoundState::Playing) {
		LOG_ERROR("Error: Sound already playing on channel %d", chanid);
		return;
	}

	auto& ch = channel[chanid];
	ch.state = SoundState::Playing;
	ch.stream_type = static_cast<int>(SoundState::PCM);
	ch.loaded_sample_num = samplenum;
	ch.looping = loop;
	ch.pos = 0;

	audio_list.push_back(chanid);
	LOG_INFO("Playing Sample #%d :%s", samplenum, lsamples[samplenum]->name.c_str());
}

int sample_playing_mixer(int chanid)
{
	return (channel[chanid].state == SoundState::Playing) ? 1 : 0;
}

void sample_end_mixer(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_stop_mixer(int chanid)
{
	std::scoped_lock lock(audioMutex);
	channel[chanid].state = SoundState::Stopped;
	channel[chanid].looping = 0;
	channel[chanid].pos = 0;
	audio_list.remove(chanid);
}

void sample_set_volume_mixer(int chanid, int volume255)
{
	const int percent = Volume255ToPercent(std::clamp(volume255, 0, 255));
	channel[chanid].vol = VolumePercentToLinear(percent); // replaces db_volume[]
}

// Stereo
void stream_start(int chanid, int /*stream*/, int bits, int frame_rate, bool stereo)
{
	// len = frames per update; create_sample stores counts properly for 1 or 2 channels
	int stream_sample = create_sample(bits, stereo, SYS_FREQ, SYS_FREQ / frame_rate, "STREAM");

	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		LOG_ERROR("Error: Stream already playing on channel %d", chanid);
		return;
	}

	ch.state = SoundState::Playing;
	ch.loaded_sample_num = stream_sample;
	ch.looping = 1;
	ch.pos = 0;
	ch.stream_type = static_cast<int>(SoundState::Stream);

	std::scoped_lock lock(audioMutex);
	audio_list.push_back(chanid);
}

// Create a mono stream:
void stream_start(int chanid, int stream, int bits, int frame_rate)
{
	stream_start(chanid, stream, bits, frame_rate, /*stereo=*/false);
}

void stream_stop(int chanid, int /*stream*/)
{
	auto& ch = channel[chanid];
	ch.state = SoundState::Stopped;
	ch.loaded_sample_num = -1;
	ch.looping = 0;
	ch.pos = 0;

	std::scoped_lock lock(audioMutex);
	audio_list.remove(chanid);
}

void stream_update(int chanid, short* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		auto& sample = lsamples[ch.loaded_sample_num];
		std::memcpy(sample->data16.get(), data, sample->dataSize);
	}
}

void stream_update(int chanid, unsigned char* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		auto& sample = lsamples[ch.loaded_sample_num];
		std::memcpy(sample->data8.get(), data, sample->dataSize);
	}
}

void restore_audio()
{
	// last_master_vol is linear (0..1). Convert to 0..100 percent.
	int percent = static_cast<int>(std::lround(std::clamp(last_master_vol, 0.0f, 1.0f) * 100.0f));
	mixer_set_master_volume(percent);
	sound_paused = false;
}

void pause_audio()
{
	last_master_vol = mixer_get_master_volume();
	mixer_set_master_volume(0);
	sound_paused = true;
#ifdef USE_VUMETER
	mixer_reset_vu();
#endif
}

// -----------------------------------------------------------------------------
// create_sample
// Allocates an empty PCM sample buffer.
// - sampleCount is stored as "elements" (samples), i.e., frames * channels.
// - dataSize is bytes = sampleCount * (bits/8).
// -----------------------------------------------------------------------------
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name)
{
	// Minimal input guards
	if (freq <= 0 || len <= 0) {
		LOG_ERROR("create_sample: invalid freq=%d or len=%d", freq, len);
		return -1;
	}

	// Normalize bits (support 8 or 16); fallback to 16-bit on odd inputs
	if (bits != 8 && bits != 16) {
		LOG_ERROR("create_sample: unsupported bits=%d, defaulting to 16-bit", bits);
		bits = 16;
	}

	auto sample = std::make_shared<SAMPLE>();
	sample->num = ++sound_id;
	sample->name = (name == "STREAM") ? name + std::to_string(sample->num) : name;

	LOG_INFO("Creating Audio Sample with name %s and sound id %d", sample->name.c_str(), sample->num);

	sample->fx.wFormatTag = WAVE_FORMAT_PCM;
	sample->fx.nChannels = is_stereo ? 2 : 1;
	sample->fx.nSamplesPerSec = freq;
	sample->fx.wBitsPerSample = static_cast<WORD>(bits);
	sample->fx.nBlockAlign = static_cast<WORD>(sample->fx.nChannels * (bits / 8));
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->fx.cbSize = 0; // PCM baseline
	sample->state = SoundState::Loaded;

	// len is in FRAMES; store "elements" (interleaved samples)
	const uint32_t channels = sample->fx.nChannels;
	sample->sampleCount = static_cast<uint32_t>(len) * channels;
	sample->dataSize = sample->sampleCount * (bits / 8);

	if (bits == 8) {
		sample->data8 = std::make_unique<uint8_t[]>(sample->dataSize);
		std::memset(sample->data8.get(), 0, sample->dataSize);
		sample->buffer = sample->data8.get();
	}
	else { // 16-bit
		sample->data16 = std::make_unique<int16_t[]>(sample->sampleCount);
		std::memset(sample->data16.get(), 0, sample->dataSize);
		sample->buffer = sample->data16.get();
	}

	std::scoped_lock lock(audioMutex);
	lsamples.push_back(sample);
	return sample->num;
}

std::string numToName(int num)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample->num == num) {
			return sample->name;
		}
	}
	LOG_ERROR("Name not found for Sample #%d!", num);
	return "";
}

int nameToNum(const std::string& name)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample->name == name) {
			return sample->num;
		}
	}
	return -1;
}

int snumlookup(int snum)
{
	std::scoped_lock lock(audioMutex);
	for (size_t i = 0; i < lsamples.size(); ++i) {
		if (lsamples[i]->num == snum) {
			return static_cast<int>(i);
		}
	}
	LOG_ERROR("Sample number not found in lookup: %d", snum);
	return -1;
}

void save_sample(int samplenum)
{
	std::scoped_lock lock(audioMutex);
	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) return;

	const auto& sample = lsamples[samplenum];
	if (!sample) return;

	std::string filename = sample->name + ".wav";
	FILE* file = nullptr;
	if (fopen_s(&file, filename.c_str(), "wb") != 0 || !file) {
		LOG_ERROR("Failed to open file for writing: %s", filename.c_str());
		return;
	}

	DWORD subchunk1Size = 16;
	DWORD subchunk2Size = static_cast<DWORD>(sample->dataSize);
	DWORD chunkSize = 4 + (8 + subchunk1Size) + (8 + subchunk2Size);

	fwrite("RIFF", 1, 4, file);
	fwrite(&chunkSize, 4, 1, file);
	fwrite("WAVE", 1, 4, file);
	fwrite("fmt ", 1, 4, file);
	fwrite(&subchunk1Size, 4, 1, file);
	fwrite(&sample->fx.wFormatTag, 2, 1, file);
	fwrite(&sample->fx.nChannels, 2, 1, file);
	fwrite(&sample->fx.nSamplesPerSec, 4, 1, file);
	fwrite(&sample->fx.nAvgBytesPerSec, 4, 1, file);
	fwrite(&sample->fx.nBlockAlign, 2, 1, file);
	fwrite(&sample->fx.wBitsPerSample, 2, 1, file);
	fwrite("data", 1, 4, file);
	fwrite(&subchunk2Size, 4, 1, file);

	if (sample->fx.wBitsPerSample == 8 && sample->data8) {
		fwrite(sample->data8.get(), 1, sample->dataSize, file);
	}
	else if (sample->fx.wBitsPerSample == 16 && sample->data16) {
		fwrite(sample->data16.get(), 1, sample->dataSize, file);
	}

	fclose(file);
	LOG_INFO("WAV file saved to %s", filename.c_str());
}

#ifdef USE_VUMETER
void mixer_get_vu(float* left, float* right)
{
	if (left)  *left = std::clamp(g_vuL.load(std::memory_order_relaxed), 0.0f, 1.0f);
	if (right) *right = std::clamp(g_vuR.load(std::memory_order_relaxed), 0.0f, 1.0f);
}

void mixer_reset_vu()
{
	g_vuL.store(0.0f, std::memory_order_relaxed);
	g_vuR.store(0.0f, std::memory_order_relaxed);
}
#endif