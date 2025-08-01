#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "colordefs.h"
#include "mathutils.h"

class Sprite;

struct SpriteAnimFrame {
	int spriteId = -1;
	float duration = 0.1f; // Duration in seconds
};

class SpriteAnim {
public:
	SpriteAnim();

	// Animation setup
	void SetFrames(const std::vector<SpriteAnimFrame>& frames, bool loop = true);
	void SetUniformFrames(const std::vector<int>& spriteIds, float frameTime, bool loop = true);

	// Playback controls
	void Play();
	void Stop();
	void Reset();
	void SetLoop(bool loop);
	void SetSpeed(float scale);

	void SetOriginOffset(const Vec2& offset);

	// Timing update (deltaTime in seconds)
	void Update(float deltaTime);

	// Query
	int GetCurrentSpriteId() const;
	bool IsPlaying() const;
	bool IsFinished() const;

	// Sheet assignment
	void SetSpriteSheet(Sprite* spriteSystem);

	// Rendering
	void Draw(float x, float y, rgb_t color = RGB_WHITE,
		float scale = 1.0f, float angle = 0.0f, float z = 1.0f,
		const Vec2& originOffset = Vec2(0.0f, 0.0f)) const;

private:
	Sprite* sprite = nullptr;
	std::vector<SpriteAnimFrame> frames;
	Vec2 originOffset = Vec2(0, 0);
	
	size_t currentFrame = 0;
	float timer = 0.0f;
	float timeScale = 1.0f;

	bool playing = false;
	bool looping = true;
	bool finished = false;
};
