#include "Islands.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"


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
	if (mbFlipX)
	{
		fU = 1.0f - fU;
	}
	if (mbFlipY)
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
	float fRelativeElevation = fNormalizedElevation - pIsland->quad.f4Misc.x;
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

	mIslands.resize(game::Frame::kiIslandCount);

	FillQuads();

	// Calculate global area bounds from all islands
	mf4GlobalArea.x = std::numeric_limits<float>::max();
	mf4GlobalArea.y = std::numeric_limits<float>::lowest();
	mf4GlobalArea.z = std::numeric_limits<float>::lowest();
	mf4GlobalArea.w = std::numeric_limits<float>::max();
	for (const Island& rIsland : mIslands)
	{
		mf4GlobalArea.x = std::min(mf4GlobalArea.x, rIsland.quad.f4VertexRect.x);
		mf4GlobalArea.y = std::max(mf4GlobalArea.y, rIsland.quad.f4VertexRect.y);
		mf4GlobalArea.z = std::max(mf4GlobalArea.z, rIsland.quad.f4VertexRect.x + rIsland.quad.f4VertexRect.z);
		mf4GlobalArea.w = std::min(mf4GlobalArea.w, rIsland.quad.f4VertexRect.y + rIsland.quad.f4VertexRect.w);
	}

	// Collect island CRCs and setup beach elevation
	int64_t iIndex = 0;
	const std::unordered_map<common::crc_t, engine::LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.header.flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		smPriorityIslands.push_back(rCrc);

		uint16_t uiBeachElevation = rChunk.header.islandHeader.uiBeachElevation;
		mIslands[iIndex++].quad.f4Misc.x = common::UnormToFloat(uiBeachElevation);
		mfBeachElevation = common::UnormToFloat(uiBeachElevation);
		mfSeaFloorElevation = gWaterDepth.Get() * -mfBeachElevation;
	}

	gpFileManager->RequestChunkLoad(smPriorityIslands, LoadPriority::kRealtime);

	while (iIndex < static_cast<int64_t>(mIslands.size()))
	{
		mIslands[iIndex].quad.f4Misc.x = mIslands[iIndex - 1].quad.f4Misc.x;
		++iIndex;
	}

	mIslandsStorageBuffer.Create(
	{
		.pcName = "Islands",
		.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
		.iCount = static_cast<int64_t>(mIslands.size()),
		.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
		.dataVkDeviceSize = mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout),
	});

	// Copy island quads to storage buffer for GPU rendering
	std::vector<shaders::AxisAlignedQuadLayout> quads(mIslands.size());
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		quads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, quads.data(), mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
}

Islands::~Islands()
{
	gpIslands = nullptr;
}

void Islands::WaitForElevationMaps()
{
	gpFileManager->WaitForChunks(smPriorityIslands);

	const std::unordered_map<common::crc_t, engine::LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (size_t i = 0; i < smPriorityIslands.size(); ++i)
	{
		const LazyChunk& rChunk = rChunkMap.at(smPriorityIslands[i]);

		mIslands[i].pfHeightmapData = reinterpret_cast<const float*>(rChunk.data.data());
		mIslands[i].iHeightmapWidth = rChunk.header.islandHeader.iHeightmapWidth;
		mIslands[i].iHeightmapHeight = rChunk.header.islandHeader.iHeightmapHeight;
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

	FillQuads();

	// Copy island quads to storage buffer for GPU rendering
	std::vector<shaders::AxisAlignedQuadLayout> quads(mIslands.size());
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		quads[i] = mIslands[i].quad;
	}
	memcpy(mIslandsStorageBuffer.mpMappedMemory, quads.data(), mIslands.size() * sizeof(shaders::AxisAlignedQuadLayout));
}

void Islands::FillQuads()
{
	for (size_t i = 0; i < mIslands.size(); ++i)
	{
		mIslands[i].quad.f4VertexRect.x = game::Frame::kpfIslandPositions[i][0];
		mIslands[i].quad.f4VertexRect.y = game::Frame::kpfIslandPositions[i][1];
		mIslands[i].quad.f4VertexRect.z = game::Frame::kpfIslandPositions[i][2];
		mIslands[i].quad.f4VertexRect.w = game::Frame::kpfIslandPositions[i][3];

		mIslands[i].quad.f4TextureRect.x = mbFlipX ? 1.0f : 0.0f;
		mIslands[i].quad.f4TextureRect.z = mbFlipX ? 0.0f : 1.0f;

		mIslands[i].quad.f4TextureRect.y = mbFlipY ? 1.0f : 0.0f;
		mIslands[i].quad.f4TextureRect.w = mbFlipY ? 0.0f : 1.0f;
	}
}

} // namespace engine
