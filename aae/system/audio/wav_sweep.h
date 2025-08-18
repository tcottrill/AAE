#pragma once

/* =============================================================================
 *
 * wav_sweep.h
 * Lightweight high-precision sweep/ramp utilities for mixer channels.
 * Runs a dedicated 1 ms timer thread that linearly interpolates values
 * and applies them via the public mixer API (sample_set_volume/pan/freq).
 *
 * Usage:
 *   mixer_ramp_volume(voice, 250, 64);     // 250 ms ramp to volume 64 (0..255)
 *   mixer_sweep_pan(voice, 300, 200);      // 300 ms sweep to pan 200 (0..255)
 *   mixer_sweep_frequency(voice, 500, 880);// 500 ms sweep to 880 Hz
 *
 * Lifetime:
 *   Thread starts on first call automatically and cleans up at process exit.
 *   You can also call wavsweep_shutdown() manually on program shutdown.
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


// -----------------------------------------------------------------------------
// wav_sweep.h
// Lightweight high-precision sweep/ramp utilities for mixer channels.
// Runs a dedicated 1 ms timer thread that linearly interpolates values
// and applies them via the public mixer API (sample_set_volume/pan/freq).
//
// Usage:
//   _mixer_ramp_volume(voice, 250, 64);     // 250 ms ramp to volume 64 (0..255)
//   _mixer_sweep_pan(voice, 300, 200);      // 300 ms sweep to pan 200 (0..255)
//   _mixer_sweep_frequency(voice, 500, 880);// 500 ms sweep to 880 Hz
//
// Lifetime:
//   Thread starts on first call automatically and cleans up at process exit.
//   You can also call wavsweep_shutdown() manually on program shutdown.
// -----------------------------------------------------------------------------

#include <cstdint>

// Forward declarations from mixer.h 

void sample_set_volume(int chanid, int volume);  // 0..255 -> sets linear gain internally
int  sample_get_volume(int chanid);              // returns 0..255

void sample_set_pan(int chanid, int pan);        // 0..255, 128=center
int  sample_get_pan(int chanid);                 // returns 0..255

void sample_set_freq(int chanid, int freq_hz);   // effective voice pitch (Hz)
int  sample_get_freq(int chanid);                // base sample rate (Hz)

// -----------------------------------------------------------------------------
// wavsweep_init
// Ensures the sweep worker thread is running. Usually unnecessary to call
// explicitly because all public sweep APIs auto-init the worker.
// -----------------------------------------------------------------------------
void wavsweep_init();

// -----------------------------------------------------------------------------
// wavsweep_shutdown
// Stops the sweep worker thread and clears all active sweeps. Safe to call
// multiple times, and will also be invoked automatically at process exit.
// -----------------------------------------------------------------------------
void wavsweep_shutdown();

// -----------------------------------------------------------------------------
// _mixer_ramp_volume
// Linearly ramp channel volume to endvol over time_ms milliseconds.
// Parameters:
//   voice   - channel index
//   time_ms - duration in milliseconds (<=0 applies immediately)
//   endvol  - target volume in [0..255]
// -----------------------------------------------------------------------------
void mixer_ramp_volume(int voice, int time_ms, int endvol);

// -----------------------------------------------------------------------------
// _mixer_sweep_frequency
// Linearly sweep channel frequency to endfreq over time_ms milliseconds.
// Parameters:
//   voice    - channel index
//   time_ms  - duration in milliseconds (<=0 applies immediately)
//   endfreq  - target frequency in Hz
//
// Note:
//   Only effective for direct XAudio2 voices. Software-mixed channels ignore
//   frequency changes by design, matching the underlying mixer behavior.
// -----------------------------------------------------------------------------
void mixer_sweep_frequency(int voice, int time_ms, int endfreq);

// -----------------------------------------------------------------------------
// _mixer_sweep_pan
// Linearly sweep channel pan to endpan over time_ms milliseconds.
// Parameters:
//   voice   - channel index
//   time_ms - duration in milliseconds (<=0 applies immediately)
//   endpan  - target pan in [0..255] (128=center)
// -----------------------------------------------------------------------------
void mixer_sweep_pan(int voice, int time_ms, int endpan);

