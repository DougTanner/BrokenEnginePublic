#include "Blasters.h"

#include "Frame/Collections/Collections.h"
#include "Frame/Frame.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/GltfPipelines.h"

namespace game
{

using enum BlasterFlags;

void BlastersInterpolate::Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	BlastersInterpolate& rCurrent = rCurrentFrameInterpolate.blasters;

	const BlastersInterpolate& rPrevious = rPreviousFrame.interpolate.blasters;
	if (!engine::ReallocateIfCapacityChanged(rCurrent, rPrevious, BLASTERS_INTERPOLATE_LIST(rCurrent)))
	{
		return;
	}

	const BlastersPostRender& rPreviousPostRender = rPreviousFrame.postRender.blasters;
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		engine::area_light_t uiAreaLight = rPrevious.puiAreaLights[i];
	
		// Update position based on velocity and delta time
		vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], vecPosition);

		// Sync area light position
		engine::area_light_t uiAreaLightIndex = rCurrentFrameInterpolate.areaLights.idToIndexMap.at(uiAreaLight);
		rCurrentFrameInterpolate.areaLights.pVecPositions[uiAreaLightIndex] = vecPosition;

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.puiAreaLights[i] = uiAreaLight;
	}
}

void BlastersPostRender::Update(FramePostRender& __restrict rCurrentFramePostRender, const Frame& __restrict rPreviousFrame, float fDeltaTime)
{
	BlastersPostRender& rCurrent = rCurrentFramePostRender.blasters;

	const BlastersPostRender& rPrevious = rPreviousFrame.postRender.blasters;
	if (!engine::ReallocateIfCapacityChanged(rCurrent, rPrevious, BLASTERS_POST_RENDER_LIST(rCurrent)))
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		BlasterFlags_t flag = rPrevious.pFlags[i];
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];

		//Save
		rCurrent.pFlags[i] = flag;
		rCurrent.pVecVelocities[i] = vecVelocity;
	}
}

void BlastersPostRender::Collide(Frame& __restrict rFrame)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	// Collide terrain
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		auto vecFinalPosition = rCurrentInterpolate.pVecPositions[i];
		float fPositionFinal = XMVectorGetZ(vecFinalPosition);
		float fElevationFinal = engine::gpIslands->GlobalElevation(vecFinalPosition);

		if (fPositionFinal <= fElevationFinal) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i] |= kDestroy;
		}
	}
}

void XM_CALLCONV BlastersPostRender::Spawn(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecVelocity)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	int64_t iNewCapacity = engine::CalculateGrowthCapacity(rCurrentInterpolate);
	if (iNewCapacity > 0)
	{
		ASSERT(rCurrentInterpolate.iCount == rCurrentPostRender.iCount);
		engine::GrowCapacityWithCopy(rCurrentInterpolate, iNewCapacity, rCurrentInterpolate.iCount, BLASTERS_INTERPOLATE_LIST(rCurrentInterpolate));
		engine::GrowCapacityWithCopy(rCurrentPostRender, iNewCapacity, rCurrentPostRender.iCount, BLASTERS_POST_RENDER_LIST(rCurrentPostRender));
	}

	int64_t iSpawnIndex = engine::IncrementCountsAndGetSpawnIndex(rCurrentInterpolate, rCurrentPostRender);

	rCurrentInterpolate.pVecPositions[iSpawnIndex] = vecPosition;

	// IMPORTANT: Any Add() calls to other collections must have a corresponding Remove() in Destroy() below
	rCurrentInterpolate.puiAreaLights[iSpawnIndex] = rFrame.postRender.areaLights.Add(rFrame);

	rCurrentPostRender.pFlags[iSpawnIndex] = {};
	rCurrentPostRender.pVecVelocities[iSpawnIndex] = vecVelocity;
}

void BlastersPostRender::Destroy(Frame& __restrict rFrame)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kDestroy)) [[likely]]
		{
			continue;
		}

		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);

		if (rCurrentInterpolate.iCount - 1 > i) [[likely]]
		{
			engine::SwapElement(rCurrentInterpolate, i, BLASTERS_INTERPOLATE_LIST(rCurrentInterpolate));
			engine::SwapElement(rCurrentPostRender, i, BLASTERS_POST_RENDER_LIST(rCurrentPostRender));
			--i;
		}

		--rCurrentInterpolate.iCount;
		--rCurrentPostRender.iCount;
	}
}

bool BlastersInterpolate::operator==(const BlastersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
	}

	return bEqual;
}

bool BlastersPostRender::operator==(const BlastersPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
	}

	return bEqual;
}

} // namespace game

#if 0

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Islands.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Frame/Frame.h"
#include "Input/Input.h"
#include "Game.h"


namespace game
{

using enum BlasterFlags;

// Terrain craters
constexpr float kfTerrainCraterIntensityImpact = 1000.0f;
constexpr float kfTerrainCraterIntensityGlow = 300.0f;

bool Blasters::operator==(const Blasters& rOther) const
{
	bool bEqual = *static_cast<const Spawnable*>(this) == *static_cast<const Spawnable*>(&rOther);

	bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pfTimes[i], rOther.pfTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::BreakOnNotEqual(puiAreaLights[i], rOther.puiAreaLights[i]);
		bEqual &= common::BreakOnNotEqual(pCrcs[i], rOther.pCrcs[i]);
		bEqual &= common::BreakOnNotEqual(pf2Sizes[i], rOther.pf2Sizes[i]);
		bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfVisibleIntensities[i], rOther.pfVisibleIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfLightAreas[i], rOther.pfLightAreas[i]);
		bEqual &= common::BreakOnNotEqual(pfLightIntensities[i], rOther.pfLightIntensities[i]);

		bEqual &= common::BreakOnNotEqual(pfSlowTimes[i], rOther.pfSlowTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfDamages[i], rOther.pfDamages[i]);
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(puiSounds[i], rOther.puiSounds[i]);
		bEqual &= common::BreakOnNotEqual(pf4Decays[i], rOther.pf4Decays[i]);
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

void Blasters::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.interpolate.blasters;
	const Blasters& rPrevious = rPreviousFrame.interpolate.blasters;

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
			rFrame.postRender.areaLights.Remove(rFrame, uiAreaLight);
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

			const auto[vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible] = common::CalculateArea(vecPosition, XMVector3Normalize(vecVelocity), fLength, fLength, fWidth);
			const auto[vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting] = common::CalculateArea(vecPosition, XMVector3Normalize(vecVelocity), fLightArea * fLength, fLightArea * fLength, fLightArea * fWidth);
			rFrame.interpolate.areaLights.Add(uiAreaLight,
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
	Blasters& rCurrent = rFrame.interpolate.blasters;
	const Blasters& rPrevious = rPreviousFrame.interpolate.blasters;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		float fSlowTime = rPrevious.pfSlowTimes[i] - fDeltaTime;
		float fDamage = rPrevious.pfDamages[i];
		float fPitch = rPrevious.pfPitches[i];
		engine::sound_t uiSound = rPrevious.puiSounds[i];
		XMFLOAT4 f4Decays = rPrevious.pf4Decays[i];

		// Sound position
		rFrame.interpolate.sounds.Add(uiSound,
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
	Blasters& rCurrent = rFrame.interpolate.blasters;

	auto vecPosition = rCurrent.pVecPositions[i];

	if (bSmoke)
	{
		rFrame.interpolate.puffControllers2.Add(rFrame.interpolate.puffs, rFrame.interpolate.fCurrentTime,
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

	rFrame.interpolate.pointLightControllers2.Add(rFrame.interpolate.pointLights, rFrame.interpolate.fCurrentTime,
	{
		.bDestroysSelf = true,
		.pfTimes =
		{
			0.0f,
			0.2f,
		},
		.pObjectInfos =
		{
			{.vecPosition = vecPosition, .fVisibleArea = 0.5f, .fVisibleIntensity = 2.0f, .fLightingArea = 1.25f, .fLightingIntensity = 2000.0f, .crc = data::kTexturesBlasterBC71pngCrc, .fRotation = common::Random<XM_2PI>(rFrame.interpolate.randomEngine)},
			{.vecPosition = vecPosition, .fVisibleArea = 0.0f, .fVisibleIntensity = 2.0f, .fLightingArea = 0.0f, .fLightingIntensity = 2000.0f, .crc = data::kTexturesBlasterBC71pngCrc, .fRotation = common::Random<XM_2PI>(rFrame.interpolate.randomEngine)},
		},
	});
}

void Blasters::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.interpolate.blasters;

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

		auto vecInitialPosition = rPreviousFrame.interpolate.blasters.pVecPositions[i];
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
		vecCollisionPosition = XMVectorAdd(XMVectorSet(-kfJitterPosition + common::Random<2.0f * kfJitterPosition>(rFrame.interpolate.randomEngine), -kfJitterPosition + common::Random<2.0f * kfJitterPosition>(rFrame.interpolate.randomEngine), 0.0f, 0.0f), vecCollisionPosition);

		static constexpr float kfExplosionSize = 0.7f;
		static constexpr float kfExplosionTime = 0.1f;

		rFrame.interpolate.pointLightControllers3.Add(rFrame.interpolate.pointLights, rFrame.interpolate.fCurrentTime,
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
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.5f * kfExplosionSize, .fVisibleIntensity = 2.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = kfTerrainCraterIntensityImpact, .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = common::Random<XM_2PI>(rFrame.interpolate.randomEngine)},
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.3f * kfExplosionSize, .fVisibleIntensity = 1.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = kfTerrainCraterIntensityGlow,   .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = 0.0f},
				{.vecPosition = vecCollisionPosition, .uiColor = 0xFFFFFFFF, .fVisibleArea = 0.2f * kfExplosionSize, .fVisibleIntensity = 0.0f, .fLightingArea = 2.0f * kfExplosionSize, .fLightingIntensity = 0.0f,                           .crc = data::kTexturesBlasterBC7TerrainImpactpngCrc, .fRotation = 0.0f},
			},
		});

		rFrame.interpolate.puffControllers2.Add(rFrame.interpolate.puffs, rFrame.interpolate.fCurrentTime,
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
				{.vecPosition = vecCollisionPosition, .fIntensity = 0.5f / (kfExplosionTime * kfExplosionSize), .fArea = kfExplosionSize / 3.0f + (kfExplosionSize / 3.0f) * common::Random(rFrame.interpolate.randomEngine), .fCookie = 4.0f},
			},
		});

		rCurrent.pFlags[i] |= kImpactTerrain;
	}
}

void Blasters::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Blasters& rCurrent = rFrame.interpolate.blasters;

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
		rCurrent.pfPitches[i] = kfPitchMin + common::Random<kfPitchRandom>(rFrame.interpolate.randomEngine);
		rCurrent.puiSounds[i] = 0;
		rCurrent.pf4Decays[i] = rCurrent.pSpawns[j].f4Decays;
	}

	rCurrent.iSpawnCount = 0;
}

void Blasters::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i)
{
	Blasters& rCurrent = rFrame.interpolate.blasters;

	rFrame.postRender.areaLights.Remove(rFrame, rCurrent.puiAreaLights[i]);
	rFrame.interpolate.sounds.Remove(rCurrent.puiSounds[i]);
}

void Blasters::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] const FrameInputPressed& __restrict rFrameInputPressed, [[maybe_unused]] float fDeltaTime)
{
	static constexpr float kfThreshold = 0.1f;

	Blasters& rCurrent = rFrame.interpolate.blasters;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		float fDistanceToPlayer = common::Distance(rFrame.interpolate.player.vecPosition, rCurrent.pVecPositions[i]);
		bool bDestroy = fDistanceToPlayer > 100.0f;
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

void Blasters::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
	PROFILE_SET_COUNT(engine::kCpuCounterBlasters, rFrame.interpolate.blasters.iCount);
}

} // namespace game

#endif
