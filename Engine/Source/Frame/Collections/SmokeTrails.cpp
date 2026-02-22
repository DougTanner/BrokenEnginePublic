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
static common::Timer sRenderTimer;
static int64_t siRendered = 0;
static int64_t siTotalCount = 0;
static float sfRenderDeltaTime = 0.0f;

// Rendering
constexpr float kfSmoothingRate = 10.4f;

void SmokeTrailsInterpolate::Register()
{
}

void SmokeTrailsInterpolate::AllocateAndCopy(SmokeTrailsInterpolate& rCurrent, const SmokeTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.pfIntensities, rPrevious.pfIntensities, rCurrent.iCount * sizeof(rCurrent.pfIntensities[0]));
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

void SmokeTrailsInterpolate::ResetRenderState()
{
	sPerFrameRenderStates.clear();
	sRenderTimer.Reset();
}

void SmokeTrailsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;
	siTotalCount = 0;
	sfRenderDeltaTime = common::NanosecondsToFloatSeconds<float>(sRenderTimer.GetDeltaNs(true));

	int64_t iTotalCapacity = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.smokeTrails.iCapacity;
		}
	}

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
}

void SmokeTrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer, uint16_t uiFrameId)
{
	const SmokeTrailsInterpolate& rCurrent = rFrameInterpolate.smokeTrails;
	siTotalCount += rCurrent.iCount;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	auto [pTrailLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	static common::RandomEngine sRandomEngine;

	{
		// Heap: RenderStateEnsureCapacity may grow per-frame render state vectors when entity count increases
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		SmokeTrailsRenderState& rSmokeTrailsRenderState = sPerFrameRenderStates[uiFrameId];
		RenderStateEnsureCapacity(rSmokeTrailsRenderState, rCurrent.iCapacity, rSmokeTrailsRenderState.Members());

		// Handle dirty index from Remove()
		if (rSmokeTrailsRenderState.iMinDirtyIndex < rSmokeTrailsRenderState.iRenderedCount)
		{
			rSmokeTrailsRenderState.iRenderedCount = rSmokeTrailsRenderState.iMinDirtyIndex;
			rSmokeTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
		}

		// Initialize render state for newly added elements (per-frame index)
		for (int64_t i = rSmokeTrailsRenderState.iRenderedCount; i < rCurrent.iCount; ++i)
		{
			rSmokeTrailsRenderState.pVecPreviousPositions[i] = rCurrent.pVecPositions[i];
			rSmokeTrailsRenderState.pVecSmoothedPositions[i] = rCurrent.pVecPositions[i];
		}

		// Smooth all positions
		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			rSmokeTrailsRenderState.pVecSmoothedPositions[i] = XMVectorLerp(rSmokeTrailsRenderState.pVecSmoothedPositions[i], rCurrent.pVecPositions[i], common::ExponentialInterpolant(kfSmoothingRate, sfRenderDeltaTime));
		}

		// Build GPU quads
		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			// Load from merged data and per-frame render state
			XMVECTOR vecPosition = rCurrent.pVecPositions[i];
			const SmokeTrailsType& rType = SmokeTrailsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
			float fIntensity = rCurrent.pfIntensities[i];
			float fWidth = rType.fWidth;
			float fStartTime = rCurrent.pfStartTimes[i];
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

			// Project current and smoothed positions to base height
			XMVECTOR vecBasePosition = ProjectToBaseHeight(vecPosition);
			XMVECTOR vecBaseSmoothedPosition = ProjectToBaseHeight(vecSmoothedPosition);

			// Calculate direction from smoothed to current
			XMVECTOR vecToSmoothed = vecBasePosition - vecBaseSmoothedPosition;
			float fLengthScale = XMVectorGetX(XMVector3Length(vecToSmoothed));
			if (fLengthScale <= 0.01f)
			{
				continue;
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

			XMVECTOR vecPointThree = vecBaseSmoothedPosition + gSmokeTrailsWidthPrevious.Get() * fJitterOne * vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;
			XMVECTOR vecPointFour = vecBaseSmoothedPosition + gSmokeTrailsWidthPrevious.Get() * fJitterTwo * -vecLeftNormal - fLength * fLengthScale * vecToSmoothedNormal;

			// Build QuadLayout (4 vertices with texcoords)
			XMStoreFloat4A(&f4Position, vecPointOne);
			pTrailLayouts[siRendered].pf4VerticesTexcoords[0] = {f4Position.x, f4Position.y, 0.0f, 0.0f};
			XMStoreFloat4A(&f4Position, vecPointTwo);
			pTrailLayouts[siRendered].pf4VerticesTexcoords[1] = {f4Position.x, f4Position.y, 1.0f, 0.0f};
			XMStoreFloat4A(&f4Position, vecPointThree);
			pTrailLayouts[siRendered].pf4VerticesTexcoords[2] = {f4Position.x, f4Position.y, 0.0f, 1.0f};
			XMStoreFloat4A(&f4Position, vecPointFour);
			pTrailLayouts[siRendered].pf4VerticesTexcoords[3] = {f4Position.x, f4Position.y, 1.0f, 1.0f};

			float fQuantity = fIntensity * gSmokeTrailsQuantity.Get() / fLengthScale;
			pTrailLayouts[siRendered].pf4Params[0] = {fQuantity, 1.0f, 0.0f, 0.0f};
			pTrailLayouts[siRendered].pf4Params[1] = {fQuantity, 1.0f, 0.0f, 0.0f};
			pTrailLayouts[siRendered].pf4Params[2] = {fQuantity, 0.0f, 0.0f, 0.0f};
			pTrailLayouts[siRendered].pf4Params[3] = {fQuantity, 0.0f, 0.0f, 0.0f};

			pTrailLayouts[siRendered].f4Params = {};
			pTrailLayouts[siRendered].uiColor = rType.uiColor;

			++siRendered;
		}

		// Snapshot per-frame positions for next render
		SnapshotRenderState(rSmokeTrailsRenderState, &rCurrent.pVecPositions[0], rCurrent.iCount);
	}
}

void SmokeTrailsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterSmokeTrails, siTotalCount);
	gpProfileManager->SetCount(kCpuCounterSmokeTrailsRendered, siRendered);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, siRendered);
}

} // namespace engine
