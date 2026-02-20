#include "SmokeTrails.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

struct SmokeTrailsRenderState : RenderStateBase
{
	int64_t iRenderedCount = 0;
	bool bNeedsReset = false;
	int64_t iMinDirtyIndex = std::numeric_limits<int64_t>::max();

	XMVECTOR* pVecPreviousPositions = nullptr;
	XMVECTOR* pVecSmoothedPositions = nullptr;

	auto Members() { return std::tie(pVecPreviousPositions, pVecSmoothedPositions); }
};

static SmokeTrailsRenderState sSmokeTrailsRenderState {};

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
	int64_t iIndex = rInterpolate.IdToIndex(rId);
	sSmokeTrailsRenderState.iMinDirtyIndex = std::min(sSmokeTrailsRenderState.iMinDirtyIndex, iIndex);

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void SmokeTrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
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
	sSmokeTrailsRenderState.bNeedsReset = true;
}

void SmokeTrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const SmokeTrailsInterpolate& rCurrent = rFrameInterpolate.smokeTrails;
	gpProfileManager->SetCount(kCpuCounterSmokeTrails, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		sSmokeTrailsRenderState.iRenderedCount = 0;
		sSmokeTrailsRenderState.bNeedsReset = false;
		sSmokeTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), rCurrent.iCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}

	RenderStateEnsureCapacity(sSmokeTrailsRenderState, rCurrent.iCapacity, sSmokeTrailsRenderState.Members());

	if (sSmokeTrailsRenderState.bNeedsReset)
	{
		sSmokeTrailsRenderState.iRenderedCount = 0;
		sSmokeTrailsRenderState.bNeedsReset = false;
		sSmokeTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
	}

	if (sSmokeTrailsRenderState.iMinDirtyIndex < sSmokeTrailsRenderState.iRenderedCount)
	{
		sSmokeTrailsRenderState.iRenderedCount = sSmokeTrailsRenderState.iMinDirtyIndex;
		sSmokeTrailsRenderState.iMinDirtyIndex = std::numeric_limits<int64_t>::max();
	}

	// Initialize render state for newly added elements
	for (int64_t i = sSmokeTrailsRenderState.iRenderedCount; i < rCurrent.iCount; ++i)
	{
		sSmokeTrailsRenderState.pVecPreviousPositions[i] = rCurrent.pVecPositions[i];
		sSmokeTrailsRenderState.pVecSmoothedPositions[i] = rCurrent.pVecPositions[i];
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		sSmokeTrailsRenderState.pVecSmoothedPositions[i] = XMVectorLerp(sSmokeTrailsRenderState.pVecSmoothedPositions[i], rCurrent.pVecPositions[i], kfSmoothingFactor);
	}

	auto [pTrailLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	static common::RandomEngine sRandomEngine;
	int64_t iTrailsRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		const SmokeTrailsType& rType = SmokeTrailsInterpolate::GetType(rCurrent.puiTypeIndices[i]);
		float fIntensity = rCurrent.pfIntensities[i];
		float fWidth = rType.fWidth;
		float fStartTime = rCurrent.pfStartTimes[i];
		XMVECTOR vecPreviousPosition = sSmokeTrailsRenderState.pVecPreviousPositions[i];
		XMVECTOR vecSmoothedPosition = sSmokeTrailsRenderState.pVecSmoothedPositions[i];

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

	gpProfileManager->SetCount(kCpuCounterSmokeTrailsRendered, iTrailsRendered);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineSmoke].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iTrailsRendered);

	// Snapshot current positions for next render
	std::memcpy(sSmokeTrailsRenderState.pVecPreviousPositions, rCurrent.pVecPositions, rCurrent.iCount * sizeof(XMVECTOR));
	sSmokeTrailsRenderState.iRenderedCount = rCurrent.iCount;
}

} // namespace engine
