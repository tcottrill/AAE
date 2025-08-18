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
// mixer_volume.h
#pragma once
#include <algorithm>
#include <cmath>

// Convert legacy 0..255 values to 0..100 percent (rounded).
static inline int Volume255ToPercent(int v255)
{
    v255 = std::clamp(v255, 0, 255);
    // +127 for rounding when dividing by 255
    return (v255 * 100 + 127) / 255;
}

// Game-friendly curve: 0..100% slider -> linear gain for XAudio2.
// - Very quiet near the bottom (fast drop to ~-80 dB).
// - Smooth, perceptual mid (50% ? -6 dB).
// - 100% = 0 dB, 0% ? -80 dB.
//
// Implementation:
//   0..5%   -> quadratic ramp in dB from -80 dB up to -12 dB
//   5..100% -> linear in dB from -12 dB up to 0 dB
static inline float VolumePercentToLinear(int percent)
{
    percent = std::clamp(percent, 0, 100);
    const float x = percent / 100.0f;
    float dB;

    if (x <= 0.05f) {
        // 0..0.05 -> -80..-12 dB (quadratic makes the very-low range feel controllable)
        const float y = x / 0.05f;           // 0..1
        dB = -80.0f + 68.0f * (y * y);       // hits -12 dB at 5%
    }
    else {
        // 0.05..1.0 -> -12..0 dB (linear in dB gives ~ -6 dB around the mid)
        const float y = (x - 0.05f) / 0.95f; // 0..1
        dB = -12.0f + 12.0f * y;             // -12 dB at 5%, 0 dB at 100%
    }

    // Convert dB -> linear amplitude for XAudio2
    return std::pow(10.0f, dB / 20.0f);
}
