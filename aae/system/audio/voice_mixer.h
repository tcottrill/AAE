//==============================================================================
// voice_mixer.h -- software mixing of IAudioBackend voices into one PCM stream.
//
// XAudio2 provides a hardware-ish voice mixer; ALSA does not - it hands back a
// single PCM stream. So everything IAudioBackend's Voice* methods promise
// (per-voice gain, frequency ratio, looping, queued-buffer counts) has to be
// done in software on such a backend.
//
// This file deliberately contains NO ALSA calls, and no OS calls of any kind.
// The Teensy 4.1 target needs exactly this mixing over a pair of DACs, and a
// second copy of it there would drift from this one. alsa_backend.cpp owns the
// device; this owns the arithmetic.
//==============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

// One software voice. The backend hands mixer.cpp an opaque VoiceHandle*
// that is really a pointer to one of these.
struct SoftVoice {
	std::vector<uint8_t> data;          // Backend-owned copy of the submitted PCM
	uint32_t channels      = 1;
	uint32_t sampleRate    = 44100;
	uint32_t bitsPerSample = 16;

	double   position      = 0.0;       // Fractional read cursor, in frames
	float    gain          = 1.0f;
	float    freqRatio     = 1.0f;
	bool     looping       = false;
	bool     playing       = false;
	bool     exitLoopReq   = false;     // VoiceExitLoop: finish this pass, then stop
	uint32_t buffersQueued = 0;
};

class VoiceMixer {
public:
	// outChannels/outRate describe the single stream voices are summed into.
	void Configure(uint32_t outChannels, uint32_t outRate);

	// Sums every playing voice into `out` (interleaved int16), REPLACING its
	// contents. frames = samples per channel.
	void MixInto(int16_t* out, uint32_t frames);

	void  SetMasterVolume(float linear);
	float GetMasterVolume() const;

	// Voices are owned here; the backend hands out opaque pointers to them.
	SoftVoice* Create();
	void       Destroy(SoftVoice* v);

private:
	mutable std::mutex m_lock;
	std::vector<std::unique_ptr<SoftVoice>> m_voices;
	std::vector<int32_t> m_acc;          // Reused accumulator, see MixInto
	uint32_t m_outChannels = 2;
	uint32_t m_outRate     = 44100;
	float    m_master      = 0.80f;      // Matches mixer.cpp's default (~-1.9 dB)
};
