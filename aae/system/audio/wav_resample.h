/* =============================================================================
 * File: wav_resample.h
 * Overview:
 *   Public API for lightweight PCM resampling and simple gain utilities.
 *   Provides endpoint-aligned linear and cubic (Catmull-Rom) resamplers for
 *   16-bit PCM, a linear resampler for unsigned 8-bit PCM, an optional 5-tap
 *   low-pass postfilter, and helpers for dB <-> amplitude scaling.
 *
 * Responsibilities:
 *   - 8-bit linear resampling: linear_interpolation_8(...)
 *   - 16-bit resampling (non-allocating): linear_interpolation_16_into(...),
 *     cubic_interpolation_16_into(...)
 *   - 16-bit resampling (allocating, buffer-reusing): linear_interpolation_16(...),
 *     cubic_interpolation_16(...)
 *   - Gain utilities: dBToAmplitude(...), adjust_volume_dB(...)
 *   - Optional image-taming low-pass filter: lowpass_postfilter_16(...)
 *
 * Data & Formats:
 *   - 16-bit signed PCM (mono or de-interleaved) for 16-bit APIs.
 *   - Unsigned 8-bit PCM for the 8-bit path.
 *   - Endpoint-aligned mapping (i=0->src[0], i=outN-1->rc[inN-1]).
 *
 * Performance Notes:
 *   - “_into” variants avoid heap churn; allocating variants reuse previous
 *     buffers when size is unchanged.
 *   - Saturation and clamping keep outputs within valid ranges.
 *
 * Dependencies:
 *   - <cstdint>, <cstddef>
 *
 * ---------------------------------------------------------------------------
 * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2022-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * =============================================================================
 */

#pragma once
#include <cstdint>
#include <cstddef>

// -----------------------------------------------------------------------------
// dBToAmplitude
// Convert decibels to linear amplitude (vol factor).
//
// Parameters:
//   db - decibels
//
// Returns:
//   Linear amplitude scalar (1.0 == 0 dB).
// -----------------------------------------------------------------------------
double dBToAmplitude(double db);

// -----------------------------------------------------------------------------
// adjust_volume_dB
// In-place volume adjust on 16-bit PCM by a dB amount. Clamps to int16.
//
// Parameters:
//   samples     - pointer to 16-bit PCM samples (mono or interleaved; unchanged)
//   num_samples - number of 16-bit sample values (not frames)
//   dB          - gain in decibels (positive to boost, negative to cut)
// -----------------------------------------------------------------------------
void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB);

// -----------------------------------------------------------------------------
// linear_interpolation_8    (DEFAULT PATH FOR 8-BIT PCM)
// Endpoint-aligned linear resampler for unsigned 8-bit PCM. No allocation.
//
// Parameters:
//   input        - pointer to 8-bit PCM (unsigned)
//   input_size   - number of input samples
//   output       - destination buffer
//   output_size  - number of output samples to generate
// -----------------------------------------------------------------------------
// OLD ORDER: (input, output, input_size, output_size)
void linear_interpolation_8(const uint8_t* input,
    uint8_t* output,
    int input_size,
    int output_size);
// -----------------------------------------------------------------------------
// lowpass_postfilter_16
// Optional light 5-tap low-pass postfilter for upsampled 16-bit PCM.
// Helpful to reduce imaging after upsampling. In-place.
//
// Parameters:
//   data    - pointer to 16-bit PCM (mono or interleaved acceptable)
//   samples - number of samples
// -----------------------------------------------------------------------------
void lowpass_postfilter_16(int16_t* data, int32_t samples);

// ============================== OPTIONAL WRAPPERS =============================
// These allocate with new[] and call the *_into() defaults under the hood.
// Use only when you explicitly want the function to allocate for you.
// ============================================================================

// -----------------------------------------------------------------------------
// linear_interpolation_16
// Allocating wrapper. Computes output length from ratio, allocates with new[],
// then calls linear_interpolation_16_into.
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).
//
// Parameters:
//   input_data     - pointer to 16-bit PCM samples
//   input_samples  - number of input samples
//   output_data    - [out] receives newly-allocated buffer (new[])
//   output_samples - [out] number of output samples
//   ratio          - output_rate / input_rate (e.g., 2.0 upsamples 22k -> 44k)
// -----------------------------------------------------------------------------
void linear_interpolation_16(const int16_t* input_data,
    int32_t input_samples,
    int16_t** output_data,
    int32_t* output_samples,
    float ratio);

// -----------------------------------------------------------------------------
// cubic_interpolation_16
// Allocating wrapper for higher-quality cubic resampling.
//
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).

// Parameters: same semantics as linear_interpolation_16.
// -----------------------------------------------------------------------------
void cubic_interpolation_16(const int16_t* input_data,
    int32_t input_samples,
    int16_t** output_data,
    int32_t* output_samples,
    float ratio);
// -------------------------- Non-allocating 16-bit -----------------------------
// NOTE: These operate on a single PCM stream (mono or deinterleaved). For
// interleaved stereo, resample L and R separately (per-channel).

void linear_interpolation_16_into(const int16_t* input_data,
    int32_t input_samples,
    int16_t* output_data,
    int32_t output_samples);

void cubic_interpolation_16_into(const int16_t* input_data,
    int32_t input_samples,
    int16_t* output_data,
    int32_t output_samples);

