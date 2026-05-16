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
	}

	// Stable, deterministic iteration order for slot assignment (Phase 3).
	mIslandCrcsSorted.reserve(mIslands.size());
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		mIslandCrcsSorted.push_back(rCrc);
	}
	std::sort(mIslandCrcsSorted.begin(), mIslandCrcsSorted.end());

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
		// Chunk payload layout (set by ExportIsland::Export): [heightmap floats][mesh positions][mesh indices].
		int64_t iHeightmapBytes = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(float));
		const std::byte* pAfterHeightmap = reinterpret_cast<const std::byte*>(rLazyChunk.pData) + iHeightmapBytes;
		rTemplate.miMeshVertexCount = rLazyChunk.header.islandHeader.iMeshVertexCount;
		rTemplate.miMeshIndexCount = rLazyChunk.header.islandHeader.iMeshIndexCount;
		rTemplate.mpfMeshPositions = reinterpret_cast<const float*>(pAfterHeightmap);
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 3 * static_cast<int64_t>(sizeof(float));
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

		// Heightmap value is already engine-meters (DataPacker offset by the global beach-height
		// constant `kfBeachHeightMeters` defined in BakeIslandIntermediates.cpp). Beach = 0;
		// negative = water; positive = land. Return directly.
		return rTemplate.mpfHeightmapData[iY * rTemplate.miHeightmapWidth + iX];
	}

	return mfSeaFloorElevation;
}

#if defined(BT_CLIENT)
namespace
{
	// Upload an island's heightmap into its mTextureMap[elevationCrc] Texture slot as an R32_SFLOAT
	// image. Reused at first-mint and on device-loss re-Create. Descriptor patching is deferred to
	// RestorationSweep so it lands inside RenderGlobal's post-fence-wait descriptor-patch window.
	void CreateElevationTextureFromHeightmap(IslandTemplate& rTemplate, common::crc_t elevationCrc, std::string_view name)
	{
		// Boot ordering invariant: WaitForElevationMaps (called once at startup) is the only writer of
		// mpfHeightmapData. AcquireTextureSlot must never run before it.
		ASSERT(rTemplate.mpfHeightmapData != nullptr);
		Texture& rElevation = gpTextureManager->mTextureMap.at(elevationCrc);
		// Heap: Texture::Create allocates GPU resources and uses a OneShotCommandBuffer.
		ScopedSuppressAllocationTracking suppress;
		rElevation.Create(
			TextureInfo
			{
				.name = name,
				.crc = elevationCrc,
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
		// Flip chunk state to Ready so RestorationSweep counts elevation as loaded alongside the
		// other 3 chunks. The chunk's pData stays untouched (load was never queued).
		gpFileManager->GetLazyChunk(elevationCrc).eState.store(ChunkState::kReady, std::memory_order_release);
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
	common::crc_t textureCrcs[4] =
	{
		rLazyChunk.header.islandHeader.elevationCrc,
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

		LOG(kTemp, kInfo, "Island first-mint islandCrc={} slot={} textureCrcs=[{},{},{},{}]", islandCrc, iSlot, textureCrcs[0], textureCrcs[1], textureCrcs[2], textureCrcs[3]);

		// Elevation: short-circuit the lazy-texture-chunk pipeline. The kIsland chunk loaded at boot
		// already contains the heightmap (mpfHeightmapData), and the standalone Elevation.R32_SFLOAT
		// texture chunk holds the same pixels. Upload directly into the pre-existing mTextureMap entry
		// (InitDeferred placeholder slot from TextureManager ctor) so descriptor wiring and array
		// indexing stay identical to the other 3 channels — only the data path changes. Descriptor
		// patching is deferred to RestorationSweep (safety window).
		CreateElevationTextureFromHeightmap(rTemplate, textureCrcs[0], rLazyChunk.header.pcPath);
		LOG(kTemp, kInfo, "Island elevation uploaded from heightmap islandCrc={} slot={} extent={}x{} bytes={}", islandCrc, iSlot, rTemplate.miHeightmapWidth, rTemplate.miHeightmapHeight, static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(float)));

		Texture* pElevation = &gpTextureManager->mTextureMap.at(textureCrcs[0]);
		Texture* pColor = &gpTextureManager->mTextureMap.at(textureCrcs[1]);
		Texture* pNormals = &gpTextureManager->mTextureMap.at(textureCrcs[2]);
		Texture* pAmbientOcclusion = &gpTextureManager->mTextureMap.at(textureCrcs[3]);

		gpTextureManager->mRenderTargetTextures.mElevationTextures[iSlot] = pElevation;
		gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = pColor;
		gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = pNormals;
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = pAmbientOcclusion;

		// First-mint is infrequent (one per unique islandCrc) but RegisterTextureBinding inserts
		// into the binding map, which can allocate. The kPipelineTerrain* binding index 2 is the
		// kCombinedSamplers array of kiMaxIslands (PipelineManager.cpp CreateTerrainDataPipelines).
		{
			ScopedSuppressAllocationTracking suppress;
			constexpr int64_t kiBindingIndex = 2;
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[0], &gpPipelineManager->mpPipelines[kPipelineTerrainElevation], kiBindingIndex, DescriptorFlags::kSamplerElevation, nullptr, gpTextureManager->mRenderTargetTextures.mElevationTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[1], &gpPipelineManager->mpPipelines[kPipelineTerrainColor], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mColorTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[2], &gpPipelineManager->mpPipelines[kPipelineTerrainNormal], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mNormalsTextures.data(), shaders::kiMaxIslands, iSlot);
			gpTextureManager->mTextureDescriptors.RegisterTextureBinding(textureCrcs[3], &gpPipelineManager->mpPipelines[kPipelineTerrainAmbientOcclusion], kiBindingIndex, DescriptorFlags::kSamplerClamp, nullptr, gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data(), shaders::kiMaxIslands, iSlot);
		}

		// Upload the Mesher-baked terrain mesh exactly once per template. Layout: [uint32 indices,
		// float3 positions] — indices first matches the existing terrain/water mesh buffer convention
		// (BufferManager.cpp). Mesh data lives for the lifetime of the template (not LRU-evicted —
		// textures dominate VRAM pressure, mesh is small and KISS).
		ASSERT(rTemplate.miMeshVertexCount > 0);
		ASSERT(rTemplate.miMeshIndexCount > 0);
		// Defensive: if eviction is ever extended to mesh memory, Create() must not be called over a
		// live buffer. Currently the miTextureSlot < 0 first-mint branch is the only path that uploads,
		// so the buffer is guaranteed-null here.
		ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE);
		int64_t iMeshIndexBytes = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t));
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 3 * static_cast<int64_t>(sizeof(float));
		rTemplate.mMeshBuffer.Create(
		{
			.name = "IslandMesh",
			.flags = {BufferFlags::kIndexVertex, BufferFlags::kDeviceLocal},
			.iCount = rTemplate.miMeshIndexCount,
			.vkIndexType = VK_INDEX_TYPE_UINT32,
			.iVertexStride = static_cast<int64_t>(3 * sizeof(float)),
			.dataVkDeviceSize = static_cast<VkDeviceSize>(iMeshIndexBytes + iMeshPositionBytes),
		},
		[&rTemplate, iMeshIndexBytes, iMeshPositionBytes](void* pData)
		{
			std::memcpy(pData, rTemplate.mpuiMeshIndices, static_cast<size_t>(iMeshIndexBytes));
			std::memcpy(static_cast<char*>(pData) + iMeshIndexBytes, rTemplate.mpfMeshPositions, static_cast<size_t>(iMeshPositionBytes));
		});
		LOG(kGraphics, kDebug, "Uploaded island mesh: crc={} vertices={} indices={}", islandCrc, rTemplate.miMeshVertexCount, rTemplate.miMeshIndexCount);
		LOG(kTemp, kInfo, "Island mesh uploaded islandCrc={} vertices={} indices={}", islandCrc, rTemplate.miMeshVertexCount, rTemplate.miMeshIndexCount);

		rTemplate.mbGpuResident = false;
		// Skip elevation (textureCrcs[0]) — uploaded directly from the in-memory heightmap above.
		// Only color/normals/AO need to be pulled from disk.
		common::crc_t loadCrcs[3] = {textureCrcs[1], textureCrcs[2], textureCrcs[3]};
		gpFileManager->RequestChunkLoad(loadCrcs, LoadPriority::kRealtime);
		LOG(kGraphics, kVerbose, "First-mint slot={} islandCrc={}", iSlot, islandCrc);

		if (gpGraphics != nullptr)
		{
			gpGraphics->meDestroyType = std::max(DestroyType::kPipelines, gpGraphics->meDestroyType);
		}

		return iSlot;
	}

	// Slot already assigned but GPU resources evicted. Re-trigger loads; slot stays in
	// slot-0 fallback (set during EvictionSweep) until RestorationSweep patches back.
	// Skip elevation in the chunk-load request — it's not evicted by EvictionSweep. However on
	// device-loss recovery the wholesale ResetTextureChunkStates wipes every Texture's GPU resources
	// (including elevation's), so detect that here and re-Create from the in-memory heightmap.
	// Descriptor patching is deferred to RestorationSweep (safety window).
	if (gpTextureManager->mTextureMap.at(textureCrcs[0]).mVkImage == VK_NULL_HANDLE)
	{
		CreateElevationTextureFromHeightmap(rTemplate, textureCrcs[0], rLazyChunk.header.pcPath);
	}
	common::crc_t loadCrcs[3] = {textureCrcs[1], textureCrcs[2], textureCrcs[3]};
	gpFileManager->RequestChunkLoad(loadCrcs, LoadPriority::kRealtime);
	LOG(kLoading, kVerbose, "Re-acquire islandCrc={} slot={}, requesting chunk loads", islandCrc, rTemplate.miTextureSlot);
	LOG(kTemp, kInfo, "Island re-acquire islandCrc={} slot={} textureCrcs=[{},{},{},{}]", islandCrc, rTemplate.miTextureSlot, textureCrcs[0], textureCrcs[1], textureCrcs[2], textureCrcs[3]);
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
		LOG(kTemp, kInfo, "Island evict islandCrc={} slot={} framesSinceUse={}", rCrc, rTemplate.miTextureSlot, gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame);

		for (common::crc_t textureCrc : evictCrcs)
		{
			gpTextureManager->mTextureMap.at(textureCrc).FreeGpuResources();
		}

		int64_t iSlot = rTemplate.miTextureSlot;
		gpTextureManager->mRenderTargetTextures.mColorTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mColorTextures[0];
		gpTextureManager->mRenderTargetTextures.mNormalsTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mNormalsTextures[0];
		gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[iSlot] = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures[0];

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
	bool bPatchedElevationDescriptor = false;
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			continue;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		// Elevation chunk state is forced to kReady at first-mint (uploaded directly from heightmap)
		// and never reset by EvictionSweep, so residency is gated only on the other 3 channels.
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
			// Patch the elevation descriptor inside the safety window (RestorationSweep runs in
			// RenderGlobal post-fence-wait). The Texture's real VkImageView was created at first-mint
			// but its descriptor entry is still the slot-0 placeholder until this write.
			gpTextureManager->mTextureDescriptors.UpdateDescriptorsForTexture(rLazyChunk.header.islandHeader.elevationCrc);
			bPatchedElevationDescriptor = true;
			LOG(kGraphics, kVerbose, "Island resident islandCrc={} slot={}", rCrc, rTemplate.miTextureSlot);
			LOG(kTemp, kInfo, "Island resident islandCrc={} slot={}", rCrc, rTemplate.miTextureSlot);
		}
	}
	if (bPatchedElevationDescriptor)
	{
		// Flush bindless Set 0 binding 4 to GPU (UpdateDescriptorsForTexture updates the CPU-side
		// mImageInfos but does not itself rewrite the global descriptor set).
		gpTextureManager->mTextureDescriptors.UpdateTextureArrayDescriptors();
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
	}
}
#endif

} // namespace engine
