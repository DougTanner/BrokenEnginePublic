#include "Blasters.h"

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Islands.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"
#include "Ui/Wrapper.h"

#include "Frame/Frame.h"
#include "Input/Input.h"
#include "Game.h"

using namespace DirectX;

namespace game
{

using enum BlasterFlags;

// Terrain craters
constexpr float kfTerrainCraterIntensityImpact = 1000.0f;
constexpr float kfTerrainCraterIntensityGlow = 300.0f;

bool Blasters::operator==(const Blasters& rOther) const
{
	bool bEqual = true;

	bEqual &= *static_cast<const Spawnable*>(this) == *static_cast<const Spawnable*>(&rOther);

	bEqual &= iCount == rOther.iCount;

	common::BreakOnNotEqual(bEqual);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= pFlags[i] == rOther.pFlags[i];
		bEqual &= pfTimes[i] == rOther.pfTimes[i];
		bEqual &= pVecPositions[i] == rOther.pVecPositions[i];
		bEqual &= pVecVelocities[i] == rOther.pVecVelocities[i];
		bEqual &= puiAreaLights[i] == rOther.puiAreaLights[i];
		bEqual &= pCrcs[i] == rOther.pCrcs[i];
		bEqual &= pf2Sizes[i] == rOther.pf2Sizes[i];
		bEqual &= pfFreezeTimes[i] == rOther.pfFreezeTimes[i];
		bEqual &= pfVisibleIntensities[i] == rOther.pfVisibleIntensities[i];
		bEqual &= pfLightAreas[i] == rOther.pfLightAreas[i];
		bEqual &= pfLightIntensities[i] == rOther.pfLightIntensities[i];

		bEqual &= pfSlowTimes[i] == rOther.pfSlowTimes[i];
		bEqual &= pfDamages[i] == rOther.pfDamages[i];
		bEqual &= pfPitches[i] == rOther.pfPitches[i];
		bEqual &= puiSounds[i] == rOther.puiSounds[i];
		bEqual &= pf4Decays[i] == rOther.pf4Decays[i];

		common::BreakOnNotEqual(bEqual);
	}

	return bEqual;
}

void Blasters::Copy(int64_t iDestIndex, int64_t iSrcIndex)
{
	pFlags[iDestIndex] = pFlags[iSrcIndex];
	pfTimes[iDestIndex] = pfTimes[iSrcIndex];
	pVecPositions[iDestIndex] = pVecPositions[iSrcIndex];
	pVecVelocities[iDestIndex] = pVecVelocities[iSrcIndex];
	puiAreaLights[iDestIndex] = puiAreaLights[iSrcIndex];
	pCrcs[iDestIndex] = pCrcs[iSrcIndex];
	pf2Sizes[iDestIndex] = pf2Sizes[iSrcIndex];
	pfFreezeTimes[iDestIndex] = pfFreezeTimes[iSrcIndex];
	pfVisibleIntensities[iDestIndex] = pfVisibleIntensities[iSrcIndex];
	pfLightAreas[iDestIndex] = pfLightAreas[iSrcIndex];
	pfLightIntensities[iDestIndex] = pfLightIntensities[iSrcIndex];

	pfSlowTimes[iDestIndex] = pfSlowTimes[iSrcIndex];
	pfDamages[iDestIndex] = pfDamages[iSrcIndex];
	pfPitches[iDestIndex] = pfPitches[iSrcIndex];
	puiSounds[iDestIndex] = puiSounds[iSrcIndex];
	pf4Decays[iDestIndex] = pf4Decays[iSrcIndex];
}

void Blasters::Global([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
}

void Blasters::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.blasters;
	const Blasters& rPrevious = rPreviousFrame.blasters;

	// 1. operator== 2. Copy() 3. Load/Save in Global() or Main() or PostRender() 4. Spawn()
	// Make sure to Remove() any pools in Destroy()
	VERIFY_SIZE(rCurrent, 256064);

	Spawnable::Interpolate(rCurrent, rPrevious);

	rCurrent.iCount = rPrevious.iCount;

	for (int64_t i = 0; i < rPrevious.iCount; ++i)
	{
		// Load
		BlasterFlags_t flags = rPrevious.pFlags[i];
		float fTime = rPrevious.pfTimes[i] + fDeltaTime;
		auto vecPosition = rPrevious.pVecPositions[i];
		auto vecVelocity = rPrevious.pVecVelocities[i];
		engine::area_light_t uiAreaLight = rPrevious.puiAreaLights[i];
		common::crc_t crc = rPrevious.pCrcs[i];
		XMFLOAT2 f2Size = rPrevious.pf2Sizes[i];
		float fFreezeTime = rPrevious.pfFreezeTimes[i] - fDeltaTime;
		float fVisibleIntensity = rPrevious.pfVisibleIntensities[i];
		float fLightArea = rPrevious.pfLightAreas[i];
		float fLightIntensity = rPrevious.pfLightIntensities[i];

		// Position & velocity
		if (fFreezeTime < 0.0f) [[likely]]
		{
			float fSlow = 1.0f - std::max(rPrevious.pfSlowTimes[i], 0.0f);
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fSlow * fDeltaTime), vecVelocity, vecPosition);
		}

		// Area light
		if (flags & kImpactObject) [[unlikely]]
		{
			rFrame.areaLights.Remove(uiAreaLight);
		}
		else
		{
			float fSizeFromSpeed = 1.0f;
			if (flags & kSizeFromSpeed)
			{
				fSizeFromSpeed = std::pow(XMVectorGetX(XMVector3Length(vecVelocity)) / 100.0f, 0.75f);
			}

			float fWidth = f2Size.x;
			float fLength = fSizeFromSpeed * f2Size.y;

			const auto [vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible] = common::CalculateArea(vecPosition, XMVector3Normalize(vecVelocity), fLength, fLength, fWidth);
			const auto [vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting] = common::CalculateArea(vecPosition, XMVector3Normalize(vecVelocity), fLightArea * fLength, fLightArea * fLength, fLightArea * fWidth);
			rFrame.areaLights.Add(uiAreaLight,
			{
				.crc = crc,
				.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f},},
				.pVecVisiblePositions = {vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible},
				.fVisibleIntensity = fVisibleIntensity,
				.pVecLightingPositions = {vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting},
				.fLightingIntensity = fLightIntensity,
			});
		}

		// Save
		rCurrent.pFlags[i] = flags;
		rCurrent.pfTimes[i] = fTime;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.puiAreaLights[i] = uiAreaLight;
		rCurrent.pCrcs[i] = crc;
		rCurrent.pf2Sizes[i] = f2Size;
		rCurrent.pfFreezeTimes[i] = fFreezeTime;
		rCurrent.pfVisibleIntensities[i] = fVisibleIntensity;
		rCurrent.pfLightAreas[i] = fLightArea;
		rCurrent.pfLightIntensities[i] = fLightIntensity;
	}
}

void Blasters::PostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.blasters;
	const Blasters& rPrevious = rPreviousFrame.blasters;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		float fSlowTime = rPrevious.pfSlowTimes[i] - fDeltaTime;
		float fDamage = rPrevious.pfDamages[i];
		float fPitch = rPrevious.pfPitches[i];
		engine::sound_t uiSound = rPrevious.puiSounds[i];
		XMFLOAT4 f4Decays = rPrevious.pf4Decays[i];

		// Sound position
		rFrame.sounds.Add(uiSound,
		{
			.uiCrc = data::kAudioBlaster514039__newlocknew__blastershot6sytrusrsmplmultiprcsngsinglewavCrc,
			.fVolume = rCurrent.pFlags[i] & kCollideEnemies ? 0.15f : 0.125f,
			.fPitch = rPrevious.pfPitches[i],
			.fFadeOutTime = 0.1f,
			.vecPosition = rCurrent.pVecPositions[i],
			.vecVelocity = rPrevious.pVecVelocities[i],
		});

		// Save
		rCurrent.pfSlowTimes[i] = fSlowTime;
		rCurrent.pfDamages[i] = fDamage;
		rCurrent.pfPitches[i] = fPitch;
		rCurrent.puiSounds[i] = uiSound;
		rCurrent.pf4Decays[i] = f4Decays;

		// Decays
		rCurrent.pVecVelocities[i] *= 1.0f - (rCurrent.pf4Decays[i].x * fDeltaTime);
		rCurrent.pfVisibleIntensities[i] *= 1.0f - (rCurrent.pf4Decays[i].y * fDeltaTime);
		rCurrent.pf2Sizes[i].x *= 1.0f - (rCurrent.pf4Decays[i].z * fDeltaTime);
		rCurrent.pf2Sizes[i].y *= 1.0f - (rCurrent.pf4Decays[i].z * fDeltaTime);
		rCurrent.pfDamages[i] *= 1.0f - (rCurrent.pf4Decays[i].w * fDeltaTime);
	}
}

void XM_CALLCONV Blasters::CollisionEffect(Frame& __restrict rFrame, int64_t i, bool bSmoke)
{
	Blasters& rCurrent = rFrame.blasters;

	auto vecPosition = rCurrent.pVecPositions[i];

	if (bSmoke)
	{
		rFrame.puffControllers2.Add(rFrame.puffs, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				0.0f,
				0.1f,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecPosition, .fIntensity = 4.0f, .fArea = 0.15f, .fCookie = 4.0f},
				{.vecPosition = vecPosition, .fIntensity = 0.0f, .fArea = 0.3f,  .fCookie = 4.0f},
			},
		});
	}

	rFrame.pointLightControllers2.Add(rFrame.pointLights, rFrame.fCurrentTime,
	{
		.bDestroysSelf = true,
		.pfTimes =
		{
			0.0f,
			0.2f,
		},
		.pObjectInfos =
		{
			{.vecPosition = vecPosition, .fVisibleArea = 0.5f, .fVisibleIntensity = 2.0f, .fLightingArea = 1.25f, .fLightingIntensity = 2000.0f, .crc = data::kTexturesBlasterBC71pngCrc, .fRotation = common::Random<XM_2PI>(rFrame.randomEngine)},
			{.vecPosition = vecPosition, .fVisibleArea = 0.0f, .fVisibleIntensity = 2.0f, .fLightingArea = 0.0f, .fLightingIntensity = 2000.0f, .crc = data::kTexturesBlasterBC71pngCrc, .fRotation = common::Random<XM_2PI>(rFrame.randomEngine)},
		},
	});
}

void Blasters::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.blasters;

	// Collide enemies

	// Collide terrain
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		auto vecFinalPosition = rCurrent.pVecPositions[i];
		float fPositionFinal = XMVectorGetZ(vecFinalPosition);
		float fElevationFinal = engine::gpIslands->GlobalElevation(vecFinalPosition);

		if (fPositionFinal > fElevationFinal) [[likely]]
		{
			continue;
		}

		auto vecInitialPosition = rPreviousFrame.blasters.pVecPositions[i];
		static constexpr int64_t kiSteps = 32;
		static constexpr float kfStepPercent = 1.0f / static_cast<float>(kiSteps);
		float fPercent = 0.0f;
		auto vecCollisionPosition = vecFinalPosition;
		for (int64_t k = 0; k < kiSteps; ++k, fPercent += kfStepPercent)
		{
			auto vecPossibleCollisionPosition = (1.0f - fPercent) * vecFinalPosition + fPercent * vecInitialPosition;
			float fPossibleElevation = engine::gpIslands->GlobalElevation(vecPossibleCollisionPosition);
			if (fPossibleElevation <= XMVectorGetZ(vecPossibleCollisionPosition))
			{
				vecCollisionPosition = XMVectorSetZ(vecPossibleCollisionPosition, fPossibleElevation);
				break;
			}
		}

		static constexpr float kfJitterPosition = 0.25f;
		vecCollisionPosition = XMVectorAdd(XMVectorSet(-kfJitterPosition + common::Random<2.0f * kfJitterPosition>(rFrame.randomEngine), -kfJitterPosition + common::Random<2.0f * kfJitterPosition>(rFrame.randomEngine), 0.0f, 0.0f), vecCollisionPosition);

		static constexpr float kfExplosionSize = 0.7f;
		static constexpr float kfExplosionTime = 0.1f;

		rFrame.pointLightControllers3.Add(rFrame.pointLights, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				0.0f,
				kfExplosionTime,
				kfExplosionTime + 5.0f,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.5f * kfExplosionSize, .fVisibleIntensity = 2.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = kfTerrainCraterIntensityImpact, .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = common::Random<XM_2PI>(rFrame.randomEngine)},
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.3f * kfExplosionSize, .fVisibleIntensity = 1.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = kfTerrainCraterIntensityGlow,   .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = 0.0f},
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.2f * kfExplosionSize, .fVisibleIntensity = 0.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = 0.0f,                           .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = 0.0f},
			},
		});

		rFrame.puffControllers2.Add(rFrame.puffs, rFrame.fCurrentTime,
		{
			.bDestroysSelf = true,
			.pfTimes =
			{
				0.0f,
				kfExplosionTime,
			},
			.pObjectInfos =
			{
				{.vecPosition = vecCollisionPosition, .fIntensity = 2.0f / (kfExplosionTime * kfExplosionSize), .fArea = kfExplosionSize / 4.0f, .fCookie = 4.0f},
				{.vecPosition = vecCollisionPosition, .fIntensity = 0.5f / (kfExplosionTime * kfExplosionSize), .fArea = kfExplosionSize / 3.0f + (kfExplosionSize / 3.0f) * common::Random(rFrame.randomEngine), .fCookie = 4.0f},
			},
		});

		rCurrent.pFlags[i] |= kImpactTerrain;
	}
}

void Blasters::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.blasters;

	for (int64_t j = 0; j < rCurrent.iSpawnCount; ++j)
	{
		if (rCurrent.iCount == kiMax)
		{
			DEBUG_BREAK();
			break;
		}

		int64_t i = rCurrent.iCount++;

		rCurrent.pFlags[i] = rCurrent.pSpawns[j].flags;
		rCurrent.pfTimes[i] = 0.0f;
		rCurrent.pVecPositions[i] = rCurrent.pSpawns[j].vecPosition;
		ASSERT(XMVectorGetW(rCurrent.pVecPositions[i]) == 1.0f);
		rCurrent.pVecVelocities[i] = rCurrent.pSpawns[j].vecVelocity;
		ASSERT(XMVectorGetW(rCurrent.pVecVelocities[i]) == 0.0f);
		rCurrent.puiAreaLights[i] = 0;
		rCurrent.pCrcs[i] = rCurrent.pSpawns[j].crc;
		rCurrent.pf2Sizes[i] = rCurrent.pSpawns[j].f2Size;
		rCurrent.pfFreezeTimes[i] = 0.0f;
		rCurrent.pfVisibleIntensities[i] = rCurrent.pSpawns[j].fVisibleIntensity;
		rCurrent.pfLightAreas[i] = rCurrent.pSpawns[j].fLightArea;
		rCurrent.pfLightIntensities[i] = rCurrent.pSpawns[j].fLightIntensity;

		rCurrent.pfSlowTimes[i] = 0.0f;
		rCurrent.pfDamages[i] = rCurrent.pSpawns[j].fDamage;
		static constexpr float kfPitchMin = 0.75f;
		static constexpr float kfPitchRandom = 0.5f;
		rCurrent.pfPitches[i] = kfPitchMin + common::Random<kfPitchRandom>(rFrame.randomEngine);
		rCurrent.puiSounds[i] = 0;
		rCurrent.pf4Decays[i] = rCurrent.pSpawns[j].f4Decays;
	}

	rCurrent.iSpawnCount = 0;
}

void Blasters::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i)
{
	Blasters& rCurrent = rFrame.blasters;

	rFrame.areaLights.Remove(rCurrent.puiAreaLights[i]);
	rFrame.sounds.Remove(rCurrent.puiSounds[i]);
}

void Blasters::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	static constexpr float kfThreshold = 0.1f;

	Blasters& rCurrent = rFrame.blasters;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		bool bDestroy = engine::OutsideVisibleArea(rFrameInput, rCurrent.pVecPositions[i], kfAutoDestroyDistance, kfAutoDestroyDistance, kfAutoDestroyDistance, kfAutoDestroyDistance);
		bDestroy |= rCurrent.pFlags[i] & kDestroy;
		bDestroy |= rCurrent.pFlags[i] & kImpactObject;
		bDestroy |= rCurrent.pFlags[i] & kImpactTerrain;
		bDestroy |= rCurrent.pfVisibleIntensities[i] < kfThreshold;
		bDestroy |= rCurrent.pf2Sizes[i].x < kfThreshold;

		if (bDestroy) [[unlikely]]
		{
			Destroy(rFrame, i);

			if (rCurrent.iCount - 1 > i) [[likely]]
			{
				rCurrent.Copy(i, rCurrent.iCount - 1);
				--i;
			}
			--rCurrent.iCount;
		}
	}
}

void Blasters::RenderGlobal([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

void Blasters::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
	PROFILE_SET_COUNT(engine::kCpuCounterBlasters, rFrame.blasters.iCount);
}

} // namespace game
