//==============================================================================
// mixer.h -- Usage Guide & API Reference 
//==============================================================================
//
// OVERVIEW
// --------
// XAudio2-based audio engine providing two playback paths:
//
//   1. VOICE PATH (direct XAudio2 per-channel playback)
//      One IXAudio2SourceVoice per channel. Best for one-shot sound effects
//      and looping samples where XAudio2 handles mixing and pitch natively.
//      Supports real-time volume, pan, and frequency ratio changes.
//
//   2. SOFTWARE MIXER PATH (frame-locked mixing)
//      All active channels are software-mixed into a single interleaved S16
//      stereo buffer each frame, then submitted to XAudio2. Best for
//      emulator-generated audio, streaming PCM, and scenarios requiring
//      sample-level control. Supports 8-bit and 16-bit mono/stereo sources.
//
// Both paths share the same CHANNEL and SAMPLE structures, volume/pan curves,
// and sample registry. Choose the path at playback time -- a sample loaded
// once can be played on either path.
//
// Output: XAudio2 submits interleaved 16-bit stereo PCM via a ring of
// NUM_BUFFERS (5) submission buffers. Buffer size is rate/fps frames.
//
//
// QUICK START
// -----------
//   #include "mixer.h"
//
//   // At startup (after your main window / message loop is ready):
//   mixer_init(44100, 60);                      // 44.1 kHz, 60 fps
//
//   // Load a sample from a memory buffer (caller does file I/O):
//   std::vector<uint8_t> wav = read_file("shoot.wav");
//   int snd = load_sample_from_buffer(wav.data(), wav.size(), "shoot");
//
//   // --- Voice path (direct XAudio2 playback) ---
//   sample_start(0, snd, 0);                    // channel 0, no loop
//   sample_set_volume(0, 200);                   // 0..255
//   sample_set_pan(0, 64);                       // pan left (0=full L, 128=center, 255=full R)
//   sample_set_freq(0, 22050);                   // pitch shift via frequency ratio
//   if (!sample_playing(0)) { /* done */ }
//   sample_stop(0);                              // immediate stop
//   sample_end(0);                               // exit loop gracefully (plays to end)
//
//   // --- Software mixer path (mixed each frame) ---
//   sample_start_mixer(1, snd, 1);              // channel 1, looping
//   // ... each frame:
//   mixer_update();                              // signals audio thread to mix & submit
//   // ...
//   sample_stop_mixer(1);                        // stop and remove from mix list
//
//   // --- Streaming (live / generated audio) ---
//   stream_start(2, 0, 16, 60, true);           // ch 2, 16-bit, 60fps, stereo
//   // each frame:
//   short pcm[735 * 2];                          // 44100/60 = 735 frames, stereo
//   generate_audio(pcm);
//   stream_update(2, pcm);
//   mixer_update();
//   // ...
//   stream_stop(2, 0);
//
//   // At shutdown:
//   mixer_end();
//
//
// CHANNEL / SAMPLE LIMITS
// -----------------------
//   MAX_CHANNELS  = 20    (fixed array, indices 0..19)
//   MAX_SOUNDS    = 255   (soft limit on sample registry)
//   NUM_BUFFERS   = 5     (XAudio2 ring buffer depth)
//
// Channels are identified by integer index (0..MAX_CHANNELS-1). Each channel
// can be used for voice path OR software mixer path, but not both at once.
// Samples are identified by a monotonically increasing ID returned from
// load_sample_from_buffer() or create_sample().
//
//
// SAMPLE LOADING
// --------------
//   int load_sample_from_buffer(data, size, name, force_resample=true)
//       Load a WAV from a raw memory buffer. The caller reads the file.
//       Supports 8-bit and 16-bit PCM WAV. If force_resample is true (default),
//       the sample is resampled to the system output rate on load (cubic for
//       16-bit, linear for 8-bit). Returns sample ID >= 0, or -1 on failure.
//       On WAV parse failure, falls back to a built-in error.wav beep.
//
//   int create_sample(bits, is_stereo, freq, len_frames, name)
//       Allocate an empty sample buffer for generated/streaming audio.
//       Returns sample ID. Used internally by stream_start().
//
//   int mixer_upload_sample16(samplenum, pcm, frames, freq, stereo=false)
//       Replace a sample's PCM data in-place. Useful for procedurally
//       generated waveforms. Returns 0 on success.
//
//   bool save_sample_to_buffer(samplenum, out_buffer)
//       Export a loaded sample as a standard WAV into a std::vector<uint8_t>.
//       The caller writes the buffer to disk.
//
//   void sample_remove(samplenum)
//       STUB -- currently does nothing. Samples persist until mixer_end().
//
//
// VOICE PATH PLAYBACK
// -------------------
//   void sample_start(chanid, samplenum, loop)
//       Create an XAudio2 source voice on the channel, submit the sample
//       buffer, and start playback. loop=1 for XAUDIO2_LOOP_INFINITE, 0
//       for one-shot. Destroys any existing voice on that channel first.
//       Initializes volume=255, pan=128 (center), freq=sample's native rate.
//       Max frequency ratio is 8.0 (supports extreme pitch shifts).
//
//   void sample_stop(chanid)
//       Immediate stop -- flushes buffers, resets state.
//
//   void sample_end(chanid)
//       Graceful loop exit -- calls ExitLoop() so the current buffer plays
//       to completion, then the voice stops naturally.
//
//   int  sample_playing(chanid)
//       Returns 1 if playing, 0 if stopped. For voice-path channels, queries
//       XAudio2 BuffersQueued to detect one-shot completion.
//
//
// SOFTWARE MIXER PATH
// -------------------
//   void sample_start_mixer(chanid, samplenum, loop)
//       Add channel to the software mix list. The channel's loaded sample
//       will be mixed into the output buffer each frame during mixer_update().
//
//   void sample_stop_mixer(chanid)
//       Remove from mix list, reset position.
//
//   void sample_end_mixer(chanid)
//       Clear loop flag -- sample plays to end, then stops.
//
// Mono sources are centered (pan ignored). Stereo sources use equal-power
// panning. The mixer accumulates in int32 and applies tanh soft clipping
// per sample to avoid pumping artifacts.
//
//
// STREAMING
// ---------
//   void stream_start(chanid, stream, bits, frame_rate)           // mono
//   void stream_start(chanid, stream, bits, frame_rate, stereo)   // mono or stereo
//       Allocate a frame-sized sample buffer and add to the mix list.
//       The buffer holds exactly (SYS_FREQ / frame_rate) frames of audio.
//       The 'stream' parameter is currently unused (reserved).
//
//   void stream_update(chanid, short* data)     // 16-bit path
//   void stream_update(chanid, unsigned char*)  // 8-bit path
//       Overwrites the stream's sample buffer with new PCM data. Call once
//       per frame before mixer_update().
//
//   void stream_stop(chanid, stream)
//       Stop streaming and remove from mix list.
//
// Streaming is the primary path for emulator audio -- the emulator generates
// one frame's worth of PCM per tick, calls stream_update(), then mixer_update()
// submits it to XAudio2.
//
//
// VOLUME / PAN / FREQUENCY CONTROL
// ---------------------------------
// All control functions work on both voice-path and software-mixer channels.
//
//   void sample_set_volume(chanid, vol)     // vol: 0..255
//   int  sample_get_volume(chanid)          // returns 0..255
//       Volume uses a perceptual dB-tapered curve:
//         0-5%   : quadratic ramp in dB (-80 to -12 dB) -- fine control at low levels
//         5-100% : linear in dB (-12 to 0 dB) -- smooth perceptual midrange
//       The 0..255 byte value is mapped through this curve via VolumeByteToLinear().
//       For voice-path channels, also updates the XAudio2 source voice gain.
//
//       NOTE: get_volume() reverse-maps the float gain linearly, NOT through
//       the inverse dB curve. This means set(X) -> get() may not return X.
//       The sweep system (mixer_ramp_volume) reads back through get_volume(),
//       so interpolation operates on the linear-gain scale, not the byte scale.
//
//   void sample_set_pan(chanid, pan)        // pan: 0..255, 128=center
//   int  sample_get_pan(chanid)             // returns 0..255
//       Constant-power panning: L=cos(theta), R=sin(theta) where theta = pan/255 * pi/2.
//       Mono voice-path sources ignore pan (always centered).
//       Stereo voice-path sources use a 2x2 output matrix.
//       Software-mixer stereo sources apply pan as L/R balance gains.
//
//   void sample_set_freq(chanid, freq_hz)   // effective playback frequency
//   int  sample_get_freq(chanid)            // returns BASE sample rate, not current
//       For voice-path channels, sets XAudio2 FrequencyRatio = freq / base_rate.
//       NOTE: Does not update the stored frequency field, so get_freq() returns
//       the original sample rate, not the currently-applied rate. The sweep system
//       maintains its own last-known frequency via g_lastFreqHz.
//       Software-mixer channels do not support pitch changes.
//
//
// VOLUME CONVERSION HELPERS
// -------------------------
//   VolumeByteToLinear(int 0..255) -> float   // 0..255 -> dB-tapered linear gain
//   VolumePercentToLinear(int 0..100) -> float // 0..100% -> dB-tapered linear gain
//   Volume255ToPercent(int 0..255) -> int      // 0..255 -> 0..100 (rounded)
//   VolPercentToByte(int 0..100) -> int        // 0..100 -> 0..255 (rounded)
//   VolByteToPercent(int 0..255) -> int        // 0..255 -> 0..100 (rounded)
//
// Usage patterns:
//   sample_set_volume(ch, vol255);              // if you have 0..255 directly
//   sample_set_volume(ch, VolPercentToByte(pct)); // if a UI gives 0..100
//
//
// MASTER VOLUME
// -------------
//   void  mixer_set_master_volume(int percent)   // 0..100
//   float mixer_get_master_volume()              // returns raw XAudio2 linear gain
//   void  mixer_set_master_volume_255(int v)     // convenience: 0..255 -> percent
//
// Master volume is applied on the XAudio2 mastering voice. Default is 0.80
// linear (~-1.9 dB) set during xaudio2_init.
//
//
// PARAMETER SWEEPS (RAMPS)
// ------------------------
// Time-based linear interpolation of volume, pan, and frequency. Runs on a
// dedicated worker thread with ~1ms tick resolution.
//
//   void mixer_ramp_volume(voice, time_ms, endvol)    // endvol: 0..255
//   void mixer_sweep_frequency(voice, time_ms, endfreq) // endfreq: Hz
//   void mixer_sweep_pan(voice, time_ms, endpan)      // endpan: 0..255
//
//   void wavsweep_init()       // auto-called by sweep functions; explicit call optional
//   void wavsweep_shutdown()   // stops worker; also registered with atexit()
//
// If time_ms <= 0, the target value is applied immediately (no sweep).
// Up to one sweep per (voice, parameter) pair can be active; starting a new
// sweep on the same voice+param replaces the old one.
//
// NOTE: Frequency sweeps only affect voice-path channels. Software-mixer
// channels ignore frequency changes by design.
//
//
// PAUSE / RESUME
// --------------
//   void pause_audio()         // mutes master volume, freezes software mixer
//   void restore_audio()       // restores previous master volume, resumes mixing
//
//
// RESAMPLING
// ----------
// Load-time and utility resamplers. These operate on SAMPLE structs or raw
// PCM buffers. For interleaved stereo, channels are deinterleaved, resampled
// independently, and re-interleaved.
//
//   void resample_wav_8(SAMPLE*, new_freq)
//       Linear interpolation for 8-bit unsigned PCM.
//
//   void resample_wav_16(SAMPLE*, new_freq, use_cubic=true)
//       Catmull-Rom cubic (default) or linear for 16-bit signed PCM.
//
// Non-allocating variants (caller provides output buffer):
//   void linear_interpolation_16_into(in, inN, out, outN)
//   void cubic_interpolation_16_into(in, inN, out, outN)
//   void linear_interpolation_8(in, out, inN, outN)
//
// Allocating variants (allocates with new[], caller must delete[]):
//   void linear_interpolation_16(in, inN, &out, &outN, ratio)
//   void cubic_interpolation_16(in, inN, &out, &outN, ratio)
//
// All 16-bit resamplers use endpoint-aligned mapping and clamp to int16 range.
// NOTE: These operate on a single PCM stream (mono or deinterleaved channel).
// For interleaved stereo, resample L and R channels separately.
//
//
// DSP / FILTERS
// -------------
//   void adjust_volume_dB(int16_t* samples, n, dB)
//       In-place gain adjustment by dB amount. Clamps to int16.
//
//   void lowpass_postfilter_16(int16_t* data, n)
//       Light 5-tap FIR (anti-imaging after upsampling). In-place.
//
//   void highPassFilter(vector<int16_t>&, cutoffHz, sampleRate)
//   void lowPassFilter(vector<int16_t>&, cutoffHz, sampleRate)
//       Simple 1-pole RC filters. In-place on std::vector.
//
//   void design_biquad_lowpass(fs, fc, Q, &b0, &b1, &b2, &a1, &a2)
//       Compute 2nd-order Butterworth lowpass coefficients (bilinear transform).
//
//   void biquad_lowpass_inplace_i16(int16_t*, n, fs, fc, Q=0.707, passes=1)
//       Apply biquad lowpass in-place on int16 buffer. Multiple passes for
//       steeper rolloff.
//
//
// UTILITY FUNCTIONS
// -----------------
//   int  nameToNum(const string& name)    // sample name -> sample ID (-1 if not found)
//   string numToName(int num)             // sample ID -> sample name ("" if not found)
//   int  snumlookup(int snum)             // sample ID -> index in lsamples vector
//   unsigned char Make8bit(int16_t s)     // 16-bit signed -> 8-bit unsigned
//   short Make16bit(uint8_t s)            // 8-bit unsigned -> 16-bit signed
//   void  samples_stop_all()              // stop all channels (voice + mixer), clear mix list
//   double dBToAmplitude(double dB)       // dB -> linear amplitude
//
//
// XAUDIO2 BACKEND (LOW-LEVEL)
// ----------------------------
// These are used internally by the mixer. Direct use is rarely needed.
//
//   HRESULT xaudio2_init(rate, fps)       // creates XAudio2 engine, mastering voice,
//                                          // streaming source voice, ring buffers
//   HRESULT xaudio2_update(buffer, len)   // submits one buffer to XAudio2 source voice
//   void    xaudio2_stop()                // destroys voices, frees buffers, CoUninitialize
//   BYTE*   GetNextBuffer()               // returns current ring buffer slot
//
//   int WavLoadFileInternal(buffer, size, SAMPLE*)
//       Parse WAV from memory into a SAMPLE struct. Returns 1 on success.
//
//
// THREADING MODEL
// ---------------
// The mixer uses a dedicated audio worker thread:
//
//   Main thread calls mixer_update()
//     -> sets audioThreadRun flag, signals condition variable
//   Audio thread wakes
//     -> calls mixer_update_internal() (locks audioMutex, mixes, submits)
//     -> clears audioThreadRun, waits for next signal
//
// The main thread MUST NOT call mixer_update_internal() directly.
// All sample/channel manipulation functions acquire audioMutex internally.
//
// The sweep system runs a separate worker thread (1ms tick) that calls
// sample_set_volume/pan/freq on behalf of active sweeps.
//
// Watchdog: the audio thread logs warnings if >1 frame queued (main thread
// too fast) or if >50ms between signals (main thread stalled). The thread
// also logs if mixer_update_internal() takes >2ms.
//
//
// WIN7 COMPATIBILITY
// ------------------
// Define Win7Build to use the Windows SDK's xaudio2.h instead of
// xaudio2redist. The redistributable is preferred for Win10+ as it
// provides consistent behavior across Windows versions.
//
//
// OPTIONAL FEATURES
// -----------------
// #define USE_VUMETER before including mixer.h to enable VU meters:
//   void mixer_get_vu(float* left, float* right)  // 0..1 peak levels
//   void mixer_reset_vu()
//
// #define OGG_DECODE / MP3_DECODE in mixer.cpp to enable stb_vorbis / minimp3
//
//
// INIT / SHUTDOWN SEQUENCE
// ------------------------
//   mixer_init(rate, fps)   -- Initializes XAudio2, allocates buffers, starts
//                              audio thread. Returns 1 on success, 0 on failure.
//                              Safe to call only once; logs error if already active.
//   mixer_end()             -- Signals audio thread exit, joins thread, destroys
//                              XAudio2 resources, frees all samples. Safe to call
//                              if not active (no-ops). Resets sound_id so next
//                              session starts with fresh sample IDs.
//   wavsweep_shutdown()     -- Stops sweep worker. Also auto-registered with atexit().
//
//
// DEPENDENCIES
// ------------
// Windows headers:  <windows.h>
// XAudio2:          <xaudio2redist.h> (or <xaudio2.h> with Win7Build)
// Link libraries:   xaudio2redist.lib (auto-linked via NuGet typically)
// Project headers:  "framework.h", "error_wav.h", "sys_log.h"
// C++ standard:     C++17 (scoped_lock, shared_ptr, optional, clamp)
//
// LICENSE: GPL-3.0-or-later -- Copyright (C) 2022-2025 Tim Cottrill
//
//==============================================================================

// Note for ME: This is the FULL INTEGER version of this code
// 1/xx/26 Changed to Buffer-based I/O version - no direct file dependencies for portability.
// 2/15/26 Fixed sample_playing return value: brute force, revisit to streamline.
#pragma once

#ifndef MIXER_H
#define MIXER_H

#include <string>
#include <cstdint>
#ifndef WIN7BUILD 
#include <xaudio2.h> 
#else 
#include <xaudio2redist.h> 
#endif

#include <memory>
#include <vector>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <cmath>


// Convert legacy 0..255 values to 0..100 percent (rounded).
static inline int Volume255ToPercent(int v255)
{
	v255 = std::clamp(v255, 0, 255);
	// +127 for rounding when dividing by 255
	return (v255 * 100 + 127) / 255;
}

// Game-friendly curve: 0..100% slider -> linear gain for XAudio2.
// - Very quiet near the bottom (fast drop to ~-80 dB).
// - Smooth, perceptual mid (50% ? -6 dB).
// - 100% = 0 dB, 0% ? -80 dB.
//
// Implementation:
//   0..5%   -> quadratic ramp in dB from -80 dB up to -12 dB
//   5..100% -> linear in dB from -12 dB up to 0 dB

static inline float VolumePercentToLinear(int percent)
{
	percent = std::clamp(percent, 0, 100);
	const float x = percent / 100.0f;
	float dB;

	if (x <= 0.05f) {
		// 0..0.05 -> -80..-12 dB (quadratic makes the very-low range feel controllable)
		const float y = x / 0.05f;           // 0..1
		dB = -80.0f + 68.0f * (y * y);       // hits -12 dB at 5%
	}
	else {
		// 0.05..1.0 -> -12..0 dB (linear in dB gives ~ -6 dB around the mid)
		const float y = (x - 0.05f) / 0.95f; // 0..1
		dB = -12.0f + 12.0f * y;             // -12 dB at 5%, 0 dB at 100%
	}

	// Convert dB -> linear amplitude for XAudio2
	return std::pow(10.0f, dB / 20.0f);
}

// Enum for sound state
enum class SoundState {
	Null,
	Loaded,
	Playing,
	Stopped,
	PCM,
	Stream
};

// Represents a playback channel
struct CHANNEL {
	IXAudio2SourceVoice* voice = nullptr;
	XAUDIO2_BUFFER buffer = {};
	SoundState state = SoundState::Stopped;
	unsigned int pos = 0;
	int loaded_sample_num = -1;
	int id = 0;
	int looping = 0;
	double vol = 1.0;
	int stream_type = 0;
	bool isAllocated = false;
	bool isPlaying = false;
	bool isReleased = false;
	int frequency = 0;
	int volume = 255;
	int pan = 128;
};

// Represents a loaded audio sample
struct SAMPLE {
	WAVEFORMATEX fx = {};

	uint32_t sampleCount = 0;
	uint32_t dataSize = 0;
	SoundState state = SoundState::Null;
	int num = -1;
	std::string name;

	std::unique_ptr<uint8_t[]> data8;    // 8-bit PCM data
	std::unique_ptr<int16_t[]> data16;   // 16-bit PCM data
	void* buffer = nullptr;              // Pointer to active buffer (either data8 or data16)
};
/*
Volume setting notes:
If you already have 0-255:
sample_set_volume(ch, vol255);
If a UI gives 0-100:
sample_set_volume(ch, VolPercentToByte(uiPercent));
*/
// Byte (0..255) -> linear gain, routed through the existing percent curve.
// This preserves the loudness taper while moving call sites to 0..255.
// -----------------------------------------------------------------------------
// VolumeByteToLinear
// Convert legacy 0..255 volume directly to linear gain 0.0f..1.0f.
// -----------------------------------------------------------------------------
inline float VolumeByteToLinear(int vol255) noexcept
{
	vol255 = std::clamp(vol255, 0, 255);
	return static_cast<float>(vol255) / 255.0f;
}

// helper to map 0..100 -> 0..255 and forward
inline int VolPercentToByte(int percent) noexcept {
	percent = std::clamp(percent, 0, 100);
	return (percent * 255 + 50) / 100; // round-to-nearest
}

inline int VolByteToPercent(int vol255) noexcept {
	return std::clamp((vol255 * 100 + 127) / 255, 0, 100);
}

//Main Audio Mixer volume controls
float mixer_get_master_volume();
void mixer_set_master_volume(int volume);
void sample_set_volume_mixer(int chanid, int volume255);
// Master volume: 
// Optional convenience wrapper if you want a 0..255 version without touching callers:
inline void mixer_set_master_volume_255(int vol255) {
	const int percent = std::clamp((vol255 * 100 + 127) / 255, 0, 100);
	mixer_set_master_volume(percent);  // existing function
}

// Mixer core functions
int mixer_init(int rate, int fps);
// Just signals the audio thread to process
void mixer_update();
// Shutdown
void mixer_end();

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
	bool stereo = false);

// -----------------------------------------------------------------------------
// Sample loading from memory buffers (no file I/O)
// -----------------------------------------------------------------------------

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
int load_sample_from_buffer(const uint8_t* data, size_t size, const char* name = nullptr, bool force_resample = true);

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
bool save_sample_to_buffer(int samplenum, std::vector<uint8_t>& out_buffer);

// Sample playback control
void sample_stop(int chanid);
void sample_start(int chanid, int samplenum, int loop);
void sample_set_position(int chanid, int pos);
void sample_set_volume(int chanid, int volume);
int sample_get_volume(int chanid);
void sample_set_freq(int chanid, int freq);
int sample_get_freq(int chanid);
void sample_set_pan(int chanid, int pan);
int sample_get_pan(int chanid);
int sample_playing(int chanid);
void sample_end(int chanid);

int nameToNum(const std::string& name);
std::string numToName(int num);
int snumlookup(int snum);

unsigned char Make8bit(int16_t sample);
short Make16bit(uint8_t sample);

// Mixer streaming
void sample_start_mixer(int chanid, int samplenum, int loop);
void sample_stop_mixer(int chanid);
void sample_end_mixer(int chanid);

// Streaming functions
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name);
void stream_start(int chanid, int stream, int bits, int frame_rate, bool stereo);
void stream_start(int chanid, int stream, int bits, int frame_rate);
void stream_stop(int chanid, int stream);
void stream_update(int chanid, short* data);
void stream_update(int chanid, unsigned char* data);

// Stops all samples and clears the mixer list
void samples_stop_all();

// Stub for removing a sample (currently empty implementation)
void sample_remove(int samplenum);

void pause_audio();
void restore_audio();

// Xaudio2 Function Headers:
HRESULT xaudio2_init(int rate, int fps);
HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength);
void xaudio2_stop();
BYTE* GetNextBuffer();

// Wav File Loader Header:
int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile);

// Resampling functions
void resample_wav_8(SAMPLE* sample, int new_freq);
void resample_wav_16(SAMPLE* sample, int new_freq, bool use_cubic = true);

#ifdef USE_VUMETER
void mixer_get_vu(float* left, float* right);
void mixer_reset_vu();
#endif

double dBToAmplitude(double db);

// -----------------------------------------------------------------------------
// adjust_volume_dB
// In-place volume adjust on 16-bit PCM by a dB amount. Clamps to int16.
//
// Parameters:
//   samples     - pointer to 16-bit PCM samples (mono or interleaved; unchanged)
//   num_samples - number of 16-bit sample values (not frames)
//   dB          - gain in decibels (positive to boost, negative to cut)
// -----------------------------------------------------------------------------
void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB);

// -----------------------------------------------------------------------------
// linear_interpolation_8    (DEFAULT PATH FOR 8-BIT PCM)
// Endpoint-aligned linear resampler for unsigned 8-bit PCM. No allocation.
//
// Parameters:
//   input        - pointer to 8-bit PCM (unsigned)
//   input_size   - number of input samples
//   output       - destination buffer
//   output_size  - number of output samples to generate
// -----------------------------------------------------------------------------
// OLD ORDER: (input, output, input_size, output_size)
void linear_interpolation_8(const uint8_t* input,
	uint8_t* output,
	int input_size,
	int output_size);
// -----------------------------------------------------------------------------
// lowpass_postfilter_16
// Optional light 5-tap low-pass postfilter for upsampled 16-bit PCM.
// Helpful to reduce imaging after upsampling. In-place.
//
// Parameters:
//   data    - pointer to 16-bit PCM (mono or interleaved acceptable)
//   samples - number of samples
// -----------------------------------------------------------------------------
void lowpass_postfilter_16(int16_t* data, int32_t samples);

// ============================== OPTIONAL WRAPPERS =============================
// These allocate with new[] and call the *_into() defaults under the hood.
// Use only when you explicitly want the function to allocate for you.
// ============================================================================

// -----------------------------------------------------------------------------
// linear_interpolation_16
// Allocating wrapper. Computes output length from ratio, allocates with new[],
// then calls linear_interpolation_16_into.
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).
//
// Parameters:
//   input_data     - pointer to 16-bit PCM samples
//   input_samples  - number of input samples
//   output_data    - [out] receives newly-allocated buffer (new[])
//   output_samples - [out] number of output samples
//   ratio          - output_rate / input_rate (e.g., 2.0 upsamples 22k -> 44k)
// -----------------------------------------------------------------------------
void linear_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio);

// -----------------------------------------------------------------------------
// cubic_interpolation_16
// Allocating wrapper for higher-quality cubic resampling.
//
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).

// Parameters: same semantics as linear_interpolation_16.
// -----------------------------------------------------------------------------
void cubic_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio);
// -------------------------- Non-allocating 16-bit -----------------------------
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).

void linear_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples);

void cubic_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples);

//
// WAV Filters
//

void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
void lowPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
// -----------------------------------------------------------------------------
// wav_filters.h
// Low-pass filter utilities for post-processing audio buffers.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// design_biquad_lowpass
// Compute coefficients for a 2nd-order low-pass biquad filter
// using bilinear transform.
//
// Parameters:
//   fs   - sample rate in Hz
//   fc   - cutoff frequency in Hz
//   Q    - quality factor (default 0.707 for Butterworth)
//
// Outputs (by reference):
//   b0, b1, b2 - numerator coefficients
//   a1, a2     - denominator coefficients (a0 normalized to 1.0)
// -----------------------------------------------------------------------------
void design_biquad_lowpass(float fs, float fc, float Q,
	float& b0, float& b1, float& b2,
	float& a1, float& a2);

// -----------------------------------------------------------------------------
// biquad_lowpass_inplace_i16
// Apply a low-pass biquad filter to an array of int16_t samples in place.
//
// Parameters:
//   x       - pointer to audio buffer
//   n       - number of samples
//   fs      - sample rate in Hz
//   fc      - cutoff frequency in Hz
//   Q       - quality factor (default 0.707 for Butterworth)
//   passes  - how many times to apply the filter (default 1)
//             (more passes = steeper roll-off)
//
// Behavior:
//   - Processes samples in place.
//   - Clamps output to [-32768, 32767].
// -----------------------------------------------------------------------------
void biquad_lowpass_inplace_i16(int16_t* x, int n,
	float fs, float fc,
	float Q = 0.707f,
	int passes = 1);

// WAV SWEEP FUNCTIONS
// -----------------------------------------------------------------------------
// wavsweep_init
// Ensures the sweep worker thread is running. Usually unnecessary to call
// explicitly because all public sweep APIs auto-init the worker.
// -----------------------------------------------------------------------------
void wavsweep_init();

// -----------------------------------------------------------------------------
// wavsweep_shutdown
// Stops the sweep worker thread and clears all active sweeps. Safe to call
// multiple times, and will also be invoked automatically at process exit.
// -----------------------------------------------------------------------------
void wavsweep_shutdown();

// -----------------------------------------------------------------------------
// _mixer_ramp_volume
// Linearly ramp channel volume to endvol over time_ms milliseconds.
// Parameters:
//   voice   - channel index
//   time_ms - duration in milliseconds (<=0 applies immediately)
//   endvol  - target volume in [0..255]
// -----------------------------------------------------------------------------
void mixer_ramp_volume(int voice, int time_ms, int endvol);

// -----------------------------------------------------------------------------
// _mixer_sweep_frequency
// Linearly sweep channel frequency to endfreq over time_ms milliseconds.
// Parameters:
//   voice    - channel index
//   time_ms  - duration in milliseconds (<=0 applies immediately)
//   endfreq  - target frequency in Hz
//
// Note:
//   Only effective for direct XAudio2 voices. Software-mixed channels ignore
//   frequency changes by design, matching the underlying mixer behavior.
// -----------------------------------------------------------------------------
void mixer_sweep_frequency(int voice, int time_ms, int endfreq);

// -----------------------------------------------------------------------------
// _mixer_sweep_pan
// Linearly sweep channel pan to endpan over time_ms milliseconds.
// Parameters:
//   voice   - channel index
//   time_ms - duration in milliseconds (<=0 applies immediately)
//   endpan  - target pan in [0..255] (128=center)
// -----------------------------------------------------------------------------
void mixer_sweep_pan(int voice, int time_ms, int endpan);

#endif // MIXER_H
