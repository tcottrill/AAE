//==============================================================================
// posix_timer.cpp -- the Linux implementation of sys_timer.h.
//
// clock_gettime(CLOCK_MONOTONIC) is always high-resolution (nanoseconds) and
// is immune to wall-clock adjustments (NTP steps, DST, the user changing the
// system clock), which matters because this drives frame pacing - a backwards
// clock jump would stall or fast-forward the emulator.
//
// There is no fallback path: CLOCK_MONOTONIC is mandatory on Linux, so
// TimerIsHighResolution() is unconditionally true and the mm_timer_* fields of
// AaeTimer - which exist for the Win32 multimedia-timer fallback - stay zero.
//
// Nor is there anything to match Win32's timeBeginPeriod(1)/timeEndPeriod(1)
// pair: Linux has no global timer-resolution setting to raise and restore, so
// TimerShutdown() has nothing to release.
//==============================================================================
#include "sys_timer.h"

#include <ctime>

AaeTimer g_timer;

static bool s_initialized = false;

static int64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

void TimerInit(void)
{
	g_timer.frequency                 = 1000000000LL;   // nanoseconds
	g_timer.resolution                = 1.0f / 1000000000.0f;
	g_timer.performance_timer         = true;
	g_timer.performance_timer_start   = now_ns();
	g_timer.performance_timer_elapsed = g_timer.performance_timer_start;
	g_timer.mm_timer_start            = 0;
	g_timer.mm_timer_elapsed          = 0;

	s_initialized = true;
}

void TimerShutdown(void)
{
	// Nothing to release - see the header comment.
	s_initialized = false;
}

float TimerGetTime(void)
{
	if (!s_initialized)
		TimerInit();

	// Difference first, THEN convert: now_ns() is ~1e18 by itself, far beyond
	// float's 24-bit mantissa, so converting the absolute value would quantise
	// to multiples of ~100 seconds. The subtraction is done in int64 and only
	// the (small) delta becomes floating point.
	const int64_t delta = now_ns() - g_timer.performance_timer_start;
	return (float)((double)delta / 1000000000.0);
}

float TimerGetTimeMS(void)
{
	return TimerGetTime() * 1000.0f;
}

static float lastTime = 0.0f;

float TimerElapsedSinceLastCall(void)
{
	float current = TimerGetTime();
	float delta = current - lastTime;
	lastTime = current;
	return delta;
}

bool TimerIsHighResolution(void)
{
	return true;
}

void TimerReset(void)
{
	if (!s_initialized)
	{
		TimerInit();
		return;
	}

	g_timer.performance_timer_start   = now_ns();
	g_timer.performance_timer_elapsed = g_timer.performance_timer_start;
	lastTime = 0.0f;
}
