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
		// Payload-derived dimensions and counts fill in only after WaitForElevationMaps validates
		// the resident kIsland chunk.
	}

	// Stable, deterministic iteration order for slot assignment (Phase 3).
	mIslandCrcsSorted.reserve(mIslands.size());
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		mIslandCrcsSorted.push_back(rCrc);
	}
	std::sort(mIslandCrcsSorted.begin(), mIslandCrcsSorted.end());

	// Assign each template its stable template-group index (drives grouping and indirect-buffer slot in
	// Islands). Bake here, never changes after boot.
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
	// Unique-channel-CRC invariant (relied upon by IslandTerrain's eviction qualification with NO refcount): every template's
	// 4 channel chunk CRCs (color/normals/AO/masks) are unique across all templates. They are
	// path-derived in DataPacker (common::Crc of "<island>/<Channel>", ExportIsland.cpp) and each
	// island lives in its own directory, so no two templates can share one. TextureDescriptors evicts
	// all five slot channels, unregisters their binding records, then IslandTerrain frees each channel
	// by CRC. Shared channels would free a Texture another template still samples while its record was
	// removed from TextureDescriptors' generation verifier. Assert it fast at boot rather than refcounting
	// (DataPacker enforces it structurally; the assert just turns a future regression into a fail-fast).
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
		const common::IslandHeader& rIslandHeader = rLazyChunk.header.islandHeader;

		// Chunk payload layout (set by ExportIsland::Export): [heightmap R16 halfs][float2 mesh positions][uint32 mesh indices][float2 valid-area hull verts].
		// Validate every count from the pack header before forming payload pointers or reclaiming a range.
		// iSize excludes the lazy-pool's alignment pad, so it must describe the layout exactly and fit the
		// actual resident extent.
		if (common::IsCompressed(rLazyChunk.header.flags) || rLazyChunk.pData == nullptr || rLazyChunk.iDataSize <= 0 || rLazyChunk.header.iSize <= 0 || rLazyChunk.header.iSize > rLazyChunk.iDataSize
			|| rIslandHeader.iHeightmapWidth <= 0 || rIslandHeader.iHeightmapHeight <= 0
			|| rIslandHeader.iMeshVertexCount <= 0 || rIslandHeader.iMeshIndexCount <= 0 || rIslandHeader.iValidAreaVertexCount < 0)
		{
			throw common::CorruptStreamException("IslandTerrain::WaitForElevationMaps");
		}

		int64_t iBytesRemaining = rLazyChunk.header.iSize;
		if (rIslandHeader.iHeightmapWidth > iBytesRemaining / static_cast<int64_t>(sizeof(uint16_t)) / rIslandHeader.iHeightmapHeight)
		{
			throw common::CorruptStreamException("IslandTerrain::WaitForElevationMaps");
		}
		int64_t iHeightmapBytes = static_cast<int64_t>(rIslandHeader.iHeightmapWidth) * rIslandHeader.iHeightmapHeight * static_cast<int64_t>(sizeof(uint16_t));
		iBytesRemaining -= iHeightmapBytes;

		auto consumeSection = [&iBytesRemaining](int64_t iElementCount, int64_t iElementBytes)
		{
			if (iElementCount > iBytesRemaining / iElementBytes)
			{
				throw common::CorruptStreamException("IslandTerrain::WaitForElevationMaps");
			}
			int64_t iSectionBytes = iElementCount * iElementBytes;
			iBytesRemaining -= iSectionBytes;
			return iSectionBytes;
		};

		[[maybe_unused]] int64_t iMeshPositionBytes = consumeSection(rIslandHeader.iMeshVertexCount, 2 * static_cast<int64_t>(sizeof(float)));
		consumeSection(rIslandHeader.iMeshIndexCount, static_cast<int64_t>(sizeof(uint32_t)));
		int64_t iValidAreaBytes = consumeSection(rIslandHeader.iValidAreaVertexCount, static_cast<int64_t>(sizeof(XMFLOAT2)));
		if (iBytesRemaining != 0)
		{
			throw common::CorruptStreamException("IslandTerrain::WaitForElevationMaps");
		}
		int64_t iMeshBytes = rLazyChunk.header.iSize - iHeightmapBytes - iValidAreaBytes;

		rTemplate.mpHeightmapHalf = reinterpret_cast<const uint16_t*>(rLazyChunk.pData);
		rTemplate.miHeightmapWidth = rIslandHeader.iHeightmapWidth;
		rTemplate.miHeightmapHeight = rIslandHeader.iHeightmapHeight;
		rTemplate.miMeshVertexCount = rIslandHeader.iMeshVertexCount;
		rTemplate.miMeshIndexCount = rIslandHeader.iMeshIndexCount;
		rTemplate.miValidAreaVertexCount = rIslandHeader.iValidAreaVertexCount;
		const std::byte* pAfterHeightmap = rLazyChunk.pData + iHeightmapBytes;
		rTemplate.mpf2ValidAreaVertices = reinterpret_cast<const XMFLOAT2*>(pAfterHeightmap + iMeshBytes);

#if defined(BT_CLIENT)
		// Mesh CPU pointers are client-only. The lazy-pool slice is reclaimed immediately and
		// asynchronously restored only when this template gains a render slot.
		rTemplate.mpfMeshPositions = reinterpret_cast<const float*>(pAfterHeightmap);
		rTemplate.mpuiMeshIndices = reinterpret_cast<const uint32_t*>(pAfterHeightmap + iMeshPositionBytes);
		gpFileManager->DecommitChunkRange(rCrc, static_cast<uint64_t>(iHeightmapBytes), static_cast<uint64_t>(iMeshBytes));
		rTemplate.mbMeshCpuDecommitted = true;
#endif

#if defined(BT_SERVER)
		// The server never reads the mesh CPU slice (no GPU upload, no device loss), so reclaim it immediately after
		// load: decommit the [positions][indices] sub-range of the kIsland chunk. Heightmap (before, offset 0) and hull
		// (after) stay resident — the server reads the heightmap for NavContour below and the hull for placement/nav.
		gpFileManager->DecommitChunkRange(rCrc, static_cast<uint64_t>(iHeightmapBytes), static_cast<uint64_t>(iMeshBytes));
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
			// boot buffer (one template at a time, freed each iteration).
			int64_t iHeightmapTexels = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight);
			std::vector<float> heightmapFloats(static_cast<size_t>(iHeightmapTexels));
			DirectX::PackedVector::XMConvertHalfToFloatStream(heightmapFloats.data(), sizeof(float), rTemplate.mpHeightmapHalf, sizeof(uint16_t), static_cast<size_t>(iHeightmapTexels));
			BuildNavContour(rTemplate.mNavContour, heightmapFloats.data(), rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, fNavThreshold, rTemplate.mfQuadFootprintX, rTemplate.mfQuadFootprintY);
		}
	}
#endif
}

namespace
{

// Grid cell containing a world position, matching GlobalElevation/GlobalNormal's cell mapping.
GridCoord CoordFromPosition(const XMFLOAT4A& f4Position)
{
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;

	int32_t iGridX = static_cast<int32_t>(std::floor((f4Position.x - fCellMinX) / fCellWidth));
	int32_t iGridY = static_cast<int32_t>(std::floor((f4Position.y - fCellMinY) / fCellHeight));
	return {iGridX, iGridY};
}

template <bool kbMultiplyUV, bool kbHoistedHeightmapMax>
bool SamplePlacementHeightmap(float fDx, float fDy, float fCos, float fSin, float fFootprintX, float fFootprintY, float fHalfX, float fHalfY, const uint16_t* pHeightmapHalf, int64_t iHeightmapWidth, int64_t iHeightmapHeight, float fInvFootprintX, float fInvFootprintY, float fHeightmapMaxU, float fHeightmapMaxV, float& rfSample)
{
	float fLocalX = fDx * fCos - fDy * fSin;
	float fLocalY = fDx * fSin + fDy * fCos;

	if constexpr (kbMultiplyUV)
	{
		if (std::abs(fLocalX) > fHalfX || std::abs(fLocalY) > fHalfY)
		{
			return false;
		}
	}
	else if (std::abs(fLocalX) > 0.5f * fFootprintX || std::abs(fLocalY) > 0.5f * fFootprintY)
	{
		return false;
	}

	// UV from local frame; V axis is world-Y inverted.
	float fU = 0.0f;
	float fV = 0.0f;
	if constexpr (kbMultiplyUV)
	{
		fU = fLocalX * fInvFootprintX + 0.5f;
		fV = 0.5f - fLocalY * fInvFootprintY;
	}
	else
	{
		fU = fLocalX / fFootprintX + 0.5f;
		fV = 0.5f - fLocalY / fFootprintY;
	}

	int64_t iX = 0;
	int64_t iY = 0;
	if constexpr (kbHoistedHeightmapMax)
	{
		iX = static_cast<int64_t>(fU * fHeightmapMaxU);
		iY = static_cast<int64_t>(fV * fHeightmapMaxV);
	}
	else
	{
		iX = static_cast<int64_t>(fU * static_cast<float>(iHeightmapWidth - 1));
		iY = static_cast<int64_t>(fV * static_cast<float>(iHeightmapHeight - 1));
	}
	iX = std::clamp(iX, static_cast<int64_t>(0), static_cast<int64_t>(iHeightmapWidth - 1));
	iY = std::clamp(iY, static_cast<int64_t>(0), static_cast<int64_t>(iHeightmapHeight - 1));

	rfSample = DirectX::PackedVector::XMConvertHalfToFloat(pHeightmapHalf[iY * iHeightmapWidth + iX]);
	return true;
}

template <typename ElevationCallable>
XMVECTOR NormalFromElevation(FXMVECTOR vecPosition, float fDistance, ElevationCallable&& rElevation)
{
	auto vecTopLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, fDistance, 0.0f, 0.0f));
	vecTopLeft = XMVectorSetZ(vecTopLeft, rElevation(vecTopLeft));
	auto vecTopRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, fDistance, 0.0f, 0.0f));
	vecTopRight = XMVectorSetZ(vecTopRight, rElevation(vecTopRight));
	auto vecBottomLeft = XMVectorAdd(vecPosition, XMVectorSet(-fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomLeft = XMVectorSetZ(vecBottomLeft, rElevation(vecBottomLeft));
	auto vecBottomRight = XMVectorAdd(vecPosition, XMVectorSet(fDistance, -fDistance, 0.0f, 0.0f));
	vecBottomRight = XMVectorSetZ(vecBottomRight, rElevation(vecBottomRight));

	return XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vecTopRight, vecBottomLeft), XMVectorSubtract(vecTopLeft, vecBottomRight)));
}

// MAX terrain elevation at f4Position over one already-resolved cell's island list — the shared body of
// GlobalElevation (a single point) and GlobalNormal (4 finite-difference taps). Split out so GlobalNormal
// can resolve the cell (hash lookup + island/query list) once and reuse it across taps that share a cell;
// results are identical to the previous per-point form.
//
// MAX over every island whose footprint rectangle contains the point. Rectangles may overlap now (the
// chain packs by hull, not rectangle), so first-match would pick an arbitrary island; the highest terrain
// must win, matching the GPU elevation prepass. Commutative max → order-independent and deterministic.
// Each placement's query (inverse-rotation cos/sin + footprint + heightmap pointer/dims) is precomputed
// once per cell (FrameStaticData::BuildRenderPlacementCache, index-parallel to islands) so this render
// path does zero hash lookups and zero libm trig per island. During the one-frame window after a
// placement edit clears the caches (network resend, menu cycle) the query cache can lag its rebuild by a
// tick — and the server never builds it at all — so fall back to inline trig + one mIslands.at lookup per
// island until RunFrameTick refills. The fallback reproduces a query from the placement + template.
float CellElevation(const IslandTerrain& rTerrain, const FrameStaticData& rStaticData, const XMFLOAT4A& f4Position)
{
	const std::vector<IslandPlacement>& rIslands = rStaticData.islands;
	const std::vector<IslandRenderQuery>& rQueries = rStaticData.islandRenderQueries;
	bool bHaveQueryCache = rQueries.size() == rIslands.size();

	// Reused across the fallback path's iterations (every field overwritten before use each time), so the
	// common cache-hit path constructs nothing per island.
	IslandRenderQuery fallbackQuery;
	float fMaxElevation = rTerrain.mfSeaFloorElevation;
	for (size_t i = 0; i < rIslands.size(); ++i)
	{
		const IslandRenderQuery* pQuery = nullptr;
		if (bHaveQueryCache)
		{
			pQuery = &rQueries[i];
		}
		else
		{
			const IslandPlacement& rPlacement = rIslands[i];
			const IslandTemplate& rTemplate = rTerrain.mIslands.at(rPlacement.islandCrc);
			fallbackQuery.fCos = std::cos(-rPlacement.fRotation);
			fallbackQuery.fSin = std::sin(-rPlacement.fRotation);
			fallbackQuery.f2WorldPos = rPlacement.f2WorldPos;
			fallbackQuery.fFootprintX = rTemplate.mfQuadFootprintX;
			fallbackQuery.fFootprintY = rTemplate.mfQuadFootprintY;
			fallbackQuery.pHeightmapHalf = rTemplate.mpHeightmapHalf;
			fallbackQuery.iHeightmapWidth = rTemplate.miHeightmapWidth;
			fallbackQuery.iHeightmapHeight = rTemplate.miHeightmapHeight;
			pQuery = &fallbackQuery;
		}
		const IslandRenderQuery& rQuery = *pQuery;

		float fDx = f4Position.x - rQuery.f2WorldPos.x;
		float fDy = f4Position.y - rQuery.f2WorldPos.y;
		float fSample = 0.0f;
		if (!SamplePlacementHeightmap<false, false>(fDx, fDy, rQuery.fCos, rQuery.fSin, rQuery.fFootprintX, rQuery.fFootprintY, 0.0f, 0.0f, rQuery.pHeightmapHalf, rQuery.iHeightmapWidth, rQuery.iHeightmapHeight, 0.0f, 0.0f, 0.0f, 0.0f, fSample))
		{
			continue;
		}

		// Heightmap value is already engine-meters (DataPacker shifted Gaea's [0,1] normalized
		// output by the per-island beach offset `Level × elevationMeters` read from the archetype
		// Sea node). Beach = 0; negative = water; positive = land. Fold into the running max.
		fMaxElevation = std::max(fMaxElevation, fSample);
	}

	return fMaxElevation;
}

} // anonymous namespace

float XM_CALLCONV IslandTerrain::GlobalElevation(FXMVECTOR vecPosition) const
{
	// Frame Purity Constraint (IslandTerrain.h): GlobalElevation/GlobalNormal walk mCoordFrames with
	// libm trig and must never run from frame-tick code — the sim hot path uses FrameElevation/FrameNormal.
	ASSERT(common::gpThreadLocal == nullptr || !common::gpThreadLocal->mbInFrameTick);

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	GridCoord coord = CoordFromPosition(f4Position);

	// Look up per-cell placements. Cells outside the simulated set fall through to sea floor.
	auto it = game::gpGame->mCoordFrames.find(coord);
	if (it == game::gpGame->mCoordFrames.end())
	{
		return mfSeaFloorElevation;
	}

	return CellElevation(*this, it->second.staticData, f4Position);
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
			float fSample = 0.0f;
			if (!SamplePlacementHeightmap<true, true>(fDx, fDy, fCos, fSin, fFootprintX, fFootprintY, fHalfX, fHalfY, rTemplate.mpHeightmapHalf, rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, fInvFootprintX, fInvFootprintY, fHeightmapMaxU, fHeightmapMaxV, fSample))
			{
				continue;
			}
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

FrameElevationSampler XM_CALLCONV IslandTerrain::MakeFrameElevationSampler(const FrameStaticData& rStaticData) const
{
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fCellMinX = game::Frame::kfBaseAreaMinX;
	static constexpr float fCellMinY = game::Frame::kfBaseAreaMinY;

	FrameElevationSampler sampler;
	sampler.fSeaFloor = mfSeaFloorElevation;
	if (rStaticData.elevationGrid.empty())
	{
		// pGrid stays null → Sample returns fSeaFloor, matching the old empty-grid early-out.
		return sampler;
	}

	sampler.pGrid = &rStaticData.elevationGrid;
	sampler.fCellOriginX = fCellMinX + static_cast<float>(rStaticData.coord.x) * fCellWidth;
	sampler.fCellOriginY = fCellMinY + static_cast<float>(rStaticData.coord.y) * fCellHeight;
	return sampler;
}

float XM_CALLCONV IslandTerrain::FrameElevation(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const
{
	// Single-source-of-truth for the sim/CRC elevation lookup: FrameElevation and every batched
	// FrameElevationSampler::Sample share one arithmetic path so they can never drift out of bit-exactness.
	return MakeFrameElevationSampler(rStaticData).Sample(vecPosition);
}

float XM_CALLCONV FrameElevationSampler::Sample(FXMVECTOR vecPosition) const
{
	if (pGrid == nullptr)
	{
		return fSeaFloor;
	}

	static constexpr int64_t kiDim = game::Frame::kiElevationGridDim;
	static constexpr float fCellWidth = game::Frame::kfCellWidth;
	static constexpr float fCellHeight = game::Frame::kfCellHeight;
	static constexpr float fGridPitchX = fCellWidth / static_cast<float>(kiDim);
	static constexpr float fGridPitchY = fCellHeight / static_cast<float>(kiDim);

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	float fLocalX = f4Position.x - fCellOriginX;
	float fLocalY = f4Position.y - fCellOriginY;
	int64_t iGx = static_cast<int64_t>(std::floor(fLocalX / fGridPitchX));
	int64_t iGy = static_cast<int64_t>(std::floor(fLocalY / fGridPitchY));
	if (iGx < 0 || iGx >= kiDim || iGy < 0 || iGy >= kiDim)
	{
		return fSeaFloor;
	}

	return (*pGrid)[static_cast<size_t>(iGy * kiDim + iGx)];
}

XMVECTOR XM_CALLCONV IslandTerrain::FrameNormal(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const
{
	// 4-tap finite-difference over FrameElevation. Same baseline as GlobalNormal so contour-following
	// AI behaves identically — only the elevation source changes.
	float fDistance = 2.0f;

	return NormalFromElevation(vecPosition, fDistance, [&](FXMVECTOR vecTap)
	{
		return FrameElevation(rStaticData, vecTap);
	});
}

XMVECTOR XM_CALLCONV IslandTerrain::GlobalNormal(FXMVECTOR vecPosition) const
{
	// Frame Purity Constraint (see GlobalElevation): must never run from frame-tick code. GlobalNormal
	// resolves cells itself (batching the 4 taps' lookups below) rather than routing each tap through
	// GlobalElevation, so it carries its own guard.
	ASSERT(common::gpThreadLocal == nullptr || !common::gpThreadLocal->mbInFrameTick);

	// 4-tap finite-difference over the terrain elevation. Each tap resolves its own cell/island list, so a
	// single fixed baseline works across multiple islands at different scales. The 2-unit cross-tap baseline
	// (1 meter per half-step, since islands use 1 m = 1 engine unit) is fine-grained enough to capture
	// normals without falling below per-pixel heightmap noise.
	float fDistance = 2.0f;

	// The 4 taps sit ±fDistance around the center, so they almost always land in the same cell. Resolve the
	// cell (hash lookup + island/query list) once and cache it, re-resolving only when a tap crosses into a
	// neighbor cell — collapsing 4 hash lookups + 4 list walks to ~1 in the common case. Same per-tap
	// arithmetic and cell mapping as GlobalElevation, so results are identical.
	bool bHaveCachedCoord = false;
	GridCoord cachedCoord {};
	const FrameStaticData* pCachedStaticData = nullptr;

	auto SampleElevation = [&](FXMVECTOR vecTap) -> float
	{
		XMFLOAT4A f4Tap {};
		XMStoreFloat4A(&f4Tap, vecTap);
		GridCoord coord = CoordFromPosition(f4Tap);
		if (!bHaveCachedCoord || coord != cachedCoord)
		{
			auto it = game::gpGame->mCoordFrames.find(coord);
			pCachedStaticData = it == game::gpGame->mCoordFrames.end() ? nullptr : &it->second.staticData;
			cachedCoord = coord;
			bHaveCachedCoord = true;
		}
		return pCachedStaticData == nullptr ? mfSeaFloorElevation : CellElevation(*this, *pCachedStaticData, f4Tap);
	};

	return NormalFromElevation(vecPosition, fDistance, SampleElevation);
}

} // namespace engine
