#include "IslandTerrain.h"

namespace engine
{

IslandTerrain::IslandTerrain()
{
	gpIslandTerrain = this;
	smPriorityIslands.clear();

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

	if (mpfHeightmapData != nullptr)
	{
		BuildNavData(mNavData, mpfHeightmapData, miHeightmapWidth, miHeightmapHeight, mfBeachElevation, game::Frame::kfIslandWidth, game::Frame::kfIslandHeight);
	}
}

float XM_CALLCONV IslandTerrain::GlobalElevation(FXMVECTOR vecPosition) const
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Compute grid cell from world position
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fIslandWidth = game::Frame::kfIslandWidth;
	static constexpr float fIslandHeight = game::Frame::kfIslandHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;
	static constexpr float fCellMaxY = game::Frame::kfBaseAreaMaxY;

	int32_t iGridX = static_cast<int32_t>(std::floor((f4Position.x - fCellMinX) / fCellWidth));
	int32_t iGridY = static_cast<int32_t>(std::floor((f4Position.y - fCellMinY) / fCellHeight));

	// Cell origin
	float fCellOriginX = fCellMinX + static_cast<float>(iGridX) * fCellWidth;
	float fCellOriginMaxY = fCellMaxY + static_cast<float>(iGridY) * fCellHeight;

	// Island offset within cell (deterministic from grid coord)
	XMFLOAT2 f2Offset = game::ComputeIslandOffset({iGridX, iGridY});

	// Island bounds
	float fIslandMinX = fCellOriginX + f2Offset.x;
	float fIslandMaxY = fCellOriginMaxY - f2Offset.y;
	float fIslandMaxX = fIslandMinX + fIslandWidth;
	float fIslandMinY = fIslandMaxY - fIslandHeight;

	// Ocean gap: position is outside island bounds
	if (f4Position.x < fIslandMinX || f4Position.x > fIslandMaxX ||
	    f4Position.y < fIslandMinY || f4Position.y > fIslandMaxY)
	{
		return mfSeaFloorElevation;
	}

	// UV within island
	float fU = (f4Position.x - fIslandMinX) / fIslandWidth;
	float fV = (fIslandMaxY - f4Position.y) / fIslandHeight;

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
	static constexpr float fIslandWidth = game::Frame::kfIslandWidth;
	static constexpr float fIslandHeight = game::Frame::kfIslandHeight;
	float fStepX = fIslandWidth / static_cast<float>(miHeightmapWidth);
	float fStepY = fIslandHeight / static_cast<float>(miHeightmapHeight);
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
