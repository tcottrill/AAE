/* =============================================================================
 *
 * wav_sweep.cpp
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



#include "wav_sweep.h"
#include "sys_log.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// --------------------------- Internal Types ----------------------------------

namespace {

enum class Param : uint8_t { Volume = 0, Pan = 1, Freq = 2 };

struct Key {
    int voice = -1;
    Param param = Param::Volume;

    bool operator==(const Key& o) const noexcept {
        return voice == o.voice && param == o.param;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        // Simple combine: (voice << 2) ^ param
        return (static_cast<std::size_t>(k.voice) << 2)
             ^ static_cast<std::size_t>(static_cast<uint8_t>(k.param));
    }
};

// Sweep state tracked by the worker thread.
struct Sweep {
    int   voice = -1;
    Param param = Param::Volume;

    int   start = 0;             // start value (vol/pan: 0..255; freq: Hz)
    int   end   = 0;             // end value   (same units)
    int   duration_ms = 0;       // total duration in ms (>=0)
    std::chrono::steady_clock::time_point t0;   // start time
};

constexpr int kTickMs = 1;       // ~1 ms tick for smooth ramps
std::atomic<bool> g_started{false};
std::atomic<bool> g_stop{false};
std::thread g_worker;

std::mutex g_mtx;
std::condition_variable g_cv;

// Active sweeps keyed by (voice,param).
std::unordered_map<Key, Sweep, KeyHash> g_sweeps;

// Best-effort current frequency we’ve applied per voice (helps when the
// base freq returned by sample_get_freq() differs from the current pitch).
std::unordered_map<int, int> g_lastFreqHz;

// Forward decls
void worker_loop();
void ensure_started();
int  clamp_int(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

// Fetch current value per parameter using public API.
// For frequency, prefer the last value we set (if any), else fall back to
// sample_get_freq() which returns the channel’s base sample rate. This matches
// the available public surface without reaching into mixer internals.
int get_current_value(int voice, Param p)
{
    switch (p) {
    case Param::Volume: return clamp_int(sample_get_volume(voice), 0, 255);
    case Param::Pan:    return clamp_int(sample_get_pan(voice),    0, 255);
    case Param::Freq: {
        auto it = g_lastFreqHz.find(voice);
        if (it != g_lastFreqHz.end()) return it->second;
        int base = sample_get_freq(voice);
        return (base > 0) ? base : 0;
    }
    }
    return 0;
}

// Apply a value to the mixer using its public API.
void apply_value(int voice, Param p, int v)
{
    switch (p) {
    case Param::Volume:
        sample_set_volume(voice, clamp_int(v, 0, 255));    // 0..255 -> linear curve inside
        break;
    case Param::Pan:
        sample_set_pan(voice, clamp_int(v, 0, 255));       // 0..255 (128=center)
        break;
    case Param::Freq:
        if (v < 1) v = 1;
        sample_set_freq(voice, v);
        {
            // Track for future ramps
            std::lock_guard<std::mutex> lk(g_mtx);
            g_lastFreqHz[voice] = v;
        }
        break;
    }
}

void ensure_started()
{
    bool expected = false;
    if (g_started.compare_exchange_strong(expected, true)) {
        g_stop = false;
        g_worker = std::thread(worker_loop);

        // Clean up automatically at process exit.
        std::atexit([] {
            try { wavsweep_shutdown(); } catch (...) {}
        });
    }
}

void worker_loop()
{
    LOG_INFO("wav_sweep: worker thread started");

    std::unique_lock<std::mutex> lk(g_mtx);

    while (!g_stop.load(std::memory_order_relaxed)) {
        // Sleep until signaled or tick timeout.
        g_cv.wait_for(lk, std::chrono::milliseconds(kTickMs),
                      [] { return g_stop.load(std::memory_order_relaxed); });
        if (g_stop.load(std::memory_order_relaxed))
            break;

        // Snapshot active sweeps to operate without holding the lock during API calls.
        const auto now = std::chrono::steady_clock::now();
        std::vector<Sweep> ops;
        ops.reserve(g_sweeps.size());

        for (auto it = g_sweeps.begin(); it != g_sweeps.end(); /*increment inside*/) {
            Sweep &s = it->second;

            // Compute progress 0..1
            const int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.t0).count();
            const double  T = (s.duration_ms <= 0) ? 1.0 : std::clamp(elapsed / static_cast<double>(s.duration_ms), 0.0, 1.0);

            // Linear interpolation
            const double cur = s.start + (s.end - s.start) * T;
            const int    ival = static_cast<int>(std::lround(cur));

            // Queue apply for this tick
            ops.push_back({s.voice, s.param, ival, ival, 0, s.t0});

            // Finished?
            if (T >= 1.0) {
                it = g_sweeps.erase(it);
            } else {
                ++it;
            }
        }

        // Release the lock while calling into the mixer (reduces contention).
        lk.unlock();
        for (const auto& op : ops) {
            apply_value(op.voice, op.param, op.start); // (we stored current in 'start')
        }
        lk.lock();
    }

    LOG_INFO("wav_sweep: worker thread exiting");
}

} // anonymous namespace

// --------------------------- Public API --------------------------------------

void wavsweep_init()
{
    ensure_started();
}

void wavsweep_shutdown()
{
    if (!g_started.load()) return;

    g_stop = true;
    g_cv.notify_all();
    if (g_worker.joinable())
        g_worker.join();

    std::lock_guard<std::mutex> lk(g_mtx);
    g_sweeps.clear();
    g_lastFreqHz.clear();
    g_started = false;
}

// Helper to start or replace a sweep for (voice,param).
static void start_sweep(int voice, Param param, int time_ms, int end_value)
{
    ensure_started();

    // Immediate apply if <= 0 ms.
    if (time_ms <= 0) {
        apply_value(voice, param, end_value);
        return;
    }

    // Build sweep
    Sweep s;
    s.voice = voice;
    s.param = param;
    s.duration_ms = time_ms;
    s.end = end_value;
    s.t0 = std::chrono::steady_clock::now();

    // Capture starting value using public getters (or last applied freq).
    s.start = get_current_value(voice, param);

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_sweeps[{voice, param}] = s; // replace any existing sweep for that key
    }

    // Nudge the worker to wake up promptly
    g_cv.notify_one();
}

// -----------------------------------------------------------------------------
// Drops-in for legacy allegro APIs
// -----------------------------------------------------------------------------

void mixer_ramp_volume(int voice, int time_ms, int endvol)
{
    // Clamp to 0..255 to match your sample_set_volume contract.
    endvol = clamp_int(endvol, 0, 255);
    start_sweep(voice, Param::Volume, time_ms, endvol);
}

void mixer_sweep_frequency(int voice, int time_ms, int endfreq)
{
    if (endfreq < 1) endfreq = 1;
    start_sweep(voice, Param::Freq, time_ms, endfreq);
}

void mixer_sweep_pan(int voice, int time_ms, int endpan)
{
    endpan = clamp_int(endpan, 0, 255);
    start_sweep(voice, Param::Pan, time_ms, endpan);
}
