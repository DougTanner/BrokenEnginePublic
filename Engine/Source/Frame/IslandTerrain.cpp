#include "IslandTerrain.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandChainPlacement.h"

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
	ASSERT(gpIslandTerrain == nullptr);

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
		rTemplate.mfQuadFootprintX = rTemplate.mfWorldFootprintXMeters;
		rTemplate.mfQuadFootprintY = rTemplate.mfWorldFootprintYMeters;
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
	mfSeaFloorElevation = common::kfSeaBottomMeters;

	gpFileManager->RequestChunkLoad(mIslandCrcsSorted, LoadPriority::kRealtime);
}

IslandTerrain::~IslandTerrain()
{
	if (gpIslandTerrain == this)
	{
		gpIslandTerrain = nullptr;
	}
}

void IslandTerrain::WaitForElevationMaps([[maybe_unused]] float fNavThreshold)
{
	gpFileManager->WaitForChunks(mIslandCrcsSorted);

	const std::unordered_map<common::crc_t, LazyChunk>& rChunkMap = gpFileManager->GetLazyChunkMap();
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		const LazyChunk& rLazyChunk = rChunkMap.at(rCrc);
		rTemplate.mpHeightmapHalf = reinterpret_cast<const uint16_t*>(rLazyChunk.pData);
		rTemplate.miHeightmapWidth = rLazyChunk.header.islandHeader.iHeightmapWidth;
		rTemplate.miHeightmapHeight = rLazyChunk.header.islandHeader.iHeightmapHeight;

		// Chunk payload layout (set by ExportIsland::Export): [heightmap R16 halfs][float2 mesh positions][uint32 mesh indices][float2 valid-area hull verts].
		// miMeshVertexCount / miMeshIndexCount already populated in ctor from manifest header. The offset
		// math + the valid-area hull are shared: the server packs island placements against the rotated
		// hull (IslandChainPlacement); the client additionally uploads the mesh and debug-renders the hull.
		int64_t iHeightmapBytes = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(uint16_t));
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

#if defined(BT_SERVER)
		// The server never reads the mesh CPU slice (no GPU upload, no device loss), so reclaim it immediately after
		// load: decommit the [positions][indices] sub-range of the kIsland chunk. Heightmap (before, offset 0) and hull
		// (after) stay resident — the server reads the heightmap for NavContour below and the hull for placement/nav.
		gpFileManager->DecommitChunkRange(rCrc, static_cast<uint64_t>(iHeightmapBytes), static_cast<uint64_t>(iMeshPositionBytes + iMeshIndexBytes));
#endif
	}

#if defined(BT_SERVER)
	// Phase 4: build NavContour for every template so multi-template cells produce correct nav data.
	LOG(kLoading, kInfo, "Building NavContour for {} island templates", mIslands.size());
	ScopedBootTimer scopedTimer(kBootTimerIslands);
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mpHeightmapHalf != nullptr)
		{
			// BuildNavContour consumes full-precision floats; dequantize the R16 heightmap into a transient
			// boot buffer (one template at a time, freed each iteration) rather than widening the nav API.
			int64_t iHeightmapTexels = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight);
			std::vector<float> heightmapFloats(static_cast<size_t>(iHeightmapTexels));
			DirectX::PackedVector::XMConvertHalfToFloatStream(heightmapFloats.data(), sizeof(float), rTemplate.mpHeightmapHalf, sizeof(uint16_t), static_cast<size_t>(iHeightmapTexels));
			BuildNavContour(rTemplate.mNavContour, heightmapFloats.data(), rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, fNavThreshold);
		}
	}
#endif
}

float XM_CALLCONV IslandTerrain::GlobalElevation(FXMVECTOR vecPosition) const
{
	// Frame Purity Constraint (IslandTerrain.h): GlobalElevation/GlobalNormal walk mCoordFrames with
	// libm trig and must never run from frame-tick code — the sim hot path uses FrameElevation/FrameNormal.
	// GlobalNormal routes every finite-difference tap through this function, so guarding here covers both.
	ASSERT(common::gpThreadLocal == nullptr || !common::gpThreadLocal->mbInFrameTick);

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
		fMaxElevation = std::max(fMaxElevation, DirectX::PackedVector::XMConvertHalfToFloat(rTemplate.mpHeightmapHalf[iY * rTemplate.miHeightmapWidth + iX]));
	}

	return fMaxElevation;
}

namespace
{

// Splat one island placement's heightmap into the per-cell elevation grid (max-blend over the rotated
// footprint). Pure deterministic computation — the grid feeds FrameElevation, which steers CRC'd sim
// positions, so client and server must build a bit-identical grid. fCellOrigin is the cell's south-west
// corner in world space.
void BlendPlacementIntoGrid(const IslandPlacement& rPlacement, const IslandTemplate& rTemplate, float fCellOriginX, float fCellOriginY, std::vector<float>& rOutGrid)
{
	static constexpr int64_t kiDim = game::Frame::kiElevationGridDim;
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fGridPitchX = fCellWidth / static_cast<float>(kiDim);
	static constexpr float fGridPitchY = fCellHeight / static_cast<float>(kiDim);

	float fFootprintX = rTemplate.mfQuadFootprintX;
	float fFootprintY = rTemplate.mfQuadFootprintY;
	float fHalfX = 0.5f * fFootprintX;
	float fHalfY = 0.5f * fFootprintY;

	// Trig is constant per placement — hoist out of the per-texel loop (GlobalElevation's per-point
	// path recomputes std::cos / std::sin per call; this grid builder needs it only once). Negated
	// rotation matches the inverse-rotate world->local convention used by GlobalElevation.
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

			float fSample = DirectX::PackedVector::XMConvertHalfToFloat(rTemplate.mpHeightmapHalf[iY * rTemplate.miHeightmapWidth + iX]);
			float& rfCell = rOutGrid[static_cast<size_t>(iGy * kiDim + iGx)];
			rfCell = std::max(rfCell, fSample);
		}
	}
}

} // anonymous namespace

void XM_CALLCONV IslandTerrain::BuildElevationGrid(GridCoord coord, const std::vector<IslandPlacement>& rPlacements, std::vector<float>& rOutGrid) const
{
	static constexpr int64_t kiDim = game::Frame::kiElevationGridDim;
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;

	// Sea floor everywhere first; each placement then max-blends its footprint over the top
	// (commutative max → splat order doesn't matter, matches GlobalElevation's per-point semantics).
	rOutGrid.assign(static_cast<size_t>(kiDim * kiDim), mfSeaFloorElevation);

	// World-space origin of this cell (south-west corner of grid texel 0,0)
	float fCellOriginX = fCellMinX + static_cast<float>(coord.x) * fCellWidth;
	float fCellOriginY = fCellMinY + static_cast<float>(coord.y) * fCellHeight;

	for (const IslandPlacement& rPlacement : rPlacements)
	{
		BlendPlacementIntoGrid(rPlacement, mIslands.at(rPlacement.islandCrc), fCellOriginX, fCellOriginY, rOutGrid);
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
	float fDistance = 2.0f;

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

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	// 4-tap finite-difference over GlobalElevation. Each tap routes through GlobalElevation
	// which finds its own island template, so a single fixed baseline works across multiple
	// islands at different scales. The 2-unit cross-tap baseline (1 meter per half-step, since
	// islands use 1 m = 1 engine unit) is fine-grained enough to capture normals without
	// falling below per-pixel heightmap noise.
	float fDistance = 2.0f;

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
