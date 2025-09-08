
// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------


#pragma once
#include <cstddef>
#include <cstdint>

// Compute OKI M6295 output rate from chip clock and pin7 state.
// Divisor is 132 when pin7 is high, 165 when low.
inline int oki6295_output_rate(int oki_clock_hz, bool pin7_high)
{
    const int divisor = pin7_high ? 132 : 165;
    return (oki_clock_hz > 0) ? (oki_clock_hz / divisor) : 8000;
}

// Decode one OKI ADPCM block into 16-bit PCM (single-channel).
// Implemented in okim6295_loader.cpp (MSM6295 ADPCM).
void oki_msm6295_decode(const uint8_t* src, size_t src_bytes,
                        std::vector<int16_t>& out_pcm);

// Loads and registers all samples from a preloaded OKI ROM region.
// Returns the number of samples successfully registered.
int load_okim6295_from_region(
    const unsigned char* rom_base, size_t rom_size,
    int out_rate,               // mixer/system rate, e.g. 44100
    int oki_clock_hz,           // e.g. OKI_CLOCK (1,056,000)
    bool pin7_high,             // true => divisor 132; false => 165
    uint32_t max_entries_to_scan = 4096
);