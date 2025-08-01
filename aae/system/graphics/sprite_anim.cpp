#include "sprite_anim.h"
#include "spritesheet.h"
#include "sys_log.h"

SpriteAnim::SpriteAnim() {}

void SpriteAnim::SetFrames(const std::vector<SpriteAnimFrame>& frameList, bool loop)
{
	frames = frameList;
	looping = loop;
	currentFrame = 0;
	timer = 0.0f;
	playing = false;
	finished = false;
}

void SpriteAnim::SetUniformFrames(const std::vector<int>& spriteIds, float frameTime, bool loop)
{
	frames.clear();
	for (int id : spriteIds)
		frames.push_back({ id, frameTime });

	SetFrames(frames, loop);
}

void SpriteAnim::Play() { playing = true;  finished = false; }
void SpriteAnim::Stop() { playing = false; }
void SpriteAnim::Reset() { currentFrame = 0; timer = 0.0f; playing = true; finished = false; }
void SpriteAnim::SetLoop(bool loop) { looping = loop; }
void SpriteAnim::SetSpeed(float scale) { timeScale = scale; }
void SpriteAnim::SetOriginOffset(const Vec2& offset) { originOffset = offset; }

void SpriteAnim::SetSpriteSheet(Sprite* spriteSystem)
{
	sprite = spriteSystem;
}

void SpriteAnim::Update(float deltaTime)
{
	if (!playing || finished || frames.empty())
		return;

	timer += deltaTime * timeScale;

	while (timer >= frames[currentFrame].duration) {
		timer -= frames[currentFrame].duration;
		currentFrame++;

		if (currentFrame >= frames.size()) {
			if (looping) {
				currentFrame = 0;
			}
			else {
				currentFrame = frames.size() - 1;
				playing = false;
				finished = true;
				break;
			}
		}
	}
}

int SpriteAnim::GetCurrentSpriteId() const
{
	if (frames.empty()) return -1;
	return frames[currentFrame].spriteId;
}

bool SpriteAnim::IsPlaying() const { return playing; }
bool SpriteAnim::IsFinished() const { return finished; }

void SpriteAnim::Draw(float x, float y, rgb_t color, float scale,
	float angle, float z, const Vec2& originOffset) const
{
	if (!sprite) return;

	int id = GetCurrentSpriteId();
	if (id >= 0) {
		sprite->SetOriginOffset(originOffset, id); //  apply per-frame pivot override
		sprite->Add(x, y, id, color, scale, angle, z);
	}
}