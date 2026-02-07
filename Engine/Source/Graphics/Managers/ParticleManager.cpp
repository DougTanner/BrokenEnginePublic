#include "ParticleManager.h"

#include "Frame/FrameBase.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"
#include "BufferManager.h"

#include "Frame/Render.h"

namespace engine
{

ParticleManager::ParticleManager()
{
	gpParticleManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerParticleManager);
}

ParticleManager::~ParticleManager()
{
	gpParticleManager = nullptr;
}

void ParticleManager::Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, const shaders::ParticleLayout& rLayout)
{
	if (rParticlesSpawnLayout.iCount == shaders::kiMaxParticlesSpawn)
	{
		// Too many particles spawn on the same frame, decrease spawn count or increase kiMaxParticlesSpawn
		common::DebugBreak();
		return;
	}

	if (rLayout.f4Position.x < game::gpCamera->f4RenderVisibleArea.x || rLayout.f4Position.x > game::gpCamera->f4RenderVisibleArea.z || rLayout.f4Position.y > game::gpCamera->f4RenderVisibleArea.y || rLayout.f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
	{
		return;
	}

	ASSERT(rLayout.fIntensity > 0.0f);
	rParticlesSpawnLayout.pParticles[rParticlesSpawnLayout.iCount] = rLayout;
	++rParticlesSpawnLayout.iCount;
}

void ParticleManager::RenderGlobal(int64_t iCommandBuffer, [[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rLongParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mLongParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rSquareParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mSquareParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4ParticlesOne.x = 1.0f;
	rGlobalLayout.f4ParticlesOne.y = 10.0f;
	rGlobalLayout.f4ParticlesOne.z = 2.0f;

	// Spawn
	rLongParticlesSpawnLayout.iCount = mLongParticlesSpawnLayout.iCount;
	memcpy(&rLongParticlesSpawnLayout.pParticles[0], &mLongParticlesSpawnLayout.pParticles[0], rLongParticlesSpawnLayout.iCount * sizeof(shaders::ParticleLayout));
	mLongParticlesSpawnLayout.iCount = 0;

	rSquareParticlesSpawnLayout.iCount = mSquareParticlesSpawnLayout.iCount;
	memcpy(&rSquareParticlesSpawnLayout.pParticles[0], &mSquareParticlesSpawnLayout.pParticles[0], rSquareParticlesSpawnLayout.iCount * sizeof(shaders::ParticleLayout));
	mSquareParticlesSpawnLayout.iCount = 0;

	// Reset?
	rLongParticlesSpawnLayout.iReset = mbReset ? 1 : 0;
	rSquareParticlesSpawnLayout.iReset = mbReset ? 1 : 0;
	mbReset = false;
}

} // namespace engine
