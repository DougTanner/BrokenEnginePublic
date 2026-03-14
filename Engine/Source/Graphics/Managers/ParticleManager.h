#pragma once

namespace engine
{

class ParticleManager
{
public:

	static void Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, shaders::ParticleLayout layout, common::crc_t textureCrc);

	ParticleManager();
	~ParticleManager();

	void RenderGlobal(int64_t iCommandBuffer);

	int32_t GetOrAssignTextureIndex(common::crc_t textureCrc);

	bool mbReset = true;

	shaders::ParticlesSpawnLayout mLongParticlesSpawnLayout {};
	shaders::ParticlesSpawnLayout mSquareParticlesSpawnLayout {};

};

inline ParticleManager* gpParticleManager = nullptr;

} // namespace engine
