#include "Islands.h"

namespace engine
{

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval)
{
	auto vecToEnd = XMVectorSubtract(vecEnd, vecStart);
	float fDistance = XMVectorGetX(XMVector3Length(vecToEnd));
	int64_t iSteps = static_cast<int64_t>(fDistance / fStepInterval);
	auto vecStep = vecToEnd / static_cast<float>(iSteps);
	float fElevation = XMVectorGetZ(vecStart);

	auto vecCurrent = vecStart;
	for (int64_t k = 0; k < iSteps; ++k, vecCurrent += vecStep)
	{
		float fTerrainElevation = gpIslands->GlobalElevation(vecCurrent);
		if (fTerrainElevation >= fElevation)
		{
			return vecCurrent;
		}
	}

	return vecEnd;
}

const shaders::AxisAlignedQuadLayout& XM_CALLCONV Islands::GetIsland(FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	for (const Island& rIsland : mIslands)
	{
		if (f4Position.x >= rIsland.quad.f4VertexRect.x && f4Position.x <= rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z && f4Position.y <= rIsland.quad.f4VertexRect.y && f4Position.y >= rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w)
		{
			return rIsland.quad;
		}
	}

	DEBUG_BREAK();
	return mIslands.at(0).quad;
}

float XM_CALLCONV Islands::GlobalElevation(FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Find island containing this position
	const Island* pIsland = nullptr;
	for (const Island& rIsland : mIslands)
	{
		if (f4Position.x >= rIsland.quad.f4VertexRect.x && f4Position.x <= rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z && f4Position.y <= rIsland.quad.f4VertexRect.y && f4Position.y >= rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w)
		{
			pIsland = &rIsland;
			break;
		}
	}

	if (!pIsland)
	{
		return mfSeaFloorElevation;
	}

	// Transform world position to island-local UV coordinates
	float fU = (f4Position.x - pIsland->quad.f4VertexRect.x) / pIsland->quad.f4VertexRect.z;
	float fV = (pIsland->quad.f4VertexRect.y - f4Position.y) / std::abs(pIsland->quad.f4VertexRect.w);

	// Apply flip transformations
	if (pIsland->bFlipX)
	{
		fU = 1.0f - fU;
	}
	if (pIsland->bFlipY)
	{
		fV = 1.0f - fV;
	}

	// Convert UV to heightmap indices
	int64_t iX = static_cast<int64_t>(fU * static_cast<float>(pIsland->iHeightmapWidth - 1));
	int64_t iY = static_cast<int64_t>(fV * static_cast<float>(pIsland->iHeightmapHeight - 1));

	// Clamp to valid range
	iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(pIsland->iHeightmapWidth - 1));
	iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(pIsland->iHeightmapHeight - 1));

	// Sample normalized heightmap value (0.0 to 1.0)
	float fNormalizedElevation = pIsland->pfHeightmapData[iY * pIsland->iHeightmapWidth + iX];

	// Apply same transformation as TerrainElevation shader
	float fRelativeElevation = fNormalizedElevation - pIsland->quad.f4Params.x;
	if (fRelativeElevation >= 0.0f)
	{
		return gIslandHeight.Get() * fRelativeElevation;
	}
	else
	{
		return gWaterDepth.Get() * fRelativeElevation;
	}
}

XMVECTOR XM_CALLCONV Islands::GlobalNormal(FXMVECTOR vecPosition)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Find island containing this position
	const Island* pIsland = nullptr;
	for (const Island& rIsland : mIslands)
	{
		if (f4Position.x >= rIsland.quad.f4VertexRect.x && f4Position.x <= rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z && f4Position.y <= rIsland.quad.f4VertexRect.y && f4Position.y >= rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w)
		{
			pIsland = &rIsland;
			break;
		}
	}

	if (!pIsland) [[unlikely]]
	{
		return XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	}

	// Calculate sample distance based on heightmap resolution
	float fStepX = pIsland->quad.f4VertexRect.z / static_cast<float>(pIsland->iHeightmapWidth);
	float fStepY = std::abs(pIsland->quad.f4VertexRect.w) / static_cast<float>(pIsland->iHeightmapHeight);
	float fDistance = 2.0f * std::max(fStepX, fStepY);

	// Clamp sample positions to island bounds
	float fMinX = pIsland->quad.f4VertexRect.x + fDistance;
	float fMaxX = pIsland->quad.f4VertexRect.x + pIsland->quad.f4VertexRect.z - fDistance;
	float fMaxY = pIsland->quad.f4VertexRect.y - fDistance;
	float fMinY = pIsland->quad.f4VertexRect.y + pIsland->quad.f4VertexRect.w + fDistance;

	float fClampedX = std::clamp(f4Position.x, fMinX, fMaxX);
	float fClampedY = std::clamp(f4Position.y, fMinY, fMaxY);
	auto vecClamped = XMVectorSet(fClampedX, fClampedY, 0.0f, 0.0f);

	// Sample 4 surrounding points
	auto vecTopLeft = XMVectorAdd(vecClamped, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, GlobalElevation(vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecClamped, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, GlobalElevation(vecTopRight));
	auto vecBotLeft = XMVectorAdd(vecClamped, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBotLeft = XMVectorSetZ(vecBotLeft, GlobalElevation(vecBotLeft));
	auto vecBotRight = XMVectorAdd(vecClamped, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBotRight = XMVectorSetZ(vecBotRight, GlobalElevation(vecBotRight));

	return XMVector3Normalize(XMVector3Cross(vecTopRight - vecBotLeft, vecTopLeft - vecBotRight));
}

Islands::Islands()
{
	gpIslands = this;

	mIslands.resize(kiDefaultIslandCapacity);

	FillQuads();

	// Calculate global area bounds from the base island template
	mf4GlobalArea.x = game::Frame::kfBaseAreaMinX;
	mf4GlobalArea.y = game::Frame::kfBaseAreaMaxY;
	mf4GlobalArea.z = game::Frame::kfBaseAreaMaxX;
	mf4GlobalArea.w = game::Frame::kfBaseAreaMinY;

	// Collect island CRCs and setup beach elevation
	int64_t iIndex = 0;
	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.header.flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		smPriorityIslands.push_back(rCrc);

		uint16_t uiBeachElevation = rChunk.header.islandHeader.uiBeachElevation;
		float fBeach = common::UnormToFloat(uiBeachElevation);
		mfBeachElevation = fBeach;
		mfSeaFloorElevation = gWaterDepth.Get() * -mfBeachElevation;
		++iIndex;
	}

	std::sort(smPriorityIslands.begin(), smPriorityIslands.end());

	gpFileManager->RequestChunkLoad(smPriorityIslands, LoadPriority::kRealtime);

	// Set beach elevation on all island slots
	for (Island& rIsland : mIslands)
	{
		rIsland.quad.f4Params.x = mfBeachElevation;
	}

#ifdef BT_CLIENT
	mIslandsStorageBuffer.Create(
	{
		.name = "Islands",
		.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
		.iCount = static_cast<int64_t>(mIslands.size()),
		.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
		.dataVkDeviceSize = mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout),
	});

	// Copy island quads to storage buffer for GPU rendering
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
#endif
}

Islands::~Islands()
{
	gpIslands = nullptr;
}

void Islands::WaitForElevationMaps()
{
	gpFileManager->WaitForChunks(smPriorityIslands);

	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (size_t i = 0; i < smPriorityIslands.size(); ++i)
	{
		const LazyChunk& rChunk = rChunkMap.at(smPriorityIslands[i]);

		mIslands[i].pfHeightmapData = reinterpret_cast<const float*>(rChunk.pData);
		mIslands[i].iHeightmapWidth = rChunk.header.islandHeader.iHeightmapWidth;
		mIslands[i].iHeightmapHeight = rChunk.header.islandHeader.iHeightmapHeight;
	}

	// All islands share the same heightmap (just flipped), so propagate to all slots
	for (size_t i = smPriorityIslands.size(); i < mIslands.size(); ++i)
	{
		mIslands[i].pfHeightmapData = mIslands[0].pfHeightmapData;
		mIslands[i].iHeightmapWidth = mIslands[0].iHeightmapWidth;
		mIslands[i].iHeightmapHeight = mIslands[0].iHeightmapHeight;
	}
}

void Islands::SetIslandsFlip(IslandsFlip eIslandsFlip)
{
	if (meCurrentIslandsFlip == eIslandsFlip)
	{
		return;
	}

	meCurrentIslandsFlip = eIslandsFlip;

	mbFlipX = meCurrentIslandsFlip == kFlipX || meCurrentIslandsFlip == kFlipXY ? true : false;
	mbFlipY = meCurrentIslandsFlip == kFlipY || meCurrentIslandsFlip == kFlipXY ? true : false;

	for (Island& rIsland : mIslands)
	{
		rIsland.bFlipX = mbFlipX;
		rIsland.bFlipY = mbFlipY;
	}

	FillQuads();

#ifdef BT_CLIENT
	// Copy island quads to storage buffer for GPU rendering
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
#endif
}

void Islands::FillQuads()
{
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		if (i < static_cast<size_t>(game::Frame::kiIslandCount))
		{
			mIslands[i].quad.f4VertexRect.x = game::Frame::kpfIslandPositions[i][0];
			mIslands[i].quad.f4VertexRect.y = game::Frame::kpfIslandPositions[i][1];
			mIslands[i].quad.f4VertexRect.z = game::Frame::kpfIslandPositions[i][2];
			mIslands[i].quad.f4VertexRect.w = game::Frame::kpfIslandPositions[i][3];
		}
		else
		{
			// Zero-width quad: GPU-culled degenerate triangle
			mIslands[i].quad.f4VertexRect = {};
		}

		mIslands[i].quad.f4TextureRect.x = mIslands[i].bFlipX ? 1.0f : 0.0f;
		mIslands[i].quad.f4TextureRect.z = mIslands[i].bFlipX ? 0.0f : 1.0f;

		mIslands[i].quad.f4TextureRect.y = mIslands[i].bFlipY ? 1.0f : 0.0f;
		mIslands[i].quad.f4TextureRect.w = mIslands[i].bFlipY ? 0.0f : 1.0f;
	}
}

void Islands::SetIslandFlip(int64_t iIndex, IslandsFlip eIslandsFlip)
{
	Island& rIsland = mIslands.at(static_cast<size_t>(iIndex));

	rIsland.bFlipX = eIslandsFlip == kFlipX || eIslandsFlip == kFlipXY;
	rIsland.bFlipY = eIslandsFlip == kFlipY || eIslandsFlip == kFlipXY;

	// Keep global state synced with first island (used by shader normals)
	if (iIndex == 0)
	{
		mbFlipX = rIsland.bFlipX;
		mbFlipY = rIsland.bFlipY;
		meCurrentIslandsFlip = eIslandsFlip;
	}

	FillQuads();

#ifdef BT_CLIENT
	// Copy island quads to storage buffer for GPU rendering
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
#endif
}

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rFrames, const std::vector<GridCoord>& rActiveCoords)
{
	int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

	// Grow capacity if needed
	if (iActiveCount > static_cast<int64_t>(mIslands.size()))
	{
		int64_t iNewCapacity = static_cast<int64_t>(mIslands.size());
		while (iNewCapacity < iActiveCount)
		{
			iNewCapacity *= 2;
		}
		iNewCapacity = std::min(iNewCapacity, static_cast<int64_t>(shaders::kiMaxIslands));
		mIslands.resize(static_cast<size_t>(iNewCapacity));

		// Initialize new slots with shared heightmap data and beach elevation
		for (size_t i = static_cast<size_t>(iActiveCount); i < mIslands.size(); ++i)
		{
			mIslands[i].pfHeightmapData = mIslands[0].pfHeightmapData;
			mIslands[i].iHeightmapWidth = mIslands[0].iHeightmapWidth;
			mIslands[i].iHeightmapHeight = mIslands[0].iHeightmapHeight;
			mIslands[i].quad.f4Params.x = mfBeachElevation;
		}

#ifdef BT_CLIENT
		// Recreate storage buffer at new capacity
		mIslandsStorageBuffer.Destroy();
		mIslandsStorageBuffer.Create(
		{
			.name = "Islands",
			.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
			.iCount = static_cast<int64_t>(mIslands.size()),
			.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
			.dataVkDeviceSize = mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout),
		});

		// Re-record command buffers to pick up new instance count
		gpGraphics->meDestroyType = DestroyType::kCommandBuffers;
#endif
	}

	// Fill active island slots from frame data
	float fBaseWidth = game::Frame::kpfIslandPositions[0][2];
	float fBaseHeight = game::Frame::kpfIslandPositions[0][3];

	for (int64_t i = 0; i < iActiveCount; ++i)
	{
		const GridCoord& rCoord = rActiveCoords[static_cast<size_t>(i)];
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second == nullptr)
		{
			continue;
		}

		const game::Frame& rFrame = *it->second;
		IslandsFlip eFlip = rFrame.postRender.eIslandsFlip;

		Island& rIsland = mIslands[static_cast<size_t>(i)];
		rIsland.bFlipX = eFlip == kFlipX || eFlip == kFlipXY;
		rIsland.bFlipY = eFlip == kFlipY || eFlip == kFlipXY;

		// Position from base island template offset by grid coordinate
		rIsland.quad.f4VertexRect.x = game::Frame::kfBaseAreaMinX + static_cast<float>(rCoord.x) * fBaseWidth;
		rIsland.quad.f4VertexRect.y = game::Frame::kfBaseAreaMaxY + static_cast<float>(rCoord.y) * std::abs(fBaseHeight);
		rIsland.quad.f4VertexRect.z = fBaseWidth;
		rIsland.quad.f4VertexRect.w = fBaseHeight;

		// Texture coords from flip state
		rIsland.quad.f4TextureRect.x = rIsland.bFlipX ? 1.0f : 0.0f;
		rIsland.quad.f4TextureRect.z = rIsland.bFlipX ? 0.0f : 1.0f;
		rIsland.quad.f4TextureRect.y = rIsland.bFlipY ? 1.0f : 0.0f;
		rIsland.quad.f4TextureRect.w = rIsland.bFlipY ? 0.0f : 1.0f;

		// Params: beach elevation, flipX, flipY
		rIsland.quad.f4Params.x = mfBeachElevation;
		rIsland.quad.f4Params.y = rIsland.bFlipX ? 1.0f : 0.0f;
		rIsland.quad.f4Params.z = rIsland.bFlipY ? 1.0f : 0.0f;
	}

	// Zero remaining slots (zero-width quads → GPU culled)
	for (size_t i = static_cast<size_t>(iActiveCount); i < mIslands.size(); ++i)
	{
		mIslands[i].quad.f4VertexRect = {};
	}

	// Recompute global area from active islands
	mf4GlobalArea.x = std::numeric_limits<float>::max();
	mf4GlobalArea.y = std::numeric_limits<float>::lowest();
	mf4GlobalArea.z = std::numeric_limits<float>::lowest();
	mf4GlobalArea.w = std::numeric_limits<float>::max();
	for (int64_t i = 0; i < iActiveCount; ++i)
	{
		const Island& rIsland = mIslands[static_cast<size_t>(i)];
		mf4GlobalArea.x = std::min(mf4GlobalArea.x, rIsland.quad.f4VertexRect.x);
		mf4GlobalArea.y = std::max(mf4GlobalArea.y, rIsland.quad.f4VertexRect.y);
		mf4GlobalArea.z = std::max(mf4GlobalArea.z, rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z);
		mf4GlobalArea.w = std::min(mf4GlobalArea.w, rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w);
	}

	// Camera island (index 0) determines CPU-side flip for GlobalNormal()
	mbFlipX = mIslands[0].bFlipX;
	mbFlipY = mIslands[0].bFlipY;
	meCurrentIslandsFlip = static_cast<IslandsFlip>((mbFlipX ? kFlipX : 0) | (mbFlipY ? kFlipY : 0));

#ifdef BT_CLIENT
	// Upload all quads to storage buffer
	auto* pQuads = common::gpThreadLocal->mWorkbuffer.PushBuffer<shaders::AxisAlignedQuadLayout*>(mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		pQuads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, pQuads, mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
	common::gpThreadLocal->mWorkbuffer.Pop();
#endif
}

} // namespace engine
