#include "WindTrails.h"

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace engine
{

struct WindTrailsRenderState
{
	std::unordered_map<wind_trail_t, XMVECTOR> previousPositions;
};

static WindTrailsRenderState sRenderState;
static int64_t siRendered = 0;

void WindTrailsInterpolate::GraphicsResources()
{
	gpBufferManager->CreateDynamicBuffer(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineWindDepositA(kCrc, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->mDynamicPipelines.CreatePipelineWindDepositB(kCrc, kName);
}

void WindTrailsInterpolate::ResetRenderState()
{
	sRenderState.previousPositions.clear();
}

void WindTrailsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords)
{
	siRendered = 0;

	if (!gWind.Get<bool>())
	{
		return;
	}

	int64_t iTotalCapacity = AccumulateRenderCapacity(rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.windTrails; });

	EraseStaleRenderState(sRenderState.previousPositions, rRenderInterpolates, rActiveCoords,
		[](const game::FrameInterpolate& rInterpolate) -> const auto& { return rInterpolate.windTrails.idToIndexMap; });

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositA].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
		gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositB].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
	}
}

void WindTrailsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] uint16_t uiFrameId)
{
	const WindTrailsInterpolate& rCurrent = rFrameInterpolate.windTrails;

	if (!gWind.Get<bool>() || rCurrent.iCount == 0)
	{
		return;
	}

	auto [pQuadLayouts, iBufferCapacity] = gpBufferManager->GetDynamicStorageBuffer<shaders::QuadLayout>(kCrc, kBufferMain, iCommandBuffer);
	ASSERT(siRendered + rCurrent.iCount <= iBufferCapacity);

	{
		// Heap: unordered_map insertions/lookups for per-trail previous positions
		ScopedSuppressAllocationTracking suppress;

		WindTrailsRenderState& rRenderState = sRenderState;

		// Build GPU quads
		for (const auto& [id, iIndex] : rCurrent.idToIndexMap)
		{
			XMVECTOR vecPosition = rCurrent.pVecPositions[iIndex];
			float fIntensity = rCurrent.pfIntensities[iIndex];
			float fWidth = rCurrent.pfWidths[iIndex];

			// Visibility culling
			XMFLOAT4A f4Position {};
			if (!IsPointVisible(vecPosition, f4Position))
			{
				continue;
			}

			// Look up or initialize previous position
			auto it = rRenderState.previousPositions.find(id);
			XMVECTOR vecPreviousPosition = (it != rRenderState.previousPositions.end()) ? it->second : vecPosition;
			float fLengthMultiplier = rCurrent.pfLengthMultipliers[iIndex];

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
			pQuadLayouts[siRendered].pf4VerticesTexcoords[0] = {f4Vertex.x, f4Vertex.y, 0.0f, 1.0f};
			XMStoreFloat4A(&f4Vertex, vecFrontRight);
			pQuadLayouts[siRendered].pf4VerticesTexcoords[1] = {f4Vertex.x, f4Vertex.y, 1.0f, 1.0f};
			XMStoreFloat4A(&f4Vertex, vecBackLeft);
			pQuadLayouts[siRendered].pf4VerticesTexcoords[2] = {f4Vertex.x, f4Vertex.y, 0.0f, 0.0f};
			XMStoreFloat4A(&f4Vertex, vecBackRight);
			pQuadLayouts[siRendered].pf4VerticesTexcoords[3] = {f4Vertex.x, f4Vertex.y, 1.0f, 0.0f};

			// Per-vertex params: {magnitude, windDirX, windDirY, 0}
			XMFLOAT4A f4Params = {fIntensity, fWindDirX, fWindDirY, 0.0f};
			pQuadLayouts[siRendered].pf4Params[0] = f4Params;
			pQuadLayouts[siRendered].pf4Params[1] = f4Params;
			pQuadLayouts[siRendered].pf4Params[2] = f4Params;
			pQuadLayouts[siRendered].pf4Params[3] = f4Params;

			pQuadLayouts[siRendered].f4Params = {};
			pQuadLayouts[siRendered].uiColor = 0xFFFFFFFF;

			++siRendered;
		}

		// Snapshot current positions as previous for next render
		for (const auto& [id, iIndex] : rCurrent.idToIndexMap)
		{
			rRenderState.previousPositions.insert_or_assign(id, rCurrent.pVecPositions[iIndex]);
		}
	}
}

void WindTrailsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositA].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? siRendered : 0);
	gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositB].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? siRendered : 0);
}

} // namespace engine

#endif // BT_CLIENT
