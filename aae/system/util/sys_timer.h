//==============================================================================
// sys_timer.h -- the platform-neutral wall-clock timer contract.
//
// Replaces wintimer.h, which could not survive on Linux for a reason that has
// nothing to do with the Win32 API it used: it did
//
//     typedef struct timer_s { ... } timer_t;
//
// while including <time.h>. POSIX <time.h> already declares timer_t as the
// timer_create() handle type, so that is a hard redefinition ERROR on Linux,
// not a warning. The type had to be renamed before any POSIX implementation
// was possible - hence AaeTimer.
//
// Implementations:
//   wintimer.cpp             QueryPerformanceCounter, with a timeGetTime
//                            fallback and timeBeginPeriod(1)
//   linux/posix_timer.cpp    clock_gettime(CLOCK_MONOTONIC)
//
// NOTE: unrelated to aae/aae/cpu_code/timer.cpp, which is the EMULATED
// machine's timer system (timer_set/timer_reset/timer_remove). This one is
// host wall-clock time, used for frame pacing and profiling.
//==============================================================================
#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include <cstdint>

struct AaeTimer
{
	int64_t  frequency;                 // Ticks per second
	float    resolution;                // Seconds per tick
	uint32_t mm_timer_start;            // Win32 multimedia-timer fallback only
	uint32_t mm_timer_elapsed;          // Win32 multimedia-timer fallback only
	bool     performance_timer;         // True when a high-resolution source is in use
	int64_t  performance_timer_start;
	int64_t  performance_timer_elapsed;
};

extern AaeTimer g_timer;

// -----------------------------------------------------------------------------
// Initialization and shutdown
// -----------------------------------------------------------------------------
void TimerInit(void);       // Call once to initialize the timer
void TimerShutdown(void);   // Call on program exit to clean up

// -----------------------------------------------------------------------------
// Time query
// -----------------------------------------------------------------------------
float TimerGetTime(void);   // Returns time in seconds since TimerInit
float TimerGetTimeMS(void); // Returns time in milliseconds
float TimerElapsedSinceLastCall(void);
bool  TimerIsHighResolution(void);
void  TimerReset(void);

#endif // SYS_TIMER_H
