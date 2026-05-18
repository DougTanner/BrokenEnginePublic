#include "IslandTerrain.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandPlacement.h"

#if defined(BT_CLIENT)
#include "Graphics/Managers/PipelineManager.h"
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
		rTemplate.mfWorldFootprintXMeters = rLazyChunk.header.islandHeader.fWorldFootprintXMeters;
		rTemplate.mfWorldFootprintYMeters = rLazyChunk.header.islandHeader.fWorldFootprintYMeters;
		rTemplate.mfWorldElevationMeters = rLazyChunk.header.islandHeader.fWorldElevationMeters;
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

	// Downstream (IslandPlacement, TextureManager slot-0 anchor) requires at least one island.
	ASSERT(!mIslandCrcsSorted.empty());

	// Open-ocean floor (outside any island) is a fixed depth below sea level. Heightmap pixel
	// values inside islands already carry real negative depth, so this constant only fires for
	// cells with no island placement.
	mfSeaFloorElevation = -common::kfOceanDepthMeters * kfMetersToUnits;

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

#if defined(BT_CLIENT)
		// Chunk payload layout (set by ExportIsland::Export): [heightmap floats][float2 mesh positions][uint32 mesh indices].
		// miMeshVertexCount / miMeshIndexCount already populated in ctor from manifest header.
		int64_t iHeightmapBytes = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(float));
		const std::byte* pAfterHeightmap = reinterpret_cast<const std::byte*>(rLazyChunk.pData) + iHeightmapBytes;
		rTemplate.mpfMeshPositions = reinterpret_cast<const float*>(pAfterHeightmap);
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 2 * static_cast<int64_t>(sizeof(float));
		int64_t iMeshIndexBytes = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t));
		// Defensive: header.iSize is the unpadded chunk-data payload size set by
		// ExportJob::AllocateHeaderAndData. A stale float3-mesh pack file (pre-StripMeshZ) would
		// carry 1.5x the expected mesh-position payload, walking mpuiMeshIndices into garbage.
		// (Uncompressed chunks only — iUncompressedSize is zlib-only and stays 0 for islands.)
		ASSERT(rLazyChunk.header.iSize == iHeightmapBytes + iMeshPositionBytes + iMeshIndexBytes);
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
		// Sea node). Beach = 0; negative = water; positive = land. Return directly.
		return rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapWidth + iX];
	}

	return mfSeaFloorElevation;
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
		static int64_t siHotPathLogCount = 0;
		if (++siHotPathLogCount <= 8)
		{
			LOG(kTemp, kDebug, "AcquireTextureSlot HOT: crc={} slot={} frame={}", islandCrc, rTemplate.miTextureSlot, gpGraphics != nullptr ? gpGraphics->muiFrameCounter : 0u);
		}
		return rTemplate.miTextureSlot;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	// Color / Normals / AO ship as standalone lazy-texture chunks. Elevation lives on the template
	// (mElevationTexture) and is uploaded directly from the in-memory heightmap — no chunk, no CRC.
	common::crc_t textureCrcs[3] =
	{
		rLazyChunk.header.islandHeader.colorsCrc,
		rLazyChunk.header.islandHeader.normalsCrc,
		rLazyChunk.header.islandHeader.ambientOcclusionCrc,
	};

	if (rTemplate.miTextureSlot < 0)
	{
		// First-mint. Slot 0 stays the neutral placeholder anchor (no real island ever maps there).
		// Slot N points at the matching mTextureMap entries directly — their mVkImageView starts as
		// the white placeholder and is swapped to the real view by AdoptTransferredImage when each
		// chunk reaches kReady. Per-slot RegisterTextureBinding hooks each CRC into the existing
		// UpdateDescriptorsForTexture pipeline so the descriptor write follows the view swap (same
		// pattern as water normals / other bindless arrays).
		int64_t iSlot = miNextTextureSlot++;
		rTemplate.miTextureSlot = iSlot;

		// Elevation: uploaded directly from the in-memory heightmap into the template-owned
		// mElevationTexture. Descriptor patching deferred to RestorationSweep (safety window).
		CreateElevationTextureFromHeightmap(rTemplate, rLazyChunk.header.pcPath);

		Texture* pColor = &gpTextureManager->mTextureMap.at(textureCrcs[0]);
		Texture* pNormals = &gpTextureManager->mTextureMap.at(textureCrcs[1]);
		Texture* pAmbientOcclusion = &gpTextureManager->mTextureMap.at(textureCrcs[2]);

		gpTextureManager->mRenderTargetTextures.mElevationTextures.at(iSlot) = &rTemplate.mElevationTexture;
		gpTextureManager->mRenderTargetTextures.mColorTextures.at(iSlot) = pColor;
		gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(iSlot) = pNormals;
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(iSlot) = pAmbientOcclusion;

		// First-mint is infrequent (one per unique islandCrc) but RegisterTextureBinding inserts
		// into the binding map, which can allocate. The kPipelineTerrain* binding index 2 is the
		// kCombinedSamplers array of kiMaxIslands (PipelineManager.cpp CreateTerrainDataPipelines).
		// Elevation registers against islandCrc (unique per template, no chunk CRC to share with);
		// the other three use their per-chunk CRCs so ProcessPendingTextures' adoption flow still
		// routes through UpdateDescriptorsForTexture as each chunk reaches kReady.
		// kPipelineShadowElevation and kPipelineTerrainElevation share mElevationTextures but each
		// owns its own set=1 binding=2 descriptor; both must be patched in lockstep by
		// RestorationSweep's UpdateArrayBindingsForKey(islandCrc), so register both.
		{
			ScopedSuppressAllocationTracking suppress;
			constexpr int64_t kiBindingIndex = 2;
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(islandCrc, &gpPipelineManager->mpPipelines[kPipelineShadowElevation], kiBindingIndex, DescriptorFlags::kSamplerElevation, nullptr, gpTextureManager->mRenderTargetTextures.mElevationTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(islandCrc, &gpPipelineManager->mpPipelines[kPipelineTerrainElevation], kiBindingIndex, DescriptorFlags::kSamplerElevation, nullptr, gpTextureManager->mRenderTargetTextures.mElevationTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[0], &gpPipelineManager->mpPipelines[kPipelineTerrainColor], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mColorTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[1], &gpPipelineManager->mpPipelines[kPipelineTerrainNormal], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mNormalsTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[2], &gpPipelineManager->mpPipelines[kPipelineTerrainAmbientOcclusion], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data(), shaders::kiMaxIslands, iSlot);
		}

		// Mesh buffer was created at boot by CreateClientMeshBuffers (record-once CB invariant —
		// terrain CB binds every template's mesh at record time).
		ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE);

		rTemplate.mbGpuResident = false;
		gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
		LOG(kGraphics, kVerbose, "First-mint slot={} islandCrc={}", iSlot, islandCrc);
		LOG(kTemp, kDebug, "AcquireTextureSlot FIRST-MINT: crc={} slot={} frame={}", islandCrc, iSlot, gpGraphics != nullptr ? gpGraphics->muiFrameCounter : 0u);

		return iSlot;
	}

	// Slot already assigned but GPU resources evicted. Re-trigger loads; slot stays in
	// slot-0 fallback (set during EvictionSweep) until RestorationSweep patches back.
	// Elevation isn't evicted by EvictionSweep, but device-loss recovery wipes every Texture's GPU
	// resources (including the template-owned mElevationTexture); detect that here and re-Create
	// from the in-memory heightmap. Descriptor patching is deferred to RestorationSweep.
	if (rTemplate.mElevationTexture.mVkImage == VK_NULL_HANDLE)
	{
		CreateElevationTextureFromHeightmap(rTemplate, rLazyChunk.header.pcPath);
	}
	gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
	LOG(kLoading, kVerbose, "Re-acquire islandCrc={} slot={}, requesting chunk loads", islandCrc, rTemplate.miTextureSlot);
	LOG(kTemp, kDebug, "AcquireTextureSlot RE-ACQUIRE: crc={} slot={} elevationVkImageNull={} frame={}",
		islandCrc, rTemplate.miTextureSlot, rTemplate.mElevationTexture.mVkImage == VK_NULL_HANDLE,
		gpGraphics != nullptr ? gpGraphics->muiFrameCounter : 0u);
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
		// Elevation (textureCrcs[0]) is uploaded once from the in-memory heightmap and stays
		// permanently resident (matches mMeshBuffer policy). Only color/normals/AO participate in
		// eviction.
		common::crc_t evictCrcs[3] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
		};

		LOG(kGraphics, kVerbose, "Evicting islandCrc={} slot={} (refCount=0, framesSinceUse={})", rCrc, rTemplate.miTextureSlot, gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame);

		for (common::crc_t textureCrc : evictCrcs)
		{
			gpTextureManager->mTextureMap.at(textureCrc).FreeGpuResources();
		}

		int64_t iSlot = rTemplate.miTextureSlot;
		gpTextureManager->mRenderTargetTextures.mColorTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mColorTextures.at(0);
		gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mNormalsTextures.at(0);
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(iSlot) = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.at(0);

		gpFileManager->ResetTextureChunkStates(evictCrcs);
		rTemplate.mbGpuResident = false;
		bDirty = true;

		LOG(kLoading, kVerbose, "Reset chunk states for evicted islandCrc={} evictCrcs=[{},{},{}]", rCrc, evictCrcs[0], evictCrcs[1], evictCrcs[2]);
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

	// Slot pointers are set at AcquireTextureSlot first-mint and never reassigned, so the only
	// remaining work here is residency tracking. Color/normals/AO descriptor writes flow through
	// ProcessPendingTextures' UpdateDescriptorsForTexture path as each chunk reaches kReady;
	// elevation is patched here on the resident transition because it bypassed that path.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		// Elevation has no chunk state — it's template-owned and uploaded once at first-mint —
		// so residency is gated only on the other 3 channels.
		common::crc_t residencyCrcs[3] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
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
			LOG(kTemp, kDebug, "RestorationSweep RESIDENT: crc={} slot={} elevationVkImageView={}  frame={}",
				rCrc, rTemplate.miTextureSlot,
				reinterpret_cast<uintptr_t>(rTemplate.mElevationTexture.mVkImageView),
				gpGraphics->muiFrameCounter);
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
		// destroy doesn't touch it — release here alongside the mesh buffer. The next AcquireTextureSlot
		// detects mVkImage == VK_NULL_HANDLE and re-Creates from the resident in-memory heightmap.
		rTemplate.mElevationTexture.FreeGpuResources();
		// Clear residency so the hot-path early-return in AcquireTextureSlot doesn't short-circuit
		// the elevation re-Create + chunk-reload branch. The full miTextureSlot reset that re-points
		// the dangling color/normals/AO mRenderTargetTextures slots happens in ResetTextureSlots,
		// which TextureManager's ctor calls.
		rTemplate.mbGpuResident = false;
	}
}

void IslandTerrain::ResetTextureSlots()
{
	int64_t iCount = 0;
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		rTemplate.miTextureSlot = -1;
		rTemplate.mbGpuResident = false;
		rTemplate.miRefCount = 0;
		rTemplate.muiLastUsedRenderFrame = 0;
		++iCount;
	}
	miNextTextureSlot = 1;
	LOG(kTemp, kDebug, "IslandTerrain::ResetTextureSlots: cleared {} templates", iCount);
}
#endif

} // namespace engine
