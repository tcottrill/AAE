#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "sprite_anim.h"
#include "anim_timer.h"
#include "colordefs.h"
#include "mathutils.h"
// -----------------------------------------------------------------------------
// Description
// SpriteAnimManager manages multiple named SpriteAnim instances (e.g., "walk",
// "jump", "shoot") and handles update timing via an internal AnimTimer.
// Only one animation plays at a time per manager instance.
// -----------------------------------------------------------------------------

class SpriteAnimManager {
public:
	SpriteAnimManager();
	~SpriteAnimManager();

	// Add an animation by name
	void AddAnimation(const std::string& name, std::shared_ptr<SpriteAnim> anim);

	// Play an animation by name
	void Play(const std::string& name, bool reset = true);

	// Stop currently playing animation
	void Stop();

	// Reset currently active animation to first frame
	void Reset();

	// Set playback speed multiplier for current anim (1.0 = normal)
	void SetSpeed(float scale);

	// Returns name of current animation (or "" if none)
	std::string GetCurrentName() const;

	// Returns pointer to currently active animation (or nullptr)
	std::shared_ptr<SpriteAnim> GetCurrent() const;

	// Update current animation (internal)
	void Update();

	// Draw the current animation at a position and offset
	void Draw(float x, float y, rgb_t color = RGB_WHITE, float scale = 1.0f,
		float angle = 0.0f, float z = 1.0f, const Vec2& originOffset = Vec2(0, 0)) const;


	// Timing control
	void InitTimer(double fps = 60.0);  // start the timer (default 60 Hz)
	void Shutdown();                   // stop the timer
	void Pause();                      // pause timer
	void Resume();                     // resume timer
	bool IsRunning() const;            // is timer thread running?

private:
	std::unordered_map<std::string, std::shared_ptr<SpriteAnim>> animations;
	std::string currentName;
	std::shared_ptr<SpriteAnim> current;
	AnimTimer timer;
};
