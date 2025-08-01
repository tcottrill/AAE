//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================


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
};

static std::vector<std::optional<Timer>> timers;

void timer_init()
{
	int x = 0;
	while (Machine->gamedrv->cpu_freq[x] && x < MAX_CPU)
	{
		sec_to_cycles[x] = Machine->gamedrv->cpu_freq[x];
		cycles_to_sec[x] = 1.0 / sec_to_cycles[x];
		if (VERBOSE) {
			LOG_INFO("Init timing for CPU #%d, CPUClock = %d", x, Machine->gamedrv->cpu_freq[x]);
		}
		x++;
	}
	timers.clear();
}

int timer_allocate_slot()
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
	timer.period = Machine->gamedrv->cpu_freq[timer.cpu] * duration;
	timer.count = 0;
	timer.callback = std::move(callback);
	timer.callback_param = data;
	timer.one_shot = (param > 0xff);
	timer.enabled = true;

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

void timer_remove(int id)
{
	if (id >= 0 && id < static_cast<int>(timers.size())) {
		if (VERBOSE) {
			LOG_INFO("Timer %d removed", id);
		}
		timers[id] = std::nullopt;
	}
}

void timer_reset(int id, double)
{
	if (id >= 0 && id < static_cast<int>(timers.size()) && timers[id].has_value()) {
		timers[id]->count = 0;
		if (VERBOSE) {
			LOG_INFO("Timer %d reset", id);
		}
	}
}

int timer_is_timer_enabled(int id)
{
	return (id >= 0 && id < static_cast<int>(timers.size()) && timers[id].has_value() && timers[id]->enabled);
}

void timer_update(int cycles, int cpunum)
{
	for (size_t i = 0; i < timers.size(); ++i) {
		auto& opt_timer = timers[i];

		if (!opt_timer || !opt_timer->enabled || opt_timer->cpu != cpunum)
			continue;

		auto& t = *opt_timer;
		t.count += cycles;

		if (VERBOSE) {
			LOG_INFO("Timer %zu update: count = %f, period = %f", i, t.count, t.period);
		}

		if (t.count >= t.period)
		{
			if (VERBOSE) {
				LOG_INFO("Timer %zu fired on CPU %d", i, t.cpu);
			}

			t.count = 0;
			if (t.callback) t.callback(t.callback_param);

			if (t.one_shot) {
				timer_remove(static_cast<int>(i));
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
