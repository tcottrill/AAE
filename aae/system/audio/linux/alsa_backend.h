//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// alsa_backend.h -- the Linux IAudioBackend implementation.
//
// Owns device handling only: open, hardware-parameter negotiation, and the
// snd_pcm_writei feed thread. All per-voice arithmetic lives in voice_mixer.h,
// which contains no ALSA at all so the Teensy target can reuse it.
//
// Only ONE backend translation unit is ever linked into a binary (see
// audio_backend.h), so this file's private VoiceHandle definition cannot
// collide with xaudio2_backend.cpp's.
//==============================================================================
#pragma once

#include "audio_backend.h"
#include "voice_mixer.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

// PulseAudio simple-stream handle, forward-declared so this header stays free
// of <pulse/*.h>. Only alsa_backend.cpp needs the real definition.
struct pa_simple;

class AlsaBackend : public IAudioBackend {
public:
	AlsaBackend() = default;
	~AlsaBackend() override;

	bool Init(int rateHz, int fps) override;
	void Shutdown() override;

	uint8_t* GetNextBuffer() override;
	bool     Submit(uint8_t* buffer, uint32_t bytes) override;

	void  SetMasterVolume(float linear) override;
	float GetMasterVolume() const override;

	uint32_t OutputChannelCount() const override;
	uint32_t OutputChannelMask() const override;

	VoiceHandle* VoiceCreate(const WaveFormat& fmt) override;
	void         VoiceDestroy(VoiceHandle* v) override;

	bool VoiceSubmit(VoiceHandle* v, const uint8_t* data,
	                 uint32_t bytes, bool loop) override;
	bool VoiceStart(VoiceHandle* v) override;
	void VoiceStop(VoiceHandle* v) override;
	void VoiceFlush(VoiceHandle* v) override;
	void VoiceExitLoop(VoiceHandle* v) override;

	void VoiceSetVolume(VoiceHandle* v, float gain) override;
	void VoiceSetFrequencyRatio(VoiceHandle* v, float ratio) override;

	uint32_t VoiceBuffersQueued(VoiceHandle* v) override;
	uint32_t VoiceInputChannels(VoiceHandle* v) override;

	bool VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
	                          uint32_t dstChannels,
	                          const float* matrix) override;

private:
	void FeedThread();
	void UpmixPeriod();   // m_scratch (stereo) -> m_devBuf (m_devChannels-wide)
	void EncodeStereoPeriod();   // in-place matrix surround encode on m_scratch
	bool InitPulseNative(unsigned int rate, int f);   // the cubeb model; see .cpp
	void SetupBuffersAndUpmix(unsigned int rate);     // shared Init tail

	// PulseAudio NATIVE stream (the cubeb model). When a Pulse/PipeWire server
	// is reachable this is used INSTEAD of m_pcm: the stream carries an
	// EXPLICIT channel map into the server, bypassing the ALSA compat shim
	// whose position labelling for >2 channels cannot be verified (chmap query
	// returns nothing through it). Null when running on raw ALSA (Pi, bare
	// systems), in which case m_pcm carries the audio exactly as before.
	struct pa_simple* m_pulse    = nullptr;

	snd_pcm_t*        m_pcm      = nullptr;
	VoiceMixer        m_mixer;
	std::thread       m_thread;
	std::atomic<bool> m_running{ false };

	uint32_t          m_channels = 2;
	snd_pcm_uframes_t m_period   = 0;

	std::vector<int16_t> m_scratch;    // One period, handed to the voice mixer
	std::vector<uint8_t> m_appBuffer;  // Returned by GetNextBuffer()

	// ---- pseudo-surround upmix (device side only) ---------------------------
	//
	// EVERYTHING above this line stays stereo: the voice mixer, the app buffer,
	// the streaming ring, and every game-facing contract. When the device
	// offers 6 channels, the feed thread expands the finished stereo mix into
	// m_devBuf with a passive matrix (Dolby Surround style) as the very last
	// step before snd_pcm_writei:
	//
	//   FL/FR = L/R untouched      C   = 0.5*(L+R)
	//   RL/RR = 0.5*(L-R) delayed  LFE = low-passed mono
	//
	// FL/FR carrying the original stereo untouched is the graceful-degradation
	// guarantee: if anything downstream drops the extra channels, the result is
	// exactly the old stereo mix, never less.
	//
	// Index order within a device frame comes from the device's channel map
	// (queried at Init); m_chIndex holds where each position lands.
	enum { kFL = 0, kFR, kRL, kRR, kFC, kLFE, kMaxCh };
	uint32_t             m_devChannels = 2;   // negotiated; 2 or 6
	std::vector<int16_t> m_devBuf;            // one period, device-interleaved
	int                  m_chIndex[kMaxCh] = { 0, 1, 2, 3, 4, 5 };
	std::vector<float>   m_rearDelay;         // ~12ms surround decorrelation
	size_t               m_rearPos  = 0;
	float                m_lfeState = 0.0f;   // one-pole low-pass state
	float                m_lfeK     = 0.0f;   // its coefficient, set from rate

	// Matrix surround ENCODE state for the STEREO path (EncodeStereoPeriod):
	// a delayed mono ambience injected antiphase into L/R so downstream
	// difference-driven upmixers (PipeWire psd, Pro Logic, soundbars) derive
	// rears from our near-mono content. Empty when disabled or when running
	// discrete 5.1 (the two are mutually exclusive by construction).
	std::vector<float>   m_encDelay;
	size_t               m_encPos = 0;

	// ---- streaming path -----------------------------------------------------
	//
	// mixer_update_internal() mixes EVERY emulated sound chip - POKEY, AY-8910,
	// TMS5220, the DACs - into GetNextBuffer() and hands it to Submit() once per
	// video frame. That is the bulk of a game's audio; the voice path carries
	// only WAV samples. Both must reach the same PCM device, so Submit() queues
	// here and the feed thread sums this with the voice mix, which is what
	// XAudio2 does internally with two source voices.
	//
	// A ring rather than a single buffer because the two sides run on different
	// threads at nominally the same rate: the game thread submits one video
	// frame's worth, the feed thread consumes one period. They match on average
	// and jitter either side of it.
	std::mutex           m_streamLock;
	std::vector<int16_t> m_streamRing;         // interleaved, m_channels-wide
	size_t               m_streamHead  = 0;    // read cursor, in samples
	size_t               m_streamCount = 0;    // samples currently queued
	uint64_t             m_streamStarved = 0;  // feed thread found it short
	uint64_t             m_streamDropped = 0;  // submit overflowed the ring
	bool                 m_streamSeen  = false;// anything ever submitted

	// Diagnostics that distinguish the three ways "no streaming audio" can
	// happen, which are otherwise indistinguishable from outside:
	//   submits == 0            -> nothing is calling Submit at all
	//   submits > 0, peak == 0  -> it is being called with SILENCE, so the
	//                              fault is upstream in the mixer, not here
	//   submits > 0, peak > 0   -> real audio is arriving and the fault is in
	//                              this backend or below it
	// Draining the ring the moment anything arrives leaves no slack: the feed
	// thread consumes a period every ~20ms and the game submits one per video
	// frame, so the slightest jitter empties the ring and plays a gap. Priming
	// waits for a small cushion before consuming, and re-primes if the ring is
	// ever emptied completely - the standard fix for a producer and consumer
	// that are rate-matched on average but not instantaneously.
	std::chrono::steady_clock::time_point m_streamFirstAt{};
	bool     m_streamPriming = true;
	uint64_t m_streamReprimes = 0;

	uint64_t m_streamSubmits = 0;
	uint64_t m_streamSamples = 0;
	int32_t  m_streamPeak    = 0;   // max |sample| ever submitted
	bool     m_streamReported = false;
};
