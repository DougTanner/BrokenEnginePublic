#include "IslandTerrain.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandPlacement.h"

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
		rTemplate.mfWorldFootprintMeters = rLazyChunk.header.islandHeader.fWorldFootprintMeters;
		rTemplate.mfWorldElevationMeters = rLazyChunk.header.islandHeader.fWorldElevationMeters;

		// Quad footprint in units. Legacy assets (no per-island dimensions) ship zero meters; fall
		// back to the global default so behavior is unchanged for them. New assets get per-template
		// size from Island.json's widthMeters override or the archetype .terrain Width property.
		rTemplate.mfQuadFootprint = rTemplate.mfWorldFootprintMeters > 0.0f ? rTemplate.mfWorldFootprintMeters * kfMetersToUnits : game::Frame::kfIslandWidth;
	}

	// Stable, deterministic iteration order for slot assignment (Phase 3).
	mIslandCrcsSorted.reserve(mIslands.size());
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		mIslandCrcsSorted.push_back(rCrc);
	}
	std::sort(mIslandCrcsSorted.begin(), mIslandCrcsSorted.end());

	// Cache canonical pointer for hot-path queries. Open-ocean floor (outside any island) is a
	// fixed depth below sea level — heightmap pixel values inside islands already carry real
	// negative depth, so this constant only fires for cells with no island placement.
	mpCanonical = &mIslands.at(data::kIslands01Crc);
	mfSeaFloorElevation = -common::kfOceanDepthMeters * kfMetersToUnits;

	gpFileManager->RequestChunkLoad(mIslandCrcsSorted, LoadPriority::kRealtime);

#if defined(BT_CLIENT)
	// Pin the menu + game canonical templates so Phase 5 LRU never evicts them. Keeps menu<->game
	// switch instant (no re-upload latency) and preserves pre-Phase-5 behavior verbatim today.
	mIslands.at(data::kIslands01Crc).mbPinned = true;
	mIslands.at(data::kIslands02Crc).mbPinned = true;
#endif
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
		rTemplate.miHeightmapSize = rLazyChunk.header.islandHeader.iHeightmapSize;
	}

#if defined(BT_SERVER)
	// Phase 4: build NavContour for every template so multi-template cells produce correct nav data.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mpfHeightmapData != nullptr)
		{
			BuildNavContour(rTemplate.mNavContour, rTemplate.mpfHeightmapData, rTemplate.miHeightmapSize, fNavThreshold);
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
		float fIslandFootprint = rTemplate.mfQuadFootprint;

		// Inverse-rotate world point into island-local frame
		float fCos = std::cos(-rPlacement.fRotation);
		float fSin = std::sin(-rPlacement.fRotation);
		float fDx = f4Position.x - rPlacement.f2WorldPos.x;
		float fDy = f4Position.y - rPlacement.f2WorldPos.y;
		float fLocalX = fDx * fCos - fDy * fSin;
		float fLocalY = fDx * fSin + fDy * fCos;

		if (std::abs(fLocalX) > 0.5f * fIslandFootprint || std::abs(fLocalY) > 0.5f * fIslandFootprint)
		{
			continue;
		}

		// UV from local frame; V axis is world-Y inverted
		float fU = fLocalX / fIslandFootprint + 0.5f;
		float fV = 0.5f - fLocalY / fIslandFootprint;

		int64_t iX = static_cast<int64_t>(fU * static_cast<float>(rTemplate.miHeightmapSize - 1));
		int64_t iY = static_cast<int64_t>(fV * static_cast<float>(rTemplate.miHeightmapSize - 1));
		iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapSize - 1));
		iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapSize - 1));

		// Heightmap value is already engine-meters (DataPacker offset by kfOceanDepthMeters).
		// Beach = 0; negative = water; positive = land. Return directly.
		return rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapSize + iX];
	}

	return mfSeaFloorElevation;
}

#if defined(BT_CLIENT)
int64_t IslandTerrain::AcquireTextureSlot(common::crc_t islandCrc)
{
	IslandTemplate& rTemplate = mIslands.at(islandCrc);

	// Hot path: slot assigned, GPU resources resident. Return early.
	if (rTemplate.miTextureSlot >= 0 && rTemplate.mbGpuResident)
	{
		return rTemplate.miTextureSlot;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	common::crc_t textureCrcs[4] =
	{
		rLazyChunk.header.islandHeader.elevationCrc,
		rLazyChunk.header.islandHeader.colorsCrc,
		rLazyChunk.header.islandHeader.normalsCrc,
		rLazyChunk.header.islandHeader.ambientOcclusionCrc,
	};

	if (rTemplate.miTextureSlot < 0)
	{
		// First-time mint. Pinned templates (kIslands01Crc + kIslands02Crc) point at real Texture*s
		// because the boot priority-load + WaitForChunks chain guarantees they are loaded before
		// any rendering; mbGpuResident = true is therefore truthful at render time. Non-pinned
		// templates start in canonical-slot fallback so the bindless slot's view is format-safe
		// while chunks load; RestorationSweep patches per-channel as each CRC reaches kReady.
		int64_t iSlot = miNextTextureSlot++;
		rTemplate.miTextureSlot = iSlot;

		if (rTemplate.mbPinned)
		{
			gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot] = &gpTextureManager->mTextureMap.at(textureCrcs[0]);
			gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = &gpTextureManager->mTextureMap.at(textureCrcs[1]);
			gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = &gpTextureManager->mTextureMap.at(textureCrcs[2]);
			gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = &gpTextureManager->mTextureMap.at(textureCrcs[3]);
			rTemplate.mbGpuResident = true;
			LOG(kGraphics, kVerbose, "Pinned mint slot={} islandCrc={}", iSlot, islandCrc);
		}
		else
		{
			gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mElevationTextures[0];
			gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mColorTextures[0];
			gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mNormalsTextures[0];
			gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[0];
			rTemplate.mbGpuResident = false;
			gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
			LOG(kGraphics, kVerbose, "First-mint slot={} islandCrc={} (canonical fallback until adopt)", iSlot, islandCrc);
		}

		if (gpGraphics != nullptr)
		{
			gpGraphics->meDestroyType = std::max(DestroyType::kPipelines, gpGraphics->meDestroyType);
		}

		return iSlot;
	}

	// Slot already assigned but GPU resources evicted. Re-trigger loads; slot stays in
	// canonical fallback (set during EvictionSweep) until RestorationSweep patches back.
	gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
	LOG(kLoading, kVerbose, "Re-acquire islandCrc={} slot={}, requesting chunk loads", islandCrc, rTemplate.miTextureSlot);
	return rTemplate.miTextureSlot;
}

void IslandTerrain::EvictionSweep()
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return;
	}

	bool bDirty = false;
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbPinned || !rTemplate.mbGpuResident || rTemplate.miRefCount != 0)
		{
			continue;
		}
		if ((gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame) <= kuiGraceRenderFrames)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		common::crc_t textureCrcs[4] =
		{
			rLazyChunk.header.islandHeader.elevationCrc,
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
		};

		LOG(kGraphics, kVerbose, "Evicting islandCrc={} slot={} (refCount=0, framesSinceUse={})", rCrc, rTemplate.miTextureSlot, gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame);

		for (common::crc_t textureCrc : textureCrcs)
		{
			gpTextureManager->mTextureMap.at(textureCrc).FreeGpuResources();
		}

		int64_t iSlot = rTemplate.miTextureSlot;
		gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mElevationTextures[0];
		gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mColorTextures[0];
		gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mNormalsTextures[0];
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[0];

		gpFileManager->ResetTextureChunkStates(textureCrcs);
		rTemplate.mbGpuResident = false;
		bDirty = true;

		LOG(kLoading, kVerbose, "Reset chunk states for evicted islandCrc={} textureCrcs=[{},{},{},{}]", rCrc, textureCrcs[0], textureCrcs[1], textureCrcs[2], textureCrcs[3]);
	}

	if (bDirty)
	{
		gpTextureManager->mTextureDescriptors.UpdateTextureArrayDescriptors();
	}
}

void IslandTerrain::RestorationSweep()
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return;
	}

	bool bDirty = false;
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		common::crc_t textureCrcs[4] =
		{
			rLazyChunk.header.islandHeader.elevationCrc,
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
		};

		int64_t iSlot = rTemplate.miTextureSlot;
		Texture** ppSlotEntries[4] =
		{
			&gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot],
			&gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot],
			&gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot],
			&gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot],
		};
		Texture** ppCanonicalEntries[4] =
		{
			&gpTextureManager->mRenderTargetTextures.mElevationTextures[0],
			&gpTextureManager->mRenderTargetTextures.mColorTextures[0],
			&gpTextureManager->mRenderTargetTextures.mNormalsTextures[0],
			&gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[0],
		};
		static constexpr const char* kpcChannelNames[4] = {"elevation", "color", "normal", "ao"};

		int32_t iRealCount = 0;
		for (int32_t i = 0; i < 4; ++i)
		{
			Texture* pReal = &gpTextureManager->mTextureMap.at(textureCrcs[i]);
			if (*ppSlotEntries[i] == pReal)
			{
				++iRealCount;
				continue;
			}

			// Slot still points at canonical for this channel. Patch back only after chunk reaches kReady.
			ChunkState eState = gpFileManager->GetLazyChunk(textureCrcs[i]).eState.load(std::memory_order_acquire);
			if (eState == ChunkState::kReady && *ppSlotEntries[i] == *ppCanonicalEntries[i])
			{
				*ppSlotEntries[i] = pReal;
				++iRealCount;
				bDirty = true;
				LOG(kGraphics, kVerbose, "Restored slot={} channel={} textureCrc={} (islandCrc={})", iSlot, kpcChannelNames[i], textureCrcs[i], rCrc);
			}
		}

		if (iRealCount == 4)
		{
			rTemplate.mbGpuResident = true;
			LOG(kGraphics, kVerbose, "Fully restored islandCrc={} slot={}", rCrc, iSlot);
		}
	}

	if (bDirty)
	{
		gpTextureManager->mTextureDescriptors.UpdateTextureArrayDescriptors();
	}
}
#endif

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	// Step is derived from the canonical template's quad size and heightmap resolution. Since
	// GlobalNormal is a 4-tap finite-difference over GlobalElevation (which routes per-placement
	// internally), the canonical step is a reasonable default sampling cadence.
	float fStep = mpCanonical->mfQuadFootprint / static_cast<float>(mpCanonical->miHeightmapSize);
	float fDistance = 2.0f * fStep;

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
