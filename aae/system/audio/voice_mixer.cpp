//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// voice_mixer.cpp -- see voice_mixer.h.
//
// No OS calls. Pure arithmetic, so the Teensy target can reuse it verbatim.
//==============================================================================
#include "voice_mixer.h"

#include <algorithm>

void VoiceMixer::Configure(uint32_t outChannels, uint32_t outRate)
{
	std::lock_guard<std::mutex> g(m_lock);
	m_outChannels = outChannels ? outChannels : 2;
	m_outRate     = outRate ? outRate : 44100;
}

void VoiceMixer::SetMasterVolume(float linear)
{
	std::lock_guard<std::mutex> g(m_lock);
	m_master = std::clamp(linear, 0.0f, 1.0f);
}

float VoiceMixer::GetMasterVolume() const
{
	std::lock_guard<std::mutex> g(m_lock);
	return m_master;
}

SoftVoice* VoiceMixer::Create()
{
	std::lock_guard<std::mutex> g(m_lock);
	m_voices.push_back(std::make_unique<SoftVoice>());
	return m_voices.back().get();
}

void VoiceMixer::Destroy(SoftVoice* v)
{
	if (!v) return;
	std::lock_guard<std::mutex> g(m_lock);
	m_voices.erase(
		std::remove_if(m_voices.begin(), m_voices.end(),
			[v](const std::unique_ptr<SoftVoice>& p) { return p.get() == v; }),
		m_voices.end());
}

void VoiceMixer::MixInto(int16_t* out, uint32_t frames)
{
	if (!out || frames == 0) return;

	std::lock_guard<std::mutex> g(m_lock);

	const size_t samples = (size_t)frames * m_outChannels;

	// Accumulate in 32-bit so summed voices cannot wrap before the final
	// clamp. The buffer is a member rather than a local so the feed thread
	// does not allocate once per period.
	m_acc.assign(samples, 0);

	for (auto& vp : m_voices) {
		SoftVoice* v = vp.get();
		if (!v->playing || v->data.empty()) continue;

		const uint32_t bytesPerSample = v->bitsPerSample / 8;
		if (bytesPerSample == 0 || v->channels == 0) continue;

		const size_t totalFrames = v->data.size() / ((size_t)bytesPerSample * v->channels);
		if (totalFrames == 0) continue;

		// Sample-rate conversion and the voice's own frequency ratio, combined
		// into one step. Nearest-neighbour: the emulated chips this feeds are
		// already producing aliased square waves, so interpolation would be
		// spending cycles to smooth something that is meant to be harsh.
		const double step = ((double)v->sampleRate / (double)m_outRate) * (double)v->freqRatio;

		for (uint32_t f = 0; f < frames; ++f) {
			size_t idx = (size_t)v->position;

			if (idx >= totalFrames) {
				if (v->looping && !v->exitLoopReq) {
					v->position = 0.0;
					idx = 0;
				} else {
					// One-shot finished, or a loop asked to stop at the end of
					// its current pass. Dropping buffersQueued to 0 is what
					// tells mixer.cpp the channel has drained.
					v->playing     = false;
					v->exitLoopReq = false;
					v->position    = 0.0;
					if (v->buffersQueued) --v->buffersQueued;
					break;
				}
			}

			for (uint32_t c = 0; c < m_outChannels; ++c) {
				// Mono voices feed every output channel; multi-channel voices
				// map channel-for-channel and repeat the last one if the
				// output is wider than the source.
				const uint32_t src = (v->channels == 1)
				                   ? 0u
				                   : std::min(c, v->channels - 1);

				int32_t sample = 0;
				const size_t si = idx * v->channels + src;

				if (v->bitsPerSample == 16) {
					const int16_t* p = reinterpret_cast<const int16_t*>(v->data.data());
					sample = p[si];
				} else {
					// 8-bit PCM is UNSIGNED (0..255, centre 128), which is what
					// the WAV format and the sample loader produce. Bias to
					// signed and scale to 16-bit.
					sample = ((int32_t)v->data[si] - 128) << 8;
				}

				m_acc[(size_t)f * m_outChannels + c] += (int32_t)(sample * v->gain);
			}

			v->position += step;
		}
	}

	for (size_t i = 0; i < samples; ++i) {
		const int32_t s = (int32_t)(m_acc[i] * m_master);
		out[i] = (int16_t)std::clamp(s, -32768, 32767);
	}
}
