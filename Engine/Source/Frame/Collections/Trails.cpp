#include "Trails.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void TrailsInterpolate::Register()
{
}

void TrailsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void TrailsInterpolate::AllocateAndCopy(TrailsInterpolate& rCurrent, const TrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
		std::memcpy(rCurrent.pVecPreviousPositions, rPrevious.pVecPreviousPositions, rCurrent.iCount * sizeof(rCurrent.pVecPreviousPositions[0]));
		std::memcpy(rCurrent.pVecSmoothedPositions, rPrevious.pVecSmoothedPositions, rCurrent.iCount * sizeof(rCurrent.pVecSmoothedPositions[0]));
	}
}

void TrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, const game::FrameInterpolate& rPreviousInterpolate, id_t id, const SyncData& rData, bool bFirstSync)
{
	TrailsInterpolate& rTrails = rFrameInterpolate.trails;
	int64_t iIndex = rTrails.IdToIndex(id);

	// Write position and intensity from owner
	rTrails.pVecPositions[iIndex] = rData.vecPosition;
	rTrails.pfIntensities[iIndex] = rData.fIntensity;

	if (bFirstSync)
	{
		// First sync - initialize all smoothing state to current position
		rTrails.pVecPreviousPositions[iIndex] = rData.vecPosition;
		rTrails.pVecSmoothedPositions[iIndex] = rData.vecPosition;
	}
	else
	{
		// Subsequent syncs - compute smoothing from previous frame
		const TrailsInterpolate& rPrevious = rPreviousInterpolate.trails;
		int64_t iPrevIndex = rPrevious.IdToIndex(id);

		static constexpr float kfSmoothingFactor = 0.15f;

		XMVECTOR vecPreviousPosition = rPrevious.pVecPositions[iPrevIndex];
		XMVECTOR vecSmoothedPosition = rPrevious.pVecSmoothedPositions[iPrevIndex];

		// Smoothed position gradually approaches current for stable direction
		vecSmoothedPosition = XMVectorLerp(vecSmoothedPosition, rData.vecPosition, kfSmoothingFactor);

		rTrails.pVecPreviousPositions[iIndex] = vecPreviousPosition;
		rTrails.pVecSmoothedPositions[iIndex] = vecSmoothedPosition;
	}
}

void TrailsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Smoothing is now handled in Sync() when owner provides position
	gpProfileManager->SetCount(kCpuCounterTrails, rFrameInterpolate.trails.iCount);
}

void TrailsPostRender::AllocateAndCopy(TrailsPostRender& rCurrent, const TrailsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void TrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void TrailsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void TrailsPostRender::Add(game::Frame& __restrict rFrame, trails_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	TrailsInterpolate& rInterpolate = rFrame.interpolate.trails;
	TrailsPostRender& rPostRender = rFrame.postRender.trails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfStartTimes[uiSpawnIndex] = rFrame.interpolate.fCurrentTime;
}

void TrailsPostRender::Remove(game::Frame& __restrict rFrame, trails_t& rId)
{
	ASSERT(rId.IsValid());

	TrailsInterpolate& rInterpolate = rFrame.interpolate.trails;
	TrailsPostRender& rPostRender = rFrame.postRender.trails;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void TrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void TrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void TrailsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void TrailsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool TrailsInterpolate::operator==(const TrailsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecPreviousPositions[i], rOther.pVecPreviousPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecSmoothedPositions[i], rOther.pVecSmoothedPositions[i]);
	}

	return bEqual;
}

bool TrailsPostRender::operator==(const TrailsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void TrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const TrailsInterpolate& rCurrent = rFrameInterpolate.trails;
	gpProfileManager->SetCount(kCpuCounterTrails, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	auto [pTrailLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	static common::RandomEngine sRandomEngine;
	int64_t iTrailsRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const TrailsType& rType = TrailsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fIntensity = rCurrent.pfIntensities[i];
		float fWidth = rType.fWidth;
		float fStartTime = rCurrent.pfStartTimes[i];
		XMVECTOR vecPreviousPosition = rCurrent.pVecPreviousPositions[i];
		XMVECTOR vecSmoothedPosition = rCurrent.pVecSmoothedPositions[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

		// Calculate jitter for visual variation
		float fJitterOne = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterOne = fJitterOne * fJitterOne;
		float fJitterTwo = gSmokeTrailsSideJitter.Get() * common::Random(sRandomEngine);
		fJitterTwo = fJitterTwo * fJitterTwo;

		// Project current and previous positions to base height
		XMVECTOR vecBasePosition = ProjectToBaseHeight(vecPosition);
		XMVECTOR vecBasePreviousPosition = ProjectToBaseHeight(vecPreviousPosition);

		// Calculate direction from previous to current
		XMVECTOR vecToPrevious = vecBasePosition - vecBasePreviousPosition;
		float fLengthScale = XMVectorGetX(XMVector3Length(vecToPrevious));
		if (fLengthScale <= 0.01f)
		{
			continue;
		}

		// Calculate smoothed direction
		XMVECTOR vecToSmoothed = vecBasePosition - vecSmoothedPosition;
		if (XMVectorGetX(XMVector3Length(vecToSmoothed)) <= 0.01f)
		{
			vecToSmoothed = vecToPrevious;
		}
		XMVECTOR vecToSmoothedNormal = XMVector3Normalize(vecToSmoothed);

		// Calculate perpendicular (left) direction for width
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecToSmoothedNormal, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		// Calculate quad corners
		XMVECTOR vecPointOne = vecBasePosition + gSmokeTrailsWidthCurrent.Get() * fWidth * vecLeftNormal;
		XMVECTOR vecPointTwo = vecBasePosition + gSmokeTrailsWidthCurrent.Get() * fWidth * -vecLeftNormal;

		float fLength = gSmokeTrailsLength.Get() + gSmokeTrailsLengthJitter.Get() * common::Random(sRandomEngine);
		if (rFrameInterpolate.fCurrentTime - fStartTime < 0.05f)
		{
			fLength = 0.0f;
		}

		XMVECTOR vecPointThree = vecBasePreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterOne * vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;
		XMVECTOR vecPointFour = vecBasePreviousPosition + gSmokeTrailsWidthPrevious.Get() * fJitterTwo * -vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;

		// Build QuadLayout (4 vertices with texcoords)
		XMStoreFloat4A(&f4Position, vecPointOne);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[0] = {f4Position.x, f4Position.y, 0.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointTwo);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[1] = {f4Position.x, f4Position.y, 1.0f, 0.0f};
		XMStoreFloat4A(&f4Position, vecPointThree);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[2] = {f4Position.x, f4Position.y, 0.0f, 1.0f};
		XMStoreFloat4A(&f4Position, vecPointFour);
		pTrailLayouts[iTrailsRendered].pf4VerticesTexcoords[3] = {f4Position.x, f4Position.y, 1.0f, 1.0f};

		float fQuantity = fIntensity * gSmokeTrailsQuantity.Get() / fLengthScale;
		pTrailLayouts[iTrailsRendered].pf4Params[0] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Params[1] = {fQuantity, 1.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Params[2] = {fQuantity, 0.0f, 0.0f, 0.0f};
		pTrailLayouts[iTrailsRendered].pf4Params[3] = {fQuantity, 0.0f, 0.0f, 0.0f};

		pTrailLayouts[iTrailsRendered].f4Params = {};
		pTrailLayouts[iTrailsRendered].uiColor = rType.uiColor;

		++iTrailsRendered;
	}

	gpProfileManager->SetCount(kCpuCounterTrailsRendered, iTrailsRendered);
	WritePipelineIndirectBuffers(iCommandBuffer, iTrailsRendered);
}

} // namespace engine
