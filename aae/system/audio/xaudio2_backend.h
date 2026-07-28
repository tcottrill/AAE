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

#ifndef WIN7BUILD
#include <xaudio2.h>
#else
#include <xaudio2redist.h>
#endif

// Defined in mixer.h. Only a reference/value round-trips through this header;
// the full definition is pulled in by xaudio2_backend.cpp where it's used.
struct WaveFormat;

// Opaque per-channel voice. The concrete definition lives in the backend
// .cpp - mixer.cpp only ever holds a pointer. A backend with no per-voice
// concept (ALSA) routes these into its own software mixer.
struct VoiceHandle;

class IAudioBackend {
public:
	virtual ~IAudioBackend() = default;

	// Build engine + mastering voice + output streaming voice + ring buffers.
	// Returns true on success. On failure, internal state is fully torn down
	// and the underlying error is logged before returning.
	virtual bool Init(int rateHz, int fps) = 0;

	// Idempotent. Safe to call from destructors.
	virtual void Shutdown() = 0;

	// Returns the next ring-buffer slot for the mixer to fill.
	virtual uint8_t* GetNextBuffer() = 0;

	// Submit a filled buffer to the output device. If the device is backed up
	// (ring buffer full), the backend may drop the frame and return true.
	virtual bool Submit(uint8_t* buffer, uint32_t bytes) = 0;

	// Master output gain, linear 0..1. SetMasterVolume(curve_applied_value).
	virtual void  SetMasterVolume(float linear) = 0;
	virtual float GetMasterVolume() const = 0;

	// Output channel count and SPEAKER_xxx mask of the mastering voice.
	// Captured after Init from XAudio2's GetVoiceDetails / GetChannelMask.
	// Used by X3DAudio init and by per-voice SetOutputMatrix sizing.
	virtual uint32_t OutputChannelCount() const = 0;
	virtual uint32_t OutputChannelMask() const = 0;

	int OutputRate() const { return m_rate; }
	int FramesPerUpdate() const { return m_frames_per_update; }

	// --- Per-channel voice path -------------------------------------------
	// Returns nullptr on failure. The backend owns the allocation; release
	// it with VoiceDestroy.
	virtual VoiceHandle* VoiceCreate(const WaveFormat& fmt) = 0;
	virtual void         VoiceDestroy(VoiceHandle* v) = 0;

	// Queue PCM for playback. loop=true repeats indefinitely.
	virtual bool VoiceSubmit(VoiceHandle* v, const uint8_t* data,
	                         uint32_t bytes, bool loop) = 0;
	virtual bool VoiceStart(VoiceHandle* v) = 0;
	virtual void VoiceStop(VoiceHandle* v) = 0;
	virtual void VoiceFlush(VoiceHandle* v) = 0;

	// Stop looping at the end of the current pass; the tail still plays.
	virtual void VoiceExitLoop(VoiceHandle* v) = 0;

	virtual void VoiceSetVolume(VoiceHandle* v, float gain) = 0;
	virtual void VoiceSetFrequencyRatio(VoiceHandle* v, float ratio) = 0;

	// Number of buffers still queued. 0 means playback has drained - this is
	// how the mixer decides a one-shot has finished.
	virtual uint32_t VoiceBuffersQueued(VoiceHandle* v) = 0;

	// Source channel count of this voice (1 or 2).
	virtual uint32_t VoiceInputChannels(VoiceHandle* v) = 0;

	// Per-channel gain matrix, srcChannels*dstChannels floats.
	//
	// LAYOUT IS DESTINATION-MAJOR: the gain from source channel S to output
	// channel D lives at matrix[srcChannels * D + S] - NOT [dstChannels*S + D].
	// Getting this backwards does not error; it silently routes audio to the
	// wrong speakers (e.g. stereo R onto the LFE channel, which is inaudible).
	// A backend implementing this must honour that convention, and a caller
	// building a matrix must fill it that way. See SetPan() in mixer.cpp for
	// a worked example.
	virtual void VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
	                                  uint32_t dstChannels,
	                                  const float* matrix) = 0;

protected:
	int m_rate = 0;
	int m_fps = 0;
	int m_frames_per_update = 0;
};

// Returns the active backend, or nullptr if the mixer hasn't initialized one
// yet. Defined in mixer.cpp (which owns the single IAudioBackend instance).
// Lets modules like audio_3d.cpp reach the backend's Voice* methods without
// duplicating a global or growing IAudioBackend's own surface.
IAudioBackend* audio_backend_instance();

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
	void VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
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
