#ifdef BT_CLIENT

#include "ParticleManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/Camera.h"
#include "Profile/ProfileManager.h"
#include "BufferManager.h"
#include "TextureManager.h"

#include "Frame/Render.h"
#include "Ui/WrapperBase.h"

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

int32_t ParticleManager::GetOrAssignTextureIndex(common::crc_t textureCrc)
{
	return static_cast<int32_t>(gpTextureManager->CrcToIndex(textureCrc));
}

void ParticleManager::Spawn(shaders::ParticlesSpawnLayout& rParticlesSpawnLayout, shaders::ParticleLayout layout, common::crc_t textureCrc)
{
	if (rParticlesSpawnLayout.iCount == shaders::kiMaxParticlesSpawn)
	{
		// Too many particles spawn on the same frame, decrease spawn count or increase kiMaxParticlesSpawn
		DEBUG_BREAK();
		return;
	}

	if (layout.f4Position.x < game::gpCamera->f4RenderVisibleArea.x || layout.f4Position.x > game::gpCamera->f4RenderVisibleArea.z || layout.f4Position.y > game::gpCamera->f4RenderVisibleArea.y || layout.f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
	{
		return;
	}

	ASSERT(layout.fIntensity > 0.0f);
	layout.iCookie = gpParticleManager->GetOrAssignTextureIndex(textureCrc);
	rParticlesSpawnLayout.pParticles[rParticlesSpawnLayout.iCount] = layout;
	++rParticlesSpawnLayout.iCount;
}

void ParticleManager::RenderGlobal(int64_t iCommandBuffer, [[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rLongParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mLongParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);
	shaders::ParticlesSpawnLayout& rSquareParticlesSpawnLayout = *reinterpret_cast<shaders::ParticlesSpawnLayout*>(&gpBufferManager->mSquareParticlesSpawnStorageBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fParticlesStretchVelocityStart = 1.0f;
	rGlobalLayout.fParticlesStretchVelocityEnd = 10.0f;
	rGlobalLayout.fParticlesStretchVelocityMultiplier = 2.0f;
	rGlobalLayout.fParticlesWindStrength = gParticlesWindStrength.Get();

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

#endif // BT_CLIENT
