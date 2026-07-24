#if defined(BT_CLIENT)

#include "ParticleManager.h"

namespace engine
{

ParticleManager::ParticleManager()
{
	ASSERT(gpParticleManager == nullptr);

	gpParticleManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerParticleManager);
}

ParticleManager::~ParticleManager()
{
	if (gpParticleManager == this)
	{
		gpParticleManager = nullptr;
	}
}

int32_t ParticleManager::GetOrAssignTextureIndex(common::crc_t textureCrc)
{
	return static_cast<int32_t>(gpTextureManager->mTextureDescriptors.CrcToIndex(textureCrc));
}

void ParticleManager::Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, shaders::ParticleLayout layout, common::crc_t textureCrc)
{
	// Cull before taking the lock: both checks read only the by-value layout copy and the immutable camera rect,
	// so culled spawns no longer serialize workers on mSpawnMutex during parallel tick dispatch.
	if (layout.f4Position.x < game::gpCamera->f4RenderVisibleArea.x || layout.f4Position.x > game::gpCamera->f4RenderVisibleArea.z || layout.f4Position.y > game::gpCamera->f4RenderVisibleArea.y || layout.f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
	{
		return;
	}

	if (layout.fVisibleIntensity <= 0.0f)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(gpParticleManager->mSpawnMutex);

	if (rParticlesSpawnLayout.iCount == shaders::kiMaxParticlesSpawn)
	{
		// Too many particles spawn on the same frame, decrease spawn count or increase kiMaxParticlesSpawn
		DEBUG_BREAK();
		return;
	}

	layout.iCookie = gpParticleManager->GetOrAssignTextureIndex(textureCrc);
	rParticlesSpawnLayout.pParticles[rParticlesSpawnLayout.iCount] = layout;
	++rParticlesSpawnLayout.iCount;
}

void ParticleManager::RenderGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rLongParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mLongParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rSquareParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mSquareParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	const float fStretchVelocityStart = 1.0f;
	const float fStretchVelocityEnd = 10.0f;
	rGlobalLayout.fParticlesStretchVelocityStart = fStretchVelocityStart;
	rGlobalLayout.fParticlesStretchVelocityMultiplier = 2.0f;
	// Stretch-range reciprocal folded CPU-side (was LongParticlesRender.vert's per-vertex max()+divide); invocation-invariant.
	rGlobalLayout.fParticlesStretchRangeInv = 1.0f / std::max(fStretchVelocityEnd - fStretchVelocityStart, shaders::kfEpsilon);

	// Spawn
	rLongParticlesSpawnLayout.iCount = mLongParticlesSpawnLayout.iCount;
	std::memcpy(&rLongParticlesSpawnLayout.pParticles[0], &mLongParticlesSpawnLayout.pParticles[0], rLongParticlesSpawnLayout.iCount * sizeof(shaders::ParticleLayout));
	mLongParticlesSpawnLayout.iCount = 0;

	rSquareParticlesSpawnLayout.iCount = mSquareParticlesSpawnLayout.iCount;
	std::memcpy(&rSquareParticlesSpawnLayout.pParticles[0], &mSquareParticlesSpawnLayout.pParticles[0], rSquareParticlesSpawnLayout.iCount * sizeof(shaders::ParticleLayout));
	mSquareParticlesSpawnLayout.iCount = 0;

	// Reset?
	rLongParticlesSpawnLayout.iReset = mbReset ? 1 : 0;
	rSquareParticlesSpawnLayout.iReset = mbReset ? 1 : 0;
	mbReset = false;
}

} // namespace engine

#endif // BT_CLIENT
