#include "IslandTerrain.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandChainPlacement.h"

#if defined(BT_CLIENT)
#include "Graphics/Managers/TextureManager.h"
#endif

#include "Game.h"

namespace engine
{

// Footprint-AREA thresholds (engine m^2) for the IslandChainPlacement size buckets. The multi-island
// export tiles a ~400 m master into 1x1 (400x400 Huge), 2x1/3x1 strips (Large), mid tiles (Medium), and
// 4x4 (100x100 Small). Area separates them where the larger dimension cannot — a 2x1 strip (200x400) and
// the 1x1 master (400x400) share the same long edge. Starting-point values; tune as the export settles.
inline constexpr float kfHugeIslandAreaMeters = 120000.0f;   // 1x1     = 400x400 = 160k
inline constexpr float kfLargeIslandAreaMeters = 48000.0f;   // 2x1/3x1 strips    = 53k-80k
inline constexpr float kfMediumIslandAreaMeters = 16000.0f;  // mid tiles; 4x4 (10k) falls below -> Small

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
		rTemplate.mfWorldFootprintXMeters = rLazyChunk.header.islandHeader.fWorldFootprintXMeters;
		rTemplate.mfWorldFootprintYMeters = rLazyChunk.header.islandHeader.fWorldFootprintYMeters;
		rTemplate.mfWorldElevationMeters = rLazyChunk.header.islandHeader.fWorldElevationMeters;
		rTemplate.mfMaxHeightMeters = rLazyChunk.header.islandHeader.fMaxHeightMeters;
		ASSERT(rTemplate.mfWorldFootprintXMeters > 0.0f);
		ASSERT(rTemplate.mfWorldFootprintYMeters > 0.0f);
		rTemplate.mfQuadFootprintX = rTemplate.mfWorldFootprintXMeters * kfMetersToUnits;
		rTemplate.mfQuadFootprintY = rTemplate.mfWorldFootprintYMeters * kfMetersToUnits;
		// Mesh counts come from manifest metadata (sync). CPU mesh pointers fill in WaitForElevationMaps
		// once the kIsland chunk payload is resident.
		rTemplate.miMeshVertexCount = rLazyChunk.header.islandHeader.iMeshVertexCount;
		rTemplate.miMeshIndexCount = rLazyChunk.header.islandHeader.iMeshIndexCount;
	}

	// Stable, deterministic iteration order for slot assignment (Phase 3).
	mIslandCrcsSorted.reserve(mIslands.size());
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		mIslandCrcsSorted.push_back(rCrc);
	}
	std::sort(mIslandCrcsSorted.begin(), mIslandCrcsSorted.end());

	// Assign each template its fixed per-frame array index (drives indirect-buffer slot and SSBO
	// stride range in Islands). Bake here, never changes after boot.
	for (int64_t i = 0; i < static_cast<int64_t>(mIslandCrcsSorted.size()); ++i)
	{
		mIslands.at(mIslandCrcsSorted[i]).miTemplateArrayIndex = i;
	}

	// Menu-browser order: largest footprint first (see header). Copy of the CRC-sorted list,
	// re-sorted by area; CRC tiebreak keeps it stable. mIslandCrcsSorted order is untouched.
	mIslandCrcsByArea = mIslandCrcsSorted;
	std::sort(mIslandCrcsByArea.begin(), mIslandCrcsByArea.end(), [this](common::crc_t crcA, common::crc_t crcB)
		{
			const IslandTemplate& rTemplateA = mIslands.at(crcA);
			const IslandTemplate& rTemplateB = mIslands.at(crcB);
			float fAreaA = rTemplateA.mfWorldFootprintXMeters * rTemplateA.mfWorldFootprintYMeters;
			float fAreaB = rTemplateB.mfWorldFootprintXMeters * rTemplateB.mfWorldFootprintYMeters;
			if (fAreaA != fAreaB)
			{
				return fAreaA > fAreaB;
			}
			return crcA < crcB;
		});

	// Bucket templates into 4 size classes by footprint area for IslandChainPlacement role selection.
	// Iterate the already-sorted CRC list so every bucket stays in deterministic CRC order (client + server).
	for (common::crc_t islandCrc : mIslandCrcsSorted)
	{
		const IslandTemplate& rTemplate = mIslands.at(islandCrc);
		float fAreaMeters = rTemplate.mfWorldFootprintXMeters * rTemplate.mfWorldFootprintYMeters;

		if (fAreaMeters >= kfHugeIslandAreaMeters)
		{
			mHugeCrcs.push_back(islandCrc);
		}
		else if (fAreaMeters >= kfLargeIslandAreaMeters)
		{
			mLargeCrcs.push_back(islandCrc);
		}
		else if (fAreaMeters >= kfMediumIslandAreaMeters)
		{
			mMediumCrcs.push_back(islandCrc);
		}
		else
		{
			mSmallCrcs.push_back(islandCrc);
		}
	}

#if defined(BT_CLIENT)
	// Unique-channel-CRC invariant (relied upon by EvictionSweep with NO refcount): every template's
	// 4 channel chunk CRCs (color/normals/AO/masks) are unique across all templates. They are
	// path-derived in DataPacker (common::Crc of "<island>/<Channel>", ExportIsland.cpp) and each
	// island lives in its own directory, so no two templates can share one. EvictionSweep frees a
	// channel Texture by CRC and UnregisterBindingsForKey()s it directly; if two templates ever shared
	// a channel CRC (e.g. a deduplicated texture), evicting one would free a Texture the other still
	// samples — a silent use-after-free that VerifyAllDescriptorGenerations cannot catch (the binding
	// records are erased on evict). Assert it fast at boot rather than refcounting (DataPacker enforces
	// it structurally; the assert just turns a future regression into a fail-fast).
	{
		std::vector<common::crc_t> channelCrcs;
		channelCrcs.reserve(mIslandCrcsSorted.size() * 4);
		for (common::crc_t islandCrc : mIslandCrcsSorted)
		{
			const common::IslandHeader& rIslandHeader = rChunkMap.at(islandCrc).header.islandHeader;
			channelCrcs.push_back(rIslandHeader.colorsCrc);
			channelCrcs.push_back(rIslandHeader.normalsCrc);
			channelCrcs.push_back(rIslandHeader.ambientOcclusionCrc);
			channelCrcs.push_back(rIslandHeader.masksCrc);
		}
		std::sort(channelCrcs.begin(), channelCrcs.end());
		ASSERT(std::adjacent_find(channelCrcs.begin(), channelCrcs.end()) == channelCrcs.end());
	}
#endif

	// Downstream (IslandChainPlacement, TextureManager slot-0 anchor) requires at least one island.
	ASSERT(!mIslandCrcsSorted.empty());

	// Open-ocean floor (outside any island) is a fixed depth below sea level. Heightmap pixel
	// values inside islands already carry real negative depth, so this constant only fires for
	// cells with no island placement. Unified with the elevation RTT clear value — keeping it
	// near the per-island sea floor avoids dramatic bilinear blends at island edges in the
	// elevation G-buffer (see Common/DataFile.h doc on kfSeaBottomMeters).
	mfSeaFloorElevation = common::kfSeaBottomMeters * kfMetersToUnits;

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

		// Chunk payload layout (set by ExportIsland::Export): [heightmap floats][float2 mesh positions][uint32 mesh indices][float2 valid-area hull verts].
		// miMeshVertexCount / miMeshIndexCount already populated in ctor from manifest header. The offset
		// math + the valid-area hull are shared: the server packs island placements against the rotated
		// hull (IslandChainPlacement); the client additionally uploads the mesh and debug-renders the hull.
		int64_t iHeightmapBytes = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(float));
		const std::byte* pAfterHeightmap = reinterpret_cast<const std::byte*>(rLazyChunk.pData) + iHeightmapBytes;
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 2 * static_cast<int64_t>(sizeof(float));
		int64_t iMeshIndexBytes = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t));
		rTemplate.miValidAreaVertexCount = rLazyChunk.header.islandHeader.iValidAreaVertexCount;
		int64_t iValidAreaBytes = static_cast<int64_t>(rTemplate.miValidAreaVertexCount) * static_cast<int64_t>(sizeof(XMFLOAT2));
		// Defensive: header.iSize is the unpadded chunk-data payload size set by
		// ExportJob::AllocateHeaderAndData. A stale float3-mesh pack file (pre-StripMeshZ) would
		// carry 1.5x the expected mesh-position payload, walking mpuiMeshIndices into garbage.
		// (Uncompressed chunks only — iUncompressedSize is zlib-only and stays 0 for islands.)
		ASSERT(rLazyChunk.header.iSize == iHeightmapBytes + iMeshPositionBytes + iMeshIndexBytes + iValidAreaBytes);
		rTemplate.mpf2ValidAreaVertices = reinterpret_cast<const XMFLOAT2*>(pAfterHeightmap + iMeshPositionBytes + iMeshIndexBytes);

#if defined(BT_CLIENT)
		// Mesh CPU pointers are client-only (feed the GPU mesh upload in CreateClientMeshBuffers).
		rTemplate.mpfMeshPositions = reinterpret_cast<const float*>(pAfterHeightmap);
		rTemplate.mpuiMeshIndices = reinterpret_cast<const uint32_t*>(pAfterHeightmap + iMeshPositionBytes);
#endif
	}

#if defined(BT_SERVER)
	// Phase 4: build NavContour for every template so multi-template cells produce correct nav data.
	LOG(kLoading, kInfo, "Building NavContour for {} island templates", mIslands.size());
	ScopedBootTimer scopedTimer(kBootTimerIslands);
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mpfHeightmapData != nullptr)
		{
			BuildNavContour(rTemplate.mNavContour, rTemplate.mpfHeightmapData, rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, fNavThreshold);
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

	// MAX over every island whose footprint rectangle contains the point. Rectangles may overlap now (the
	// chain packs by hull, not rectangle), so first-match would pick an arbitrary island; the highest terrain
	// must win, matching the GPU elevation prepass. Commutative max → order-independent and deterministic.
	float fMaxElevation = mfSeaFloorElevation;
	for (const IslandPlacement& rPlacement : it->second.staticData.islands)
	{
		const IslandTemplate& rTemplate = mIslands.at(rPlacement.islandCrc);
		float fFootprintX = rTemplate.mfQuadFootprintX;
		float fFootprintY = rTemplate.mfQuadFootprintY;

		// Inverse-rotate world point into island-local frame
		float fCos = std::cos(-rPlacement.fRotation);
		float fSin = std::sin(-rPlacement.fRotation);
		float fDx = f4Position.x - rPlacement.f2WorldPos.x;
		float fDy = f4Position.y - rPlacement.f2WorldPos.y;
		float fLocalX = fDx * fCos - fDy * fSin;
		float fLocalY = fDx * fSin + fDy * fCos;

		if (std::abs(fLocalX) > 0.5f * fFootprintX || std::abs(fLocalY) > 0.5f * fFootprintY)
		{
			continue;
		}

		// UV from local frame; V axis is world-Y inverted
		float fU = fLocalX / fFootprintX + 0.5f;
		float fV = 0.5f - fLocalY / fFootprintY;

		int64_t iX = static_cast<int64_t>(fU * static_cast<float>(rTemplate.miHeightmapWidth - 1));
		int64_t iY = static_cast<int64_t>(fV * static_cast<float>(rTemplate.miHeightmapHeight - 1));
		iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapWidth - 1));
		iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapHeight - 1));

		// Heightmap value is already engine-meters (DataPacker shifted Gaea's [0,1] normalized
		// output by the per-island beach offset `Level × elevationMeters` read from the archetype
		// Sea node). Beach = 0; negative = water; positive = land. Fold into the running max.
		fMaxElevation = std::max(fMaxElevation, rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapWidth + iX]);
	}

	return fMaxElevation;
}

void XM_CALLCONV IslandTerrain::BuildElevationGrid(GridCoord coord, const std::vector<IslandPlacement>& rPlacements, std::vector<float>& rOutGrid) const
{
	static constexpr int64_t kiDim = game::Frame::kiElevationGridDim;
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;
	static constexpr float fGridPitchX = fCellWidth / static_cast<float>(kiDim);
	static constexpr float fGridPitchY = fCellHeight / static_cast<float>(kiDim);

	// Sea floor everywhere first; each placement then max-blends its footprint over the top
	// (commutative max → splat order doesn't matter, matches GlobalElevation's per-point semantics).
	rOutGrid.assign(static_cast<size_t>(kiDim * kiDim), mfSeaFloorElevation);

	// World-space origin of this cell (south-west corner of grid texel 0,0)
	float fCellOriginX = fCellMinX + static_cast<float>(coord.x) * fCellWidth;
	float fCellOriginY = fCellMinY + static_cast<float>(coord.y) * fCellHeight;

	for (const IslandPlacement& rPlacement : rPlacements)
	{
		const IslandTemplate& rTemplate = mIslands.at(rPlacement.islandCrc);
		float fFootprintX = rTemplate.mfQuadFootprintX;
		float fFootprintY = rTemplate.mfQuadFootprintY;
		float fHalfX = 0.5f * fFootprintX;
		float fHalfY = 0.5f * fFootprintY;

		// Trig is constant per placement — hoist out of the per-texel loop (the old per-point
		// GlobalElevation recomputed std::cos / std::sin on every call). Negated rotation
		// matches the inverse-rotate world->local convention used by GlobalElevation.
		common::SinCos rotation = common::DeterministicSinCos(-rPlacement.fRotation);
		float fCos = rotation.fCos;
		float fSin = rotation.fSin;

		// World-AABB of the rotated quad: the 4 corners of the rotated footprint, projected onto X/Y.
		float fAbsCos = std::abs(fCos);
		float fAbsSin = std::abs(fSin);
		float fAabbHalfX = fAbsCos * fHalfX + fAbsSin * fHalfY;
		float fAabbHalfY = fAbsSin * fHalfX + fAbsCos * fHalfY;
		float fAabbMinX = rPlacement.f2WorldPos.x - fAabbHalfX;
		float fAabbMaxX = rPlacement.f2WorldPos.x + fAabbHalfX;
		float fAabbMinY = rPlacement.f2WorldPos.y - fAabbHalfY;
		float fAabbMaxY = rPlacement.f2WorldPos.y + fAabbHalfY;

		// Clamp AABB to this cell's grid index range. Texel center at (ix + 0.5) * pitch.
		int64_t iMinGx = static_cast<int64_t>(std::floor((fAabbMinX - fCellOriginX) / fGridPitchX - 0.5f));
		int64_t iMaxGx = static_cast<int64_t>(std::floor((fAabbMaxX - fCellOriginX) / fGridPitchX - 0.5f));
		int64_t iMinGy = static_cast<int64_t>(std::floor((fAabbMinY - fCellOriginY) / fGridPitchY - 0.5f));
		int64_t iMaxGy = static_cast<int64_t>(std::floor((fAabbMaxY - fCellOriginY) / fGridPitchY - 0.5f));
		iMinGx = std::clamp(iMinGx, static_cast<int64_t>(0), kiDim - 1);
		iMaxGx = std::clamp(iMaxGx, static_cast<int64_t>(0), kiDim - 1);
		iMinGy = std::clamp(iMinGy, static_cast<int64_t>(0), kiDim - 1);
		iMaxGy = std::clamp(iMaxGy, static_cast<int64_t>(0), kiDim - 1);

		float fHeightmapMaxU = static_cast<float>(rTemplate.miHeightmapWidth - 1);
		float fHeightmapMaxV = static_cast<float>(rTemplate.miHeightmapHeight - 1);
		float fInvFootprintX = 1.0f / fFootprintX;
		float fInvFootprintY = 1.0f / fFootprintY;

		for (int64_t iGy = iMinGy; iGy <= iMaxGy; ++iGy)
		{
			float fWorldY = fCellOriginY + (static_cast<float>(iGy) + 0.5f) * fGridPitchY;
			float fDy = fWorldY - rPlacement.f2WorldPos.y;
			for (int64_t iGx = iMinGx; iGx <= iMaxGx; ++iGx)
			{
				float fWorldX = fCellOriginX + (static_cast<float>(iGx) + 0.5f) * fGridPitchX;
				float fDx = fWorldX - rPlacement.f2WorldPos.x;
				float fLocalX = fDx * fCos - fDy * fSin;
				float fLocalY = fDx * fSin + fDy * fCos;

				if (std::abs(fLocalX) > fHalfX || std::abs(fLocalY) > fHalfY)
				{
					continue;
				}

				float fU = fLocalX * fInvFootprintX + 0.5f;
				float fV = 0.5f - fLocalY * fInvFootprintY;

				int64_t iX = static_cast<int64_t>(fU * fHeightmapMaxU);
				int64_t iY = static_cast<int64_t>(fV * fHeightmapMaxV);
				iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapWidth - 1));
				iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(rTemplate.miHeightmapHeight - 1));

				float fSample = rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapWidth + iX];
				float& rfCell = rOutGrid[static_cast<size_t>(iGy * kiDim + iGx)];
				rfCell = std::max(rfCell, fSample);
			}
		}
	}
}

float XM_CALLCONV IslandTerrain::FrameElevation(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const
{
	if (rStaticData.elevationGrid.empty())
	{
		return mfSeaFloorElevation;
	}

	static constexpr int64_t kiDim = game::Frame::kiElevationGridDim;
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;
	static constexpr float fGridPitchX = fCellWidth / static_cast<float>(kiDim);
	static constexpr float fGridPitchY = fCellHeight / static_cast<float>(kiDim);

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	float fCellOriginX = fCellMinX + static_cast<float>(rStaticData.coord.x) * fCellWidth;
	float fCellOriginY = fCellMinY + static_cast<float>(rStaticData.coord.y) * fCellHeight;
	float fLocalX = f4Position.x - fCellOriginX;
	float fLocalY = f4Position.y - fCellOriginY;
	int64_t iGx = static_cast<int64_t>(std::floor(fLocalX / fGridPitchX));
	int64_t iGy = static_cast<int64_t>(std::floor(fLocalY / fGridPitchY));
	if (iGx < 0 || iGx >= kiDim || iGy < 0 || iGy >= kiDim)
	{
		return mfSeaFloorElevation;
	}

	return rStaticData.elevationGrid[static_cast<size_t>(iGy * kiDim + iGx)];
}

XMVECTOR XM_CALLCONV IslandTerrain::FrameNormal(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const
{
	// 4-tap finite-difference over FrameElevation. Same baseline as GlobalNormal so contour-following
	// AI behaves identically — only the elevation source changes.
	float fDistance = 2.0f * kfMetersToUnits;

	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, FrameElevation(rStaticData, vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, FrameElevation(rStaticData, vecTopRight));
	auto vecBottomLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomLeft = XMVectorSetZ(vecBottomLeft, FrameElevation(rStaticData, vecBottomLeft));
	auto vecBottomRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomRight = XMVectorSetZ(vecBottomRight, FrameElevation(rStaticData, vecBottomRight));

	return XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vecTopRight, vecBottomLeft), XMVectorSubtract(vecTopLeft, vecBottomRight)));
}

#if defined(BT_CLIENT)
void IslandTerrain::CreateClientMeshBuffers()
{
	// One-shot at boot: upload each template's Gaea Mesher-baked terrain mesh into a permanent
	// GPU buffer. Layout: [uint32 indices, float2 XY positions] — indices first matches the
	// existing terrain/water mesh-buffer convention (BufferManager.cpp). Z is omitted —
	// Terrain.vert re-derives world Z from the elevation sampler.
	//
	// Eager (not lazy) because the terrain command buffer is record-once and binds every
	// template's mesh at record time; lazy create would force a runtime CB re-record on
	// first visit, which violates the record-once invariant (see Graphics/CLAUDE.md).
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		ASSERT(rTemplate.mpfMeshPositions != nullptr);
		ASSERT(rTemplate.mpuiMeshIndices != nullptr);
		ASSERT(rTemplate.miMeshVertexCount > 0);
		ASSERT(rTemplate.miMeshIndexCount > 0);
		ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE);

		int64_t iMeshIndexBytes = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t));
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 2 * static_cast<int64_t>(sizeof(float));
		rTemplate.mMeshBuffer.Create(
		{
			.name = "IslandMesh",
			.flags = {BufferFlags::kIndexVertex, BufferFlags::kDeviceLocal},
			.iCount = rTemplate.miMeshIndexCount,
			.vkIndexType = VK_INDEX_TYPE_UINT32,
			.iVertexStride = static_cast<int64_t>(2 * sizeof(float)),
			.dataVkDeviceSize = static_cast<VkDeviceSize>(iMeshIndexBytes + iMeshPositionBytes),
		},
		[&rTemplate, iMeshIndexBytes, iMeshPositionBytes](void* pData)
		{
			std::memcpy(pData, rTemplate.mpuiMeshIndices, static_cast<size_t>(iMeshIndexBytes));
			std::memcpy(static_cast<char*>(pData) + iMeshIndexBytes, rTemplate.mpfMeshPositions, static_cast<size_t>(iMeshPositionBytes));
		});
		LOG(kGraphics, kDebug, "Uploaded island mesh: crc={} vertices={} indices={}", rCrc, rTemplate.miMeshVertexCount, rTemplate.miMeshIndexCount);
	}

	// Resident-memory footprint instrumentation (Documents/Plans/Graphics resident-memory scaling).
	// meshCpu / heightmap / hull all slice into the kIsland chunk payload (FileManager-resident
	// CPU RAM); meshGpu is the device-local VRAM buffer uploaded above.
	int64_t iTotalMeshCpu = 0;
	int64_t iTotalMeshGpu = 0;
	int64_t iTotalHeightmap = 0;
	int64_t iTotalHull = 0;
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		int64_t iMeshCpu = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t))
			+ static_cast<int64_t>(rTemplate.miMeshVertexCount) * 2 * static_cast<int64_t>(sizeof(float));
		int64_t iMeshGpu = iMeshCpu;
		int64_t iHeightmap = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(float));
		int64_t iHull = static_cast<int64_t>(rTemplate.miValidAreaVertexCount) * static_cast<int64_t>(sizeof(XMFLOAT2));
		LOG(kGraphics, kDebug, "[DEBUG-resmem] Island residency: crc={} meshCpu={} meshGpu={} heightmap={} hull={}", rCrc, iMeshCpu, iMeshGpu, iHeightmap, iHull);
		iTotalMeshCpu += iMeshCpu;
		iTotalMeshGpu += iMeshGpu;
		iTotalHeightmap += iHeightmap;
		iTotalHull += iHull;
	}
	LOG(kGraphics, kDebug, "[DEBUG-resmem] Island residency aggregate: templates={} meshCpu={} meshGpu={} heightmap={} hull={}", static_cast<int64_t>(mIslands.size()), iTotalMeshCpu, iTotalMeshGpu, iTotalHeightmap, iTotalHull);
}

namespace
{
	// Upload an island's heightmap into its template-owned mElevationTexture as an R32_SFLOAT image.
	// Reused at first-mint and on device-loss re-Create. Descriptor patching is deferred to
	// RestorationSweep so it lands inside RenderGlobal's post-fence-wait descriptor-patch window.
	void CreateElevationTextureFromHeightmap(IslandTemplate& rTemplate, std::string_view name)
	{
		// Boot ordering invariant: WaitForElevationMaps (called once at startup) is the only writer of
		// mpfHeightmapData. AcquireTextureSlot must never run before it.
		ASSERT(rTemplate.mpfHeightmapData != nullptr);
		// Heap: Texture::Create allocates GPU resources and uses a OneShotCommandBuffer.
		ScopedSuppressAllocationTracking suppress;
		rTemplate.mElevationTexture.Create(
			TextureInfo
			{
				.name = name,
				.format = VK_FORMAT_R32_SFLOAT,
				.extent = {static_cast<uint32_t>(rTemplate.miHeightmapWidth), static_cast<uint32_t>(rTemplate.miHeightmapHeight), 1u},
				.mipLevels = 1u,
				.arrayLayers = 1u,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.eTextureLayout = TextureLayout::kShaderReadOnly,
			},
			[&rTemplate](void* pData, int64_t iPosition, int64_t iSize)
			{
				std::memcpy(pData, reinterpret_cast<const std::byte*>(rTemplate.mpfHeightmapData) + iPosition, static_cast<size_t>(iSize));
			});
	}
}

int64_t IslandTerrain::AcquireTextureSlot(common::crc_t islandCrc)
{
	IslandTemplate& rTemplate = mIslands.at(islandCrc);

	// Hot path: slot assigned, GPU resources resident. Return early.
	if (rTemplate.miTextureSlot >= 0 && rTemplate.mbGpuResident)
	{
		return rTemplate.miTextureSlot;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	// Color / Normals / AO / Masks ship as standalone lazy-texture chunks. Elevation lives on the template
	// (mElevationTexture) and is uploaded directly from the in-memory heightmap — no chunk, no CRC.
	common::crc_t textureCrcs[4] =
	{
		rLazyChunk.header.islandHeader.colorsCrc,
		rLazyChunk.header.islandHeader.normalsCrc,
		rLazyChunk.header.islandHeader.ambientOcclusionCrc,
		rLazyChunk.header.islandHeader.masksCrc,
	};

	if (rTemplate.miTextureSlot < 0)
	{
		// First-mint. Slot 0 stays the neutral placeholder anchor (no real island ever maps there).
		// Slot N points at the matching mTextureMap entries directly — their mVkImageView starts as
		// the white placeholder and is swapped to the real view by AdoptTransferredImage when each
		// chunk reaches kReady. Per-slot RegisterTextureBinding hooks each CRC into the existing
		// UpdateDescriptorsForTexture pipeline so the descriptor write follows the view swap (same
		// pattern as water normals / other bindless arrays).
		// Reuse a slot reclaimed by a prior eviction before extending the high-water mark, so churn
		// (e.g. the menu island browser cycling repeatedly) reuses indices rather than exhausting the
		// fixed kiMaxIslands-sized descriptor arrays.
		int64_t iSlot = 0;
		if (mFreeTextureSlots.empty())
		{
			iSlot = miNextTextureSlot++;
		}
		else
		{
			iSlot = mFreeTextureSlots.back();
			mFreeTextureSlots.pop_back();
		}
		ASSERT(iSlot >= 1 && iSlot < shaders::kiMaxIslands);
		rTemplate.miTextureSlot = iSlot;

		// Elevation: uploaded directly from the in-memory heightmap into the template-owned
		// mElevationTexture. Descriptor patching deferred to RestorationSweep (safety window).
		CreateElevationTextureFromHeightmap(rTemplate, rLazyChunk.header.pcPath);

		Texture* pColor = &gpTextureManager->mTextureMap.at(textureCrcs[0]);
		Texture* pNormals = &gpTextureManager->mTextureMap.at(textureCrcs[1]);
		Texture* pAmbientOcclusion = &gpTextureManager->mTextureMap.at(textureCrcs[2]);
		Texture* pMasks = &gpTextureManager->mTextureMap.at(textureCrcs[3]);

		gpTextureManager->mRenderTargetTextures.mElevationTextures.at(iSlot) = &rTemplate.mElevationTexture;
		gpTextureManager->mRenderTargetTextures.mColorTextures.at(iSlot) = pColor;
		gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(iSlot) = pNormals;
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(iSlot) = pAmbientOcclusion;
		gpTextureManager->mRenderTargetTextures.mMasksTextures.at(iSlot) = pMasks;

		// First-mint is infrequent (one per unique islandCrc) but RegisterTextureBinding inserts
		// into the binding map, which can allocate.
		//
		// Source of truth for the (array, consumer-pipelines, binding, sampler) tuple is the
		// pipeline declarations in PipelineManager.cpp — each DescriptorInfo flagged with
		// kBindlessArrayConsumer self-registers into TextureDescriptors::mBindlessArrayConsumers
		// at pipeline-create time, keyed by ppTextures. Here we iterate the per-array consumer
		// list and register each pipeline under the correct binding key:
		//   * Elevation: islandCrc (template-owned Texture, no mTextureMap entry; patched by
		//     RestorationSweep's UpdateArrayBindingsForKey).
		//   * Color / Normals / AO / Masks: per-chunk CRCs (chunk Textures live in mTextureMap;
		//     each is patched by UpdateDescriptorsForTexture when its chunk reaches kReady).
		{
			ScopedSuppressAllocationTracking suppress;
			TextureDescriptors& rTextureDescriptors = gpTextureManager->mTextureDescriptors;
			auto Register = [&](common::crc_t bindingKey, Texture** ppArray)
			{
				// find() + ASSERT instead of operator[]: a missing kBindlessArrayConsumer flag on the
				// pipeline declaration would otherwise silently insert an empty vector here and drop
				// the registration — precisely the bug class commit 09fb128 introduced and this design
				// exists to prevent.
				auto it = rTextureDescriptors.mBindlessArrayConsumers.find(ppArray);
				ASSERT(it != rTextureDescriptors.mBindlessArrayConsumers.end());
				for (const TextureDescriptors::BindlessArrayConsumer& rConsumer : it->second)
				{
					rTextureDescriptors.RegisterTextureBinding(bindingKey, rConsumer.pPipeline, rConsumer.iBinding, rConsumer.samplerFlags, nullptr, ppArray, rConsumer.iCount, iSlot);
				}
			};
			Register(islandCrc,      gpTextureManager->mRenderTargetTextures.mElevationTextures.data());
			Register(textureCrcs[0], gpTextureManager->mRenderTargetTextures.mColorTextures.data());
			Register(textureCrcs[1], gpTextureManager->mRenderTargetTextures.mNormalsTextures.data());
			Register(textureCrcs[2], gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data());
			Register(textureCrcs[3], gpTextureManager->mRenderTargetTextures.mMasksTextures.data());
		}

		// Mesh buffer was created at boot by CreateClientMeshBuffers (record-once CB invariant —
		// terrain CB binds every template's mesh at record time).
		ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE);

		rTemplate.mbGpuResident = false;
		gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
		LOG(kGraphics, kVerbose, "First-mint slot={} islandCrc={}", iSlot, islandCrc);

		return iSlot;
	}

	// Slot assigned but not yet resident. Reached only while a freshly-minted template's chunks are
	// still loading — the slot stays in slot-0 fallback until RestorationSweep patches it back. Two
	// paths that might seem to land here do not: LRU eviction fully tears the slot down (miTextureSlot
	// = -1 — see EvictionSweep) so an evicted-then-revisited template re-mints above; and device-loss
	// recovery runs ResetTextureSlots (TextureManager ctor) which forces miTextureSlot < 0 for every
	// template, so they all re-mint above too — re-Creating mElevationTexture via the first-mint path.
	gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
	LOG(kLoading, kVerbose, "Re-acquire islandCrc={} slot={}, requesting chunk loads", islandCrc, rTemplate.miTextureSlot);
	return rTemplate.miTextureSlot;
}

bool IslandTerrain::AnyEvictionPending() const
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return false;
	}
	// Mirrors the EvictionSweep skip logic: a template evicts when it owns a real slot, is resident,
	// has no active references, and its grace window has elapsed.
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.miTextureSlot != 0 && rTemplate.mbGpuResident && rTemplate.miRefCount == 0
			&& (gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame) > kuiGraceRenderFrames)
		{
			return true;
		}
	}
	return false;
}

bool IslandTerrain::AnyRestorationPending() const
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return false;
	}
	// Mirrors the RestorationSweep condition: a non-resident template with a real slot whose 4 chunk
	// channels have all reached kReady is about to be patched back to its real Texture*s.
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			continue;
		}
		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		common::crc_t residencyCrcs[4] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
			rLazyChunk.header.islandHeader.masksCrc,
		};
		bool bAllReady = true;
		for (common::crc_t textureCrc : residencyCrcs)
		{
			if (gpFileManager->GetLazyChunk(textureCrc).eState.load(std::memory_order_acquire) < ChunkState::kReady)
			{
				bAllReady = false;
				break;
			}
		}
		if (bAllReady)
		{
			return true;
		}
	}
	return false;
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
		// Slot 0 is the neutral placeholder anchor (never adopted by a real island). The guard is
		// defensive — miNextTextureSlot starts at 1 so no template should ever have slot 0 — but
		// keep it to document the invariant.
		if (rTemplate.miTextureSlot == 0 || !rTemplate.mbGpuResident || rTemplate.miRefCount != 0)
		{
			continue;
		}
		if ((gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame) <= kuiGraceRenderFrames)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		// The 4 chunk-backed channels (color/normals/AO/masks). Elevation is template-owned (no chunk
		// CRC) and is evicted separately below — it is no longer permanently resident.
		common::crc_t evictCrcs[4] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
			rLazyChunk.header.islandHeader.masksCrc,
		};

		LOG(kGraphics, kVerbose, "Evicting islandCrc={} slot={} (refCount=0, framesSinceUse={})", rCrc, rTemplate.miTextureSlot, gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame);

		// FreeGpuResources destroys the VkImageView, so reset the bindless Set 0 mImageInfos slot
		// back to the white placeholder (matches the initial fill in TextureManager::Create) before
		// UpdateTextureArrayDescriptors below writes the array. Otherwise the dangling handle trips
		// VUID-VkWriteDescriptorSet-descriptorType-02996 at the next descriptor update.
		TextureDescriptors& rTextureDescriptors = gpTextureManager->mTextureDescriptors;
		for (common::crc_t textureCrc : evictCrcs)
		{
			gpTextureManager->mTextureMap.at(textureCrc).FreeGpuResources();
			rTextureDescriptors.mImageInfos.at(rTextureDescriptors.mImageInfosMap.at(textureCrc)).imageView = gpTextureManager->mWhiteTexture.mVkImageView;
		}

		int64_t iSlot = rTemplate.miTextureSlot;
		gpTextureManager->mRenderTargetTextures.mColorTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mColorTextures.at(0);
		gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(0);
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(0);
		gpTextureManager->mRenderTargetTextures.mMasksTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mMasksTextures.at(0);

		// Elevation participates in eviction too: free the template-owned image and drop the slot to
		// the elevation placeholder. It has no Set 0 mImageInfos entry, so unlike the 4 channels above
		// there is no white-image reset — only the Set 1 array pointer. The next AcquireTextureSlot
		// first-mint (forced by miTextureSlot = -1 below) re-Creates it from the in-memory heightmap.
		rTemplate.mElevationTexture.FreeGpuResources();
		gpTextureManager->mRenderTargetTextures.mElevationTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mElevationTextures.at(0);

		// The pointer resets above only redirect the slot to the slot-0 placeholders; the per-pipeline
		// Set-1 array descriptors at iSlot still physically hold the freed island's destroyed
		// VkImageViews (FreeGpuResources destroyed them, and only Set-0 mImageInfos was reset). A slot
		// recycled off mFreeTextureSlots renders its new occupant's quad before that occupant's
		// restoration patch, sampling the destroyed views (VUID-vkCmdDrawIndexed-None-08114 use-after-
		// free → GPU hang). EvictionSweep runs in RenderGlobal's drained window, so rewrite those
		// elements now to the placeholder views the pointers were just reset to.
		rTextureDescriptors.WriteArrayElementFromLive(gpTextureManager->mRenderTargetTextures.mElevationTextures.data(), iSlot);
		rTextureDescriptors.WriteArrayElementFromLive(gpTextureManager->mRenderTargetTextures.mColorTextures.data(), iSlot);
		rTextureDescriptors.WriteArrayElementFromLive(gpTextureManager->mRenderTargetTextures.mNormalsTextures.data(), iSlot);
		rTextureDescriptors.WriteArrayElementFromLive(gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data(), iSlot);
		rTextureDescriptors.WriteArrayElementFromLive(gpTextureManager->mRenderTargetTextures.mMasksTextures.data(), iSlot);

		// Reclaim the slot: erase the binding records for all 5 keys (elevation islandCrc + 4 channel
		// CRCs) so PipelineManager::VerifyAllDescriptorGenerations never observes a snapshot pointing
		// at a freed image, and return the index to the free-list. Re-mint re-registers fresh records.
		{
			// Heap: unordered_map::erase + vector push_back; runs inside RenderGlobal (EvictionSweep).
			ScopedSuppressAllocationTracking suppress;
			rTextureDescriptors.UnregisterBindingsForKey(rCrc);
			for (common::crc_t textureCrc : evictCrcs)
			{
				rTextureDescriptors.UnregisterBindingsForKey(textureCrc);
			}
			mFreeTextureSlots.push_back(iSlot);
		}
		rTemplate.miTextureSlot = -1;

		gpFileManager->ResetTextureChunkStates(evictCrcs);
		rTemplate.mbGpuResident = false;
		bDirty = true;

		LOG(kLoading, kVerbose, "Reset chunk states for evicted islandCrc={} evictCrcs=[{},{},{},{}]", rCrc, evictCrcs[0], evictCrcs[1], evictCrcs[2], evictCrcs[3]);
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

	// Slot pointers are set at AcquireTextureSlot first-mint (and re-set on re-mint after an eviction
	// reclaimed the slot), so the remaining work here is residency tracking. Color/normals/AO
	// descriptor writes flow through ProcessPendingTextures' UpdateDescriptorsForTexture path as each
	// chunk reaches kReady; elevation is patched here on the resident transition because it bypassed
	// that path.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		// Elevation has no chunk state — it's template-owned and uploaded once at first-mint —
		// so residency is gated only on the other 4 channels.
		common::crc_t residencyCrcs[4] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
			rLazyChunk.header.islandHeader.masksCrc,
		};

		bool bAllReady = true;
		for (common::crc_t textureCrc : residencyCrcs)
		{
			ChunkState eState = gpFileManager->GetLazyChunk(textureCrc).eState.load(std::memory_order_acquire);
			if (eState < ChunkState::kReady)
			{
				bAllReady = false;
				break;
			}
		}
		if (bAllReady)
		{
			rTemplate.mbGpuResident = true;
			// Patch the elevation array binding inside the safety window (RestorationSweep runs in
			// RenderGlobal post-fence-wait). The Texture's real VkImageView was created at first-mint
			// but the per-pipeline array descriptor still points at the slot-0 placeholder snapshot
			// taken at RegisterTextureBinding time. islandCrc was used as the binding key (the
			// template-owned mElevationTexture has no chunk CRC).
			gpTextureManager->mTextureDescriptors.UpdateArrayBindingsForKey(rCrc);
			LOG(kGraphics, kVerbose, "Island resident islandCrc={} slot={}", rCrc, rTemplate.miTextureSlot);
		}
	}
}
#endif

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	// 4-tap finite-difference over GlobalElevation. Each tap routes through GlobalElevation
	// which finds its own island template, so a single fixed baseline works across multiple
	// islands at different scales. The 2-unit cross-tap baseline (1 meter per half-step at
	// the current kfMetersToUnits = 1.0) is fine-grained enough to capture normals without
	// falling below per-pixel heightmap noise.
	float fDistance = 2.0f * kfMetersToUnits;

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

#if defined(BT_CLIENT)
void IslandTerrain::ReleaseGpuResources()
{
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		rTemplate.mMeshBuffer.Destroy();
		// mElevationTexture is template-owned (no mTextureMap entry), so TextureManager's wholesale
		// destroy doesn't touch it — release here alongside the mesh buffer. On device-loss recovery the
		// TextureManager ctor's ResetTextureSlots forces miTextureSlot < 0 for every template, so the
		// next AcquireTextureSlot re-Creates mElevationTexture via the first-mint path.
		rTemplate.mElevationTexture.FreeGpuResources();
		// Clear residency so no template is left marked resident across the GPU-resource release. The
		// full miTextureSlot reset that re-points the dangling color/normals/AO mRenderTargetTextures
		// slots (and forces first-mint) happens in ResetTextureSlots, which TextureManager's ctor calls.
		rTemplate.mbGpuResident = false;
	}
}

void IslandTerrain::ResetTextureSlots()
{
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		rTemplate.miTextureSlot = -1;
		rTemplate.mbGpuResident = false;
		rTemplate.miRefCount = 0;
		rTemplate.muiLastUsedRenderFrame = 0;
	}
	miNextTextureSlot = 1;
	// Device-loss resets the high-water mark to 1; stale recycled indices would collide with the
	// freshly re-minted slots against the reset descriptor arrays.
	mFreeTextureSlots.clear();
}
#endif

} // namespace engine
