#include "ParticleManager.h"

#include "Frame/FrameBase.h"
#include "Profile/ProfileManager.h"
#include "BufferManager.h"

#include "Frame/Render.h"

namespace engine
{

ParticleManager::ParticleManager()
{
	gpParticleManager = this;

	SCOPED_BOOT_TIMER(kBootTimerParticleManager);
}

ParticleManager::~ParticleManager()
{
	gpParticleManager = nullptr;
}

void ParticleManager::Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, const shaders::ParticleLayout& rLayout)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	if (rParticlesSpawnLayout.i4Misc.x == shaders::kiMaxParticlesSpawn)
	{
		// Too many particles spawn on the same frame, decrease spawn count or increase kiMaxParticlesSpawn
		DEBUG_BREAK();
		return;
	}

	if (rLayout.f4Position.x < gf4RenderVisibleArea.x || rLayout.f4Position.x > gf4RenderVisibleArea.z || rLayout.f4Position.y > gf4RenderVisibleArea.y || rLayout.f4Position.y < gf4RenderVisibleArea.w)
	{
		return;
	}

	ASSERT(rLayout.f4MiscTwo.z > 0.0f);
	rParticlesSpawnLayout.pParticles[rParticlesSpawnLayout.i4Misc.x] = rLayout;
	++rParticlesSpawnLayout.i4Misc.x;
}

void ParticleManager::RenderGlobal(int64_t iCommandBuffer, [[maybe_unused]] const game::Frame& __restrict rFrame)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rLongParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mLongParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rSquareParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mSquareParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4ParticlesOne.x = 1.0f;
	rGlobalLayout.f4ParticlesOne.y = 10.0f;
	rGlobalLayout.f4ParticlesOne.z = 2.0f;

	// Spawn
	rLongParticlesSpawnLayout.i4Misc = mLongParticlesSpawnLayout.i4Misc;
	memcpy(&rLongParticlesSpawnLayout.pParticles[0], &mLongParticlesSpawnLayout.pParticles[0], rLongParticlesSpawnLayout.i4Misc.x * sizeof(shaders::ParticleLayout));
	mLongParticlesSpawnLayout.i4Misc.x = 0;

	rSquareParticlesSpawnLayout.i4Misc = mSquareParticlesSpawnLayout.i4Misc;
	memcpy(&rSquareParticlesSpawnLayout.pParticles[0], &mSquareParticlesSpawnLayout.pParticles[0], rSquareParticlesSpawnLayout.i4Misc.x * sizeof(shaders::ParticleLayout));
	mSquareParticlesSpawnLayout.i4Misc.x = 0;

	// Reset?
	rLongParticlesSpawnLayout.i4Misc.y = mbReset ? 1 : 0;
	rSquareParticlesSpawnLayout.i4Misc.y = mbReset ? 1 : 0;
	mbReset = false;
}

} // namespace engine
