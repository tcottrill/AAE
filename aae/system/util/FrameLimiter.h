// -----------------------------------------------------------------------------
// FrameLimiter.h
// High-precision Windows frame rate limiter (header-only).
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
//   - Uses QueryPerformanceCounter for precise timing.
//   - Uses coarse Sleep + short spin to reduce CPU usage while maintaining accuracy.
//   - Handles oversleep by snapping the schedule forward to prevent drift.
// -----------------------------------------------------------------------------

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace FrameLimiter
{
    static LARGE_INTEGER s_qpcFreq = {};
    static LARGE_INTEGER s_nextTick = {};
    static long long     s_ticksPerFrame = 0;
    static UINT          s_timerPeriodMs = 1;

    static inline double TicksToMs(long long ticks)
    {
        return (1000.0 * (double)ticks) / (double)s_qpcFreq.QuadPart;
    }

    // -------------------------------------------------------------------------
    // Init
    // Initialize for target FPS. Call once before loop.
    // -------------------------------------------------------------------------
    inline void Init(double fps)
    {
        if (fps <= 0.0) fps = 60.0;

        timeBeginPeriod(s_timerPeriodMs); // improve Sleep() granularity

        QueryPerformanceFrequency(&s_qpcFreq);
        s_ticksPerFrame = (long long)((double)s_qpcFreq.QuadPart / fps);

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        s_nextTick.QuadPart = now.QuadPart + s_ticksPerFrame;
    }

    // -------------------------------------------------------------------------
    // Shutdown
    // Restore timer resolution.
    // -------------------------------------------------------------------------
    inline void Shutdown()
    {
        timeEndPeriod(s_timerPeriodMs);
    }

    // -------------------------------------------------------------------------
    // Throttle
    // Wait until next frame boundary, then schedule the next one.
    // -------------------------------------------------------------------------
    inline void Throttle()
    {
        LARGE_INTEGER now;
        for (;;)
        {
            QueryPerformanceCounter(&now);
            long long ticksRemaining = s_nextTick.QuadPart - now.QuadPart;
            if (ticksRemaining <= 0)
                break;

            double msRemaining = TicksToMs(ticksRemaining);
            if (msRemaining > 2.0)
            {
                DWORD sleepMs = (DWORD)(msRemaining - 1.0);
                if (sleepMs > 0)
                    Sleep(sleepMs);
            }
            else
            {
                SwitchToThread();
                for (int i = 0; i < 50; ++i) { YieldProcessor(); }
            }
        }

        s_nextTick.QuadPart += s_ticksPerFrame;

        // Handle oversleep: snap forward to avoid drift.
        QueryPerformanceCounter(&now);
        if (now.QuadPart > s_nextTick.QuadPart)
        {
            long long behind = now.QuadPart - s_nextTick.QuadPart;
            long long framesBehind = (behind / s_ticksPerFrame) + 1;
            s_nextTick.QuadPart += framesBehind * s_ticksPerFrame;
        }
    }
}
