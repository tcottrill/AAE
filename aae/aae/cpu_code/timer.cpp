
// Notes:
// The timer pulse in this code doesn't work like mame, it's just a resettable one-shot timer
// The main timer code is a repeatable timer, it's confusing and just another thing I need to fix

#include "aae_mame_driver.h"
#include "timer.h"
#include "sys_log.h"
#include <vector>
#include <optional>
#include <algorithm>

#define VERBOSE 0

double cycles_to_sec[MAX_CPU + 1]{};
double sec_to_cycles[MAX_CPU + 1]{};

struct Timer {
	std::function<void(int)> callback;
	int callback_param = 0;
	bool one_shot = false;
	double period = 0;
	double count = 0;
	int cpu = 0;
	bool enabled = false;
	// After the first firing, switch to this repeat period (in cycles).
	// 0.0 means one-shot or no special repeat handling.
	double repeat = 0.0;
	// If true, timer_clear_all_eof() will NOT zero this timer's count.
	// Used for rtimer-style elapsed-time trackers (e.g. POKEY RNG) that must
	// accumulate across frame boundaries and are only reset by explicit
	// timer_reset() calls.
	bool elapsed_only = false;
};

static std::vector<std::optional<Timer>> timers;

void timer_init()
{
	int x = 0;
	while (x < MAX_CPU && Machine->gamedrv->cpu[x].cpu_freq)
	{
		sec_to_cycles[x] = Machine->gamedrv->cpu[x].cpu_freq;
		cycles_to_sec[x] = 1.0 / sec_to_cycles[x];
		if (VERBOSE) {
			LOG_INFO("Init timing for CPU #%d, CPUClock = %d", x, Machine->gamedrv->cpu[x].cpu_freq);
		}
		x++;
	}
	timers.clear();
	timers.reserve(16);
}

static int timer_allocate_slot()
{
	for (size_t i = 0; i < timers.size(); ++i) {
		if (!timers[i].has_value())
			return static_cast<int>(i);
	}
	timers.emplace_back();
	if (VERBOSE) {
		LOG_INFO("New timer slot allocated: %zu", timers.size() - 1);
	}
	return static_cast<int>(timers.size() - 1);
}

int timer_set(double duration, int param, int data, std::function<void(int)> callback)
{
	if (!callback) return -1;

	int index = timer_allocate_slot();
	auto& timer = timers[index].emplace();

	timer.cpu = param & 0x0f;
	timer.period = Machine->gamedrv->cpu[timer.cpu].cpu_freq * duration;
	timer.count = 0;
	timer.callback = std::move(callback);
	timer.callback_param = data;
	timer.one_shot = ((param & ONE_SHOT) != 0);  //(param > 0xff);
	timer.enabled = true;
	timer.repeat = 0.0;

	if (VERBOSE) {
		LOG_INFO("Timer %d set: CPU %d, period %f, one_shot %d", index, timer.cpu, timer.period, timer.one_shot);
	}

	return index;
}

int timer_set(double duration, int param, std::function<void(int)> callback)
{
	return timer_set(duration, param, 0, std::move(callback));
}

int timer_pulse(double duration, int param, std::function<void(int)> callback)
{
	return timer_set(duration, ONE_SHOT + param, 0, std::move(callback));
}

int timer_pulse(double duration, int param, int data, std::function<void(int)> callback)
{
	return timer_set(duration, ONE_SHOT + param, data, std::move(callback));
}

/* timer_set_elapsed - create an elapsed-time tracker timer.
 * Like timer_set(TIME_NEVER,...) but marked elapsed_only=true so that
 * timer_clear_all_eof() will never zero its count. The timer never fires
 * (period = TIME_NEVER). Use timer_timeelapsed() to read accumulated time
 * and timer_reset(id, TIME_NEVER) to restart the interval from zero.
 * cpu_index selects which CPU's frequency is used for cycle conversion. */
int timer_set_elapsed(int cpu_index)
{
	int index = timer_allocate_slot();
	auto& timer = timers[index].emplace();
	timer.cpu          = cpu_index & 0x0f;
	timer.period       = Machine->gamedrv->cpu[timer.cpu].cpu_freq * TIME_NEVER;
	timer.count        = 0;
	timer.callback     = nullptr;
	timer.callback_param = 0;
	timer.one_shot     = false;
	timer.enabled      = true;
	timer.repeat       = 0.0;
	timer.elapsed_only = true;
	if (VERBOSE) {
		LOG_INFO("Elapsed timer %d created for CPU %d", index, timer.cpu);
	}
	return index;
}

void timer_remove(int id)
{
	if (id >= 0 && id < static_cast<int>(timers.size())) {
		if (VERBOSE) {
			LOG_INFO("Timer %d removed", id);
		}
		timers[id] = std::nullopt;
	}
}

void timer_reset(int id, double duration)
{
	if (id >= 0 && id < static_cast<int>(timers.size()) && timers[id].has_value()) {
		timers[id]->count = 0;
		if (duration > 0.0) {
			timers[id]->period = Machine->gamedrv->cpu[timers[id]->cpu].cpu_freq * duration;
		}
		if (VERBOSE) {
			LOG_INFO("Timer %d reset, period = %f", id, timers[id]->period);
		}
	}
}

int timer_is_timer_enabled(int id)
{
	return (id >= 0 && id < static_cast<int>(timers.size()) && timers[id].has_value() && timers[id]->enabled);
}


void timer_update(int cycles, int cpunum)
{
	size_t count = timers.size();  // snapshot size   only process existing timers
	for (size_t i = 0; i < count; ++i) {
		if (!timers[i] || !timers[i]->enabled || timers[i]->cpu != cpunum)
			continue;

		timers[i]->count += cycles;

		/* elapsed_only timers just accumulate cycles and never fire */
		if (timers[i]->elapsed_only)
			continue;

		while (timers[i] && timers[i]->count >= timers[i]->period)
		{
			timers[i]->count -= timers[i]->period;

			auto cb = timers[i]->callback;       // copy before mutation
			int param = timers[i]->callback_param;
			bool is_oneshot = timers[i]->one_shot;
			double repeat = timers[i]->repeat;

			if (cb) cb(param);

			// After callback, the slot may have been removed or replaced
			if (!timers[i]) break;

			if (is_oneshot) {
				timer_remove(static_cast<int>(i));
				break;
			}
			else if (repeat > 0.0) {
				timers[i]->period = repeat;
			}
		}
	}
}

void timer_cpu_reset(int cpunum)
{
	for (size_t i = 0; i < timers.size(); ++i) {
		auto& opt_timer = timers[i];
		if (opt_timer && opt_timer->cpu == cpunum) {
			opt_timer->count = 0;
			if (VERBOSE) {
				LOG_INFO("Timer %zu reset for CPU %d", i, cpunum);
			}
		}
	}
}

void timer_clear_all_eof()
{
	for (size_t i = 0; i < timers.size(); ++i) {
		if (timers[i]) {
			/* Skip elapsed-only timers (e.g. POKEY rtimer) - they must
			 * accumulate across frame boundaries and are only reset by
			 * explicit timer_reset() calls from the owning subsystem. */
			if (timers[i]->elapsed_only)
				continue;
			timers[i]->count = 0;
			if (VERBOSE) {
				LOG_INFO("Timer %zu count cleared (EOF)", i);
			}
		}
	}
}

void timer_clear_end_of_game()
{
	for (size_t i = 0; i < timers.size(); ++i) {
		if (timers[i]) {
			timers[i] = std::nullopt;
			if (VERBOSE) {
				LOG_INFO("Timer %zu cleared (end of game)", i);
			}
		}
	}
}

double timer_timeleft(int id)
{
	if (id >= 0 && id < static_cast<int>(timers.size()) && timers[id] && timers[id]->enabled) {
		// Find remaining cycles
		double cycles_left = timers[id]->period - timers[id]->count;
		if (cycles_left < 0) cycles_left = 0;

		// Convert to seconds
		return cycles_left / Machine->gamedrv->cpu[timers[id]->cpu].cpu_freq;
	}
	return 0.0;
}
double timer_timeelapsed(int id)
{
	if (id >= 0 && id < static_cast<int>(timers.size()) && timers[id] && timers[id]->enabled) {
		// Calculate the amount of time that has passed since the timer last fired or was reset.
		// timers[id]->count tracks the elapsed cycles.
		return timers[id]->count / Machine->gamedrv->cpu[timers[id]->cpu].cpu_freq;
	}
	return 0.0;
}

int timer_alloc(std::function<void(int)> callback)
{
	int index = timer_allocate_slot();
	auto& timer = timers[index].emplace();

	timer.cpu = 0;       // default CPU 0; timer_adjust will set real timing
	timer.period = 0;
	timer.count = 0;
	timer.callback = std::move(callback);
	timer.callback_param = 0;
	timer.one_shot = true;    // dormant until timer_adjust is called
	timer.enabled = false;   // not armed yet
	timer.repeat = 0.0;
	timer.elapsed_only = false;

	if (VERBOSE) {
		LOG_INFO("Timer %d allocated (dormant)", index);
	}
	return index;
}

void timer_adjust(int timer_id, double duration, int param, double period)
{
	if (timer_id < 0 || timer_id >= static_cast<int>(timers.size())
		|| !timers[timer_id].has_value())
		return;

	auto& t = *timers[timer_id];

	// Use CPU 0's frequency for cycle conversion, the duration is already in seconds via TIME_IN_HZ)
	double freq = Machine->gamedrv->cpu[t.cpu].cpu_freq;

	if (duration <= 0.0) {
		// TIME_NOW: fire the callback immediately
		if (t.callback) {
			t.callback(param);
		}
		// If there's a repeat period, arm the timer for that period
		if (period > 0.0) {
			t.period = freq * period;
			t.count = 0;
			t.callback_param = param;
			t.one_shot = false;
			t.enabled = true;
		}
		else {
			t.enabled = false;
		}
	}
	else {
		// Arm the timer to fire after 'duration' seconds
		t.period = freq * duration;
		t.count = 0;
		t.callback_param = param;
		t.enabled = true;

		if (period > 0.0) {
			// After the first firing, repeat at this interval
			t.repeat = freq * period;
			t.one_shot = false;
		}
		else {
			t.repeat = 0.0;
			t.one_shot = true;
		}
	}

	if (VERBOSE) {
		LOG_INFO("Timer %d adjusted: duration=%f, param=%d, period=%f, enabled=%d",
			timer_id, duration, param, period, t.enabled);
	}
}
