#pragma once

#include <thread>
#include <atomic>
#include <functional>

// -----------------------------------------------------------------------------
// Description
// AnimTimer provides a simple high-precision background timer loop that calls
// a lambda function at a fixed interval (e.g., 60Hz). Used by SpriteAnimManager.
// -----------------------------------------------------------------------------

class AnimTimer {
public:
	AnimTimer();
	~AnimTimer();

	void Start(double intervalSeconds, std::function<void()> callback);
	void Stop();
	void Pause();
	void Resume();
	bool IsRunning() const;
	double GetInterval() const { return interval; }

private:
	std::thread worker;
	std::atomic<bool> stopFlag{ false };
	std::atomic<bool> paused{ false };
	std::function<void()> func;
	double interval = 0.016; // default 60 FPS
};
