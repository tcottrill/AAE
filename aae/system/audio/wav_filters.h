/* =============================================================================
* -------------------------------------------------------------------------- -
*License(GPLv3) :
    *This file is part of GameEngine Alpha.
    *
    *<Project Name> is free software : you can redistribute it and /or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    *(at your option) any later version.
    *
    *<Project Name> is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    * GNU General Public License for more details.
    *
    * You should have received a copy of the GNU General Public License
    * along with GameEngine Alpha.If not, see < https://www.gnu.org/licenses/>.
*
*Copyright(C) 2022 - 2025  Tim Cottrill
* SPDX - License - Identifier : GPL - 3.0 - or -later
* ============================================================================ =
*/

#pragma once
#include <cstdint>
#include <vector>


void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
void lowPassFilter (std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
// -----------------------------------------------------------------------------
// wav_filters.h
// Low-pass filter utilities for post-processing audio buffers.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// design_biquad_lowpass
// Compute coefficients for a 2nd-order low-pass biquad filter
// using bilinear transform.
//
// Parameters:
//   fs   - sample rate in Hz
//   fc   - cutoff frequency in Hz
//   Q    - quality factor (default 0.707 for Butterworth)
//
// Outputs (by reference):
//   b0, b1, b2 - numerator coefficients
//   a1, a2     - denominator coefficients (a0 normalized to 1.0)
// -----------------------------------------------------------------------------
void design_biquad_lowpass(float fs, float fc, float Q,
    float& b0, float& b1, float& b2,
    float& a1, float& a2);

// -----------------------------------------------------------------------------
// biquad_lowpass_inplace_i16
// Apply a low-pass biquad filter to an array of int16_t samples in place.
//
// Parameters:
//   x       - pointer to audio buffer
//   n       - number of samples
//   fs      - sample rate in Hz
//   fc      - cutoff frequency in Hz
//   Q       - quality factor (default 0.707 for Butterworth)
//   passes  - how many times to apply the filter (default 1)
//             (more passes = steeper roll-off)
//
// Behavior:
//   - Processes samples in place.
//   - Clamps output to [-32768, 32767].
// -----------------------------------------------------------------------------
void biquad_lowpass_inplace_i16(int16_t* x, int n,
    float fs, float fc,
    float Q = 0.707f,
    int passes = 1);
