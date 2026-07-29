// =============================================================================
// audio_backend.h
// The platform-neutral audio output contract.
//
// This is what an audio backend implements. It deliberately does NOT live in
// xaudio2_backend.h, where it used to: that header includes <xaudio2.h>
// unconditionally, so the neutral contract could not be read on any platform
// but Windows - and mixer.cpp, which is entirely portable, could not even be
// PARSED on Linux. An ALSA backend would also have had to include the XAudio2
// header just to learn what it was implementing.
//
// Implementations:
//   xaudio2_backend.cpp   Windows
//   alsa_backend.cpp      Linux (software voice mixing - see voice_mixer.h)
//   Teensy 4.1            later: DAC-driven, per docs/superpowers/specs
//
// Exactly ONE backend translation unit is linked into any given binary. Each
// defines its own layout for the opaque VoiceHandle below, so linking two at
// once would be an ODR violation - the build must never do it.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>

// Defined in mixer.h. Only a reference/value round-trips through this header;
// the full definition is pulled in by the backend .cpp where it is used.
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
	//
	// Returns false on failure (and logs the underlying error); true on
	// success.
	virtual bool VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
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

// -----------------------------------------------------------------------------
// Creates the backend this binary was built for. Defined in exactly one
// translation unit per platform: xaudio2_backend.cpp on Windows,
// alsa_backend.cpp on Linux.
//
// Returns nullptr if the audio device cannot be opened. Callers MUST handle
// that - a machine with no sound card is a legitimate configuration, not an
// error, and the emulator should run silently rather than refuse to start.
//
// This replaces mixer.cpp's hardcoded `std::make_unique<XAudio2Backend>()`,
// which named a concrete Windows type from otherwise portable code.
// -----------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend();
