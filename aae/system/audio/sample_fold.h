#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Dual-mono detection for 2-channel sample files.
//
// Some sample sets (spacfury, redbaron, llander, zektor, aae.zip ambients) ship
// 2-channel WAVs whose channels are identical. They are mono content, but the
// soundbar/TV widening only touches mono sources, so as "stereo" they got no
// rears or sub. Folding them to mono at load makes them eligible. No gain
// change: a stereo voice plays each channel at full gain, same as a mono voice
// (Linux levels stay identical; on Windows these become ~3 dB louder, matching
// Linux and the games' mono samples). See docs/audio-soundbar-widening.md.
//
// Rule: dual mono iff for EVERY frame |L - R| <= 2 (tolerates dither/rounding).
// Then mono[i] = (L + R) / 2. Otherwise returns false and leaves mono empty.

inline bool fold_dual_mono_16(const int16_t* stereo, size_t frames, std::vector<int16_t>& mono)
{
	mono.clear();
	if (!stereo || frames == 0) return false;
	for (size_t i = 0; i < frames; ++i) {
		const int32_t d = (int32_t)stereo[2 * i] - (int32_t)stereo[2 * i + 1];
		if (d > 2 || d < -2) return false;
	}
	mono.resize(frames);
	for (size_t i = 0; i < frames; ++i)
		mono[i] = (int16_t)(((int32_t)stereo[2 * i] + (int32_t)stereo[2 * i + 1]) / 2);
	return true;
}

// 8-bit unsigned (centered at 128): compare raw bytes, same tolerance.
inline bool fold_dual_mono_8(const uint8_t* stereo, size_t frames, std::vector<uint8_t>& mono)
{
	mono.clear();
	if (!stereo || frames == 0) return false;
	for (size_t i = 0; i < frames; ++i) {
		const int32_t d = (int32_t)stereo[2 * i] - (int32_t)stereo[2 * i + 1];
		if (d > 2 || d < -2) return false;
	}
	mono.resize(frames);
	for (size_t i = 0; i < frames; ++i)
		mono[i] = (uint8_t)(((int32_t)stereo[2 * i] + (int32_t)stereo[2 * i + 1]) / 2);
	return true;
}
