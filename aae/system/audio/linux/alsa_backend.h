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
#include <thread>
#include <vector>

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

	snd_pcm_t*        m_pcm      = nullptr;
	VoiceMixer        m_mixer;
	std::thread       m_thread;
	std::atomic<bool> m_running{ false };

	uint32_t          m_channels = 2;
	snd_pcm_uframes_t m_period   = 0;

	std::vector<int16_t> m_scratch;    // One period, handed to the voice mixer
	std::vector<uint8_t> m_appBuffer;  // Returned by GetNextBuffer()
};
