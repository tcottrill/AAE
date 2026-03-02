// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E. Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and modernized for integration
// with the Game Engine Alpha project.
//
// Integration:
//   This library is part of the A.A.E emulator project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Licensing Notice:
//   - Original portions of this code remain (C) the M.A.M.E. Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are (C) 2025/2026 Tim Cottrill and
//     released under the GNU General Public License v3 (GPLv3) or later.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
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
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E. Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

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
	size_t count = timers.size();  // snapshot size — only process existing timers
	for (size_t i = 0; i < count; ++i) {
		if (!timers[i] || !timers[i]->enabled || timers[i]->cpu != cpunum)
			continue;

		timers[i]->count += cycles;

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


