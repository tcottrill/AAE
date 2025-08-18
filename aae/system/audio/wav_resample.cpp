/* =============================================================================
 * File: wav_resample.cpp
 * Overview:
 *   Implementation of PCM resampling and gain/filter utilities declared in
 *   wav_resample.h. Includes endpoint-aligned linear and Catmull-Rom cubic
 *   resamplers with out-of-bounds safety, 8-bit linear resampling, a light
 *   5-tap low-pass postfilter, and dB-based volume adjustment with saturation.
 *
 * Implementation Details:
 *   - 16-bit paths: linear_into_core(...) and cubic_into_core(...) perform the
 *     core work; wrappers expose allocating and non-allocating APIs.
 *   - 8-bit path: linear_interpolation_8(...) with clamping to 0..255.
 *   - Gain: adjust_volume_dB(...) multiplies by 10^(dB/20) and saturates.
 *   - LPF: kernel [-1, 4, 6, 4, -1] / 12 to reduce upsampling imaging.
 *
 * Safety & Robustness:
 *   - Saturates to int16_t where applicable; clamps 8-bit outputs to 0..255.
 *   - Handles degenerate sizes (inN==1 or outN==1) sensibly.
 *   - Allocating wrappers reuse buffers when the size is unchanged.
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


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include "wav_resample.h"

// ============================== Utilities ====================================

double dBToAmplitude(double db)
{
    return std::pow(10.0, db / 20.0);
}

void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB)
{
    if (!samples || num_samples == 0) return;
    const double factor = std::pow(10.0, static_cast<double>(dB) / 20.0);
    for (size_t i = 0; i < num_samples; ++i) {
        const double s = static_cast<double>(samples[i]) * factor;
        const int32_t y = static_cast<int32_t>(std::llround(s));
        samples[i] = static_cast<int16_t>(std::clamp(
            y, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX)));
    }
}

static inline int16_t saturate_i16(int32_t v)
{
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(v);
}

static inline int16_t sample_at_safe_i16(const int16_t* s, int32_t n, int32_t idx)
{
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return s[idx];
}

static inline double catmull_rom_scalar(double p0, double p1, double p2, double p3, double x)
{
    const double a0 = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    const double a1 = 1.0 * p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    const double a2 = -0.5 * p0 + 0.5 * p2;
    const double a3 = p1;
    return ((a0 * x + a1) * x + a2) * x + a3;
}

// ============================= Core resamplers ================================
// Endpoint-aligned mapping (i=0→src0, i=outN-1→srcN-1). OOB-safe.

static inline void linear_into_core(const int16_t* in, int32_t inN,
    int16_t* out, int32_t outN)
{
    if (inN <= 0 || outN <= 0) return;

    if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
    if (outN == 1) { out[0] = in[0]; return; }

    const double scale = static_cast<double>(inN - 1) / static_cast<double>(outN - 1);

    for (int32_t i = 0; i < outN; ++i) {
        const double src_pos = static_cast<double>(i) * scale;
        int32_t idx = static_cast<int32_t>(src_pos);
        double  frac = src_pos - static_cast<double>(idx);

        if (idx >= inN - 1) { idx = inN - 2; frac = 1.0; }

        const int16_t a = in[idx + 0];
        const int16_t b = in[idx + 1];

        const int32_t a32 = static_cast<int32_t>(a);
        const int32_t diff = static_cast<int32_t>(b) - a32;
        const double  y = static_cast<double>(a32) + static_cast<double>(diff) * frac;

        out[i] = saturate_i16(static_cast<int32_t>(std::llround(y)));
    }
}

static inline void cubic_into_core(const int16_t* in, int32_t inN,
    int16_t* out, int32_t outN)
{
    if (inN <= 0 || outN <= 0) return;

    if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
    if (outN == 1) { out[0] = in[0]; return; }

    const double scale = static_cast<double>(inN - 1) / static_cast<double>(outN - 1);

    for (int32_t i = 0; i < outN; ++i) {
        const double src_pos = static_cast<double>(i) * scale;
        int32_t idx = static_cast<int32_t>(std::floor(src_pos));
        double  frac = src_pos - static_cast<double>(idx);

        if (idx >= inN - 1) { idx = inN - 2; frac = 1.0; }

        const double p0 = static_cast<double>(sample_at_safe_i16(in, inN, idx - 1));
        const double p1 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 0));
        const double p2 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 1));
        const double p3 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 2));

        const double y = catmull_rom_scalar(p0, p1, p2, p3, frac);
        out[i] = saturate_i16(static_cast<int32_t>(std::llround(y)));
    }
}

// =========================== Non-allocating wrappers ==========================
// (Use these if you want zero heap churn in some paths.)

void linear_interpolation_16_into(const int16_t* input_data,
    int32_t input_samples,
    int16_t* output_data,
    int32_t output_samples)
{
    if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
    linear_into_core(input_data, input_samples, output_data, output_samples);
}

void cubic_interpolation_16_into(const int16_t* input_data,
    int32_t input_samples,
    int16_t* output_data,
    int32_t output_samples)
{
    if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
    cubic_into_core(input_data, input_samples, output_data, output_samples);
}

// =================== Allocating, buffer-reusing resamplers ====================
// They allocate on first use and REUSE the buffer on
// subsequent calls if the required size is unchanged; otherwise they delete[]
// the old block and allocate once.

void linear_interpolation_16(const int16_t* input_data,
    int32_t input_samples,
    int16_t** output_data,
    int32_t* output_samples,
    float ratio)
{
    if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
        if (output_data)    *output_data = nullptr;
        if (output_samples) *output_samples = 0;
        return;
    }

    int32_t outN = static_cast<int32_t>(
        std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
    if (outN < 1) outN = 1;

    // Reuse if size matches; otherwise reallocate once.
    if (*output_data && *output_samples != outN) {
        delete[] * output_data;
        *output_data = nullptr;
    }
    if (!*output_data) {
        *output_data = new int16_t[outN];
    }
    *output_samples = outN;

    linear_into_core(input_data, input_samples, *output_data, outN);
}

void cubic_interpolation_16(const int16_t* input_data,
    int32_t input_samples,
    int16_t** output_data,
    int32_t* output_samples,
    float ratio)
{
    if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
        if (output_data)    *output_data = nullptr;
        if (output_samples) *output_samples = 0;
        return;
    }

    int32_t outN = static_cast<int32_t>(
        std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
    if (outN < 1) outN = 1;

    if (*output_data && *output_samples != outN) {
        delete[] * output_data;
        *output_data = nullptr;
    }
    if (!*output_data) {
        *output_data = new int16_t[outN];
    }
    *output_samples = outN;

    cubic_into_core(input_data, input_samples, *output_data, outN);
}

// ============================ 8-bit  ==============================
// Original Order preserved: (input, output, input_size, output_size).
// Endpoint-aligned, OOB-safe, clamps 0..255.

void linear_interpolation_8(const uint8_t* input,
    uint8_t* output,
    int input_size,
    int output_size)
{
    if (!input || !output || input_size <= 0 || output_size <= 0) return;

    if (input_size == 1) { std::fill(output, output + output_size, input[0]); return; }
    if (output_size == 1) { output[0] = input[0]; return; }

    const double scale = static_cast<double>(input_size - 1)
        / static_cast<double>(output_size - 1);

    for (int i = 0; i < output_size; ++i) {
        const double src_pos = static_cast<double>(i) * scale;
        int idx = static_cast<int>(src_pos);
        double frac = src_pos - static_cast<double>(idx);

        if (idx >= input_size - 1) { idx = input_size - 2; frac = 1.0; }

        const int a = input[idx + 0];
        const int b = input[idx + 1];
        const double y = static_cast<double>(a) + (static_cast<double>(b) - a) * frac;

        int yi = static_cast<int>(std::llround(y));
        yi = std::clamp(yi, 0, 255);
        output[i] = static_cast<uint8_t>(yi);
    }
}

// ============================ Optional postfilter =============================
// Light 5-tap LPF to tame upsampling imaging (in-place).
// Kernel (sum = 12): [-1, 4, 6, 4, -1] / 12

void lowpass_postfilter_16(int16_t* data, int32_t samples)
{
    if (!data || samples <= 4) return;

    int16_t* temp = new int16_t[samples];

    for (int32_t i = 0; i < samples; ++i) {
        const int32_t s0 = data[(i - 2 < 0) ? 0 : i - 2];
        const int32_t s1 = data[(i - 1 < 0) ? 0 : i - 1];
        const int32_t s2 = data[i];
        const int32_t s3 = data[(i + 1 >= samples) ? samples - 1 : i + 1];
        const int32_t s4 = data[(i + 2 >= samples) ? samples - 1 : i + 2];

        const int32_t num = (-s0) + (4 * s1) + (6 * s2) + (4 * s3) - s4;
        const int32_t y = static_cast<int32_t>(
            std::lrint(static_cast<double>(num) / 12.0));
        temp[i] = saturate_i16(y);
    }

    std::copy(temp, temp + samples, data);
    delete[] temp;
}


