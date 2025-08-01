#include "anim_timer.h"
#include "wintimer.h"
#include <chrono>
#include <thread>

AnimTimer::AnimTimer()
{
	TimerInit(); // Ensure the high-resolution timer is initialized
}

AnimTimer::~AnimTimer()
{
	Stop(); // Clean shutdown on destruction
}

void AnimTimer::Start(double intervalSeconds, std::function<void()> callback)
{
	Stop(); // Stop existing thread if running

	stopFlag = false;
	paused = false;
	func = std::move(callback);
	interval = intervalSeconds;

	worker = std::thread([this]() {
		double lastTime = TimerGetTime();

		while (!stopFlag)
		{
			if (paused)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				lastTime = TimerGetTime();
				continue;
			}

			double now = TimerGetTime();

			if ((now - lastTime) >= interval)
			{
				lastTime += interval;

				// Guard against excessive catch-up lag
				if ((now - lastTime) > 1.0)
					lastTime = now;

				if (func)
					func();
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
		});
}

void AnimTimer::Stop()
{
	stopFlag = true;
	if (worker.joinable())
		worker.join();
}

void AnimTimer::Pause()
{
	paused = true;
}

void AnimTimer::Resume()
{
	paused = false;
}

bool AnimTimer::IsRunning() const
{
	return !stopFlag && worker.joinable();
}
