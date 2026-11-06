#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Global "cabinet tone" output filter ([main] audio_filter), MAME-style
// softening of the final mix.
//
//   0 OFF   : true bypass, process() does nothing.
//   1 LIGHT : 20 Hz DC-blocking high-pass + 2nd-order Butterworth low-pass at 12 kHz.
//   2 WARM  : same DC block + Butterworth low-pass at 7 kHz.
//
// Both stages are RBJ-cookbook biquads (Q = 1/sqrt(2)), coefficients in double,
// state in double per channel (up to 8). Switching LIGHT<->WARM only swaps the
// low-pass coefficients (state kept, no click); coming from OFF clears state.
//
// Threading: configure() at backend init before audio threads run, then
// configure()/process() from the single audio thread only.
class OutputTone {
public:
	static constexpr unsigned kMaxChannels = 8;

	void configure(unsigned rate, unsigned channels)
	{
		rate_ = rate ? rate : 44100;
		channels_ = (std::min)((std::max)(channels, 1u), kMaxChannels);
		last_ = 0;
		clear_state();
	}

	// Interleaved float, +-1.0 scale.
	void process(float* interleaved, unsigned frames, int preset)
	{
		if (preset < 0 || preset > 2) preset = 1;
		if (preset == 0) { last_ = 0; return; }
		if (preset != last_) {
			if (last_ == 0) clear_state();
			design(preset);
			last_ = preset;
		}
		const unsigned nch = channels_;
		for (unsigned c = 0; c < nch; ++c) {
			State& s = st_[c];
			float* p = interleaved + c;
			for (unsigned i = 0; i < frames; ++i, p += nch) {
				const double x = (double)*p;
				// transposed direct form II: high-pass then low-pass
				double y = hp_.b0 * x + s.h1;
				s.h1 = hp_.b1 * x - hp_.a1 * y + s.h2;
				s.h2 = hp_.b2 * x - hp_.a2 * y;
				const double v = y;
				y = lp_.b0 * v + s.l1;
				s.l1 = lp_.b1 * v - lp_.a1 * y + s.l2;
				s.l2 = lp_.b2 * v - lp_.a2 * y;
				*p = (float)y;
			}
			// flush denormals / tiny residue
			if (std::fabs(s.h1) < 1e-20) s.h1 = 0;
			if (std::fabs(s.h2) < 1e-20) s.h2 = 0;
			if (std::fabs(s.l1) < 1e-20) s.l1 = 0;
			if (std::fabs(s.l2) < 1e-20) s.l2 = 0;
		}
	}

	// 16-bit interleaved: convert, filter, round, clamp (the low-pass overshoots).
	void process_i16(int16_t* interleaved, unsigned frames, int preset)
	{
		if (preset < 0 || preset > 2) preset = 1;
		if (preset == 0) { last_ = 0; return; }
		const size_t n = (size_t)frames * channels_;
		if (scratch_.size() < n) scratch_.resize(n);
		for (size_t i = 0; i < n; ++i) scratch_[i] = (float)interleaved[i] * (1.0f / 32768.0f);
		process(scratch_.data(), frames, preset);
		for (size_t i = 0; i < n; ++i) {
			float v = scratch_[i] * 32768.0f;
			v = v < -32768.0f ? -32768.0f : (v > 32767.0f ? 32767.0f : v);
			interleaved[i] = (int16_t)std::lrintf(v);
		}
	}

private:
	struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
	struct State { double h1 = 0, h2 = 0, l1 = 0, l2 = 0; };

	void clear_state() { for (auto& s : st_) s = State(); }

	void design(int preset)
	{
		const double pi = 3.14159265358979323846;
		const double q = 0.70710678118654752;
		const double fs = (double)rate_;
		const double maxfc = 0.45 * fs;

		double fc = preset == 2 ? 7000.0 : 12000.0;
		if (fc > maxfc) fc = maxfc;
		{
			const double w0 = 2.0 * pi * fc / fs, cw = std::cos(w0), al = std::sin(w0) / (2.0 * q);
			const double a0 = 1.0 + al;
			lp_.b0 = ((1.0 - cw) * 0.5) / a0;
			lp_.b1 = (1.0 - cw) / a0;
			lp_.b2 = lp_.b0;
			lp_.a1 = (-2.0 * cw) / a0;
			lp_.a2 = (1.0 - al) / a0;
		}
		{
			const double hc = (std::min)(20.0, maxfc);
			const double w0 = 2.0 * pi * hc / fs, cw = std::cos(w0), al = std::sin(w0) / (2.0 * q);
			const double a0 = 1.0 + al;
			hp_.b0 = ((1.0 + cw) * 0.5) / a0;
			hp_.b1 = -(1.0 + cw) / a0;
			hp_.b2 = hp_.b0;
			hp_.a1 = (-2.0 * cw) / a0;
			hp_.a2 = (1.0 - al) / a0;
		}
	}

	unsigned rate_ = 44100;
	unsigned channels_ = 2;
	int last_ = 0;
	Coef hp_, lp_;
	State st_[kMaxChannels];
	std::vector<float> scratch_;
};
