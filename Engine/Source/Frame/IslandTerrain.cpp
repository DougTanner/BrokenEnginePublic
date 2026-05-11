#include "IslandTerrain.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandPlacement.h"
#include "Ui/TerrainWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

#if defined(BT_CLIENT)
#include "Graphics/Managers/TextureManager.h"
#endif

#include "Game.h"

#include "Data/Data.h"

namespace engine
{

IslandTerrain::IslandTerrain()
{
	gpIslandTerrain = this;

	// Build a template entry for every kIsland chunk in the manifest. Header fields are
	// available synchronously; heightmap pointer fills in WaitForElevationMaps once data
	// is resident.
	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (const auto& [rCrc, rLazyChunk] : rChunkMap)
	{
		if (!(rLazyChunk.header.flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		IslandTemplate& rTemplate = mIslands.try_emplace(rCrc).first->second;
		rTemplate.mIslandCrc = rCrc;
		rTemplate.mfBeachElevation = common::UnormToFloat(rLazyChunk.header.islandHeader.uiBeachElevation);
		rTemplate.mfWorldWidthMeters = rLazyChunk.header.islandHeader.fWorldWidthMeters;
		rTemplate.mfWorldHeightMeters = rLazyChunk.header.islandHeader.fWorldHeightMeters;

		// Quad footprint in units. Legacy assets (no world.json) ship zero meters; fall back to
		// the global default so behavior is unchanged for them. New assets get per-template size.
		rTemplate.mfQuadWidth = rTemplate.mfWorldWidthMeters > 0.0f ? rTemplate.mfWorldWidthMeters * kfMetersToUnits : game::Frame::kfIslandWidth;
		rTemplate.mfQuadHeight = rTemplate.mfWorldHeightMeters > 0.0f ? rTemplate.mfWorldHeightMeters * kfMetersToUnits : game::Frame::kfIslandHeight;
	}

	// Stable, deterministic iteration order for slot assignment (Phase 3).
	mIslandCrcsSorted.reserve(mIslands.size());
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		mIslandCrcsSorted.push_back(rCrc);
	}
	std::sort(mIslandCrcsSorted.begin(), mIslandCrcsSorted.end());

	// Cache canonical pointer for hot-path queries. Sea floor derives from canonical beach.
	mpCanonical = &mIslands.at(data::kIslands01Crc);
	mfSeaFloorElevation = gWaterDepth.Get() * -mpCanonical->mfBeachElevation;

	gpFileManager->RequestChunkLoad(mIslandCrcsSorted, LoadPriority::kRealtime);
}

IslandTerrain::~IslandTerrain()
{
	gpIslandTerrain = nullptr;
}

void IslandTerrain::WaitForElevationMaps([[maybe_unused]] float fNavThreshold)
{
	gpFileManager->WaitForChunks(mIslandCrcsSorted);

	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		const LazyChunk& rLazyChunk = rChunkMap.at(rCrc);
		rTemplate.mpfHeightmapData = reinterpret_cast<const float*>(rLazyChunk.pData);
		rTemplate.miHeightmapWidth = rLazyChunk.header.islandHeader.iHeightmapWidth;
		rTemplate.miHeightmapHeight = rLazyChunk.header.islandHeader.iHeightmapHeight;
	}

#if defined(BT_SERVER)
	// Phase 4: build NavContour for every template so multi-template cells produce correct nav data.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mpfHeightmapData != nullptr)
		{
			BuildNavContour(rTemplate.mNavContour, rTemplate.mpfHeightmapData, rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, rTemplate.mfBeachElevation, fNavThreshold);
		}
	}
#endif
}

float XM_CALLCONV IslandTerrain::GlobalElevation(FXMVECTOR vecPosition) const
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Compute grid cell from world position
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;

	int32_t iGridX = static_cast<int32_t>(std::floor((f4Position.x - fCellMinX) / fCellWidth));
	int32_t iGridY = static_cast<int32_t>(std::floor((f4Position.y - fCellMinY) / fCellHeight));
	GridCoord coord {iGridX, iGridY};

	// Look up per-cell placements. Cells outside the simulated set fall through to sea floor.
	auto it = game::gpGame->mCoordFrames.find(coord);
	if (it == game::gpGame->mCoordFrames.end())
	{
		return mfSeaFloorElevation;
	}

	for (const IslandPlacement& rPlacement : it->second.staticData.islands)
	{
		const IslandTemplate& rTemplate = mIslands.at(rPlacement.islandCrc);
		float fIslandWidth = rTemplate.mfQuadWidth;
		float fIslandHeight = rTemplate.mfQuadHeight;

		// Inverse-rotate world point into island-local frame
		float fCos = std::cos(-rPlacement.fRotation);
		float fSin = std::sin(-rPlacement.fRotation);
		float fDx = f4Position.x - rPlacement.f2WorldPos.x;
		float fDy = f4Position.y - rPlacement.f2WorldPos.y;
		float fLocalX = fDx * fCos - fDy * fSin;
		float fLocalY = fDx * fSin + fDy * fCos;

		if (std::abs(fLocalX) > 0.5f * fIslandWidth || std::abs(fLocalY) > 0.5f * fIslandHeight)
		{
			continue;
		}

		// UV from local frame; V axis is world-Y inverted
		float fU = fLocalX / fIslandWidth + 0.5f;
		float fV = 0.5f - fLocalY / fIslandHeight;

		int64_t iX = static_cast<int64_t>(fU * static_cast<float>(rTemplate.miHeightmapWidth - 1));
		int64_t iY = static_cast<int64_t>(fV * static_cast<float>(rTemplate.miHeightmapHeight - 1));
		iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapWidth - 1));
		iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapHeight - 1));

		float fNormalizedElevation = rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapWidth + iX];
		float fRelativeElevation = fNormalizedElevation - rTemplate.mfBeachElevation;
		if (fRelativeElevation >= 0.0f)
		{
			return gTerrainIslandHeight.Get() * fRelativeElevation;
		}
		else
		{
			return gWaterDepth.Get() * fRelativeElevation;
		}
	}

	return mfSeaFloorElevation;
}

#if defined(BT_CLIENT)
int64_t IslandTerrain::AcquireTextureSlot(common::crc_t islandCrc)
{
	IslandTemplate& rTemplate = mIslands.at(islandCrc);
	if (rTemplate.miTextureSlot >= 0)
	{
		return rTemplate.miTextureSlot;
	}

	int64_t iSlot = miNextTextureSlot++;
	rTemplate.miTextureSlot = iSlot;

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot] = &gpTextureManager->mTextureMap.at(rLazyChunk.header.islandHeader.elevationCrc);
	gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = &gpTextureManager->mTextureMap.at(rLazyChunk.header.islandHeader.colorsCrc);
	gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = &gpTextureManager->mTextureMap.at(rLazyChunk.header.islandHeader.normalsCrc);
	gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = &gpTextureManager->mTextureMap.at(rLazyChunk.header.islandHeader.ambientOcclusionCrc);

	if (gpGraphics != nullptr)
	{
		gpGraphics->meDestroyType = std::max(DestroyType::kPipelines, gpGraphics->meDestroyType);
	}

	return iSlot;
}
#endif

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	// Step is derived from the canonical template's quad size and heightmap resolution. Since
	// GlobalNormal is a 4-tap finite-difference over GlobalElevation (which routes per-placement
	// internally), the canonical step is a reasonable default sampling cadence.
	float fStepX = mpCanonical->mfQuadWidth / static_cast<float>(mpCanonical->miHeightmapWidth);
	float fStepY = mpCanonical->mfQuadHeight / static_cast<float>(mpCanonical->miHeightmapHeight);
	float fDistance = 2.0f * std::max(fStepX, fStepY);

	// Sample 4 surrounding points (seamless across grid cell boundaries)
	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, GlobalElevation(vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, GlobalElevation(vecTopRight));
	auto vecBottomLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomLeft = XMVectorSetZ(vecBottomLeft, GlobalElevation(vecBottomLeft));
	auto vecBottomRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomRight = XMVectorSetZ(vecBottomRight, GlobalElevation(vecBottomRight));

	return XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vecTopRight, vecBottomLeft), XMVectorSubtract(vecTopLeft, vecBottomRight)));
}

} // namespace engine
