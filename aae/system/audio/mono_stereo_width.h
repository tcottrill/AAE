#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

// Mono-to-stereo widening for TVs and soundbars that make their own surround
// from the stereo PCM they are handed ([main] hdmi_passthrough, Linux).
//
// A TV or soundbar that derives rears and sub from stereo gets nothing from
// dual mono (L == R): the difference signal is zero. AAE's mono chips are dual
// mono after any symmetric upmix, so when this is enabled a controlled L/R
// difference is added to the MONO sources only; genuine stereo sources are
// never touched. Chosen by listening on a Hisense TV -> eARC -> Sonos: s is
// the mono signal 12 ms late at 0.7, full band (bass included), added to L and
// subtracted from R. This is stereo PCM processing, not Dolby/DTS bitstream
// passthrough.
//
// The mono source already sits equally in both channels, so the output is a
// per-sample DELTA (dl, dr) to add to L and R; the chip mixer and the voice
// mixer share this one class.
//
// Threading: configure() is called once at backend Init, before any mixing
// thread runs. enabled() and process() belong to the single thread that mixes
// (the chip mixer's audio thread, or the PipeWire loop thread for VoiceMixer).
class MonoStereoWidth {
public:
	void configure(bool enabled, unsigned rate)
	{
		enabled_ = enabled;
		delay_.clear();
		at_ = 0;
		if (enabled_) delay_.assign(1 + (size_t)rate * 12 / 1000, 0.f);
	}

	bool enabled() const { return enabled_; }

	void process(int mono, int& dl, int& dr)
	{
		const float delayed = delay_[at_];
		delay_[at_] = (float)mono;
		if (++at_ == delay_.size()) at_ = 0;
		dl = static_cast<int>(0.7f * delayed);
		dr = -dl;
	}

	// Float twin of process() for the XAudio2 widen bus: same delay line, same
	// 12 ms / 0.7 / antiphase semantics, no int truncation. mono is in the
	// same scale as the stream (+-1.0 float).
	void processf(float mono, float& dl, float& dr)
	{
		const float delayed = delay_[at_];
		delay_[at_] = mono;
		if (++at_ == delay_.size()) at_ = 0;
		dl = 0.7f * delayed;
		dr = -dl;
	}

private:
	bool enabled_ = false;
	std::vector<float> delay_;
	std::size_t at_ = 0;
};
