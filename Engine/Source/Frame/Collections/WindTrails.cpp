#include "WindTrails.h"

#ifdef BT_CLIENT

#include "Ui/WrapperBase.h"

namespace engine
{

template struct Collection<WindTrailsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<WindTrailsPostRender>;

struct WindTrailsRenderState
{
	std::unordered_map<wind_trail_t, XMVECTOR> previousPositions;
};

static WindTrailsRenderState sRenderState;
static int64_t siRendered = 0;

void WindTrailsInterpolate::Register()
{
}

void WindTrailsInterpolate::AllocateAndCopy(WindTrailsInterpolate& rCurrent, const WindTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.pfIntensities, rPrevious.pfIntensities, rCurrent.iCount * sizeof(rCurrent.pfIntensities[0]));
		std::memcpy(rCurrent.pfWidths, rPrevious.pfWidths, rCurrent.iCount * sizeof(rCurrent.pfWidths[0]));
		std::memcpy(rCurrent.pfLengthMultipliers, rPrevious.pfLengthMultipliers, rCurrent.iCount * sizeof(rCurrent.pfLengthMultipliers[0]));
	}
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
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void WindTrailsPostRender::Remove(game::Frame& __restrict rFrame, wind_trail_t& rId)
{
	ASSERT(rId.IsValid());

	WindTrailsInterpolate& rInterpolate = rFrame.interpolate.windTrails;
	WindTrailsPostRender& rPostRender = rFrame.postRender.windTrails;

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
	gpPipelineManager->CreateDynamicPipelineWindDepositA(kCrc, kName, sizeof(shaders::QuadLayout));
	gpPipelineManager->CreateDynamicPipelineWindDepositB(kCrc, kName);
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

	int64_t iTotalCapacity = 0;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it != rRenderInterpolates.end())
		{
			iTotalCapacity += it->second.windTrails.iCapacity;
		}
	}

	{
		// Heap: unordered_map erase for stale previous positions
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		std::erase_if(sRenderState.previousPositions, [&rRenderInterpolates, &rActiveCoords](const auto& pair) {
			for (const GridCoord& rCoord : rActiveCoords)
			{
				auto it = rRenderInterpolates.find(rCoord);
				if (it != rRenderInterpolates.end() && it->second.windTrails.idToIndexMap.contains(pair.first))
				{
					return false;
				}
			}
			return true;
		});
	}

	if (iTotalCapacity == 0)
	{
		return;
	}

	if (Buffer* pBuffer = gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, kBufferMain, kName, sizeof(shaders::QuadLayout), iTotalCapacity, iCommandBuffer))
	{
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositA].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
		gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositB].at(kCrc)->UpdateStorageBufferDescriptor(iCommandBuffer, 1, pBuffer);
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
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

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
			XMFLOAT4 f4Params = {fIntensity, fWindDirX, fWindDirY, 0.0f};
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
			rRenderState.previousPositions[id] = rCurrent.pVecPositions[iIndex];
		}
	}
}

void WindTrailsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositA].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? siRendered : 0);
	gpPipelineManager->mDynamicPipelineMaps[kDynamicPipelineWindDepositB].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? siRendered : 0);
}

} // namespace engine

#endif // BT_CLIENT
