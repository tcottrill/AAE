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

#include "mixer.h"
#include "framework.h"
#include "error_wav.h"
#include "sys_log.h"
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
#include <unordered_map>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

//#define OGG_DECODE
//#define MP3_DECODE

#ifdef OGG_DECODE
#include "stb_vorbis.h"
#endif

#ifdef MP3_DECODE
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

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

//Xaudio2 Globals Section
const int NUM_BUFFERS = 5;
IXAudio2* pXAudio2 = nullptr;
IXAudio2MasteringVoice* pMasterVoice = nullptr;
IXAudio2SourceVoice* pSourceVoice = nullptr;
BYTE* audioBuffers[NUM_BUFFERS];
DWORD                   bufferSize = 0;
static bool             g_comInitLocal = false;
int UpdatesPerSecond = 60;
int SamplesPerSecond = 22050;
int BufferDurationMs = 1000 / UpdatesPerSecond;
int SamplesPerBuffer = SamplesPerSecond / UpdatesPerSecond;
int currentBufferIndex = 0;
//
// END XAudio2 Globals

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
// FIX: Add flag to track if mixer is initialized (thread is running)
static std::atomic<bool> audioThreadActive{ false };

// Safeguards
static std::atomic<int> queuedFrames{ 0 };
static std::chrono::steady_clock::time_point lastSignalTime;

// Forward declaration
static void mixer_update_internal();

// 0..255 (128=center).
// Uses "Square Root" law for constant power (volume stays consistent across pan).
static inline void mixer_pan_gains(int panByte, float& gainL, float& gainR)
{
	panByte = std::clamp(panByte, 0, 255);

	// Map 0..255 to 0.0..1.0 (approximate PI/2 curve)
	// 0 = Left, 128 = Center, 255 = Right
	float pan = panByte / 255.0f;

	// Constant power calculation
	// Left = cos(pan * PI/2), Right = sin(pan * PI/2)
	// This ensures Left^2 + Right^2 = 1.0 (constant energy)
	float angle = pan * (static_cast<float>(M_PI) / 2.0f);

	gainL = std::cos(angle);
	gainR = std::sin(angle);
}

// -----------------------------------------------------------------------------
// Audio Thread Function
// Waits for a signal each frame, then runs mixer_update_internal().
// Includes timing and watchdog logging.
// -----------------------------------------------------------------------------
static void audio_thread_func() {
	LOG_INFO("Audio thread: started");
	std::unique_lock<std::mutex> lock(audioCVMutex);

	while (!audioThreadExit.load(std::memory_order_acquire)) {
		// Wait with timeout to detect long idle times
		bool woke = audioCV.wait_for(lock, std::chrono::seconds(1), [] {
			return audioThreadExit.load(std::memory_order_acquire) || audioThreadRun.load(std::memory_order_acquire);
			});
		
		// FIX: Check exit flag immediately after waking
		if (audioThreadExit.load(std::memory_order_acquire)) break;
		
		if (!woke) {
			LOG_INFO("Audio thread: waited >1s without signal (emulator paused?)");
			continue;
		}

		// FIX: Double-check exit flag before processing
		if (audioThreadExit.load(std::memory_order_acquire)) break;
		
		// FIX: Check if we should actually run (audioThreadRun may be false if we woke due to exit)
		if (!audioThreadRun.load(std::memory_order_acquire)) continue;

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
		audioThreadRun.store(false, std::memory_order_release);
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

// -----------------------------------------------------------------------------
// load_sample_from_buffer
// Load a WAV sample from a memory buffer containing raw WAV file data.
// The caller is responsible for reading the file into the buffer using their
// own file I/O utilities.
//
// Parameters:
//   data           - pointer to WAV file data in memory
//   size           - size of the data buffer in bytes
//   name           - sample name for identification/logging (optional, can be nullptr
//                    or empty string - will auto-generate "sample_N" if not provided)
//   force_resample - if true, resample to system frequency on load (default: true)
//
// Returns:
//   Sample ID on success (>= 0), -1 on failure
// -----------------------------------------------------------------------------
int load_sample_from_buffer(const uint8_t* data, size_t size, const char* name, bool force_resample)
{
	if (!data || size == 0) {
		LOG_ERROR("load_sample_from_buffer: invalid buffer (data=%p, size=%zu)", (const void*)data, size);
		return -1;
	}

	auto sample = std::make_shared<SAMPLE>();
	HRESULT result = S_OK;

	result = WavLoadFileInternal(const_cast<unsigned char*>(data), static_cast<int>(size), sample.get());

	if (FAILED(result)) {
		LOG_ERROR("Error loading WAV from buffer: %s", name ? name : "(unnamed)");
		// Fallback to error.wav if provided
		result = WavLoadFileInternal(error_wav, sizeof(error_wav), sample.get());
		if (FAILED(result)) {
			LOG_ERROR("Failed to load fallback error.wav");
			return -1;
		}
	}

	sample->state = SoundState::Loaded;
	sample->num = ++sound_id;
	
	// Use provided name if valid, otherwise auto-generate from sample ID
	if (name && name[0] != '\0') {
		sample->name = name;
	} else {
		sample->name = "sample_" + std::to_string(sample->num);
	}

	if (force_resample && sample->fx.nSamplesPerSec != SYS_FREQ) {
		if (sample->fx.wBitsPerSample == 8) resample_wav_8(sample.get(), SYS_FREQ);
		else                                resample_wav_16(sample.get(), SYS_FREQ, /*use_cubic=*/true);
	}

	LOG_INFO("Loaded sample '%s' with ID %d", sample->name.c_str(), sample->num);
	{
		std::scoped_lock lock(audioMutex);
		lsamples.push_back(sample);
	}
	LOG_INFO("Sample load completed");
	return sample->num;
}

// -----------------------------------------------------------------------------
// mixer_upload_sample16
// Replace/initialize a sample's PCM with mono/stereo 16-bit data and format.
// Allocates/reallocates internal storage as needed.
// Returns 0 on success, -1 on failure.
//
// Parameters:
//   samplenum - index into the mixer sample registry (the same value used by sample_start)
//   pcm       - pointer to interleaved 16-bit PCM
//   frames    - number of frames (samples per channel)
//   freq      - sample rate (e.g., 44100)
//   stereo    - false=mono, true=stereo
// -----------------------------------------------------------------------------
int mixer_upload_sample16(int samplenum,
	const int16_t* pcm,
	uint32_t frames,
	int freq,
	bool stereo)
{
	if (!pcm || frames == 0 || freq <= 0) {
		LOG_ERROR("mixer_upload_sample16: invalid args (pcm=%p frames=%u freq=%d)", (void*)pcm, frames, freq);
		return -1;
	}

	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("mixer_upload_sample16: invalid samplenum %d", samplenum);
		return -1;
	}

	auto& s = lsamples[samplenum];
	if (!s) return -1;

	// (Re)configure format
	s->fx.wFormatTag = WAVE_FORMAT_PCM;
	s->fx.nChannels = stereo ? 2 : 1;
	s->fx.wBitsPerSample = 16;
	s->fx.nSamplesPerSec = freq;
	s->fx.nBlockAlign = static_cast<WORD>(s->fx.nChannels * (s->fx.wBitsPerSample / 8));
	s->fx.nAvgBytesPerSec = s->fx.nSamplesPerSec * s->fx.nBlockAlign;
	s->fx.cbSize = 0;

	const uint32_t channels = s->fx.nChannels;
	s->sampleCount = frames * channels;
	s->dataSize = s->sampleCount * sizeof(int16_t);

	// Allocate/resize storage
	s->data8.reset();
	s->data16 = std::make_unique<int16_t[]>(s->sampleCount);

	// Copy PCM
	std::memcpy(s->data16.get(), pcm, s->dataSize);

	s->buffer = s->data16.get();
	s->state = SoundState::Loaded;
	return 0;
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
int mixer_init(int rate, int fps)  // <<< integer FPS
{
	// FIX: Ensure any previous instance is fully shut down
	if (audioThreadActive.load(std::memory_order_acquire)) {
		LOG_ERROR("mixer_init: mixer already active, call mixer_end() first");
		return 0;
	}
	
	// Validate inputs.
	if (rate <= 0 || fps <= 0) {
		LOG_ERROR("mixer_init: invalid args (rate=%d fps=%d)", rate, fps);
		return 0;
	}

	// Fixed buffer size for each frame/update.
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;

	if (BUFFER_SIZE <= 0) {
		LOG_ERROR("mixer_init: invalid BUFFER_SIZE=%d (rate=%d fps=%d)", BUFFER_SIZE, rate, fps);
		return 0;
	}

	LOG_INFO("Mixer init, BUFFER SIZE = %d, freq %d framerate %d", BUFFER_SIZE, rate, fps);

	// Initialize the xaudio2 backend at the correct rate.
	const HRESULT hr = xaudio2_init(rate, fps);
	if (FAILED(hr)) {
		LOG_ERROR("mixer_init: xaudio2_init failed (hr=0x%08X)", (unsigned)hr);
		xaudio2_stop(); // clean up any partial init
		return 0;
	}

	// Reset channel state.
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		channel[i] = CHANNEL();
	}

	sound_paused = false;

	// FIX: Reset ALL thread state flags BEFORE starting the thread
	audioThreadExit.store(false, std::memory_order_release);
	audioThreadRun.store(false, std::memory_order_release);
	queuedFrames.store(0, std::memory_order_release);
	lastSignalTime = std::chrono::steady_clock::now();

	try {
		audioThread = std::thread(audio_thread_func);
		// FIX: Mark mixer as active after thread starts successfully
		audioThreadActive.store(true, std::memory_order_release);
	}
	catch (const std::exception& e) {
		LOG_ERROR("mixer_init: failed to start audio thread: %s", e.what());
		xaudio2_stop();
		return 0;
	}
	catch (...) {
		LOG_ERROR("mixer_init: failed to start audio thread (unknown exception)");
		xaudio2_stop();
		return 0;
	}

	return 1;
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
				
				// FIX: Validate sample index before accessing
				if (ch.loaded_sample_num < 0 || ch.loaded_sample_num >= static_cast<int>(lsamples.size())) {
					it = audio_list.erase(it);
					continue;
				}
				
				auto& sample = lsamples[ch.loaded_sample_num];
				
				// FIX: Validate sample pointer
				if (!sample) {
					it = audio_list.erase(it);
					continue;
				}

				// End-of-sample handling
				if (ch.pos >= sample->sampleCount)
				{
					if (!ch.looping) {
						ch.state = SoundState::Stopped;
						ch.isPlaying = false;
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
						if (ch.pos + 1 < sample->sampleCount) {
							sL = static_cast<int32_t>(sample->data16[ch.pos + 0]);
							sR = static_cast<int32_t>(sample->data16[ch.pos + 1]);
						}
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

	/*
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
	*/
	// ---- write out with Soft Clipping (Per-Sample Saturation) ----

	// We process every sample individually. This prevents the "pumping" 
	// effect where a loud sound momentarily drops the volume of the entire frame.

	for (int i = 0; i < samplesThisFrame; ++i) {
		// 1. Get raw accumulated values (potentially much larger than 32767)
		float mixL = static_cast<float>(accumL[i]);
		float mixR = static_cast<float>(accumR[i]);

		// --- OPTION 1: True Soft Clipping (Analog Style) ---
		// Normalize to approx -1.0 to 1.0, apply tanh curve, scale back.
		// This rounds off peaks smoothly. 
		// Note: std::tanh is fast enough for audio on modern CPUs.

		float normL = std::tanh(mixL / 32768.0f);
		float normR = std::tanh(mixR / 32768.0f);

		out[2 * i + 0] = static_cast<int16_t>(normL * 32767.0f);
		out[2 * i + 1] = static_cast<int16_t>(normR * 32767.0f);


		// --- OPTION 2: Hard Clamping (Crisp / Retro Standard) ---
		// If you find Option 1 sounds too "muddy" or "quiet" for 8-bit games,
		// comment out Option 1 and uncomment this block instead.
		/*
		int32_t L = std::clamp(accumL[i], -32768, 32767);
		int32_t R = std::clamp(accumR[i], -32768, 32767);
		out[2 * i + 0] = static_cast<int16_t>(L);
		out[2 * i + 1] = static_cast<int16_t>(R);
		*/
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
	// FIX: Only signal if mixer is active
	if (!audioThreadActive.load(std::memory_order_acquire)) {
		return;
	}
	
	lastSignalTime = std::chrono::steady_clock::now();
	queuedFrames++;
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		audioThreadRun.store(true, std::memory_order_release);
	}
	audioCV.notify_one();
}

void mixer_end()
{
	// FIX: Check if mixer is even active
	if (!audioThreadActive.load(std::memory_order_acquire)) {
		LOG_INFO("mixer_end: mixer not active, nothing to do");
		return;
	}
	
	LOG_INFO("mixer_end: shutting down audio thread...");
	
	// FIX: Signal the thread to exit
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		audioThreadExit.store(true, std::memory_order_release);
	}
	audioCV.notify_one();
	
	// FIX: Wait for thread to actually finish
	if (audioThread.joinable()) {
		audioThread.join();
	}
	
	// FIX: Mark mixer as inactive AFTER thread has joined
	audioThreadActive.store(false, std::memory_order_release);
	
	LOG_INFO("mixer_end: audio thread stopped, cleaning up resources...");

	// FIX: Now it's safe to stop XAudio2 and clear resources
	xaudio2_stop();
	
	// FIX: Clear resources under lock
	{
		std::scoped_lock lock(audioMutex);
		for (auto& sample : lsamples) {
			if (sample && sample->buffer)
				LOG_INFO("Freeing sample #%d named %s", sample->num, sample->name.c_str());
		}
		lsamples.clear();
		audio_list.clear();
	}
	
	// FIX: Reset sound_id so new samples start fresh
	sound_id = -1;
	
	LOG_INFO("mixer_end: complete");
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


int sample_get_position(int chanid)
{
	return channel[chanid].pos;
}

// -----------------------------------------------------------------------------
// sample_set_volume
// Convert a legacy 0..255 volume directly to an XAudio2 linear gain 0.0f..1.0f.
// This is a straight linear mapping with no perceptual taper.
// -----------------------------------------------------------------------------
void sample_set_volume(int chanid, int volume)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_volume: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	volume = std::clamp(volume, 0, 255);
	ch.volume = volume;

	LOG_DEBUG("VOLUME PASSED is : %d", volume);
	const float gain = static_cast<float>(volume) / 255.0f;
	ch.vol = static_cast<double>(gain);
	LOG_DEBUG("NEW MIXER SAMPLE VOL: %f", gain);

	if (ch.voice) {
		ch.voice->SetVolume(gain);
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
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.voice && ch.frequency > 0 && freq > 0) {
		float ratio = static_cast<float>(freq) / static_cast<float>(ch.frequency);
		ch.voice->SetFrequencyRatio(ratio);
	}
}

void sample_set_pan(int chanid, int pan)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	ch.pan = std::clamp(pan, 0, 255);
	if (ch.voice) {
		SetPan(ch.voice, ch.pan);
	}
}

int sample_get_pan(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 128;
	return channel[chanid].pan;
}

void sample_start(int chanid, int samplenum, int loop)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_start: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum] == nullptr ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	auto& ch = channel[chanid];

	// If there is an existing voice on this channel, stop and destroy it.
	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}

	auto& sample = lsamples[samplenum];

	// The 8.0f is really important here and required for the StarCastle drone.
	if (FAILED(pXAudio2->CreateSourceVoice(&ch.voice, &sample->fx, 0, 8.0f)))
	{
		LOG_ERROR("Failed to create voice for sample %d", sample->num);
		return;
	}

	// Initialize channel state.
	ch.isAllocated = true;
	ch.isReleased = false;
	ch.isPlaying = true;
	ch.state = SoundState::Playing;   // Important: keep state consistent for voice channels
	ch.looping = loop;
	ch.volume = 255;
	ch.pan = 128;
	ch.frequency = sample->fx.nSamplesPerSec;
	ch.loaded_sample_num = samplenum;

	// Build and submit the XAudio2 buffer.
	std::memset(&ch.buffer, 0, sizeof(ch.buffer));
	ch.buffer.AudioBytes = sample->dataSize;
	ch.buffer.pAudioData = static_cast<BYTE*>(sample->buffer);
	ch.buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

	if (FAILED(ch.voice->SubmitSourceBuffer(&ch.buffer))) {
		LOG_ERROR("sample_start: SubmitSourceBuffer failed (sample=%d chan=%d)", samplenum, chanid);
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
		return;
	}

	// We have to set the volume manually here to avoid a scoped_lock recursive error.
	const float gain = static_cast<float>(VolumeByteToLinear(ch.volume));
	ch.vol = gain;

	if (ch.voice) {
		ch.voice->SetVolume(static_cast<float>(ch.vol));
	}

	SetPan(ch.voice, ch.pan);

	HR(ch.voice->Start());
}

int sample_playing(int chanid)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_playing: invalid channel %d", chanid);
		return 0;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	// If this channel is using an XAudio2 voice, isPlaying must reflect the real voice state.
	// Otherwise, Sega speech queue logic (and any other queue logic) can get stuck forever.
	if (ch.voice)
	{
		XAUDIO2_VOICE_STATE st{};
		ch.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);

		// When no buffers are queued, the one-shot is finished.
		if (st.BuffersQueued == 0) {
			ch.isPlaying = false;
			ch.state = SoundState::Stopped;
		}
		else {
			ch.isPlaying = true;
			ch.state = SoundState::Playing;
		}
		LOG_INFO("RETURNING SAMPLE PLAYING %d", ch.isPlaying);
		return ch.isPlaying ? 1 : 0;
	}
	LOG_INFO("RETURNING SAMPLE PLAYING %d", ch.isPlaying);
	// Fallback: software-mixer path (older behavior)
	return ch.isPlaying ? 1 : 0;
}


void sample_end(int chanid)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_end: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	// Stop looping for both paths.
	ch.looping = 0;

	// If this channel is using an XAudio2 voice and was looping, exit the loop.
	// Note: ExitLoop affects only buffers submitted with XAUDIO2_LOOP_INFINITE.
	if (ch.voice) {
		ch.voice->ExitLoop();
	}
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
	//const int percent = Volume255ToPercent(std::clamp(volume255, 0, 255));
	//LOG_DEBUG("MIXER  VOL PERCENT: %d", percent);
	channel[chanid].vol = VolumePercentToLinear(volume255); // replaces db_volume[]
	LOG_DEBUG("NEW MIXER SAMPLE VOL: %f", channel[chanid].vol);
	
}

// Stereo
void stream_start(int chanid, int /*stream*/, int bits, int frame_rate, bool stereo)
{
	// FIX: Acquire lock for the entire operation
	std::scoped_lock lock(audioMutex);
	
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		LOG_ERROR("Error: Stream already playing on channel %d", chanid);
		return;
	}
	
	// len = frames per update; create_sample stores counts properly for 1 or 2 channels
	// FIX: create_sample_unlocked to avoid recursive lock (or release lock temporarily)
	// For now, we'll create the sample before taking the lock
	int stream_sample = -1;
	{
		// Temporarily release the lock to call create_sample which takes its own lock
		// Actually, we need to refactor this. Let's inline the sample creation.
		auto sample = std::make_shared<SAMPLE>();
		sample->num = ++sound_id;
		sample->name = "STREAM" + std::to_string(sample->num);
		
		LOG_INFO("Creating Audio Sample with name %s and sound id %d", sample->name.c_str(), sample->num);
		
		sample->fx.wFormatTag = WAVE_FORMAT_PCM;
		sample->fx.nChannels = stereo ? 2 : 1;
		sample->fx.nSamplesPerSec = SYS_FREQ;
		sample->fx.wBitsPerSample = static_cast<WORD>(bits);
		sample->fx.nBlockAlign = static_cast<WORD>(sample->fx.nChannels * (bits / 8));
		sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
		sample->fx.cbSize = 0;
		sample->state = SoundState::Loaded;
		
		int len = SYS_FREQ / frame_rate;
		const uint32_t channels = sample->fx.nChannels;
		sample->sampleCount = static_cast<uint32_t>(len) * channels;
		sample->dataSize = sample->sampleCount * (bits / 8);
		
		if (bits == 8) {
			sample->data8 = std::make_unique<uint8_t[]>(sample->dataSize);
			std::memset(sample->data8.get(), 0, sample->dataSize);
			sample->buffer = sample->data8.get();
		}
		else {
			sample->data16 = std::make_unique<int16_t[]>(sample->sampleCount);
			std::memset(sample->data16.get(), 0, sample->dataSize);
			sample->buffer = sample->data16.get();
		}
		
		stream_sample = sample->num;
		lsamples.push_back(sample);
	}

	ch.state = SoundState::Playing;
	ch.loaded_sample_num = stream_sample;
	ch.looping = 1;
	ch.pos = 0;
	ch.stream_type = static_cast<int>(SoundState::Stream);

	audio_list.push_back(chanid);
}

// Create a mono stream:
void stream_start(int chanid, int stream, int bits, int frame_rate)
{
	LOG_INFO("Starting a mono stream");
	stream_start(chanid, stream, bits, frame_rate, /*stereo=*/false);
}

void stream_stop(int chanid, int /*stream*/)
{
	// FIX: Acquire lock FIRST, then modify all state atomically
	std::scoped_lock lock(audioMutex);
	
	auto& ch = channel[chanid];
	ch.state = SoundState::Stopped;
	ch.loaded_sample_num = -1;
	ch.looping = 0;
	ch.pos = 0;

	audio_list.remove(chanid);
}

void stream_update(int chanid, short* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing && 
	    ch.loaded_sample_num >= 0 && 
	    ch.loaded_sample_num < static_cast<int>(lsamples.size())) {
		auto& sample = lsamples[ch.loaded_sample_num];
		if (sample && sample->data16) {
			std::memcpy(sample->data16.get(), data, sample->dataSize);
		}
	}
}

void stream_update(int chanid, unsigned char* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing &&
	    ch.loaded_sample_num >= 0 && 
	    ch.loaded_sample_num < static_cast<int>(lsamples.size())) {
		auto& sample = lsamples[ch.loaded_sample_num];
		if (sample && sample->data8) {
			std::memcpy(sample->data8.get(), data, sample->dataSize);
		}
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

// -----------------------------------------------------------------------------
// save_sample_to_buffer
// Export a loaded sample to a WAV file format buffer.
// The caller is responsible for writing the buffer to disk using their
// own file I/O utilities.
//
// Parameters:
//   samplenum  - sample ID to export
//   out_buffer - output vector that will receive the WAV file data
//
// Returns:
//   true on success, false on failure
// -----------------------------------------------------------------------------
bool save_sample_to_buffer(int samplenum, std::vector<uint8_t>& out_buffer)
{
	std::scoped_lock lock(audioMutex);
	
	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("save_sample_to_buffer: invalid samplenum %d", samplenum);
		return false;
	}

	const auto& sample = lsamples[samplenum];
	if (!sample) {
		LOG_ERROR("save_sample_to_buffer: sample %d is null", samplenum);
		return false;
	}

	// Calculate sizes
	uint32_t subchunk1Size = 16;
	uint32_t subchunk2Size = static_cast<uint32_t>(sample->dataSize);
	uint32_t chunkSize = 4 + (8 + subchunk1Size) + (8 + subchunk2Size);
	
	// Total file size: RIFF header (8) + chunkSize
	size_t totalSize = 8 + chunkSize;
	out_buffer.clear();
	out_buffer.reserve(totalSize);
	
	// Helper lambda to append data to buffer
	auto append = [&out_buffer](const void* data, size_t size) {
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		out_buffer.insert(out_buffer.end(), bytes, bytes + size);
	};
	
	// Write RIFF header
	append("RIFF", 4);
	append(&chunkSize, 4);
	append("WAVE", 4);
	
	// Write fmt chunk
	append("fmt ", 4);
	append(&subchunk1Size, 4);
	append(&sample->fx.wFormatTag, 2);
	append(&sample->fx.nChannels, 2);
	append(&sample->fx.nSamplesPerSec, 4);
	append(&sample->fx.nAvgBytesPerSec, 4);
	append(&sample->fx.nBlockAlign, 2);
	append(&sample->fx.wBitsPerSample, 2);
	
	// Write data chunk
	append("data", 4);
	append(&subchunk2Size, 4);
	
	if (sample->fx.wBitsPerSample == 8 && sample->data8) {
		append(sample->data8.get(), sample->dataSize);
	}
	else if (sample->fx.wBitsPerSample == 16 && sample->data16) {
		append(sample->data16.get(), sample->dataSize);
	}
	else {
		LOG_ERROR("save_sample_to_buffer: no valid sample data for sample %d", samplenum);
		out_buffer.clear();
		return false;
	}

	LOG_INFO("Sample '%s' exported to buffer (%zu bytes)", sample->name.c_str(), out_buffer.size());
	return true;
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

//
// XAUDIO2 Buffer Section.
//

void mixer_set_master_volume(int volumePercent)
{
	volumePercent = std::clamp(volumePercent, 0, 100);
	const float amplitude = VolumePercentToLinear(volumePercent);
	if (pMasterVoice) pMasterVoice->SetVolume(amplitude);

	float dB = (amplitude > 0.0f) ? 20.0f * log10f(amplitude) : -1000.0f;
	LOG_INFO("Master volume: %d%% -> %.2f dB -> %.6f linear", volumePercent, dB, amplitude);
}

float mixer_get_master_volume()
{
	float vol = 1.0f;
	if (pMasterVoice) pMasterVoice->GetVolume(&vol);
	return vol;
}

void samples_stop_all()
{
	std::scoped_lock lock(audioMutex);
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		auto& ch = channel[i];
		if (ch.voice) {
			ch.voice->Stop();
			ch.voice->FlushSourceBuffers();
		}
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.looping = 0;
		ch.pos = 0;
	}
	audio_list.clear();
}

BYTE* GetNextBuffer()
{
	return audioBuffers[currentBufferIndex];
}

HRESULT xaudio2_init(int rate, int fps)  // <<< integer FPS
{
	HRESULT hr;

	UpdatesPerSecond = fps;
	SamplesPerSecond = rate;
	SamplesPerBuffer = SamplesPerSecond / UpdatesPerSecond;      // fixed integer frames per update
	BufferDurationMs = 1000 / UpdatesPerSecond;

	const int remainder = SamplesPerSecond % UpdatesPerSecond;
	if (remainder != 0) {
		LOG_INFO("xaudio2_init: %d Hz / %d FPS leaves remainder %d (using %d frames per update)",
			SamplesPerSecond, UpdatesPerSecond, remainder, SamplesPerBuffer);
	}

	HRESULT hrCI = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (hrCI == S_OK || hrCI == S_FALSE) g_comInitLocal = true;

	HR(XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR));
	HR(pXAudio2->CreateMasteringVoice(&pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, SamplesPerSecond, 0, 0));
	// I am defaulting to 80 percent here.
	pMasterVoice->SetVolume(.80f);

	// Stereo 16-bit PCM source voice
	WAVEFORMATEX wf = {};
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 2;
	wf.nSamplesPerSec = SamplesPerSecond;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8; // 4 bytes per stereo frame
	wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
	wf.cbSize = 0;

	hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &wf, XAUDIO2_VOICE_NOPITCH,
		XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
	if (FAILED(hr)) {
		LOG_INFO("Failed to create source voice: %#X", hr);
		return hr;
	}

	LOG_INFO("Source Voice OK. SamplesPerBuffer=%d stereo frames (~%d ms per update)",
		SamplesPerBuffer, BufferDurationMs);

	// Allocate ring buffers: frames * 4 bytes
	bufferSize = SamplesPerBuffer * wf.nBlockAlign;
	for (int i = 0; i < NUM_BUFFERS; ++i) {
		audioBuffers[i] = new BYTE[bufferSize];
		std::memset(audioBuffers[i], 0, bufferSize);
	}

	HR(pSourceVoice->Start());
	currentBufferIndex = 0;
	return S_OK;
}

HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength)
{
	if (!pSourceVoice) return E_FAIL;
	if (bufferLength == 0) return S_OK;

	// Check voice state to prevent overwriting data currently being played
	XAUDIO2_VOICE_STATE state;
	pSourceVoice->GetState(&state);

	// If we have too many buffers queued, the game loop is running too fast.
	// We should drop this frame or wait. For a game engine, dropping/skipping
	// update is usually better than stalling the main thread.
	if (state.BuffersQueued >= NUM_BUFFERS - 1) {
		LOG_INFO("Audio warning: Ring buffer full, skipping update to prevent overwrite.");
		return S_OK;
	}

	BYTE* payload = buffera ? buffera : audioBuffers[currentBufferIndex];

	// Safety clamp
	if (!buffera && bufferLength > bufferSize) {
		bufferLength = (DWORD)bufferSize;
	}

	XAUDIO2_BUFFER xb = {};
	xb.AudioBytes = bufferLength;
	xb.pAudioData = payload;

	HRESULT hr = pSourceVoice->SubmitSourceBuffer(&xb);
	if (FAILED(hr)) {
		LOG_ERROR("xaudio2_update: SubmitSourceBuffer failed, hr=0x%08X", (unsigned)hr);
		return hr;
	}

	// Only advance index if submission succeeded
	currentBufferIndex = (currentBufferIndex + 1) % NUM_BUFFERS;
	return hr;
}

void xaudio2_stop()
{
	if (pSourceVoice) { pSourceVoice->DestroyVoice(); pSourceVoice = nullptr; }
	if (pMasterVoice) { pMasterVoice->DestroyVoice(); pMasterVoice = nullptr; }
	if (pXAudio2) { pXAudio2->Release();          pXAudio2 = nullptr; }

	for (int i = 0; i < NUM_BUFFERS; ++i) {
		delete[] audioBuffers[i];
		audioBuffers[i] = nullptr;
	}
	if (g_comInitLocal) { CoUninitialize(); g_comInitLocal = false; }
}

// End of Xaudio

#pragma warning(disable : 4018)

// =====================================================================
// WAV
// =====================================================================
int processWaveDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	if (bufferSize < 12) {
		LOG_ERROR("Invalid WAV buffer size.");
		return -1;
	}

	if (std::memcmp(buffer, "RIFF", 4) != 0 || std::memcmp(buffer + 8, "WAVE", 4) != 0) {
		LOG_ERROR("Invalid WAV header.");
		return -1;
	}

	size_t pos = 12;
	bool haveFmt = false;
	bool haveData = false;

	while (pos + 8 <= bufferSize) {
		char chunkID[5] = {};
		std::memcpy(chunkID, buffer + pos, 4);
		pos += 4;

		uint32_t chunkSize = 0;
		std::memcpy(&chunkSize, buffer + pos, sizeof(uint32_t));
		pos += 4;

		// Bounds check for the declared chunk size
		if (pos + chunkSize > bufferSize) {
			LOG_ERROR("WAV chunk overruns buffer (chunk '%c%c%c%c', size=%u)",
				chunkID[0], chunkID[1], chunkID[2], chunkID[3], chunkSize);
			return -1;
		}

		const size_t chunkStart = pos;

		if (std::strncmp(chunkID, "fmt ", 4) == 0) {
			if (chunkSize < 16) {
				LOG_ERROR("WAV fmt chunk too small.");
				return -1;
			}

			std::memcpy(&audioFile->fx.wFormatTag, buffer + pos, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.nChannels, buffer + pos + 2, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.nSamplesPerSec, buffer + pos + 4, sizeof(uint32_t));
			std::memcpy(&audioFile->fx.nAvgBytesPerSec, buffer + pos + 8, sizeof(uint32_t));
			std::memcpy(&audioFile->fx.nBlockAlign, buffer + pos + 12, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.wBitsPerSample, buffer + pos + 14, sizeof(uint16_t));
			haveFmt = true;

			pos += chunkSize;
		}
		else if (std::strncmp(chunkID, "data", 4) == 0) {
			if (!haveFmt) {
				LOG_ERROR("WAV data before fmt.");
				return -1;
			}

			audioFile->dataSize = chunkSize;

			if (audioFile->fx.wBitsPerSample == 8) {
				audioFile->data8 = std::make_unique<uint8_t[]>(chunkSize);
				std::memcpy(audioFile->data8.get(), buffer + pos, chunkSize);
				audioFile->buffer = audioFile->data8.get();
				audioFile->sampleCount = static_cast<uint32_t>(chunkSize); // 8-bit = 1 byte/sample
				haveData = true;
			}
			else if (audioFile->fx.wBitsPerSample == 16) {
				size_t sampleCount = chunkSize / sizeof(int16_t);
				audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
				std::memcpy(audioFile->data16.get(), buffer + pos, chunkSize);
				audioFile->buffer = audioFile->data16.get();
				audioFile->sampleCount = static_cast<uint32_t>(sampleCount);
				haveData = true;
			}
			else {
				LOG_INFO("Unsupported bit depth: %d", audioFile->fx.wBitsPerSample);
				return -1;
			}

			pos += chunkSize;
		}
		else {
			// Skip unknown chunk payload
			pos += chunkSize;
		}

		// Pad to even boundary per RIFF spec
		if ((pos & 1u) != 0) {
			++pos;
		}

		// If we've read both fmt and data, we can stop
		if (haveFmt && haveData) break;

		// Guard against stalled loop
		if (pos <= chunkStart) {
			LOG_ERROR("WAV parser stalled.");
			return -1;
		}
	}

	audioFile->fx.cbSize = 0; // PCM baseline
	return haveData ? 0 : -1;
}

// =====================================================================
// Top-level loader: returns 1 on success, 0 on failure
// =====================================================================
int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile)
{
	const unsigned char* p = buffer;
	const size_t n = static_cast<size_t>(fileSize);

	// 1) WAV?
	if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WAVE", 4) == 0) {
		LOG_INFO("Processing as WAV.");
		return (processWaveDataBuffer(p, n, audioFile) == 0) ? 1 : 0;
	}

	LOG_ERROR("Unknown audio format (no RIFF/WAVE)");
	return 0;
}

// RESAMPLE

// ============================== Utilities ====================================

double dBToAmplitude(double db)
{
	return std::pow(10.0, db / 20.0);
}

void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB)
{
	if (!samples || num_samples == 0) return;
	const double factor = std::pow(10.0, static_cast<double>(dB) / 20.0);
	for (size_t i = 0; i < num_samples; ++i) {
		const double s = static_cast<double>(samples[i]) * factor;
		const int32_t y = static_cast<int32_t>(std::llround(s));
		samples[i] = static_cast<int16_t>(std::clamp(
			y, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX)));
	}
}

static inline int16_t saturate_i16(int32_t v)
{
	if (v > INT16_MAX) return INT16_MAX;
	if (v < INT16_MIN) return INT16_MIN;
	return static_cast<int16_t>(v);
}

static inline int16_t sample_at_safe_i16(const int16_t* s, int32_t n, int32_t idx)
{
	if (idx < 0) idx = 0;
	if (idx >= n) idx = n - 1;
	return s[idx];
}

static inline double catmull_rom_scalar(double p0, double p1, double p2, double p3, double x)
{
	const double a0 = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
	const double a1 = 1.0 * p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
	const double a2 = -0.5 * p0 + 0.5 * p2;
	const double a3 = p1;
	return ((a0 * x + a1) * x + a2) * x + a3;
}

// ============================= Core resamplers ================================
// Endpoint-aligned mapping (i=0-src0, i=outN-1->srcN-1). OOB-safe.

static inline void linear_into_core(const int16_t* in, int32_t inN,
	int16_t* out, int32_t outN)
{
	if (inN <= 0 || outN <= 0) return;

	if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
	if (outN == 1) { out[0] = in[0]; return; }

	const double scale = static_cast<double>(inN - 1) / static_cast<double>(outN - 1);

	for (int32_t i = 0; i < outN; ++i) {
		const double src_pos = static_cast<double>(i) * scale;
		int32_t idx = static_cast<int32_t>(src_pos);
		double  frac = src_pos - static_cast<double>(idx);

		if (idx >= inN - 1) { idx = inN - 2; frac = 1.0; }

		const int16_t a = in[idx + 0];
		const int16_t b = in[idx + 1];

		const int32_t a32 = static_cast<int32_t>(a);
		const int32_t diff = static_cast<int32_t>(b) - a32;
		const double  y = static_cast<double>(a32) + static_cast<double>(diff) * frac;

		out[i] = saturate_i16(static_cast<int32_t>(std::llround(y)));
	}
}

static inline void cubic_into_core(const int16_t* in, int32_t inN,
	int16_t* out, int32_t outN)
{
	if (inN <= 0 || outN <= 0) return;

	if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
	if (outN == 1) { out[0] = in[0]; return; }

	const double scale = static_cast<double>(inN - 1) / static_cast<double>(outN - 1);

	for (int32_t i = 0; i < outN; ++i) {
		const double src_pos = static_cast<double>(i) * scale;
		int32_t idx = static_cast<int32_t>(std::floor(src_pos));
		double  frac = src_pos - static_cast<double>(idx);

		if (idx >= inN - 1) { idx = inN - 2; frac = 1.0; }

		const double p0 = static_cast<double>(sample_at_safe_i16(in, inN, idx - 1));
		const double p1 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 0));
		const double p2 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 1));
		const double p3 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 2));

		const double y = catmull_rom_scalar(p0, p1, p2, p3, frac);
		out[i] = saturate_i16(static_cast<int32_t>(std::llround(y)));
	}
}

// =========================== Non-allocating wrappers ==========================

void linear_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples)
{
	if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
	linear_into_core(input_data, input_samples, output_data, output_samples);
}

void cubic_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples)
{
	if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
	cubic_into_core(input_data, input_samples, output_data, output_samples);
}

// =================== Allocating resamplers ====================

void linear_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio)
{
	if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
		if (output_data)    *output_data = nullptr;
		if (output_samples) *output_samples = 0;
		return;
	}

	int32_t outN = static_cast<int32_t>(
		std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
	if (outN < 1) outN = 1;

	if (*output_data && *output_samples != outN) {
		delete[] * output_data;
		*output_data = nullptr;
	}
	if (!*output_data) {
		*output_data = new int16_t[outN];
	}
	*output_samples = outN;

	linear_into_core(input_data, input_samples, *output_data, outN);
}

void cubic_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio)
{
	if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
		if (output_data)    *output_data = nullptr;
		if (output_samples) *output_samples = 0;
		return;
	}

	int32_t outN = static_cast<int32_t>(
		std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
	if (outN < 1) outN = 1;

	if (*output_data && *output_samples != outN) {
		delete[] * output_data;
		*output_data = nullptr;
	}
	if (!*output_data) {
		*output_data = new int16_t[outN];
	}
	*output_samples = outN;

	cubic_into_core(input_data, input_samples, *output_data, outN);
}

// ============================ 8-bit  ==============================

void linear_interpolation_8(const uint8_t* input,
	uint8_t* output,
	int input_size,
	int output_size)
{
	if (!input || !output || input_size <= 0 || output_size <= 0) return;

	if (input_size == 1) { std::fill(output, output + output_size, input[0]); return; }
	if (output_size == 1) { output[0] = input[0]; return; }

	const double scale = static_cast<double>(input_size - 1)
		/ static_cast<double>(output_size - 1);

	for (int i = 0; i < output_size; ++i) {
		const double src_pos = static_cast<double>(i) * scale;
		int idx = static_cast<int>(src_pos);
		double frac = src_pos - static_cast<double>(idx);

		if (idx >= input_size - 1) { idx = input_size - 2; frac = 1.0; }

		const int a = input[idx + 0];
		const int b = input[idx + 1];
		const double y = static_cast<double>(a) + (static_cast<double>(b) - a) * frac;

		int yi = static_cast<int>(std::llround(y));
		yi = std::clamp(yi, 0, 255);
		output[i] = static_cast<uint8_t>(yi);
	}
}

// ============================ Optional postfilter =============================

void lowpass_postfilter_16(int16_t* data, int32_t samples)
{
	if (!data || samples <= 4) return;

	int16_t* temp = new int16_t[samples];

	for (int32_t i = 0; i < samples; ++i) {
		const int32_t s0 = data[(i - 2 < 0) ? 0 : i - 2];
		const int32_t s1 = data[(i - 1 < 0) ? 0 : i - 1];
		const int32_t s2 = data[i];
		const int32_t s3 = data[(i + 1 >= samples) ? samples - 1 : i + 1];
		const int32_t s4 = data[(i + 2 >= samples) ? samples - 1 : i + 2];

		const int32_t num = (-s0) + (4 * s1) + (6 * s2) + (4 * s3) - s4;
		const int32_t y = static_cast<int32_t>(
			std::lrint(static_cast<double>(num) / 12.0));
		temp[i] = saturate_i16(y);
	}

	std::copy(temp, temp + samples, data);
	delete[] temp;
}

//
// WAV Filters
//

void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate) {
	if (audioSample.empty()) return;

	float RC = 1.0f / (cutoffFreq * 2.0f * static_cast<float>(M_PI));
	float dt = 1.0f / sampleRate;
	float alpha = RC / (RC + dt);

	int16_t previousSample = audioSample[0];
	int16_t previousFilteredSample = audioSample[0];

	for (size_t i = 1; i < audioSample.size(); ++i) {
		int16_t currentSample = audioSample[i];
		audioSample[i] = static_cast<int16_t>(alpha * (previousFilteredSample + currentSample - previousSample));
		previousFilteredSample = audioSample[i];
		previousSample = currentSample;
	}
}

void lowPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate) {
	if (audioSample.empty()) return;

	float RC = 1.0f / (cutoffFreq * 2.0f * static_cast<float>(M_PI));
	float dt = 1.0f / sampleRate;
	float alpha = dt / (RC + dt);

	int16_t previousSample = audioSample[0];

	for (size_t i = 1; i < audioSample.size(); ++i) {
		audioSample[i] = static_cast<int16_t>(previousSample + alpha * (audioSample[i] - previousSample));
		previousSample = audioSample[i];
	}
}

void design_biquad_lowpass(float fs, float fc, float Q, float& b0, float& b1, float& b2, float& a1, float& a2)
{
	const float w0 = 2.0f * float(M_PI) * fc / fs;
	const float cosw = std::cos(w0);
	const float sinw = std::sin(w0);
	const float alpha = sinw / (2.0f * Q);

	float b0u = (1.0f - cosw) * 0.5f;
	float b1u = 1.0f - cosw;
	float b2u = (1.0f - cosw) * 0.5f;
	float a0u = 1.0f + alpha;
	float a1u = -2.0f * cosw;
	float a2u = 1.0f - alpha;

	b0 = b0u / a0u;
	b1 = b1u / a0u;
	b2 = b2u / a0u;
	a1 = a1u / a0u;
	a2 = a2u / a0u;
}

void biquad_lowpass_inplace_i16(int16_t* x, int n, float fs, float fc, float Q, int passes)
{
	if (!x || n <= 2 || fc <= 0.0f || fs <= 0.0f) return;
	float b0, b1, b2, a1, a2;
	design_biquad_lowpass(fs, fc, Q, b0, b1, b2, a1, a2);

	for (int p = 0; p < passes; ++p)
	{
		float x1 = 0.0f, x2 = 0.0f;
		float y1 = 0.0f, y2 = 0.0f;

		for (int i = 0; i < n; ++i)
		{
			const float xf = (float)x[i];
			const float y = b0 * xf + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

			x2 = x1; x1 = xf;
			y2 = y1; y1 = y;

			int v = (int)std::lrintf(y);
			x[i] = (int16_t)std::clamp(v, -32768, 32767);
		}
	}
}

// WAV SWEEP FUNCTIONS

// --------------------------- Internal Types ----------------------------------

namespace {
	enum class Param : uint8_t { Volume = 0, Pan = 1, Freq = 2 };

	struct Key {
		int voice = -1;
		Param param = Param::Volume;

		bool operator==(const Key& o) const noexcept {
			return voice == o.voice && param == o.param;
		}
	};

	struct KeyHash {
		std::size_t operator()(const Key& k) const noexcept {
			return (static_cast<std::size_t>(k.voice) << 2)
				^ static_cast<std::size_t>(static_cast<uint8_t>(k.param));
		}
	};

	struct Sweep {
		int   voice = -1;
		Param param = Param::Volume;
		int   start = 0;
		int   end = 0;
		int   duration_ms = 0;
		std::chrono::steady_clock::time_point t0;
	};

	constexpr int kTickMs = 1;
	std::atomic<bool> g_started{ false };
	std::atomic<bool> g_stop{ false };
	std::thread g_worker;

	std::mutex g_mtx;
	std::condition_variable g_cv;

	std::unordered_map<Key, Sweep, KeyHash> g_sweeps;
	std::unordered_map<int, int> g_lastFreqHz;

	void worker_loop();
	void ensure_started();
	int  clamp_int(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

	int get_current_value(int voice, Param p)
	{
		switch (p) {
		case Param::Volume: return clamp_int(sample_get_volume(voice), 0, 255);
		case Param::Pan:    return clamp_int(sample_get_pan(voice), 0, 255);
		case Param::Freq: {
			auto it = g_lastFreqHz.find(voice);
			if (it != g_lastFreqHz.end()) return it->second;
			int base = sample_get_freq(voice);
			return (base > 0) ? base : 0;
		}
		}
		return 0;
	}

	void apply_value(int voice, Param p, int v)
	{
		switch (p) {
		case Param::Volume:
			sample_set_volume(voice, clamp_int(v, 0, 255));
			break;
		case Param::Pan:
			sample_set_pan(voice, clamp_int(v, 0, 255));
			break;
		case Param::Freq:
			if (v < 1) v = 1;
			sample_set_freq(voice, v);
			{
				std::lock_guard<std::mutex> lk(g_mtx);
				g_lastFreqHz[voice] = v;
			}
			break;
		}
	}

	void ensure_started()
	{
		bool expected = false;
		if (g_started.compare_exchange_strong(expected, true)) {
			g_stop = false;
			g_worker = std::thread(worker_loop);

			std::atexit([] {
				try { wavsweep_shutdown(); }
				catch (...) {}
				});
		}
	}

	void worker_loop()
	{
		LOG_INFO("wav_sweep: worker thread started");

		std::unique_lock<std::mutex> lk(g_mtx);

		while (!g_stop.load(std::memory_order_relaxed)) {
			g_cv.wait_for(lk, std::chrono::milliseconds(kTickMs),
				[] { return g_stop.load(std::memory_order_relaxed); });
			if (g_stop.load(std::memory_order_relaxed))
				break;

			const auto now = std::chrono::steady_clock::now();
			std::vector<Sweep> ops;
			ops.reserve(g_sweeps.size());

			for (auto it = g_sweeps.begin(); it != g_sweeps.end(); ) {
				Sweep& s = it->second;

				const int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.t0).count();
				const double  T = (s.duration_ms <= 0) ? 1.0 : std::clamp(elapsed / static_cast<double>(s.duration_ms), 0.0, 1.0);

				const double cur = s.start + (s.end - s.start) * T;
				const int    ival = static_cast<int>(std::lround(cur));

				ops.push_back({ s.voice, s.param, ival, ival, 0, s.t0 });

				if (T >= 1.0) {
					it = g_sweeps.erase(it);
				}
				else {
					++it;
				}
			}

			lk.unlock();
			for (const auto& op : ops) {
				apply_value(op.voice, op.param, op.start);
			}
			lk.lock();
		}

		LOG_INFO("wav_sweep: worker thread exiting");
	}
} // anonymous namespace

// --------------------------- Public API --------------------------------------

void wavsweep_init()
{
	ensure_started();
}

void wavsweep_shutdown()
{
	if (!g_started.load()) return;

	g_stop = true;
	g_cv.notify_all();
	if (g_worker.joinable())
		g_worker.join();

	std::lock_guard<std::mutex> lk(g_mtx);
	g_sweeps.clear();
	g_lastFreqHz.clear();
	g_started = false;
}

static void start_sweep(int voice, Param param, int time_ms, int end_value)
{
	ensure_started();

	if (time_ms <= 0) {
		apply_value(voice, param, end_value);
		return;
	}

	Sweep s;
	s.voice = voice;
	s.param = param;
	s.duration_ms = time_ms;
	s.end = end_value;
	s.t0 = std::chrono::steady_clock::now();
	s.start = get_current_value(voice, param);

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_sweeps[{voice, param}] = s;
	}

	g_cv.notify_one();
}

void mixer_ramp_volume(int voice, int time_ms, int endvol)
{
	endvol = clamp_int(endvol, 0, 255);
	start_sweep(voice, Param::Volume, time_ms, endvol);
}

void mixer_sweep_frequency(int voice, int time_ms, int endfreq)
{
	if (endfreq < 1) endfreq = 1;
	start_sweep(voice, Param::Freq, time_ms, endfreq);
}

void mixer_sweep_pan(int voice, int time_ms, int endpan)
{
	endpan = clamp_int(endpan, 0, 255);
	start_sweep(voice, Param::Pan, time_ms, endpan);
}
