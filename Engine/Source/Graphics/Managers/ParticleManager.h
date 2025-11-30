#pragma once

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

class ParticleManager
{
public:

	static void Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, const shaders::ParticleLayout& rLayout);

	ParticleManager();
	~ParticleManager();

	void RenderGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate);

	bool mbReset = true;

	shaders::ParticlesSpawnLayout mLongParticlesSpawnLayout {};
	shaders::ParticlesSpawnLayout mSquareParticlesSpawnLayout {};
};

inline ParticleManager* gpParticleManager = nullptr;

} // namespace engine
