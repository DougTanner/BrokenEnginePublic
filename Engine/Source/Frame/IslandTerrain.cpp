#include "IslandTerrain.h"

namespace engine
{

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval)
{
	auto vecToEnd = XMVectorSubtract(vecEnd, vecStart);
	float fDistance = XMVectorGetX(XMVector3Length(vecToEnd));
	int64_t iSteps = std::max(1LL, static_cast<int64_t>(fDistance / fStepInterval));
	auto vecStep = vecToEnd / static_cast<float>(iSteps);
	float fElevation = XMVectorGetZ(vecStart);

	auto vecCurrent = vecStart;
	for (int64_t k = 0; k < iSteps; ++k, vecCurrent += vecStep)
	{
		float fTerrainElevation = gpIslandTerrain->GlobalElevation(vecCurrent);
		if (fTerrainElevation >= fElevation)
		{
			return vecCurrent;
		}
	}

	return vecEnd;
}

IslandTerrain::IslandTerrain()
{
	gpIslandTerrain = this;

	// Collect island CRCs and setup beach elevation
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
	}

	std::sort(smPriorityIslands.begin(), smPriorityIslands.end());

	gpFileManager->RequestChunkLoad(smPriorityIslands, LoadPriority::kRealtime);
}

IslandTerrain::~IslandTerrain()
{
	gpIslandTerrain = nullptr;
}

void IslandTerrain::WaitForElevationMaps()
{
	gpFileManager->WaitForChunks(smPriorityIslands);

	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (size_t i = 0; i < smPriorityIslands.size(); ++i)
	{
		const LazyChunk& rChunk = rChunkMap.at(smPriorityIslands.at(i));

		mpfHeightmapData = reinterpret_cast<const float*>(rChunk.pData);
		miHeightmapWidth = rChunk.header.islandHeader.iHeightmapWidth;
		miHeightmapHeight = rChunk.header.islandHeader.iHeightmapHeight;
	}
}

float XM_CALLCONV IslandTerrain::GlobalElevation(FXMVECTOR vecPosition) const
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Compute grid cell from world position
	constexpr float fBaseWidth = game::Frame::kpfIslandPositions[0][2];
	constexpr float fBaseHeight = -game::Frame::kpfIslandPositions[0][3];
	constexpr float fBaseMinX = game::Frame::kfBaseAreaMinX;
	constexpr float fBaseMinY = game::Frame::kfBaseAreaMinY;
	constexpr float fBaseMaxY = game::Frame::kfBaseAreaMaxY;

	int32_t iGridX = static_cast<int32_t>(std::floor((f4Position.x - fBaseMinX) / fBaseWidth));
	int32_t iGridY = static_cast<int32_t>(std::floor((f4Position.y - fBaseMinY) / fBaseHeight));

	// Island bounds for this grid cell
	float fMinX = fBaseMinX + static_cast<float>(iGridX) * fBaseWidth;
	float fMaxY = fBaseMaxY + static_cast<float>(iGridY) * fBaseHeight;

	// UV within island
	float fU = (f4Position.x - fMinX) / fBaseWidth;
	float fV = (fMaxY - f4Position.y) / fBaseHeight;

	// Flip from grid coord parity (same logic as CreateFrameAtCoord)
	if ((std::abs(iGridX) % 2) == 1)
	{
		fU = 1.0f - fU;
	}
	if ((std::abs(iGridY) % 2) == 1)
	{
		fV = 1.0f - fV;
	}

	// Sample heightmap
	int64_t iX = static_cast<int64_t>(fU * static_cast<float>(miHeightmapWidth - 1));
	int64_t iY = static_cast<int64_t>(fV * static_cast<float>(miHeightmapHeight - 1));
	iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(miHeightmapWidth - 1));
	iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(miHeightmapHeight - 1));

	float fNormalizedElevation = mpfHeightmapData[iY * miHeightmapWidth + iX];
	float fRelativeElevation = fNormalizedElevation - mfBeachElevation;
	if (fRelativeElevation >= 0.0f)
	{
		return gIslandHeight.Get() * fRelativeElevation;
	}
	else
	{
		return gWaterDepth.Get() * fRelativeElevation;
	}
}

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	constexpr float fBaseWidth = game::Frame::kpfIslandPositions[0][2];
	constexpr float fBaseHeight = -game::Frame::kpfIslandPositions[0][3];
	float fStepX = fBaseWidth / static_cast<float>(miHeightmapWidth);
	float fStepY = fBaseHeight / static_cast<float>(miHeightmapHeight);
	float fDistance = 2.0f * std::max(fStepX, fStepY);

	// Sample 4 surrounding points (seamless across grid cell boundaries)
	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, GlobalElevation(vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, GlobalElevation(vecTopRight));
	auto vecBotLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBotLeft = XMVectorSetZ(vecBotLeft, GlobalElevation(vecBotLeft));
	auto vecBotRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBotRight = XMVectorSetZ(vecBotRight, GlobalElevation(vecBotRight));

	return XMVector3Normalize(XMVector3Cross(vecTopRight - vecBotLeft, vecTopLeft - vecBotRight));
}

} // namespace engine
