#include "SmokeTrails.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

struct SmokeTrailsRenderState : RenderStateBase
{
	int64_t iRenderedCount = 0;
	int64_t iMinDirtyIndex = std::numeric_limits<int64_t>::max();

	XMVECTOR* pVecPreviousPositions = nullptr;
	XMVECTOR* pVecSmoothedPositions = nullptr;

	auto Members() { return std::tie(pVecPreviousPositions, pVecSmoothedPositions); }
};

static std::unordered_map<uint16_t, SmokeTrailsRenderState> sPerFrameRenderStates;
static std::vector<RenderSegment> sRenderSegments;

// Rendering
constexpr float kfSmoothingFactor = 0.15f;

void SmokeTrailsInterpolate::Register()
{
}

void SmokeTrailsInterpolate::AllocateAndCopy(SmokeTrailsInterpolate& rCurrent, const SmokeTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
	}
}

void SmokeTrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	SmokeTrailsInterpolate& rSmokeTrails = rFrameInterpolate.smokeTrails;
	int64_t iIndex = rSmokeTrails.IdToIndex(id);

	rSmokeTrails.pVecPositions[iIndex] = rData.vecPosition;
	rSmokeTrails.pfIntensities[iIndex] = rData.fIntensity;
}

void SmokeTrailsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Smoothing is handled in Render() using static render state
}

void SmokeTrailsPostRender::AllocateAndCopy(SmokeTrailsPostRender& rCurrent, const SmokeTrailsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void SmokeTrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfStartTimes[uiSpawnIndex] = rFrame.interpolate.fCurrentTime;
}

void SmokeTrailsPostRender::Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId)
{
	ASSERT(rId.IsValid());

	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	// Flag dirty index for render thread to re-initialize (don't modify render state directly — it races with Render())
	FlagRenderStateDirty(sPerFrameRenderStates, rFrame.postRender.uiFrameId, rInterpolate.IdToIndex(rId));

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void SmokeTrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void SmokeTrailsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void SmokeTrailsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool SmokeTrailsInterpolate::operator==(const SmokeTrailsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
	}

	return bEqual;
}

bool SmokeTrailsPostRender::operator==(const SmokeTrailsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void SmokeTrailsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->CreateDynamicPipelineSmoke(kCrc, kName, sizeof(shaders::QuadLayout));
}

void SmokeTrailsInterpolate::SetRenderSegments(const RenderSegment* pSegments, int64_t iSegmentCount)
{
	sRenderSegments.assign(pSegments, pSegments + iSegmentCount);
}

void SmokeTrailsInterpolate::ResetRenderState()
{
	sPerFrameRenderStates.clear();
	sRenderSegments.clear();
}

void SmokeTrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const SmokeTrailsInterpolate& rCurrent = rFrameInterpolate.smokeTrails;
	gpProfileManager->SetCount(kCpuCounterSmokeTrails, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), rCurrent.iCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}

	auto [pTrailLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	static common::RandomEngine sRandomEngine;
	int64_t iTrailsRendered = 0;

	for (const RenderSegment& rRenderSegment : sRenderSegments)
	{
		// Heap: RenderStateEnsureCapacity may grow per-frame render state vectors when entity count increases
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		SmokeTrailsRenderState& rSmokeTrailsRenderState = sPerFrameRenderStates[rRenderSegment.uiFrameId];
		RenderStateEnsureCapacity(rSmokeTrailsRenderState, rRenderSegment.iCapacity, rSmokeTrailsRenderState.Members());

		// Handle dirty index from Remove()
		if (rSmokeTrailsRenderState.iMinDirtyIndex < rSmokeTrailsRenderState.iRenderedCount)
		{
			rSmokeTrailsRenderState.iRenderedCount = rSmokeTrailsRenderState.iMinDirtyIndex;
			rSmokeTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
		}

		// Initialize render state for newly added elements (per-frame index)
		for (int64_t i = rSmokeTrailsRenderState.iRenderedCount; i < rRenderSegment.iCount; ++i)
		{
			rSmokeTrailsRenderState.pVecPreviousPositions[i] = rCurrent.pVecPositions[rRenderSegment.iOffset + i];
			rSmokeTrailsRenderState.pVecSmoothedPositions[i] = rCurrent.pVecPositions[rRenderSegment.iOffset + i];
		}

		// Smooth all positions
		for (int64_t i = 0; i < rRenderSegment.iCount; ++i)
		{
			rSmokeTrailsRenderState.pVecSmoothedPositions[i] = XMVectorLerp(rSmokeTrailsRenderState.pVecSmoothedPositions[i], rCurrent.pVecPositions[rRenderSegment.iOffset + i], kfSmoothingFactor);
		}

		// Build GPU quads
		for (int64_t i = 0; i < rRenderSegment.iCount; ++i)
		{
			int64_t iMerged = rRenderSegment.iOffset + i;

			// Load from merged data and per-frame render state
			XMVECTOR vecPosition = rCurrent.pVecPositions[iMerged];
			const SmokeTrailsType& rType = SmokeTrailsInterpolate::GetType(rCurrent.puiTypeIndices[iMerged]);
			float fIntensity = rCurrent.pfIntensities[iMerged];
			float fWidth = rType.fWidth;
			float fStartTime = rCurrent.pfStartTimes[iMerged];
			XMVECTOR vecPreviousPosition = rSmokeTrailsRenderState.pVecPreviousPositions[i];
			XMVECTOR vecSmoothedPosition = rSmokeTrailsRenderState.pVecSmoothedPositions[i];

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

		// Snapshot per-frame positions for next render
		SnapshotRenderState(rSmokeTrailsRenderState, &rCurrent.pVecPositions[rRenderSegment.iOffset], rRenderSegment.iCount);
	}

	gpProfileManager->SetCount(kCpuCounterSmokeTrailsRendered, iTrailsRendered);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iTrailsRendered);
}

} // namespace engine
