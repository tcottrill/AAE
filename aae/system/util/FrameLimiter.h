// -----------------------------------------------------------------------------
// FrameLimiter.h
// High-precision frame rate limiter (header-only).
//
// Usage:
//   #include "FrameLimiter.h"
//
//   FrameLimiter::Init(60.0); // target FPS
//   while (running) {
//       ... update, render ...
//       FrameLimiter::Throttle();
//   }
//   FrameLimiter::Shutdown();
//
// Notes:
//   - Uses std::chrono::steady_clock for precise timing.
//   - Uses a coarse sleep + short spin to reduce CPU usage while maintaining
//     accuracy, because no OS sleep is dependable at frame granularity.
//   - Handles oversleep by snapping the schedule forward to prevent drift.
//
// Rewritten in Phase 3c from QueryPerformanceCounter/Sleep/timeBeginPeriod to
// <chrono> + <thread>. steady_clock IS QueryPerformanceCounter on MSVC and
// clock_gettime(CLOCK_MONOTONIC) on Linux, so Windows timing is unchanged -
// the same clock, reached portably - and the sleep/spin structure below is
// preserved exactly.
//
// One Windows-only piece remains and genuinely cannot be expressed portably:
// timeBeginPeriod(1), which raises the SYSTEM timer resolution so that a short
// sleep returns in ~1 ms instead of ~15. Linux has no global resolution to
// raise; its nanosleep is already fine-grained.
// -----------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace FrameLimiter
{
    using Clock    = std::chrono::steady_clock;
    using Duration = Clock::duration;

    static Clock::time_point s_nextTick{};
    static Duration          s_frameDuration{};

#ifdef _WIN32
    static UINT s_timerPeriodMs = 1;
#endif

    // -------------------------------------------------------------------------
    // Init
    // Initialize for target FPS. Call once before loop.
    // -------------------------------------------------------------------------
    inline void Init(double fps)
    {
        if (fps <= 0.0) fps = 60.0;

#ifdef _WIN32
        timeBeginPeriod(s_timerPeriodMs); // improve sleep granularity
#endif

        const double nsPerFrame = 1e9 / fps;
        s_frameDuration = std::chrono::duration_cast<Duration>(
            std::chrono::nanoseconds(static_cast<long long>(nsPerFrame)));
        s_nextTick = Clock::now() + s_frameDuration;
    }

    // -------------------------------------------------------------------------
    // Shutdown
    // Restore timer resolution.
    // -------------------------------------------------------------------------
    inline void Shutdown()
    {
#ifdef _WIN32
        timeEndPeriod(s_timerPeriodMs);
#endif
    }

    // -------------------------------------------------------------------------
    // Throttle
    // Wait until next frame boundary, then schedule the next one.
    // -------------------------------------------------------------------------
    inline void Throttle()
    {
        for (;;)
        {
            const Clock::time_point now = Clock::now();
            if (now >= s_nextTick)
                break;

            const double msRemaining =
                std::chrono::duration<double, std::milli>(s_nextTick - now).count();

            if (msRemaining > 2.0)
            {
                // Sleep all but the last millisecond; the yield-spin below
                // covers the rest, where no OS sleep is dependable.
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(msRemaining - 1.0));
            }
            else
            {
                std::this_thread::yield();
            }
        }

        s_nextTick += s_frameDuration;

        // Handle oversleep: snap forward to avoid drift.
        const Clock::time_point now = Clock::now();
        if (now > s_nextTick)
        {
            const auto behind = now - s_nextTick;
            const long long framesBehind = (behind / s_frameDuration) + 1;
            s_nextTick += s_frameDuration * framesBehind;
        }
    }
}
