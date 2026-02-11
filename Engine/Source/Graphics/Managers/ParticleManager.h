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

	static void Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, shaders::ParticleLayout layout, common::crc_t textureCrc);

	ParticleManager();
	~ParticleManager();

	void RenderGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate);

	int32_t GetOrAssignTextureIndex(common::crc_t textureCrc);

	bool mbReset = true;

	shaders::ParticlesSpawnLayout mLongParticlesSpawnLayout {};
	shaders::ParticlesSpawnLayout mSquareParticlesSpawnLayout {};

private:

	common::crc_t mParticleTextureCrcs[shaders::kiParticlesCookieCount] {};
	int32_t miParticleTextureCount = 0;
};

inline ParticleManager* gpParticleManager = nullptr;

} // namespace engine
