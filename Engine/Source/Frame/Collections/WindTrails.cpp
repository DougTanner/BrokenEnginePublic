#include "WindTrails.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Ui/WrapperBase.h"

namespace engine
{

struct WindTrailsRenderState : RenderStateBase
{
	int64_t iRenderedCount = 0;
	int64_t iMinDirtyIndex = std::numeric_limits<int64_t>::max();

	XMVECTOR* pVecPreviousPositions = nullptr;

	auto Members() { return std::tie(pVecPreviousPositions); }
};

static std::unordered_map<uint16_t, WindTrailsRenderState> sPerFrameRenderStates;
static std::vector<RenderSegment> sRenderSegments;

void WindTrailsInterpolate::Register()
{
}

void WindTrailsInterpolate::AllocateAndCopy(WindTrailsInterpolate& rCurrent, const WindTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void WindTrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	WindTrailsInterpolate& rWindTrails = rFrameInterpolate.windTrails;
	int64_t iIndex = rWindTrails.IdToIndex(id);

	rWindTrails.pVecPositions[iIndex] = rData.vecPosition;
	rWindTrails.pfIntensities[iIndex] = rData.fIntensity;
	rWindTrails.pfWidths[iIndex] = rData.fWidth;
	rWindTrails.pfLengthMultipliers[iIndex] = rData.fLengthMultiplier;
}

void WindTrailsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::AllocateAndCopy(WindTrailsPostRender& rCurrent, const WindTrailsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void WindTrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::Add(game::Frame& __restrict rFrame, wind_trail_t& rId)
{
	ASSERT(!rId.IsValid());

	WindTrailsInterpolate& rInterpolate = rFrame.interpolate.windTrails;
	WindTrailsPostRender& rPostRender = rFrame.postRender.windTrails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void WindTrailsPostRender::Remove(game::Frame& __restrict rFrame, wind_trail_t& rId)
{
	ASSERT(rId.IsValid());

	WindTrailsInterpolate& rInterpolate = rFrame.interpolate.windTrails;
	WindTrailsPostRender& rPostRender = rFrame.postRender.windTrails;

	// Flag dirty index for render thread to re-initialize (don't modify render state directly — it races with Render())
	FlagRenderStateDirty(sPerFrameRenderStates, rFrame.postRender.uiFrameId, rInterpolate.IdToIndex(rId));

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void WindTrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void WindTrailsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindTrailsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool WindTrailsInterpolate::operator==(const WindTrailsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfWidths[i], rOther.pfWidths[i]);
		bEqual &= common::BreakOnNotEqual(pfLengthMultipliers[i], rOther.pfLengthMultipliers[i]);
	}

	return bEqual;
}

bool WindTrailsPostRender::operator==(const WindTrailsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void WindTrailsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->CreateDynamicPipelineWindDeposit(kCrc, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->CreateDynamicPipelineWindDepositTwo(kCrc, kName);
}

void WindTrailsInterpolate::SetRenderSegments(const RenderSegment* pSegments, int64_t iSegmentCount)
{
	sRenderSegments.assign(pSegments, pSegments + iSegmentCount);
}

void WindTrailsInterpolate::ResetRenderState()
{
	sPerFrameRenderStates.clear();
	sRenderSegments.clear();
}

void WindTrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const WindTrailsInterpolate& rCurrent = rFrameInterpolate.windTrails;

	if (!gWind.Get<bool>() || rCurrent.iCount == 0)
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDeposit].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositTwo].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), rCurrent.iCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDeposit].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositTwo].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}

	auto [pQuadLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iRendered = 0;

	for (const RenderSegment& rRenderSegment : sRenderSegments)
	{
		// Heap: RenderStateEnsureCapacity may grow per-frame render state vectors when entity count increases
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		WindTrailsRenderState& rWindTrailsRenderState = sPerFrameRenderStates[rRenderSegment.uiFrameId];
		RenderStateEnsureCapacity(rWindTrailsRenderState, rRenderSegment.iCapacity, rWindTrailsRenderState.Members());

		// Handle dirty index from Remove()
		if (rWindTrailsRenderState.iMinDirtyIndex < rWindTrailsRenderState.iRenderedCount)
		{
			rWindTrailsRenderState.iRenderedCount = rWindTrailsRenderState.iMinDirtyIndex;
			rWindTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
		}

		// Auto-init new elements (replaces bFirstSync)
		for (int64_t i = rWindTrailsRenderState.iRenderedCount; i < rRenderSegment.iCount; ++i)
		{
			rWindTrailsRenderState.pVecPreviousPositions[i] = rCurrent.pVecPositions[rRenderSegment.iOffset + i];
		}

		// Build GPU quads
		for (int64_t i = 0; i < rRenderSegment.iCount; ++i)
		{
			int64_t iMerged = rRenderSegment.iOffset + i;

			// Load from merged data and per-frame render state
			XMVECTOR vecPosition = rCurrent.pVecPositions[iMerged];
			float fIntensity = rCurrent.pfIntensities[iMerged];
			float fWidth = rCurrent.pfWidths[iMerged];

			// Visibility culling
			XMFLOAT4A f4Position {};
			if (!IsPointVisible(vecPosition, f4Position))
			{
				continue;
			}

			// Directional: oriented quad from previous to current position
			XMVECTOR vecPreviousPosition = rWindTrailsRenderState.pVecPreviousPositions[i];
			float fLengthMultiplier = rCurrent.pfLengthMultipliers[iMerged];

			// Project to base height
			XMVECTOR vecBasePosition = ProjectToBaseHeight(vecPosition);
			XMVECTOR vecBasePreviousPosition = ProjectToBaseHeight(vecPreviousPosition);

			// Calculate direction from previous to current, scaled by length multiplier
			XMVECTOR vecDirection = vecBasePosition - vecBasePreviousPosition;
			vecDirection = vecDirection * fLengthMultiplier;
			vecBasePreviousPosition = vecBasePosition - vecDirection;
			float fDistance = XMVectorGetX(XMVector3Length(vecDirection));
			if (fDistance <= 0.001f)
			{
				continue;
			}

			XMVECTOR vecDirNormal = XMVector3Normalize(vecDirection);

			// Calculate perpendicular direction for width
			XMVECTOR vecPerpNormal = XMVector3Normalize(XMVector3Cross(vecDirNormal, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

			// Build oriented quad: front (current) ± width, back (previous) ± width
			XMVECTOR vecFrontLeft = vecBasePosition + fWidth * vecPerpNormal;
			XMVECTOR vecFrontRight = vecBasePosition - fWidth * vecPerpNormal;
			XMVECTOR vecBackLeft = vecBasePreviousPosition + fWidth * vecPerpNormal;
			XMVECTOR vecBackRight = vecBasePreviousPosition - fWidth * vecPerpNormal;

			// Wind direction from motion
			float fWindDirX = XMVectorGetX(vecDirNormal);
			float fWindDirY = XMVectorGetY(vecDirNormal);

			// Build QuadLayout vertices
			XMFLOAT4A f4Vertex {};

			XMStoreFloat4A(&f4Vertex, vecFrontLeft);
			pQuadLayouts[iRendered].pf4VerticesTexcoords[0] = {f4Vertex.x, f4Vertex.y, 0.0f, 1.0f};
			XMStoreFloat4A(&f4Vertex, vecFrontRight);
			pQuadLayouts[iRendered].pf4VerticesTexcoords[1] = {f4Vertex.x, f4Vertex.y, 1.0f, 1.0f};
			XMStoreFloat4A(&f4Vertex, vecBackLeft);
			pQuadLayouts[iRendered].pf4VerticesTexcoords[2] = {f4Vertex.x, f4Vertex.y, 0.0f, 0.0f};
			XMStoreFloat4A(&f4Vertex, vecBackRight);
			pQuadLayouts[iRendered].pf4VerticesTexcoords[3] = {f4Vertex.x, f4Vertex.y, 1.0f, 0.0f};

			// Per-vertex params: {magnitude, windDirX, windDirY, 0}
			XMFLOAT4 f4Params = {fIntensity, fWindDirX, fWindDirY, 0.0f};
			pQuadLayouts[iRendered].pf4Params[0] = f4Params;
			pQuadLayouts[iRendered].pf4Params[1] = f4Params;
			pQuadLayouts[iRendered].pf4Params[2] = f4Params;
			pQuadLayouts[iRendered].pf4Params[3] = f4Params;

			pQuadLayouts[iRendered].f4Params = {};
			pQuadLayouts[iRendered].uiColor = 0xFFFFFFFF;

			++iRendered;
		}

		// Snapshot per-frame positions for next render
		SnapshotRenderState(rWindTrailsRenderState, &rCurrent.pVecPositions[rRenderSegment.iOffset], rRenderSegment.iCount);
	}

	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDeposit].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? iRendered : 0);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositTwo].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? iRendered : 0);
}

} // namespace engine
