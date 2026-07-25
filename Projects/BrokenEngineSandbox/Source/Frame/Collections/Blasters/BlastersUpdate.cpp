// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Blasters.h"

#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Ui/LightingWrappers.h"
#include "Ui/SmokeWrappers.h"
#include "Ui/SoundWrappers.h"
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

struct BlasterCollisionIntervalScratch
{
	std::vector<float> startTimes;
	std::vector<float> endTimes;
	std::vector<float> maxTimes;
	std::vector<SegmentHit> terrainHits;
	std::vector<SegmentHit> boundaryHits;
};

static BlasterCollisionIntervalScratch& GetBlasterCollisionIntervalScratch()
{
	// Function-local TLS defers construction until first use; default construction is allocation-free
	// (empty vectors), so it is safe even before allocator startup completes. Growth sites suppress tracking.
	static thread_local BlasterCollisionIntervalScratch sScratch;
	return sScratch;
}

// Collision
constexpr float kfBlasterCollisionRadius = 0.5f;

// Terrain impact
constexpr float kfTerrainImpactJitter = 0.25f;

#if defined(BT_CLIENT)
// Terrain effect registrations
static uint8_t suiTerrainCraterTypeIndex = 0xFF;
static uint8_t suiTerrainCraterControllerIndex = 0xFF;
static uint8_t suiTerrainPuffTypeIndex = 0xFF;
static uint8_t suiTerrainPuffControllerIndex = 0xFF;

// Terrain crater effect timing
constexpr float kfTerrainCraterTimeOne = 0.1f;
constexpr float kfTerrainCraterTimeTwo = 3.0f;
constexpr float kfTerrainCraterTimeThree = 5.1f;

// Terrain puff effect timing
constexpr float kfTerrainPuffTime = 0.15f;
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
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
			{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
		},
		.ppVisibleAreaScales = {&gCraterVisibleAreaOne, &gCraterVisibleAreaTwo, &gCraterVisibleAreaThree, &gCraterVisibleAreaFour},
		.ppVisibleIntensityScales = {&gCraterVisibleIntensityOne, &gCraterVisibleIntensityTwo, &gCraterVisibleIntensityThree, &gCraterVisibleIntensityFour},
		.ppLightingAreaScales = {&gCraterLightingAreaOne, &gCraterLightingAreaTwo, &gCraterLightingAreaThree, &gCraterLightingAreaFour},
		.ppLightingIntensityScales = {&gCraterLightingIntensityOne, &gCraterLightingIntensityTwo, &gCraterLightingIntensityThree, &gCraterLightingIntensityFour},
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
			{.fArea = 1.0f, .fIntensity = 1.0f, .fRotation = 0.0f},
			{.fArea = 1.0f, .fIntensity = 1.0f, .fRotation = kfTerrainPuffRotationEnd},
			{},
			{},
		},
		.ppAreaScales = {&gBlasterPuffAreaStart, &gBlasterPuffAreaEnd, nullptr, nullptr},
		.ppIntensityScales = {&gBlasterPuffIntensityStart, &gBlasterPuffIntensityEnd, nullptr, nullptr},
	});
}

// Helper to sync owned objects for a blaster
static void XM_CALLCONV SyncBlaster(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight, engine::point_lights_t uiPointLight, [[maybe_unused]] engine::sound_t uiSound, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, [[maybe_unused]] float fPitch)
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
			.fVisibleIntensity = rPointLightType.pVisibleIntensityWrapper != nullptr ? rPointLightType.pVisibleIntensityWrapper->Get() : rPointLightType.fVisibleIntensity,
			.fLightingArea = rPointLightType.pLightingAreaWrapper != nullptr ? rPointLightType.pLightingAreaWrapper->Get() : rPointLightType.fLightingArea,
			.fLightingIntensity = rPointLightType.pLightingIntensityWrapper != nullptr ? rPointLightType.pLightingIntensityWrapper->Get() : rPointLightType.fLightingIntensity,
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
	// DT: TEMP kAudioBlasterNew609840__eminyildirim__spacedroneambience7variation_0wavCrc sounds bad
	// engine::SoundsInterpolate::Sync(rFrameInterpolate, uiSound,
	// {
	// 	.vecPosition = vecPosition,
	// 	.vecVelocity = vecVelocity,
	// 	.uiCrc = data::kAudioBlasterNew609840__eminyildirim__spacedroneambience7variation_0wavCrc,
	// 	.fVolume = kfBlasterVolume,
	// 	.fPitch = fPitch,
	// 	.fFadeOutTime = kfBlasterFadeOutTime,
	// });
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
		XMVECTOR vecVelocity = rPreviousPostRender.pVecVelocities[i];

		XMVECTOR vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), vecVelocity, rPrevious.pVecPositions[i]);
		// Positions must always have W=1.0 — prevents W-lane drift via MultiplyAdd.
		vecPosition = XMVectorSetW(vecPosition, 1.0f);

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
	BlasterCollisionIntervalScratch& rCollisionScratch = GetBlasterCollisionIntervalScratch();
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppress;

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
	rCollisionScratch.startTimes.resize(uiCount);
	rCollisionScratch.endTimes.resize(uiCount);
	rCollisionScratch.maxTimes.resize(uiCount);
	rCollisionScratch.terrainHits.resize(uiCount);
	rCollisionScratch.boundaryHits.resize(uiCount);
	const BlastersInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pBlasters;
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		size_t uiIndex = static_cast<size_t>(i);
		sCollisionFlags.at(uiIndex) = engine::CollisionFlags::kDestroyOnCollide;
		sCollisionRadii.at(uiIndex) = kfBlasterCollisionRadius;
		sCollisionDamages.at(uiIndex) = kfBlasterDamage;
		rCollisionScratch.startTimes.at(uiIndex) = 0.0f;
		rCollisionScratch.endTimes.at(uiIndex) = 1.0f;
		rCollisionScratch.terrainHits.at(uiIndex) = TracePointAgainstTerrain(rStaticData, rPreviousInterpolate.pVecPositions[i], rCurrentInterpolate.pVecPositions[i], 0.0f, 1.0f);
		rCollisionScratch.boundaryHits.at(uiIndex) = TracePointToFrameExit(rStaticData.vecArea, rPreviousInterpolate.pVecPositions[i], rCurrentInterpolate.pVecPositions[i], 0.0f, 1.0f);
		float fMaxTime = std::numeric_limits<float>::max();
		if (rCollisionScratch.terrainHits.at(uiIndex).bHit)
		{
			fMaxTime = rCollisionScratch.terrainHits.at(uiIndex).fTime;
		}
		if (rCollisionScratch.boundaryHits.at(uiIndex).bHit)
		{
			fMaxTime = std::min(fMaxTime, rCollisionScratch.boundaryHits.at(uiIndex).fTime);
		}
		rCollisionScratch.maxTimes.at(uiIndex) = fMaxTime;
	}

	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecStartPositions = rPreviousInterpolate.pVecPositions,
		.pVecEndPositions = rCurrentInterpolate.pVecPositions,
		.pfStartTimes = rCollisionScratch.startTimes.data(),
		.pfEndTimes = rCollisionScratch.endTimes.data(),
		.pfMaxTimes = rCollisionScratch.maxTimes.data(),
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
	BlasterCollisionIntervalScratch& rCollisionScratch = GetBlasterCollisionIntervalScratch();
	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		size_t uiIndex = static_cast<size_t>(i);
		// Entity results are pre-filtered against terrain and frame-exit cutoffs.
		if (engine::Collision::HasCollision(suiCollisionLayerIndex, i))
		{
			rCurrentPostRender.pFlags[i].Set(kDestroy);
			continue;
		}

		const SegmentHit& rTerrainHit = rCollisionScratch.terrainHits.at(uiIndex);
		const SegmentHit& rBoundaryHit = rCollisionScratch.boundaryHits.at(uiIndex);
		if (rTerrainHit.bHit && (!rBoundaryHit.bHit || rTerrainHit.fTime <= rBoundaryHit.fTime)) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kDestroy);
			XMVECTOR vecCollisionPosition = rTerrainHit.vecPosition;

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
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster16793__pushtobreak__earth1wavCrc, vecCollisionPosition, gTerrainImpactVolume.Get());
#endif
		}
		else if (rBoundaryHit.bHit) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kTransfer);
		}
	}
}

} // namespace game
