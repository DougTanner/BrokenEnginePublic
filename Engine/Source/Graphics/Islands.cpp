#if defined(BT_CLIENT)

#include "Islands.h"
#include "Frame/FrameStaticData.h"
#include "Frame/IslandChainPlacement.h"

namespace engine
{

Islands::Islands()
{
	ASSERT(gpIslands == nullptr);

	gpIslands = this;

	// The record-once terrain CB binds this arena once. Template mesh residency changes only its
	// sub-allocation offsets in indirect commands, never this VkBuffer handle.
	mIslandMeshArena.Create(
	{
		.name = "IslandMeshArena",
		.flags = {BufferFlags::kIndexVertex, BufferFlags::kDeviceLocal},
		.vkIndexType = VK_INDEX_TYPE_UINT32,
		.iVertexStride = static_cast<int64_t>(2 * sizeof(float)),
		.dataVkDeviceSize = kiIslandMeshArenaBytes,
	});
	VmaVirtualBlockCreateInfo vmaVirtualBlockCreateInfo
	{
		.size = kiIslandMeshArenaBytes,
	};
	CHECK_VK(vmaCreateVirtualBlock(&vmaVirtualBlockCreateInfo, &mIslandMeshVirtualBlock));

	miTemplateCount = static_cast<int64_t>(gpIslandTerrain->mIslandCrcsSorted.size());
	ASSERT(miTemplateCount > 0);
	// Slot 0 is the reserved neutral placeholder (miNextTextureSlot starts at 1), so the usable budget
	// is kiMaxIslands - 1 real templates.
	ASSERT(miTemplateCount < shaders::kiMaxIslands);

	// SSBO + indirect buffers are triple-buffered: one instance per framebuffer index (kiMaxFramebuffers),
	// all created once here and indexed by gpSwapchainManager->miFramebufferIndex thereafter. This keeps the
	// per-frame host rewrite in UpdateActiveIslands off the memory an in-flight frame is still GPU-reading.
	// All kiMaxFramebuffers instances are allocated regardless of the live framebuffer count so any index
	// stays valid across a swapchain recreation that changes the count (mpIslands is not rebuilt then).
	int64_t iSsboEntryCount = kiMaxActivePlacements;
	VkDeviceSize vkIndirectSize = static_cast<VkDeviceSize>(miTemplateCount) * sizeof(VkDrawIndexedIndirectCommand);

	for (int64_t iFramebuffer = 0; iFramebuffer < kiMaxFramebuffers; ++iFramebuffer)
	{
		// SSBO: one shared kiMaxActivePlacements-entry arena. Zero-initialized — every slot is a zero-width
		// quad until UpdateActiveIslands writes a real placement, which produces a degenerate triangle the
		// vertex shader culls.
		mIslandsStorageBuffers.at(iFramebuffer).Create(
		{
			.name = "Islands",
			.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
			.iCount = iSsboEntryCount,
			.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
			.dataVkDeviceSize = static_cast<VkDeviceSize>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout),
		});
		std::memset(mIslandsStorageBuffers.at(iFramebuffer).mpMappedMemory, 0, static_cast<size_t>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout));

		// Per-template VkDrawIndexedIndirectCommand buffer. Residency writes indexCount / firstIndex /
		// vertexOffset in the drained churn window; firstInstance and instanceCount are rewritten per frame by
		// UpdateActiveIslands.
		VmaAllocationInfo vmaAllocationInfo {};
		Buffer::CreateBuffer("IslandsIndirect", vkIndirectSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mIslandsIndirectVkBuffers.at(iFramebuffer), mIslandsIndirectVmaAllocations.at(iFramebuffer), &vmaAllocationInfo);
		mppIslandsIndirectMapped.at(iFramebuffer) = static_cast<VkDrawIndexedIndirectCommand*>(vmaAllocationInfo.pMappedData);

		for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
		{
			mppIslandsIndirectMapped.at(iFramebuffer)[iTemplate] = VkDrawIndexedIndirectCommand
			{
				.indexCount = 0,
				.instanceCount = 0,
				.firstIndex = 0,
				.vertexOffset = 0,
			};
		}
	}
}

Islands::~Islands()
{
	if (mIslandMeshVirtualBlock != VK_NULL_HANDLE)
	{
		vmaClearVirtualBlock(mIslandMeshVirtualBlock);
		++muiMeshArenaCapacityGeneration;
		vmaDestroyVirtualBlock(mIslandMeshVirtualBlock);
		mIslandMeshVirtualBlock = VK_NULL_HANDLE;
	}
	mIslandMeshArena.Destroy();

	// SSBO buffers (std::array<Buffer>) free via RAII; the manually-allocated indirect buffers do not.
	for (int64_t iFramebuffer = 0; iFramebuffer < kiMaxFramebuffers; ++iFramebuffer)
	{
		if (mIslandsIndirectVkBuffers.at(iFramebuffer) != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mIslandsIndirectVkBuffers.at(iFramebuffer), mIslandsIndirectVmaAllocations.at(iFramebuffer));
			mIslandsIndirectVkBuffers.at(iFramebuffer) = VK_NULL_HANDLE;
			mIslandsIndirectVmaAllocations.at(iFramebuffer) = VK_NULL_HANDLE;
			mppIslandsIndirectMapped.at(iFramebuffer) = nullptr;
		}
	}
	if (gpIslands == this)
	{
		gpIslands = nullptr;
	}
}

bool Islands::AllocateMeshRanges(VkDeviceSize vkIndexSize, VkDeviceSize vkVertexSize, VmaVirtualAllocation& rIndexAllocation, VkDeviceSize& rIndexOffset, VmaVirtualAllocation& rVertexAllocation, VkDeviceSize& rVertexOffset)
{
	ASSERT(mIslandMeshVirtualBlock != VK_NULL_HANDLE);

	VmaVirtualAllocation vmaIndexAllocation = VK_NULL_HANDLE;
	VkDeviceSize vkIndexOffset = 0;
	VmaVirtualAllocationCreateInfo indexAllocationCreateInfo
	{
		.size = vkIndexSize,
		.alignment = sizeof(uint32_t),
	};
	if (vmaVirtualAllocate(mIslandMeshVirtualBlock, &indexAllocationCreateInfo, &vmaIndexAllocation, &vkIndexOffset) != VK_SUCCESS)
	{
		return false;
	}

	VmaVirtualAllocation vmaVertexAllocation = VK_NULL_HANDLE;
	VkDeviceSize vkVertexOffset = 0;
	VmaVirtualAllocationCreateInfo vertexAllocationCreateInfo
	{
		.size = vkVertexSize,
		.alignment = 2 * sizeof(float),
	};
	if (vmaVirtualAllocate(mIslandMeshVirtualBlock, &vertexAllocationCreateInfo, &vmaVertexAllocation, &vkVertexOffset) != VK_SUCCESS)
	{
		vmaVirtualFree(mIslandMeshVirtualBlock, vmaIndexAllocation);
		return false;
	}

	rIndexAllocation = vmaIndexAllocation;
	rIndexOffset = vkIndexOffset;
	rVertexAllocation = vmaVertexAllocation;
	rVertexOffset = vkVertexOffset;
	return true;
}

void Islands::FreeMeshRanges(VmaVirtualAllocation vmaIndexAllocation, VmaVirtualAllocation vmaVertexAllocation)
{
	ASSERT(mIslandMeshVirtualBlock != VK_NULL_HANDLE);
	ASSERT(vmaIndexAllocation != VK_NULL_HANDLE);
	ASSERT(vmaVertexAllocation != VK_NULL_HANDLE);
	vmaVirtualFree(mIslandMeshVirtualBlock, vmaIndexAllocation);
	vmaVirtualFree(mIslandMeshVirtualBlock, vmaVertexAllocation);
	++muiMeshArenaCapacityGeneration;
}

void Islands::UploadMesh(VkDeviceSize vkIndexOffset, const void* pIndexData, VkDeviceSize vkIndexSize, VkDeviceSize vkVertexOffset, const void* pVertexData, VkDeviceSize vkVertexSize)
{
	ASSERT(vkIndexOffset <= kiIslandMeshArenaBytes && vkIndexSize <= kiIslandMeshArenaBytes - vkIndexOffset);
	ASSERT(vkVertexOffset <= kiIslandMeshArenaBytes && vkVertexSize <= kiIslandMeshArenaBytes - vkVertexOffset);
	DeviceLocalBufferUpload uploads[]
	{
		{.pData = pIndexData, .vkDestinationOffset = vkIndexOffset, .vkSize = vkIndexSize},
		{.pData = pVertexData, .vkDestinationOffset = vkVertexOffset, .vkSize = vkVertexSize},
	};
	Buffer::UploadToDeviceLocal(mIslandMeshArena.mDeviceLocalVkBuffer, uploads);
}

void Islands::WriteMeshIndirect(int64_t iTemplate, VkDeviceSize vkIndexOffset, VkDeviceSize vkVertexOffset, uint32_t uiIndexCount)
{
	ASSERT(iTemplate >= 0 && iTemplate < miTemplateCount);
	ASSERT(vkIndexOffset % sizeof(uint32_t) == 0);
	ASSERT(vkVertexOffset % (2 * sizeof(float)) == 0);
	for (int64_t iFramebuffer = 0; iFramebuffer < kiMaxFramebuffers; ++iFramebuffer)
	{
		VkDrawIndexedIndirectCommand& rIndirect = mppIslandsIndirectMapped.at(iFramebuffer)[iTemplate];
		rIndirect.indexCount = uiIndexCount;
		rIndirect.firstIndex = static_cast<uint32_t>(vkIndexOffset / sizeof(uint32_t));
		rIndirect.vertexOffset = static_cast<int32_t>(vkVertexOffset / (2 * sizeof(float)));
	}
}

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, std::span<const GridCoord> rActiveCoords)
{
	// Write only the framebuffer instance the current frame will consume. miFramebufferIndex was set by the
	// trailing AcquireNextImage of the prior render (Graphics.cpp); it is the index RenderGlobal reads
	// (Graphics.cpp:166) and the record-once CB for that framebuffer binds, and is stable until this frame's
	// submission. Re-acquiring this image index implies the prior frame that used it has presented, so its
	// GPU read of this instance has finished; this frame's render is not yet submitted — hence no host/GPU race.
	int64_t iFramebuffer = gpSwapchainManager->miFramebufferIndex;
	Buffer& rStorageBuffer = mIslandsStorageBuffers.at(iFramebuffer);
	VkDrawIndexedIndirectCommand* pIndirect = mppIslandsIndirectMapped.at(iFramebuffer);

	// Phase 5 LRU: recompute per-template ref counts from scratch each frame. Templates with ref
	// count 0 for kuiGraceRenderFrames become eviction candidates in the next RenderGlobal
	// pre-fence EvictionSweep.
	for (auto& [rCrc, rTemplate] : gpIslandTerrain->mIslands)
	{
		rTemplate.miRefCount = 0;
	}

	auto pSsbo = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(rStorageBuffer.mpMappedMemory);
	uint32_t uiPreviousWrittenTotal = mLastWrittenCounts.at(iFramebuffer);

	// Counting, prefix, and emission cursors are per-frame scratch in one contiguous thread-workbuffer
	// reservation. Keeping them in one reservation ensures a possible workbuffer grow happens before any
	// derived pointer is taken; the first two counts then stay immutable through emission so the indirect
	// record and final stale-tail clear use the same totals that established each run's base.
	int64_t iTemplateArrayBytes = miTemplateCount * static_cast<int64_t>(sizeof(uint32_t));
	auto puiPerTemplateScratch = common::gpThreadLocal->mWorkbuffer.PushBuffer<uint32_t*>(4 * iTemplateArrayBytes);
	uint32_t* puiPerTemplateMeshVisibleCount = puiPerTemplateScratch;
	uint32_t* puiPerTemplateTotalCount = puiPerTemplateScratch + miTemplateCount;
	uint32_t* puiPerTemplateBase = puiPerTemplateTotalCount + miTemplateCount;
	uint32_t* puiPerTemplateEmitCount = puiPerTemplateBase + miTemplateCount;
	std::memset(puiPerTemplateMeshVisibleCount, 0, static_cast<size_t>(iTemplateArrayBytes));
	std::memset(puiPerTemplateTotalCount, 0, static_cast<size_t>(iTemplateArrayBytes));
	std::memset(puiPerTemplateBase, 0, static_cast<size_t>(iTemplateArrayBytes));
	std::memset(puiPerTemplateEmitCount, 0, static_cast<size_t>(iTemplateArrayBytes));

	// f4RenderVisibleArea is the straight-down frustum footprint at Z=0. The lowest terrain vertices
	// are sunk to mfSeaFloorElevation, whose perspective footprint is the widest; expand analytically
	// about the camera XY so every higher vertex lies within this conservative area.
	XMFLOAT4A f4EyePosition {};
	XMStoreFloat4A(&f4EyePosition, game::gpCamera->mVecEyePosition);
	float fSeaFloorScale = (f4EyePosition.z - gpIslandTerrain->mfSeaFloorElevation) / f4EyePosition.z;
	XMFLOAT4 f4MeshVisibleArea = game::gpCamera->f4RenderVisibleArea;
	f4MeshVisibleArea.x = f4EyePosition.x + (f4MeshVisibleArea.x - f4EyePosition.x) * fSeaFloorScale;
	f4MeshVisibleArea.y = f4EyePosition.y + (f4MeshVisibleArea.y - f4EyePosition.y) * fSeaFloorScale;
	f4MeshVisibleArea.z = f4EyePosition.x + (f4MeshVisibleArea.z - f4EyePosition.x) * fSeaFloorScale;
	f4MeshVisibleArea.w = f4EyePosition.y + (f4MeshVisibleArea.w - f4EyePosition.y) * fSeaFloorScale;

	auto IsMeshVisible = [&](const IslandPlacement& rPlacement, const IslandTemplate& rTemplate)
	{
		float fRadius = 0.5f * std::hypot(rTemplate.mfQuadFootprintX, rTemplate.mfQuadFootprintY);
		XMFLOAT4 f4Position {rPlacement.f2WorldPos.x, rPlacement.f2WorldPos.y, 0.0f, 1.0f};
		return game::gpCamera->InVisibleArea(f4MeshVisibleArea, f4Position, fRadius, fRadius, fRadius, fRadius);
	};

	// Count every active placement with the same frame and visibility predicates used by both emission passes.
	// Residency bookkeeping and texture-slot acquisition remain once per placement, matching the previous
	// visible-first pass while leaving the actual writes until after all per-template bases are known.
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second.iSnapshotCount == 0)
		{
			continue;
		}

		const FrameStaticData& rStaticData = it->second.staticData;
		for (const IslandPlacement& rPlacement : rStaticData.islands)
		{
			IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			int64_t iTemplate = rTemplate.miTemplateArrayIndex;
			ASSERT(iTemplate >= 0 && iTemplate < miTemplateCount);
			++rTemplate.miRefCount;
			rTemplate.muiLastUsedRenderFrame = gpGraphics->muiFrameCounter;
			(void)gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc);
			++puiPerTemplateTotalCount[iTemplate];
			if (IsMeshVisible(rPlacement, rTemplate))
			{
				++puiPerTemplateMeshVisibleCount[iTemplate];
			}
		}
	}

	// Check the aggregate before publishing any indirect command. A malformed placement set must not expose
	// a firstInstance/instanceCount pair that addresses beyond the shared SSBO arena.
	uint64_t uiCurrentWrittenTotal = 0;
	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		uiCurrentWrittenTotal += puiPerTemplateTotalCount[iTemplate];
	}
	ASSERT(uiCurrentWrittenTotal <= static_cast<uint64_t>(kiMaxActivePlacements));

	// Exclusive prefix sum of per-template totals establishes each contiguous run in the shared arena.
	// firstInstance and the mesh-visible instanceCount are refreshed for every template, including zero-count
	// templates, in the acquired framebuffer's indirect buffer only.
	uint32_t uiCurrentWrittenOffset = 0;
	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		puiPerTemplateBase[iTemplate] = uiCurrentWrittenOffset;
		pIndirect[iTemplate].firstInstance = puiPerTemplateBase[iTemplate];
		pIndirect[iTemplate].instanceCount = puiPerTemplateMeshVisibleCount[iTemplate];
		uiCurrentWrittenOffset += puiPerTemplateTotalCount[iTemplate];
	}

	auto EmitPlacement = [&](const IslandPlacement& rPlacement, const IslandTemplate& rTemplate, uint32_t uiTextureSlot)
	{
		int64_t iTemplate = rTemplate.miTemplateArrayIndex;
		ASSERT(iTemplate >= 0 && iTemplate < miTemplateCount);
		uint32_t uiSlotInTemplate = puiPerTemplateEmitCount[iTemplate];
		uint64_t uiStorageBufferIndex = static_cast<uint64_t>(puiPerTemplateBase[iTemplate]) + uiSlotInTemplate;
		if (uiStorageBufferIndex >= static_cast<uint64_t>(kiMaxActivePlacements))
		{
			// The subscription and placement contracts prove this global arena bound. Keep the guard at the
			// write boundary so contract drift cannot corrupt an adjacent arena entry.
			ASSERT(false);
			return;
		}

		shaders::AxisAlignedQuadLayout& rQuad = pSsbo[uiStorageBufferIndex];

		rQuad.f4VertexRect.x = rPlacement.f2WorldPos.x - 0.5f * rTemplate.mfQuadFootprintX;
		rQuad.f4VertexRect.y = rPlacement.f2WorldPos.y + 0.5f * rTemplate.mfQuadFootprintY;
		rQuad.f4VertexRect.z = rTemplate.mfQuadFootprintX;
		rQuad.f4VertexRect.w = -rTemplate.mfQuadFootprintY;

		rQuad.f4TextureRect.x = 0.0f;
		rQuad.f4TextureRect.z = 1.0f;
		rQuad.f4TextureRect.y = 0.0f;
		rQuad.f4TextureRect.w = 1.0f;

		rQuad.f4Params.x = 0.0f;
		rQuad.fRotation = rPlacement.fRotation;
		rQuad.uiTextureSlot = uiTextureSlot;

		++puiPerTemplateEmitCount[iTemplate];
	};

	// First emission pass packs the mesh-visible prefix for every template. Visible placements receive the
	// beginning of each template's run so the indirect terrain draw consumes exactly this prefix.
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second.iSnapshotCount == 0)
		{
			continue;
		}

		const FrameStaticData& rStaticData = it->second.staticData;
		for (const IslandPlacement& rPlacement : rStaticData.islands)
		{
			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			if (IsMeshVisible(rPlacement, rTemplate))
			{
				EmitPlacement(rPlacement, rTemplate, static_cast<uint32_t>(rTemplate.miTextureSlot));
			}
		}
	}

	// Second pass appends every offscreen placement. Texture slots were acquired in the first pass; reuse the
	// template-owned slot without touching residency. Mesh-visible placements always remain in the prefix so
	// TerrainElevation covers every terrain mesh instance, while the full subscribed set remains available to
	// ShadowElevation.
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = rFrames.find(rCoord);
		if (it == rFrames.end() || it->second.iSnapshotCount == 0)
		{
			continue;
		}

		const FrameStaticData& rStaticData = it->second.staticData;
		for (const IslandPlacement& rPlacement : rStaticData.islands)
		{
			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			if (!IsMeshVisible(rPlacement, rTemplate))
			{
				EmitPlacement(rPlacement, rTemplate, static_cast<uint32_t>(rTemplate.miTextureSlot));
			}
		}
	}

	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		ASSERT(puiPerTemplateEmitCount[iTemplate] == puiPerTemplateTotalCount[iTemplate]);
	}

	if (uiCurrentWrittenTotal < uiPreviousWrittenTotal)
	{
		// The current run overwrote [0, uiCurrentWrittenTotal). Clear only the stale tail from this frame's
		// total through the previous total; the prepasses draw the entire fixed arena and would otherwise see
		// placements left by the prior population of this framebuffer instance.
		std::memset(&pSsbo[static_cast<size_t>(uiCurrentWrittenTotal)], 0, static_cast<size_t>(uiPreviousWrittenTotal - uiCurrentWrittenTotal) * sizeof(shaders::AxisAlignedQuadLayout));
	}
	mLastWrittenCounts.at(iFramebuffer) = static_cast<uint32_t>(uiCurrentWrittenTotal);
}

} // namespace engine

#endif // BT_CLIENT
