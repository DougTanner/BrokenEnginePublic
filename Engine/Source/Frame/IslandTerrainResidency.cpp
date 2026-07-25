#if defined(BT_CLIENT)

#include "IslandTerrain.h"

#include "Graphics/Islands.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

namespace
{
	struct MeshRange
	{
		uint64_t uiOffset = 0;
		uint64_t uiLength = 0;
		VkDeviceSize vkIndexSize = 0;
		VkDeviceSize vkVertexSize = 0;
	};

	MeshRange GetMeshRange(const IslandTemplate& rTemplate)
	{
		MeshRange range {};
		range.uiOffset = static_cast<uint64_t>(rTemplate.miHeightmapWidth) * static_cast<uint64_t>(rTemplate.miHeightmapHeight) * sizeof(uint16_t);
		range.vkVertexSize = static_cast<VkDeviceSize>(rTemplate.miMeshVertexCount) * 2 * sizeof(float);
		range.vkIndexSize = static_cast<VkDeviceSize>(rTemplate.miMeshIndexCount) * sizeof(uint32_t);
		range.uiLength = static_cast<uint64_t>(range.vkVertexSize + range.vkIndexSize);
		return range;
	}

	void ReleaseMeshCpuRange(common::crc_t islandCrc, IslandTemplate& rTemplate, const MeshRange& range)
	{
		gpFileManager->DecommitChunkRange(islandCrc, range.uiOffset, range.uiLength);
		rTemplate.mbMeshCpuDecommitted = true;
		gpFileManager->ResetChunkRangeReloadState(islandCrc, range.uiOffset, range.uiLength);
	}

	bool IsTextureRestorationPending(common::crc_t islandCrc, const IslandTemplate& rTemplate)
	{
		if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0)
		{
			return false;
		}
		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
		common::crc_t residencyCrcs[4] =
		{
			rLazyChunk.header.islandHeader.colorsCrc,
			rLazyChunk.header.islandHeader.normalsCrc,
			rLazyChunk.header.islandHeader.ambientOcclusionCrc,
			rLazyChunk.header.islandHeader.masksCrc,
		};
		for (common::crc_t textureCrc : residencyCrcs)
		{
			if (gpFileManager->GetLazyChunk(textureCrc).eState.load(std::memory_order_acquire) < ChunkState::kReady)
			{
				return false;
			}
		}
		return true;
	}

	// Upload an island's heightmap into its template-owned mElevationTexture as an R16_SFLOAT image (raw
	// byte-copy — the resident heightmap is already R16 half-float, matching the image's texel size).
	// Reused at first-mint and on device-loss re-Create. Descriptor patching is deferred to
	// RestorationSweep so it lands inside RenderGlobal's post-fence-wait descriptor-patch window.
	void CreateElevationTextureFromHeightmap(IslandTemplate& rTemplate, std::string_view name)
	{
		// Boot ordering invariant: WaitForElevationMaps (called once at startup) is the only writer of
		// mpHeightmapHalf. AcquireTextureSlot must never run before it.
		ASSERT(rTemplate.mpHeightmapHalf != nullptr);
		// Heap: Texture::Create allocates GPU resources and uses a OneShotCommandBuffer.
		ScopedSuppressAllocationTracking suppress;
		rTemplate.mElevationTexture.Create(
			TextureInfo
			{
				.name = name,
				.format = shaders::keElevationFormat,
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
				std::memcpy(pData, reinterpret_cast<const std::byte*>(rTemplate.mpHeightmapHalf) + iPosition, static_cast<size_t>(iSize));
			});
	}
}

int64_t IslandTerrain::FirstMintTextureSlot(common::crc_t islandCrc, IslandTemplate& rTemplate, const common::crc_t (&textureCrcs)[4], std::string_view name)
{
	// First-mint. Slot 0 stays the neutral placeholder anchor (no real island ever maps there).
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
	CreateElevationTextureFromHeightmap(rTemplate, name);

	gpTextureManager->mTextureDescriptors.MintIslandSlot(iSlot, islandCrc, rTemplate.mElevationTexture, textureCrcs);

	rTemplate.mbGpuResident = false;
	gpFileManager->RequestChunkLoad(textureCrcs, LoadPriority::kRealtime);
	LOG(kGraphics, kVerbose, "First-mint slot={} islandCrc={}", iSlot, islandCrc);

	return iSlot;
}

int64_t IslandTerrain::AcquireTextureSlot(common::crc_t islandCrc)
{
	IslandTemplate& rTemplate = mIslands.at(islandCrc);

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	if (rTemplate.meMeshResidency == IslandMeshResidency::kNonresident)
	{
		MeshRange range = GetMeshRange(rTemplate);
		gpFileManager->RequestChunkRangeReload(islandCrc, range.uiOffset, range.uiLength, LoadPriority::kRealtime);
		rTemplate.meMeshResidency = IslandMeshResidency::kAsyncPending;
	}

	// Hot path still starts a mesh reload when a device recreation or earlier full teardown made
	// the mesh nonresident; texture residency itself remains unchanged.
	if (rTemplate.miTextureSlot >= 0 && rTemplate.mbGpuResident)
	{
		return rTemplate.miTextureSlot;
	}

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
		return FirstMintTextureSlot(islandCrc, rTemplate, textureCrcs, rLazyChunk.header.pcPath);
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
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		if (IsEvictionPending(rTemplate))
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
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		if (IsRestorationPending(rCrc, rTemplate))
		{
			return true;
		}
	}
	return false;
}

bool IslandTerrain::IsEvictionPending(const IslandTemplate& rTemplate) const
{
	return rTemplate.miTextureSlot != 0 && rTemplate.mbGpuResident && rTemplate.miRefCount == 0
		&& (gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame) > kuiGraceRenderFrames;
}

bool IslandTerrain::IsRestorationPending(common::crc_t islandCrc, const IslandTemplate& rTemplate) const
{
	if (IsTextureRestorationPending(islandCrc, rTemplate))
	{
		return true;
	}

	MeshRange range = GetMeshRange(rTemplate);
	switch (rTemplate.meMeshResidency)
	{
		case IslandMeshResidency::kAsyncPending:
			return gpFileManager->GetChunkRangeReloadState(islandCrc, range.uiOffset, range.uiLength) != ChunkRangeReloadState::kPending;
		case IslandMeshResidency::kCpuReady:
			return rTemplate.mbGpuResident;
		case IslandMeshResidency::kArenaBlocked:
			return rTemplate.mbGpuResident && (gpIslands->muiMeshArenaCapacityGeneration != rTemplate.muiMeshArenaBlockedGeneration || HasArenaEvictionCandidate(islandCrc));
		case IslandMeshResidency::kNonresident:
		case IslandMeshResidency::kFailed:
		case IslandMeshResidency::kResident:
			return false;
	}
	return false;
}

bool IslandTerrain::HasArenaEvictionCandidate(common::crc_t excludedCrc) const
{
	for (const auto& [rCrc, rTemplate] : mIslands)
	{
		if (rCrc != excludedCrc && rTemplate.meMeshResidency == IslandMeshResidency::kResident && rTemplate.mbGpuResident && rTemplate.miRefCount == 0)
		{
			return true;
		}
	}
	return false;
}

bool IslandTerrain::EvictTemplate(common::crc_t islandCrc, IslandTemplate& rTemplate, MeshEvictionReason eReason)
{
	bool bEligible = eReason == MeshEvictionReason::kGrace ? IsEvictionPending(rTemplate)
		: rTemplate.miTextureSlot != 0 && rTemplate.mbGpuResident && rTemplate.meMeshResidency == IslandMeshResidency::kResident && rTemplate.miRefCount == 0;
	if (!bEligible)
	{
		return false;
	}

	const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);
	// The 4 chunk-backed channels (color/normals/AO/masks). Elevation is template-owned (no chunk
	// CRC) and is evicted separately below, via the template's own image rather than the chunk pool.
	common::crc_t evictCrcs[4] =
	{
		rLazyChunk.header.islandHeader.colorsCrc,
		rLazyChunk.header.islandHeader.normalsCrc,
		rLazyChunk.header.islandHeader.ambientOcclusionCrc,
		rLazyChunk.header.islandHeader.masksCrc,
	};

	LOG(kGraphics, kVerbose, "Evicting islandCrc={} slot={} (refCount=0, framesSinceUse={})", islandCrc, rTemplate.miTextureSlot, gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame);

	int64_t iSlot = rTemplate.miTextureSlot;
	// Redirect every live descriptor to placeholders and retire its five generation records before
	// freeing any image view. A recycled slot cannot observe a destroyed prior occupant this way.
	gpTextureManager->mTextureDescriptors.EvictIslandSlot(iSlot, islandCrc, evictCrcs);

	for (common::crc_t textureCrc : evictCrcs)
	{
		gpTextureManager->mTextureMap.at(textureCrc).FreeGpuResources();
	}
	rTemplate.mElevationTexture.FreeGpuResources();

	MeshRange range = GetMeshRange(rTemplate);
	switch (rTemplate.meMeshResidency)
	{
		case IslandMeshResidency::kResident:
			// Indirect count must be zero in every framebuffer before the virtual ranges can be reused.
			gpIslands->WriteMeshIndirect(rTemplate.miTemplateArrayIndex, 0, 0, 0);
			gpIslands->FreeMeshRanges(rTemplate.mMeshIndexAllocation, rTemplate.mMeshVertexAllocation);
			rTemplate.mMeshIndexAllocation = VK_NULL_HANDLE;
			rTemplate.mMeshVertexAllocation = VK_NULL_HANDLE;
			rTemplate.mMeshIndexOffset = 0;
			rTemplate.mMeshVertexOffset = 0;
			rTemplate.muiMeshArenaBlockedGeneration = 0;
			rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
			break;
		case IslandMeshResidency::kCpuReady:
		case IslandMeshResidency::kArenaBlocked:
			ReleaseMeshCpuRange(islandCrc, rTemplate, range);
			rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
			break;
		case IslandMeshResidency::kFailed:
			ReleaseMeshCpuRange(islandCrc, rTemplate, range);
			rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
			break;
		case IslandMeshResidency::kAsyncPending:
		case IslandMeshResidency::kNonresident:
			break;
	}

	{
		// Heap: free-list growth runs inside RenderGlobal (EvictionSweep).
		ScopedSuppressAllocationTracking suppress;
		mFreeTextureSlots.push_back(iSlot);
	}
	rTemplate.miTextureSlot = -1;

	gpFileManager->ResetTextureChunkStates(evictCrcs);
	rTemplate.mbGpuResident = false;

	LOG(kLoading, kVerbose, "Reset chunk states for evicted islandCrc={} evictCrcs=[{},{},{},{}]", islandCrc, evictCrcs[0], evictCrcs[1], evictCrcs[2], evictCrcs[3]);
	return true;
}

void IslandTerrain::EvictionSweep()
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return;
	}

	for (auto& [rCrc, rTemplate] : mIslands)
	{
		EvictTemplate(rCrc, rTemplate);
	}
}

void IslandTerrain::RestorationSweep()
{
	if (gpGraphics == nullptr || gpTextureManager == nullptr)
	{
		return;
	}

	// First-mint assigns the four chunk-backed pointers, while elevation remains at the slot-0
	// placeholder until this all-four-ready transition. Chunk descriptor writes flow through
	// ProcessPendingTextures as each channel reaches kReady; RestoreIslandSlot switches elevation.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (IsTextureRestorationPending(rCrc, rTemplate))
		{
			// Patch the elevation array binding inside the safety window (RestorationSweep runs in
			// RenderGlobal post-fence-wait). The Texture's real VkImageView was created at first-mint
			// but the per-pipeline array descriptor still points at the slot-0 placeholder snapshot
			// taken at RegisterTextureBinding time. islandCrc was used as the binding key (the
			// template-owned mElevationTexture has no chunk CRC).
			gpTextureManager->mTextureDescriptors.RestoreIslandSlot(rCrc);
			rTemplate.mbGpuResident = true;
			LOG(kGraphics, kVerbose, "Island resident islandCrc={} slot={}", rCrc, rTemplate.miTextureSlot);
		}
	}

	for (auto& [rCrc, rTemplate] : mIslands)
	{
		MeshRange range = GetMeshRange(rTemplate);
		if (rTemplate.meMeshResidency == IslandMeshResidency::kAsyncPending)
		{
			ChunkRangeReloadState eRangeState = gpFileManager->GetChunkRangeReloadState(rCrc, range.uiOffset, range.uiLength);
			if (eRangeState == ChunkRangeReloadState::kPending)
			{
				continue;
			}
			if (eRangeState == ChunkRangeReloadState::kFailed)
			{
				LOG(kGraphics, kError, "Island mesh async reload failed: crc={}", rCrc);
				gpIslands->WriteMeshIndirect(rTemplate.miTemplateArrayIndex, 0, 0, 0);
				ReleaseMeshCpuRange(rCrc, rTemplate, range);
				if (rTemplate.miTextureSlot < 0)
				{
					rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
				}
				else
				{
					rTemplate.meMeshResidency = IslandMeshResidency::kFailed;
				}
				continue;
			}
			ASSERT(eRangeState == ChunkRangeReloadState::kReady);
			rTemplate.mbMeshCpuDecommitted = false;
			rTemplate.meMeshResidency = IslandMeshResidency::kCpuReady;
			if (rTemplate.miTextureSlot < 0)
			{
				ReleaseMeshCpuRange(rCrc, rTemplate, range);
				rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
				continue;
			}
		}

		if (rTemplate.meMeshResidency == IslandMeshResidency::kArenaBlocked)
		{
			if (rTemplate.miTextureSlot < 0)
			{
				continue;
			}
			if (gpIslands->muiMeshArenaCapacityGeneration == rTemplate.muiMeshArenaBlockedGeneration && !HasArenaEvictionCandidate(rCrc))
			{
				continue;
			}
			rTemplate.meMeshResidency = IslandMeshResidency::kCpuReady;
		}

		if (rTemplate.meMeshResidency != IslandMeshResidency::kCpuReady)
		{
			continue;
		}
		if (rTemplate.miTextureSlot < 0)
		{
			continue;
		}
		if (!rTemplate.mbGpuResident)
		{
			continue;
		}

		while (!gpIslands->AllocateMeshRanges(range.vkIndexSize, range.vkVertexSize, rTemplate.mMeshIndexAllocation, rTemplate.mMeshIndexOffset, rTemplate.mMeshVertexAllocation, rTemplate.mMeshVertexOffset))
		{
			common::crc_t evictCrc = 0;
			IslandTemplate* pEvictTemplate = nullptr;
			for (auto& [rCandidateCrc, rCandidate] : mIslands)
			{
				if (rCandidateCrc != rCrc && rCandidate.meMeshResidency == IslandMeshResidency::kResident && rCandidate.mbGpuResident && rCandidate.miRefCount == 0
					&& (pEvictTemplate == nullptr || rCandidate.muiLastUsedRenderFrame < pEvictTemplate->muiLastUsedRenderFrame))
				{
					evictCrc = rCandidateCrc;
					pEvictTemplate = &rCandidate;
				}
			}
			if (pEvictTemplate == nullptr)
			{
				rTemplate.muiMeshArenaBlockedGeneration = gpIslands->muiMeshArenaCapacityGeneration;
				rTemplate.meMeshResidency = IslandMeshResidency::kArenaBlocked;
				gpIslands->WriteMeshIndirect(rTemplate.miTemplateArrayIndex, 0, 0, 0);
				LOG(kGraphics, kWarning, "Island mesh arena exhausted: crc={}", rCrc);
				break;
			}
			ASSERT(EvictTemplate(evictCrc, *pEvictTemplate, MeshEvictionReason::kArenaExhaustion));
		}
		if (rTemplate.meMeshResidency != IslandMeshResidency::kCpuReady)
		{
			continue;
		}

		ASSERT(rTemplate.mpfMeshPositions != nullptr);
		ASSERT(rTemplate.mpuiMeshIndices != nullptr);
		ASSERT(rTemplate.miMeshVertexCount > 0);
		ASSERT(rTemplate.miMeshIndexCount > 0);
		{
			// Heap: UploadMesh creates transient VMA staging allocations in the RenderGlobal residency sweep.
			ScopedSuppressAllocationTracking suppress;
			gpIslands->UploadMesh(rTemplate.mMeshIndexOffset, rTemplate.mpuiMeshIndices, range.vkIndexSize, rTemplate.mMeshVertexOffset, rTemplate.mpfMeshPositions, range.vkVertexSize);
		}
		ReleaseMeshCpuRange(rCrc, rTemplate, range);
		gpIslands->WriteMeshIndirect(rTemplate.miTemplateArrayIndex, rTemplate.mMeshIndexOffset, rTemplate.mMeshVertexOffset, static_cast<uint32_t>(rTemplate.miMeshIndexCount));
		rTemplate.muiMeshArenaBlockedGeneration = 0;
		rTemplate.meMeshResidency = IslandMeshResidency::kResident;
		LOG(kGraphics, kDebug, "Restored island mesh: crc={} vertices={} indices={}", rCrc, rTemplate.miMeshVertexCount, rTemplate.miMeshIndexCount);
	}
}

void IslandTerrain::ReleaseGpuResources()
{
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		rTemplate.mMeshIndexAllocation = VK_NULL_HANDLE;
		rTemplate.mMeshVertexAllocation = VK_NULL_HANDLE;
		rTemplate.mMeshIndexOffset = 0;
		rTemplate.mMeshVertexOffset = 0;
		if (rTemplate.meMeshResidency == IslandMeshResidency::kResident)
		{
			rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
		}
		else if (rTemplate.meMeshResidency == IslandMeshResidency::kArenaBlocked)
		{
			rTemplate.meMeshResidency = IslandMeshResidency::kCpuReady;
		}
		// mElevationTexture is template-owned (no mTextureMap entry), so TextureManager's wholesale
		// destroy doesn't touch it — release here. On device-loss recovery the
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
		if (rTemplate.meMeshResidency == IslandMeshResidency::kAsyncPending)
		{
			MeshRange range = GetMeshRange(rTemplate);
			if (gpFileManager->GetChunkRangeReloadState(rCrc, range.uiOffset, range.uiLength) == ChunkRangeReloadState::kFailed)
			{
				// Teardown can run after the File-owned async request failed but before RestorationSweep
				// promoted this template state. Consume that destroyed-lifecycle failure so re-mint can retry.
				ReleaseMeshCpuRange(rCrc, rTemplate, range);
				rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
			}
		}
		else if (rTemplate.meMeshResidency == IslandMeshResidency::kFailed)
		{
			rTemplate.meMeshResidency = IslandMeshResidency::kNonresident;
		}
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

} // namespace engine

#endif
