// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Blasters.h"

#include "Frame/FrameStaticData.h"
#include "Data/Audio.h"

namespace engine
{
template struct Collection<game::BlastersInterpolate>;
template struct Collection<game::BlastersPostRender>;
}

namespace game
{

using enum BlasterFlags;

#if defined(BT_CLIENT)
// Defined in BlastersUpdate.cpp
void RegisterBlasterTerrainEffects();
#endif

void BlastersInterpolate::Register()
{
#if defined(BT_CLIENT)
	RegisterBlasterTerrainEffects();
#endif
}

void BlastersInterpolate::AllocateAndCopy(BlastersInterpolate& rCurrent, const BlastersInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiAreaLights, rPrevious.puiAreaLights, rCurrent.iCount * sizeof(rCurrent.puiAreaLights[0]));
		std::memcpy(rCurrent.puiPointLights, rPrevious.puiPointLights, rCurrent.iCount * sizeof(rCurrent.puiPointLights[0]));
		std::memcpy(rCurrent.puiWindTrails, rPrevious.puiWindTrails, rCurrent.iCount * sizeof(rCurrent.puiWindTrails[0]));
		std::memcpy(rCurrent.pfWindTrailIntensities, rPrevious.pfWindTrailIntensities, rCurrent.iCount * sizeof(rCurrent.pfWindTrailIntensities[0]));
		std::memcpy(rCurrent.pfWindTrailWidths, rPrevious.pfWindTrailWidths, rCurrent.iCount * sizeof(rCurrent.pfWindTrailWidths[0]));
		std::memcpy(rCurrent.pfWindTrailLengthMultipliers, rPrevious.pfWindTrailLengthMultipliers, rCurrent.iCount * sizeof(rCurrent.pfWindTrailLengthMultipliers[0]));
#endif
	}
}

void BlastersPostRender::AllocateAndCopy(BlastersPostRender& rCurrent, const BlastersPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
		std::memcpy(rCurrent.pVecVelocities, rPrevious.pVecVelocities, rCurrent.iCount * sizeof(rCurrent.pVecVelocities[0]));
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiSounds, rPrevious.puiSounds, rCurrent.iCount * sizeof(rCurrent.puiSounds[0]));
#endif
		std::memcpy(rCurrent.pfPitches, rPrevious.pfPitches, rCurrent.iCount * sizeof(rCurrent.pfPitches[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

#if defined(BT_CLIENT)
void BlastersInterpolate::ClientInit(Frame& rFrame, int64_t iIndex)
{
	BlastersInterpolate& rBlasters = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rPostRender = *rFrame.postRender.pBlasters;

	// Add client-only owned objects
	const BlastersType& rType = GetType(rBlasters.puiTypeIndices[iIndex]);

	rBlasters.puiAreaLights[iIndex] = {};
	rBlasters.puiPointLights[iIndex] = {};

	if (rType.uiPointLightTypeIndex != 0xFF)
	{
		engine::PointLightsPostRender::Add(rFrame, rBlasters.puiPointLights[iIndex], rType.uiPointLightTypeIndex);
	}
	else
	{
		rFrame.postRender.areaLights.Add(rFrame, rBlasters.puiAreaLights[iIndex], rType.uiAreaLightTypeIndex);
	}

	rBlasters.puiWindTrails[iIndex] = {};
	if (rBlasters.pfWindTrailIntensities[iIndex] > 0.0f)
	{
		engine::WindTrailsPostRender::Add(rFrame, rBlasters.puiWindTrails[iIndex]);
	}

	rPostRender.puiSounds[iIndex] = {};
	engine::SoundsPostRender::Add(rFrame, rPostRender.puiSounds[iIndex]);

	// Sync wind trail
	if (rBlasters.pfWindTrailIntensities[iIndex] > 0.0f)
	{
		engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rBlasters.puiWindTrails[iIndex],
		{
			.vecPosition = rBlasters.pVecPositions[iIndex],
			.fIntensity = rBlasters.pfWindTrailIntensities[iIndex],
			.fWidth = rBlasters.pfWindTrailWidths[iIndex],
			.fLengthMultiplier = rBlasters.pfWindTrailLengthMultipliers[iIndex],
		});
	}

	// Sync light
	if (rBlasters.puiPointLights[iIndex].IsValid())
	{
		float fSize = rType.f2Size.x;
		const engine::PointLightsType& rPointLightType = engine::PointLightsInterpolate::GetType(rType.uiPointLightTypeIndex);
		engine::PointLightsInterpolate::Sync(rFrame.interpolate, rBlasters.puiPointLights[iIndex],
		{
			.vecPosition = rBlasters.pVecPositions[iIndex],
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

		XMVECTOR vecDirection = rBlasters.pVecDirections[iIndex];
		auto [vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight] = common::CalculateArea(rBlasters.pVecPositions[iIndex], vecDirection, fLength, fLength, fWidth);

		engine::AreaLightsInterpolate::Sync(rFrame.interpolate, rBlasters.puiAreaLights[iIndex],
		{
			.uiTypeIndex = rType.uiAreaLightTypeIndex,
			.vecVisiblePositions = {vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight},
		});
	}

	// Sync sound
	// DT: TEMP kAudioBlasterNew609840__eminyildirim__spacedroneambience7variation_0wavCrc sounds bad
	// engine::SoundsInterpolate::Sync(rFrame.interpolate, rPostRender.puiSounds[iIndex],
	// {
	// 	.vecPosition = rBlasters.pVecPositions[iIndex],
	// 	.vecVelocity = rPostRender.pVecVelocities[iIndex],
	// 	.uiCrc = data::kAudioBlasterNew609840__eminyildirim__spacedroneambience7variation_0wavCrc,
	// 	.fVolume = kfBlasterVolume,
	// 	.fPitch = rPostRender.pfPitches[iIndex],
	// 	.fFadeOutTime = kfBlasterFadeOutTime,
	// });
}

void BlastersInterpolate::ClientInitAll(Frame& rFrame)
{
	for (int64_t i = 0; i < rFrame.interpolate.pBlasters->iCount; ++i)
	{
		ClientInit(rFrame, i);
	}
}
#endif // BT_CLIENT

void BlastersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void BlastersPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	common::ValidateVector<true >(rInfo.vecPosition);
	common::ValidateVector<false>(rInfo.vecVelocity);

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = XMVector3Normalize(rInfo.vecVelocity);
	rCurrentInterpolate.puiTypeIndices[iIndex] = rInfo.uiTypeIndex;
#if defined(BT_CLIENT)
	rCurrentInterpolate.pfWindTrailIntensities[iIndex] = rInfo.fWindTrailIntensity;
	rCurrentInterpolate.pfWindTrailWidths[iIndex] = rInfo.fWindTrailWidth;
	rCurrentInterpolate.pfWindTrailLengthMultipliers[iIndex] = rInfo.fWindTrailLengthMultiplier;
#endif

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;

	// Create sound with random pitch variation
	float fPitch = kfBlasterPitchMin + common::Random<kfBlasterPitchRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfPitches[iIndex] = fPitch;

#if defined(BT_CLIENT)
	BlastersInterpolate::ClientInit(rFrame, iIndex);
#endif
}

static void RemoveOwnedObjects([[maybe_unused]] Frame& rFrame, [[maybe_unused]] BlastersInterpolate& rCurrentInterpolate, [[maybe_unused]] BlastersPostRender& rCurrentPostRender, [[maybe_unused]] int64_t i)
{
#if defined(BT_CLIENT)
	if (rCurrentInterpolate.puiPointLights[i].IsValid())
	{
		engine::PointLightsPostRender::Remove(rFrame, rCurrentInterpolate.puiPointLights[i]);
	}
	else
	{
		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
	}
	if (rCurrentInterpolate.puiWindTrails[i].IsValid())
	{
		engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
	}
	engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
#endif // BT_CLIENT
}

void BlastersPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

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
#if defined(BT_CLIENT)
				.fWindTrailIntensity = rCurrentInterpolate.pfWindTrailIntensities[i],
				.fWindTrailWidth = rCurrentInterpolate.pfWindTrailWidths[i],
				.fWindTrailLengthMultiplier = rCurrentInterpolate.pfWindTrailLengthMultipliers[i],
#endif
			},
			.iPushedTick = rFrame.interpolate.iTick,
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		// Heap realloc warning: capacity exceeded during burst transfers. Expected max ~1-2/tick
		// per source frame — anything higher suggests entities are re-flagging kTransfer across
		// iterations, DestroyElement isn't removing them, or there's an unexpected push path.
		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			LOG(kDefault, kError,
				"Blaster Transfer capacity hit Tick: {} Source: ({},{}) Index: {} Position: {} Velocity: {} Delta: ({},{}) TypeIndex: {} Alignment: {} SourceCount: {} Pushed: {} Capacity: {}",
				rFrame.interpolate.iTick,
				rStaticData.coord.x, rStaticData.coord.y,
				i,
				common::WbV2(vecPosition, 1),
				common::WbV2(rCurrentPostRender.pVecVelocities[i], 1),
				static_cast<int32_t>(request.iDeltaX), static_cast<int32_t>(request.iDeltaY),
				static_cast<int32_t>(rCurrentInterpolate.puiTypeIndices[i]),
				rCurrentPostRender.pAlignments[i],
				rCurrentInterpolate.iCount,
				rFrame.postRender.transferRequests.size(),
				rFrame.postRender.transferRequests.capacity());
			DEBUG_BREAK();
		}
		common::ValidateVector<true >(request.data.vecPosition);
		common::ValidateVector<false>(request.data.vecDirection);
		common::ValidateVector<false>(request.data.vecVelocity);
		rFrame.postRender.transferRequests.push_back(request);

		RemoveOwnedObjects(rFrame, rCurrentInterpolate, rCurrentPostRender, i);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void BlastersPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	BlastersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pBlasters;
	BlastersPostRender& rCurrentPostRender = *rFrame.postRender.pBlasters;

	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kDestroy)) [[likely]]
		{
			continue;
		}

		RemoveOwnedObjects(rFrame, rCurrentInterpolate, rCurrentPostRender, i);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

bool BlastersInterpolate::LogDifferences(const BlastersInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("BlastersInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"puiTypeIndices">(i, puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
	}

	return bEqual;
}

bool BlastersPostRender::LogDifferences(const BlastersPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("BlastersPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference_Vec("pVecVelocities", i, pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::LogDifference<"pfPitches">(i, pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

} // namespace game
