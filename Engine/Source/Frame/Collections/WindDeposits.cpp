#include "WindDeposits.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/BufferManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"

namespace engine
{

struct WindDepositsRenderState : RenderStateBase
{
	XMVECTOR* pVecPreviousPositions = nullptr;

	auto Members() { return std::tie(pVecPreviousPositions); }
};

static WindDepositsRenderState sWindDepositsRenderState {};

void WindDepositsInterpolate::Register()
{
}

void WindDepositsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

void WindDepositsInterpolate::AllocateAndCopy(WindDepositsInterpolate& rCurrent, const WindDepositsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void WindDepositsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData, bool bFirstSync)
{
	WindDepositsInterpolate& rWindDeposits = rFrameInterpolate.windDeposits;
	int64_t iIndex = rWindDeposits.IdToIndex(id);

	// Write position, intensity, width, and length multiplier from owner
	rWindDeposits.pVecPositions[iIndex] = rData.vecPosition;
	rWindDeposits.pfIntensities[iIndex] = rData.fIntensity;
	rWindDeposits.pfWidths[iIndex] = rData.fWidth;
	rWindDeposits.pfLengthMultipliers[iIndex] = rData.fLengthMultiplier;

	if (bFirstSync)
	{
		// First sync - no trail on first frame
		RenderStateEnsureCapacity(sWindDepositsRenderState, rWindDeposits.iCapacity, sWindDepositsRenderState.Members());
		sWindDepositsRenderState.pVecPreviousPositions[iIndex] = rData.vecPosition;
	}
}

void WindDepositsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindDepositsPostRender::AllocateAndCopy(WindDepositsPostRender& rCurrent, const WindDepositsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void WindDepositsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindDepositsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindDepositsPostRender::Add(game::Frame& __restrict rFrame, wind_deposit_t& rId)
{
	ASSERT(!rId.IsValid());

	WindDepositsInterpolate& rInterpolate = rFrame.interpolate.windDeposits;
	WindDepositsPostRender& rPostRender = rFrame.postRender.windDeposits;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void WindDepositsPostRender::Remove(game::Frame& __restrict rFrame, wind_deposit_t& rId)
{
	ASSERT(rId.IsValid());

	WindDepositsInterpolate& rInterpolate = rFrame.interpolate.windDeposits;
	WindDepositsPostRender& rPostRender = rFrame.postRender.windDeposits;

	// Keep render state ordered
	int64_t iIndex = rInterpolate.IdToIndex(rId);
	RenderStateSwapRemove(sWindDepositsRenderState, iIndex, rInterpolate.iCount, sWindDepositsRenderState.Members());

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void WindDepositsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindDepositsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindDepositsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindDepositsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool WindDepositsInterpolate::operator==(const WindDepositsInterpolate& rOther) const
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

bool WindDepositsPostRender::operator==(const WindDepositsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void WindDepositsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	const WindDepositsInterpolate& rCurrent = rFrameInterpolate.windDeposits;

	if (rCurrent.iCount == 0)
	{
		WritePipelineIndirectBuffers(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	RenderStateEnsureCapacity(sWindDepositsRenderState, rCurrent.iCapacity, sWindDepositsRenderState.Members());

	auto [pQuadLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iRendered = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rCurrent.pVecPositions[i];
		XMVECTOR vecPreviousPosition = sWindDepositsRenderState.pVecPreviousPositions[i];
		float fIntensity = rCurrent.pfIntensities[i];
		float fWidth = rCurrent.pfWidths[i];
		float fLengthMultiplier = rCurrent.pfLengthMultipliers[i];

		// Visibility culling
		XMFLOAT4A f4Position {};
		if (!IsPointVisible(vecPosition, f4Position))
		{
			continue;
		}

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

		float fMagnitude = fIntensity;

		// Build oriented quad: front (current) ± width, back (previous) ± width
		XMVECTOR vecFrontLeft = vecBasePosition + fWidth * vecPerpNormal;
		XMVECTOR vecFrontRight = vecBasePosition - fWidth * vecPerpNormal;
		XMVECTOR vecBackLeft = vecBasePreviousPosition + fWidth * vecPerpNormal;
		XMVECTOR vecBackRight = vecBasePreviousPosition - fWidth * vecPerpNormal;

		// Wind direction from motion (flip Y for world convention)
		float fWindDirX = -XMVectorGetX(vecDirNormal);
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
		XMFLOAT4 f4Params = {fMagnitude, fWindDirX, fWindDirY, 0.0f};
		pQuadLayouts[iRendered].pf4Params[0] = f4Params;
		pQuadLayouts[iRendered].pf4Params[1] = f4Params;
		pQuadLayouts[iRendered].pf4Params[2] = f4Params;
		pQuadLayouts[iRendered].pf4Params[3] = f4Params;

		pQuadLayouts[iRendered].f4Params = {};
		pQuadLayouts[iRendered].uiColor = 0xFFFFFFFF;

		++iRendered;
	}

	WritePipelineIndirectBuffers(iCommandBuffer, iRendered);

	// Snapshot current positions for next render
	std::memcpy(sWindDepositsRenderState.pVecPreviousPositions, rCurrent.pVecPositions, rCurrent.iCount * sizeof(XMVECTOR));
}

} // namespace engine
