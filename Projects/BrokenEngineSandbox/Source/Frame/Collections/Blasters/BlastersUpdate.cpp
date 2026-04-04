// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Blasters.h"

#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#endif

#include "Data/Audio.h"
#if defined(BT_CLIENT)
#include "Data/Texture.h"
#endif

namespace game
{

using enum BlasterFlags;

// Collision layer index (set each frame in PreCollision)
// thread_local: parallel per-Frame tick via Dispatch
static thread_local size_t suiCollisionLayerIndex = 0;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;

// Collision
constexpr float kfBlasterCollisionRadius = 0.5f;

// Terrain impact
constexpr float kfTerrainImpactJitter = 0.25f;
constexpr float kfTerrainImpactVolume = 0.3f;

// Terrain collision search
constexpr int64_t kiTerrainSearchSteps = 32;
constexpr float kfTerrainSearchStepPercent = 1.0f / static_cast<float>(kiTerrainSearchSteps);

#if defined(BT_CLIENT)
// Terrain effect registrations
static uint8_t suiTerrainCraterTypeIndex = 0xFF;
static uint8_t suiTerrainCraterControllerIndex = 0xFF;
static uint8_t suiTerrainPuffTypeIndex = 0xFF;
static uint8_t suiTerrainPuffControllerIndex = 0xFF;

// Terrain crater effect
constexpr float kfTerrainCraterTimeOne = 0.1f;
constexpr float kfTerrainCraterTimeTwo = 3.0f;
constexpr float kfTerrainCraterTimeThree = 5.1f;

constexpr float kfTerrainCraterVisibleArea = 0.35f;
constexpr float kfTerrainCraterVisibleIntensity = 2.0f;
constexpr float kfTerrainCraterVisibleAreaEnd = 0.25f;
constexpr float kfTerrainCraterVisibleIntensityEnd = 1.0f;

constexpr float kfTerrainCraterLightingArea = 1.0f;
constexpr float kfTerrainCraterLightingAreaEnd = 1.0f;
constexpr float kfTerrainCraterLightingIntensityStart = 10000.0f;
constexpr float kfTerrainCraterLightingIntensityMid = 5000.0f;
constexpr float kfTerrainCraterLightingIntensityEnd = 2000.0f;

// Terrain puff effect
constexpr float kfTerrainPuffTime = 0.15f;
constexpr float kfTerrainPuffAreaStart = 0.25f;
constexpr float kfTerrainPuffIntensityStart = 6.0f;
constexpr float kfTerrainPuffAreaEnd = 0.75f;
constexpr float kfTerrainPuffIntensityEnd = 0.5f;
constexpr float kfTerrainPuffRotationEnd = 10.0f;

// Called from Register() in Blasters.cpp
void RegisterBlasterTerrainEffects()
{
	if (suiTerrainCraterTypeIndex != 0xFF)
	{
		return;
	}

	// Terrain crater effect
	engine::PointLightsInterpolate::RegisterType(suiTerrainCraterTypeIndex,
	{
		.crc = data::kTexturesBlasterBC7TerrainImpactpngCrc,
		.uiColor = 0xFFFFFFFF,
	});

	engine::PointLightsInterpolate::RegisterControllerType(suiTerrainCraterControllerIndex,
	{
		.uiBaseTypeIndex = suiTerrainCraterTypeIndex,
		.uiKeyframeCount = 4,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfTerrainCraterTimeOne, kfTerrainCraterTimeTwo, kfTerrainCraterTimeThree},
		.keyframes =
		{
			{.fVisibleArea = kfTerrainCraterVisibleArea, .fVisibleIntensity = kfTerrainCraterVisibleIntensity, .fLightingArea = kfTerrainCraterLightingArea, .fLightingIntensity = kfTerrainCraterLightingIntensityStart, .fRotation = 0.0f},
			{.fVisibleArea = kfTerrainCraterVisibleArea, .fVisibleIntensity = kfTerrainCraterVisibleIntensity, .fLightingArea = kfTerrainCraterLightingArea, .fLightingIntensity = kfTerrainCraterLightingIntensityMid, .fRotation = 0.0f},
			{.fVisibleArea = kfTerrainCraterVisibleAreaEnd, .fVisibleIntensity = kfTerrainCraterVisibleIntensityEnd, .fLightingArea = kfTerrainCraterLightingAreaEnd, .fLightingIntensity = kfTerrainCraterLightingIntensityEnd, .fRotation = 0.0f},
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
		},
	});

	// Terrain impact smoke puff effect
	engine::PuffsInterpolate::RegisterType(suiTerrainPuffTypeIndex,
	{
		.crc = data::kTexturesSmokeBC44jpgCrc,
		.uiColor = 0xFFFFFFFF,
	});

	engine::PuffsInterpolate::RegisterControllerType(suiTerrainPuffControllerIndex,
	{
		.uiBaseTypeIndex = suiTerrainPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfTerrainPuffTime, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = kfTerrainPuffAreaStart, .fIntensity = kfTerrainPuffIntensityStart, .fRotation = 0.0f},
			{.fArea = kfTerrainPuffAreaEnd, .fIntensity = kfTerrainPuffIntensityEnd, .fRotation = kfTerrainPuffRotationEnd},
			{},
			{},
		},
	});
}

// Helper to sync owned objects for a blaster
static void XM_CALLCONV SyncBlaster(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight,
	engine::point_lights_t uiPointLight, engine::sound_t uiSound,
	FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, float fPitch)
{
	const BlastersType& rType = BlastersInterpolate::GetType(uiTypeIndex);

	// Sync light (point light or area light)
	if (uiPointLight.IsValid())
	{
		float fSize = rType.f2Size.x;
		const engine::PointLightsType& rPointLightType = engine::PointLightsInterpolate::GetType(rType.uiPointLightTypeIndex);
		engine::PointLightsInterpolate::Sync(rFrameInterpolate, uiPointLight,
		{
			.vecPosition = vecPosition,
			.fVisibleArea = fSize,
			.fVisibleIntensity = rPointLightType.fVisibleIntensity,
			.fLightingArea = rPointLightType.fLightingArea,
			.fLightingIntensity = rPointLightType.fLightingIntensity,
			.fRotation = 0.0f,
		});
	}
	else
	{
		float fWidth = rType.f2Size.x;
		float fLength = rType.f2Size.y;

		XMVECTOR vecDirection = XMVector3Normalize(vecVelocity);
		auto [vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight] = common::CalculateArea(vecPosition, vecDirection, fLength, fLength, fWidth);

		engine::AreaLightsInterpolate::Sync(rFrameInterpolate, uiAreaLight,
		{
			.uiTypeIndex = rType.uiAreaLightTypeIndex,
			.vecVisiblePositions = {vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight},
		});
	}

	// Sync sound
	engine::SoundsInterpolate::Sync(rFrameInterpolate, uiSound,
	{
		.vecPosition = vecPosition,
		.vecVelocity = vecVelocity,
		.uiCrc = data::kAudioBlaster514039__newlocknew__blastershot6sytrusrsmplmultiprcsngsinglewavCrc,
		.fVolume = kfBlasterVolume,
		.fPitch = fPitch,
		.fFadeOutTime = kfBlasterFadeOutTime,
	});
}
#endif // BT_CLIENT

void BlastersInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	BlastersInterpolate& rCurrent = *rCurrentFrameInterpolate.pBlasters;
	const BlastersInterpolate& rPrevious = *rPreviousFrame.interpolate.pBlasters;
	const BlastersPostRender& rPreviousPostRender = *rPreviousFrame.postRender.pBlasters;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load (type index copied in AllocateAndCopy)
		[[maybe_unused]] uint8_t uiTypeIndex = rCurrent.puiTypeIndices[i];

		// Update position based on velocity and delta time
		XMVECTOR vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], rPrevious.pVecPositions[i]);
		XMVECTOR vecVelocity = rPreviousPostRender.pVecVelocities[i];

		// Direction from velocity
		XMVECTOR vecDirection = XMVector3Normalize(vecVelocity);

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;

#if defined(BT_CLIENT)
		// Sync owned objects
		SyncBlaster(rCurrentFrameInterpolate, rCurrent.puiAreaLights[i], rCurrent.puiPointLights[i], rPreviousPostRender.puiSounds[i], vecPosition, vecVelocity, uiTypeIndex, rPreviousPostRender.pfPitches[i]);

		// Sync wind deposit
		if (rCurrent.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsInterpolate::Sync(rCurrentFrameInterpolate, rCurrent.puiWindTrails[i],
			{
				.vecPosition = vecPosition,
				.fIntensity = rCurrent.pfWindTrailIntensities[i],
				.fWidth = rCurrent.pfWindTrailWidths[i],
				.fLengthMultiplier = rCurrent.pfWindTrailLengthMultipliers[i],
			});
		}
#endif // BT_CLIENT
	}

}

void BlastersPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void BlastersPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	// Build collision arrays
	size_t uiCount = static_cast<size_t>(rCurrentInterpolate.iCount);
	sCollisionFlags.resize(uiCount);
	sCollisionRadii.resize(uiCount);
	sCollisionDamages.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = engine::CollisionFlags::kDestroyOnCollide;
		sCollisionRadii.at(static_cast<size_t>(i)) = kfBlasterCollisionRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = kfBlasterDamage;
	}

	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.pVecVelocities = rCurrentPostRender.pVecVelocities,
		.iCount = rCurrentInterpolate.iCount,
		.bSweptTest = true,
		.uiCategory = CollisionCategory::kBlaster,
		.uiCollidesWith = CollidesWith::kBlaster,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void BlastersPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Flag for transfer if outside frame boundaries (Transfer phase handles removal)
		if (IsOutOfBounds(bounds, vecPosition)) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kTransfer);
			continue;
		}

		// Check collision
		if (engine::Collision::HasCollision(suiCollisionLayerIndex, i))
		{
			rCurrentPostRender.pFlags[i].Set(kDestroy);
			continue;
		}

		// Collide terrain
		float fPositionFinal = XMVectorGetZ(vecPosition);
		float fElevationFinal = engine::gpIslandTerrain->GlobalElevation(vecPosition);

		if (fPositionFinal <= fElevationFinal) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kDestroy);

			XMVECTOR vecVelocity = rCurrentPostRender.pVecVelocities[i];
			XMVECTOR vecInitialPosition = vecPosition;
			XMVECTOR vecFinalPosition = vecPosition;

			// Binary search to find exact terrain intersection
			float fPercent = 0.0f;
			XMVECTOR vecCollisionPosition = vecFinalPosition;

			for (int64_t k = 0; k < kiTerrainSearchSteps; ++k, fPercent += kfTerrainSearchStepPercent)
			{
				XMVECTOR vecPossibleCollisionPosition = XMVectorLerp(vecFinalPosition, vecInitialPosition, fPercent);
				float fPossibleElevation = engine::gpIslandTerrain->GlobalElevation(vecPossibleCollisionPosition);
				if (fPossibleElevation <= XMVectorGetZ(vecPossibleCollisionPosition))
				{
					vecCollisionPosition = XMVectorSetZ(vecPossibleCollisionPosition, fPossibleElevation);
					break;
				}
			}

			// Add jitter for visual variety
			vecCollisionPosition = common::RandomPositionJitter<kfTerrainImpactJitter>(vecCollisionPosition, rFrame.postRender.randomEngine);

			// Spawn terrain effects
			[[maybe_unused]] float fRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
#if defined(BT_CLIENT)
			engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiTerrainCraterControllerIndex, vecCollisionPosition, fRotation);
			engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiTerrainPuffControllerIndex, vecCollisionPosition);
#endif

			// Play terrain impact sound
#if defined(BT_CLIENT)
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster16793__pushtobreak__earth1wavCrc, vecCollisionPosition, kfTerrainImpactVolume);
#endif
		}
	}
}

void BlastersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

} // namespace game
