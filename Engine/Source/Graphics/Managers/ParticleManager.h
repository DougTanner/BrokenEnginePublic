#pragma once

namespace game
{

struct Frame;

}

namespace engine
{

class ParticleManager
{
public:

	static void Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, const shaders::ParticleLayout& rLayout);

	ParticleManager();
	~ParticleManager();

	void RenderGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);

	bool mbReset = true;

	shaders::ParticlesSpawnLayout mLongParticlesSpawnLayout {};
	shaders::ParticlesSpawnLayout mSquareParticlesSpawnLayout {};
};

inline ParticleManager* gpParticleManager = nullptr;

} // namespace engine
