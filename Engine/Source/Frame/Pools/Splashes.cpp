#include "Splashes.h"

#include "Graphics/Managers/ParticleManager.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

using enum SplashFlags;

constexpr float kfDuration = 2.0f;
constexpr float kfParticleSpawnInterval = 0.01f;

// Particles
constexpr int32_t kiParticleCookie = 6;
constexpr float kfParticleWidth = 0.1f;
constexpr float kfParticleLength = 0.2f;
constexpr float kfParticleVerticalVelocity = 10.0f;
constexpr float kfParticleIntensityMin = 0.25f;
constexpr float kfParticleIntensityRandom = 3.0f;
constexpr float kfParticleIntensityDecay = 1.4f;
constexpr float kfParticleIntensityPower = 2.5f;

void Splashes::SetupSplash([[maybe_unused]] game::Frame& rFrame, [[maybe_unused]] const SplashInfo& rSplashInfo, Splash& rSplash)
{
	rSplash.fTime = 0.0f;
	rSplash.fNextParticleTime = 0.0f;
}

void Splashes::PostRender(game::Frame& __restrict rFrame, float fDeltaTime)
{
	Splashes& rCurrent = rFrame.splashes;

	for (decltype(rCurrent.uiMaxIndex) i = 0; i <= rCurrent.uiMaxIndex; ++i)
	{
		if (!rCurrent.pbUsed[i])
		{
			continue;
		}

		SplashInfo& rSplashInfo = rCurrent.pObjectInfos[i];
		Splash& rSplash = rCurrent.pObjects[i];

		rSplash.fTime += fDeltaTime;

		if (rSplash.fTime >= kfDuration) [[unlikely]]
		{
			rCurrent.Remove(i);
			continue;
		}

		rSplash.fNextParticleTime -= fDeltaTime;

		if (rSplash.fNextParticleTime <= 0.0f)
		{
			rSplash.fNextParticleTime = kfParticleSpawnInterval;

			uint32_t uiParticleColor = 0x000000FF | ((25 + common::Random(50, rFrame.randomEngine)) << 24) | ((25 + common::Random(50, rFrame.randomEngine)) << 16) | ((200 + common::Random(55, rFrame.randomEngine)) << 8);

			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, rSplashInfo.vecPosition);

			XMFLOAT4A f4Velocity {0.0f, 0.0f, kfParticleVerticalVelocity, 0.0f};

			ParticleManager::Spawn(gpParticleManager->mLongParticlesSpawnLayout,
			{
				.i4Misc = {static_cast<int32_t>(uiParticleColor), kiParticleCookie, static_cast<int32_t>(0.0f), 0},
				.f4MiscOne = {0.0f, 0.0f, kfParticleIntensityDecay, 0.0f},
				.f4MiscTwo = {kfParticleWidth, kfParticleLength, kfParticleIntensityMin + common::Random<kfParticleIntensityRandom>(rFrame.randomEngine), kfParticleIntensityPower},
				.f4MiscThree = {},
				.f4Position = f4Position,
				.f4Velocity = f4Velocity,
			});
		}
	}
}

} // namespace engine
