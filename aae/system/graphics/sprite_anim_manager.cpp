#include "sprite_anim_manager.h"
#include "sys_log.h"


SpriteAnimManager::SpriteAnimManager()
{
}

SpriteAnimManager::~SpriteAnimManager()
{
	Shutdown();
}

void SpriteAnimManager::AddAnimation(const std::string& name, std::shared_ptr<SpriteAnim> anim)
{
	if (!anim)
		return;

	animations[name] = anim;
}

void SpriteAnimManager::Play(const std::string& name, bool reset)
{
	auto it = animations.find(name);
	if (it == animations.end())
	{
		LOG_ERROR("SpriteAnimManager::Play - animation '%s' not found", name.c_str());
		return;
	}

	if (current && current != it->second)
		current->Stop();

	currentName = name;
	current = it->second;

	if (reset)
		current->Reset();

	current->Play();
}

void SpriteAnimManager::Stop()
{
	if (current)
		current->Stop();
}

void SpriteAnimManager::Reset()
{
	if (current)
		current->Reset();
}

void SpriteAnimManager::SetSpeed(float scale)
{
	if (current)
		current->SetSpeed(scale);
}

std::string SpriteAnimManager::GetCurrentName() const
{
	return currentName;
}

std::shared_ptr<SpriteAnim> SpriteAnimManager::GetCurrent() const
{
	return current;
}

void SpriteAnimManager::Update()
{
	if (current)
		current->Update(timer.GetInterval());
}

void SpriteAnimManager::Draw(float x, float y, rgb_t color, float scale,
	float angle, float z, const Vec2& originOffset) const
{
	if (!current)
		return;

	// Set temporary origin offset if needed
	if (originOffset.x != 0.0f || originOffset.y != 0.0f)
		current->SetOriginOffset(originOffset);

	current->Draw(x, y, color, scale, angle, z);
}
void SpriteAnimManager::InitTimer(double fps)
{
	double interval = 1.0 / fps;

	timer.Start(interval, [this]() {
		this->Update();
		});
}

void SpriteAnimManager::Shutdown()
{
	timer.Stop();
	current.reset();
	currentName.clear();
	animations.clear();
}

void SpriteAnimManager::Pause()
{
	timer.Pause();
}

void SpriteAnimManager::Resume()
{
	timer.Resume();
}

bool SpriteAnimManager::IsRunning() const
{
	return timer.IsRunning();
}
