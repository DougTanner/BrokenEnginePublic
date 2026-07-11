#if defined(BT_CLIENT)

#include "IslandTerrain.h"

#include "Graphics/Managers/TextureManager.h"

namespace engine
{

void IslandTerrain::CreateClientMeshBuffers()
{
	// One-shot at boot: upload each template's Gaea Mesher-baked terrain mesh into a permanent
	// GPU buffer. Layout: [uint32 indices, float2 XY positions] — indices first matches the
	// existing terrain/water mesh-buffer convention (BufferManager.cpp). Z is omitted —
	// Terrain.vert re-derives world Z from the elevation sampler.
	//
	// Eager (not lazy) because the terrain command buffer is record-once and binds every
	// template's mesh at record time; lazy create would force a runtime CB re-record on
	// first visit, which violates the record-once invariant.
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		ASSERT(rTemplate.mpfMeshPositions != nullptr);
		ASSERT(rTemplate.mpuiMeshIndices != nullptr);
		ASSERT(rTemplate.miMeshVertexCount > 0);
		ASSERT(rTemplate.miMeshIndexCount > 0);
		ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE);

		int64_t iMeshIndexBytes = static_cast<int64_t>(rTemplate.miMeshIndexCount) * static_cast<int64_t>(sizeof(uint32_t));
		int64_t iMeshPositionBytes = static_cast<int64_t>(rTemplate.miMeshVertexCount) * 2 * static_cast<int64_t>(sizeof(float));

		// The mesh CPU slice ([positions][indices]) sits in the kIsland chunk payload right after the heightmap halfs.
		// On device-loss recovery the pool pages were decommitted after the previous upload, so recommit + reload them
		// from disk before the memcpy below re-reads mpfMeshPositions/mpuiMeshIndices (their pool pointers are unchanged).
		int64_t iHeightmapBytes = static_cast<int64_t>(rTemplate.miHeightmapWidth) * static_cast<int64_t>(rTemplate.miHeightmapHeight) * static_cast<int64_t>(sizeof(uint16_t));
		int64_t iMeshBytes = iMeshPositionBytes + iMeshIndexBytes;
		if (rTemplate.mbMeshCpuDecommitted)
		{
			if (!gpFileManager->RecommitAndReloadChunkRange(rCrc, static_cast<uint64_t>(iHeightmapBytes), static_cast<uint64_t>(iMeshBytes)))
			{
				// Recommit soft-failed (MEM_COMMIT / pack-open / short read): the mesh CPU slice was NOT reloaded, so the
				// Buffer::Create memcpy below would read decommitted/partial pages and fault far from the true cause. Fail
				// loud here at the real cause site instead.
				ScopedSuppressAllocationTracking suppress; // Heap: std::format builds the throw message on the allocation-tracked device-loss recovery path
				LOG(kGraphics, kError, "Island mesh CPU recommit failed for chunk {} on device-loss recovery", rCrc);
				throw std::runtime_error(std::format("Island mesh CPU recommit failed for chunk {}", rCrc));
			}
		}

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

		// Reclaim the mesh CPU slice: it is dead after this one-time GPU upload (never read again; the server never
		// even assigns these pointers). CreateClientMeshBuffers reloads it on device-loss recovery via the gate above.
		gpFileManager->DecommitChunkRange(rCrc, static_cast<uint64_t>(iHeightmapBytes), static_cast<uint64_t>(iMeshBytes));
		rTemplate.mbMeshCpuDecommitted = true;
	}
}

namespace
{
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
				.format = VK_FORMAT_R16_SFLOAT,
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
	CreateElevationTextureFromHeightmap(rTemplate, name);

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
				rTextureDescriptors.RegisterTextureBinding({.crc = bindingKey, .pPipeline = rConsumer.pPipeline, .iBinding = rConsumer.iBinding, .samplerFlags = rConsumer.samplerFlags, .ppTextures = ppArray, .iCount = rConsumer.iCount, .iArrayIndex = iSlot});
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

bool IslandTerrain::EvictTemplate(common::crc_t islandCrc, IslandTemplate& rTemplate)
{
	// Slot 0 is the neutral placeholder anchor (never adopted by a real island). The guard is
	// defensive — miNextTextureSlot starts at 1 so no template should ever have slot 0 — but
	// keep it to document the invariant.
	if (rTemplate.miTextureSlot == 0 || !rTemplate.mbGpuResident || rTemplate.miRefCount != 0)
	{
		return false;
	}
	if ((gpGraphics->muiFrameCounter - rTemplate.muiLastUsedRenderFrame) <= kuiGraceRenderFrames)
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
		rTextureDescriptors.UnregisterBindingsForKey(islandCrc);
		for (common::crc_t textureCrc : evictCrcs)
		{
			rTextureDescriptors.UnregisterBindingsForKey(textureCrc);
		}
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

	bool bDirty = false;
	for (auto& [rCrc, rTemplate] : mIslands)
	{
		if (EvictTemplate(rCrc, rTemplate))
		{
			bDirty = true;
		}
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

} // namespace engine

#endif
