// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Blasters.h"

#ifdef BT_CLIENT
#include "Audio/AudioManager.h"
#endif
#include "Frame/Collision.h"
#include "Frame/HealthDamage.h"
#ifdef BT_CLIENT
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#endif
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"
#ifdef BT_CLIENT
#include "Frame/Collections/Puffs.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#endif

#include "Data/Audio.h"
#ifdef BT_CLIENT
#include "Data/Texture.h"
#endif

namespace engine
{
template struct Collection<game::BlastersInterpolate>;
template struct Collection<game::BlastersPostRender>;
}

namespace game
{

using enum BlasterFlags;

// Collision layer index (set each frame in PreCollision)
static inline size_t suiCollisionLayerIndex = 0;
static inline std::vector<engine::CollisionFlags_t> sCollisionFlags;
static inline std::vector<float> sCollisionRadii;
static inline std::vector<float> sCollisionDamages;

#ifdef BT_CLIENT
// Terrain effect registrations
static uint8_t suiTerrainCraterTypeIndex = 0xFF;
static uint8_t suiTerrainCraterControllerIndex = 0xFF;
static uint8_t suiTerrainPuffTypeIndex = 0xFF;
static uint8_t suiTerrainPuffControllerIndex = 0xFF;
#endif

// Blaster audio
constexpr float kfBlasterVolume = 0.25f;
constexpr float kfBlasterFadeOutTime = 0.1f;

#ifdef BT_CLIENT
// Terrain crater effect
constexpr float kfTerrainCraterTimeOne = 0.1f;
constexpr float kfTerrainCraterTimeTwo = 3.0f;
constexpr float kfTerrainCraterTimeThree = 5.1f;
constexpr float kfTerrainCraterVisibleArea = 0.35f;
constexpr float kfTerrainCraterVisibleIntensity = 2.0f;
constexpr float kfTerrainCraterLightingArea = 1.0f;
constexpr float kfTerrainCraterLightingIntensityStart = 5000.0f;
constexpr float kfTerrainCraterLightingIntensityMid = 1000.0f;
constexpr float kfTerrainCraterVisibleAreaEnd = 0.25f;
constexpr float kfTerrainCraterVisibleIntensityEnd = 1.0f;
constexpr float kfTerrainCraterLightingAreaEnd = 0.5f;
constexpr float kfTerrainCraterLightingIntensityEnd = 500.0f;

// Terrain puff effect
constexpr float kfTerrainPuffTime = 0.15f;
constexpr float kfTerrainPuffAreaStart = 0.25f;
constexpr float kfTerrainPuffIntensityStart = 6.0f;
constexpr float kfTerrainPuffAreaEnd = 0.75f;
constexpr float kfTerrainPuffIntensityEnd = 0.5f;
constexpr float kfTerrainPuffRotationEnd = 10.0f;
#endif

// Collision
constexpr float kfBlasterCollisionRadius = 0.5f;

// Terrain impact
constexpr float kfTerrainImpactJitter = 0.25f;
constexpr float kfTerrainImpactVolume = 0.3f;

// Terrain collision search
constexpr int64_t kiTerrainSearchSteps = 32;
constexpr float kfTerrainSearchStepPercent = 1.0f / static_cast<float>(kiTerrainSearchSteps);

// Blaster pitch
constexpr float kfPitchMin = 0.75f;
constexpr float kfPitchRandom = 0.5f;

#ifdef BT_CLIENT
// Forward declaration for registration function (called from Register())
static void RegisterTerrainEffects();

// Helper to sync owned objects for a blaster
static void XM_CALLCONV SyncBlaster(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight,
	engine::sound_t uiSound,
	FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, float fPitch)
{
	const BlastersType& rType = BlastersInterpolate::GetType(uiTypeIndex);

	// Get blaster dimensions from type
	float fWidth = rType.f2Size.x;
	float fLength = rType.f2Size.y;

	// Create velocity-aligned quad using common::CalculateArea
	XMVECTOR vecDirection = XMVector3Normalize(vecVelocity);
	auto [vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight] = common::CalculateArea(vecPosition, vecDirection, fLength, fLength, fWidth);

	// Sync area light
	engine::AreaLightsInterpolate::Sync(rFrameInterpolate, uiAreaLight,
	{
		.uiTypeIndex = rType.uiAreaLightTypeIndex,
		.vecVisiblePositions = {vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight},
	});

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
#endif

#ifdef BT_CLIENT
void BlastersInterpolate::AllocateClientObjects(Frame& rFrame, int64_t iIndex)
{
	BlastersInterpolate& rBlasters = rFrame.interpolate.blasters;
	BlastersPostRender& rPostRender = rFrame.postRender.blasters;

	const BlastersType& rType = GetType(rBlasters.puiTypeIndices[iIndex]);
	rBlasters.puiAreaLights[iIndex] = {};
	rFrame.postRender.areaLights.Add(rFrame, rBlasters.puiAreaLights[iIndex], rType.uiAreaLightTypeIndex);

	rBlasters.puiWindTrails[iIndex] = {};
	if (rBlasters.pfWindTrailIntensities[iIndex] > 0.0f)
	{
		engine::WindTrailsPostRender::Add(rFrame, rBlasters.puiWindTrails[iIndex]);
	}

	rPostRender.puiSounds[iIndex] = {};
	engine::SoundsPostRender::Add(rFrame, rPostRender.puiSounds[iIndex]);
}

void BlastersInterpolate::HydrateClientObjects(Frame& rFrame)
{
	for (int64_t i = 0; i < rFrame.interpolate.blasters.iCount; ++i)
	{
		AllocateClientObjects(rFrame, i);
	}
}
#endif

void BlastersInterpolate::Register()
{
#ifdef BT_CLIENT
	RegisterTerrainEffects();
#endif
}

#ifdef BT_CLIENT
void BlastersInterpolate::GraphicsResources()
{
}
#endif

void BlastersInterpolate::AllocateAndCopy(BlastersInterpolate& rCurrent, const BlastersInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
#ifdef BT_CLIENT
		std::memcpy(rCurrent.puiAreaLights, rPrevious.puiAreaLights, rCurrent.iCount * sizeof(rCurrent.puiAreaLights[0]));
		std::memcpy(rCurrent.puiWindTrails, rPrevious.puiWindTrails, rCurrent.iCount * sizeof(rCurrent.puiWindTrails[0]));
		std::memcpy(rCurrent.pfWindTrailIntensities, rPrevious.pfWindTrailIntensities, rCurrent.iCount * sizeof(rCurrent.pfWindTrailIntensities[0]));
		std::memcpy(rCurrent.pfWindTrailWidths, rPrevious.pfWindTrailWidths, rCurrent.iCount * sizeof(rCurrent.pfWindTrailWidths[0]));
		std::memcpy(rCurrent.pfWindTrailLengthMultipliers, rPrevious.pfWindTrailLengthMultipliers, rCurrent.iCount * sizeof(rCurrent.pfWindTrailLengthMultipliers[0]));
#endif
	}
}

#ifdef BT_CLIENT
static void RegisterTerrainEffects()
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
#endif

void BlastersInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	BlastersInterpolate& rCurrent = rCurrentFrameInterpolate.blasters;
	const BlastersInterpolate& rPrevious = rPreviousFrame.interpolate.blasters;
	const BlastersPostRender& rPreviousPostRender = rPreviousFrame.postRender.blasters;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load (type index copied in AllocateAndCopy)
		uint8_t uiTypeIndex = rCurrent.puiTypeIndices[i];

		// Update position based on velocity and delta time
		XMVECTOR vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], rPrevious.pVecPositions[i]);
		XMVECTOR vecVelocity = rPreviousPostRender.pVecVelocities[i];

		// Direction from velocity
		XMVECTOR vecDirection = XMVector3Normalize(vecVelocity);

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;

#ifdef BT_CLIENT
		// Sync owned objects
		SyncBlaster(rCurrentFrameInterpolate, rCurrent.puiAreaLights[i],
			rPreviousPostRender.puiSounds[i],
			vecPosition, vecVelocity, uiTypeIndex, rPreviousPostRender.pfPitches[i]);

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
#endif
	}

	gpProfileManager->SetCount(game::kCpuCounterBlasters, rCurrent.iCount);
}

void BlastersPostRender::AllocateAndCopy(BlastersPostRender& rCurrent, const BlastersPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
		std::memcpy(rCurrent.pVecVelocities, rPrevious.pVecVelocities, rCurrent.iCount * sizeof(rCurrent.pVecVelocities[0]));
#ifdef BT_CLIENT
		std::memcpy(rCurrent.puiSounds, rPrevious.puiSounds, rCurrent.iCount * sizeof(rCurrent.puiSounds[0]));
#endif
		std::memcpy(rCurrent.pfPitches, rPrevious.pfPitches, rCurrent.iCount * sizeof(rCurrent.pfPitches[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void BlastersPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void BlastersPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

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
		.uiCollidesWith = CollisidesWith::kBlaster,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void BlastersPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

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
		float fElevationFinal = engine::gpIslands->GlobalElevation(vecPosition);

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
				float fPossibleElevation = engine::gpIslands->GlobalElevation(vecPossibleCollisionPosition);
				if (fPossibleElevation <= XMVectorGetZ(vecPossibleCollisionPosition))
				{
					vecCollisionPosition = XMVectorSetZ(vecPossibleCollisionPosition, fPossibleElevation);
					break;
				}
			}

			// Add jitter for visual variety
			vecCollisionPosition = common::RandomPositionJitter<kfTerrainImpactJitter>(vecCollisionPosition, rFrame.postRender.randomEngine);

			// Spawn terrain effects
			float fRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
#ifdef BT_CLIENT
			engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiTerrainCraterControllerIndex, vecCollisionPosition, fRotation);
			engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiTerrainPuffControllerIndex, vecCollisionPosition);
#endif

			// Play terrain impact sound
#ifdef BT_CLIENT
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster16793__pushtobreak__earth1wavCrc, vecCollisionPosition, kfTerrainImpactVolume);
#endif
		}
	}
}

void BlastersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void BlastersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
}

void BlastersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = XMVector3Normalize(rInfo.vecVelocity);
	rCurrentInterpolate.puiTypeIndices[iIndex] = rInfo.uiTypeIndex;
#ifdef BT_CLIENT
	rCurrentInterpolate.pfWindTrailIntensities[iIndex] = rInfo.fWindTrailIntensity;
	rCurrentInterpolate.pfWindTrailWidths[iIndex] = rInfo.fWindTrailWidth;
	rCurrentInterpolate.pfWindTrailLengthMultipliers[iIndex] = rInfo.fWindTrailLengthMultiplier;
#endif

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;

	// Create sound with random pitch variation
	float fPitch = kfPitchMin + common::Random<kfPitchRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfPitches[iIndex] = fPitch;

#ifdef BT_CLIENT
	BlastersInterpolate::AllocateClientObjects(rFrame, iIndex);

	if (rCurrentInterpolate.pfWindTrailIntensities[iIndex] > 0.0f)
	{
		engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.puiWindTrails[iIndex],
		{
			.vecPosition = rInfo.vecPosition,
			.fIntensity = rCurrentInterpolate.pfWindTrailIntensities[iIndex],
			.fWidth = rCurrentInterpolate.pfWindTrailWidths[iIndex],
			.fLengthMultiplier = rCurrentInterpolate.pfWindTrailLengthMultipliers[iIndex],
		});
	}

	// Sync owned objects after Add()
	SyncBlaster(rFrame.interpolate, rCurrentInterpolate.puiAreaLights[iIndex],
		rCurrentPostRender.puiSounds[iIndex],
		rInfo.vecPosition, rInfo.vecVelocity, rInfo.uiTypeIndex, fPitch);
#endif
}

void BlastersPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kTransfer)) [[likely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Build transfer request
		TransferRequest request
		{
			.eType = StatusChangeType::kTransferBlaster,
			.data = {
				.vecPosition = vecPosition,
				.vecDirection = rCurrentInterpolate.pVecDirections[i],
				.vecVelocity = rCurrentPostRender.pVecVelocities[i],
				.alignment = rCurrentPostRender.pAlignments[i],
				.uiTypeIndex = rCurrentInterpolate.puiTypeIndices[i],
#ifdef BT_CLIENT
				.fWindTrailIntensity = rCurrentInterpolate.pfWindTrailIntensities[i],
				.fWindTrailWidth = rCurrentInterpolate.pfWindTrailWidths[i],
				.fWindTrailLengthMultiplier = rCurrentInterpolate.pfWindTrailLengthMultipliers[i],
#endif
			},
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);

		// Remove owned objects
#ifdef BT_CLIENT
		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}
		engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
#endif

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void BlastersPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	BlastersInterpolate& rCurrentInterpolate = rFrame.interpolate.blasters;
	BlastersPostRender& rCurrentPostRender = rFrame.postRender.blasters;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kDestroy)) [[likely]]
		{
			continue;
		}

#ifdef BT_CLIENT
		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}
		engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
#endif

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

bool BlastersInterpolate::operator==(const BlastersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(puiAreaLights[i], rOther.puiAreaLights[i]);
		bEqual &= common::BreakOnNotEqual(puiWindTrails[i], rOther.puiWindTrails[i]);
		bEqual &= common::BreakOnNotEqual(pfWindTrailIntensities[i], rOther.pfWindTrailIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfWindTrailWidths[i], rOther.pfWindTrailWidths[i]);
		bEqual &= common::BreakOnNotEqual(pfWindTrailLengthMultipliers[i], rOther.pfWindTrailLengthMultipliers[i]);
#endif
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
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
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(puiSounds[i], rOther.puiSounds[i]);
#endif
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

#ifdef BT_CLIENT
void BlastersInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const BlastersInterpolate& rCurrent = rFrameInterpolate.blasters;
	gpProfileManager->SetCount(game::kCpuCounterBlastersRendered, rCurrent.iCount);
}
#endif

} // namespace game
