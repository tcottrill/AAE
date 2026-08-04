//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// =============================================================================
// xaudio2_backend.h
// Audio streaming backend abstraction.
//
// IAudioBackend is the interface mixer.cpp uses to push interleaved S16 stereo
// audio to the OS, AND (as of Task 2) to drive the per-channel voice path --
// mixer.cpp holds only an opaque VoiceHandle* per channel and never touches
// XAudio2 types directly. It hides the engine (XAudio2 today, WASAPI / ALSA
// tomorrow) from the software mixer.
//
// XAudio2Backend is the concrete implementation. A backend with no per-voice
// concept (e.g. ALSA) implements the Voice* methods against its own software
// mixer instead of a hardware voice.
// =============================================================================
#pragma once

#include <cstdint>

// The neutral contract this class implements, plus the WaveFormat/VoiceHandle
// forward declarations that used to live here. IAudioBackend was declared in
// THIS file until Phase 3b, which trapped the portable interface behind the
// unconditional <xaudio2.h> below - mixer.cpp could not be parsed on Linux,
// and an ALSA backend would have had to include the XAudio2 header to learn
// what it implements.
#include "audio_backend.h"

#ifndef WIN7BUILD
#include <xaudio2.h>
#else
#include <xaudio2redist.h>
#endif



class XAudio2Backend : public IAudioBackend {
public:
	XAudio2Backend() = default;
	~XAudio2Backend() override { Shutdown(); }

	bool Init(int rateHz, int fps) override;
	void Shutdown() override;

	uint8_t* GetNextBuffer() override;
	bool     Submit(uint8_t* buffer, uint32_t bytes) override;

	void    SetMasterVolume(float linear) override;
	float   GetMasterVolume() const override;

	uint32_t OutputChannelCount() const override { return m_output_channels; }
	uint32_t OutputChannelMask() const override { return m_output_channel_mask; }

	VoiceHandle* VoiceCreate(const WaveFormat& fmt) override;
	void         VoiceDestroy(VoiceHandle* v) override;
	bool VoiceSubmit(VoiceHandle* v, const uint8_t* data, uint32_t bytes, bool loop) override;
	bool VoiceStart(VoiceHandle* v) override;
	void VoiceStop(VoiceHandle* v) override;
	void VoiceFlush(VoiceHandle* v) override;
	void VoiceExitLoop(VoiceHandle* v) override;
	void VoiceSetVolume(VoiceHandle* v, float gain) override;
	void VoiceSetFrequencyRatio(VoiceHandle* v, float ratio) override;
	uint32_t VoiceBuffersQueued(VoiceHandle* v) override;
	uint32_t VoiceInputChannels(VoiceHandle* v) override;
	bool VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
	                          uint32_t dstChannels, const float* matrix) override;

private:
	static constexpr int kNumBuffers = 5;

	IXAudio2*               m_xaudio2 = nullptr;
	IXAudio2MasteringVoice* m_master = nullptr;
	IXAudio2SourceVoice*    m_source = nullptr;
	BYTE*                   m_buffers[kNumBuffers]{};
	DWORD                   m_buffer_size = 0;
	int                     m_current = 0;
	bool                    m_com_init_local = false;
	uint32_t                m_output_channels = 0;
	uint32_t                m_output_channel_mask = 0;
};
