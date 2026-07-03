#include "Islands.h"

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Frame/IslandChainPlacement.h"

namespace engine
{

Islands::Islands()
{
	ASSERT(gpIslands == nullptr);

	gpIslands = this;

	// Create per-template mesh buffers before any other render setup. CB record happens later in
	// Graphics::Create after this ctor returns; the recorded CB binds every template's mesh, so
	// they must exist now. WaitForElevationMaps (Main.cpp) populated mesh CPU pointers before
	// Graphics ctor runs.
	gpIslandTerrain->CreateClientMeshBuffers();

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
	int64_t iSsboEntryCount = miTemplateCount * kiMaxPlacementsPerTemplate;
	VkDeviceSize vkIndirectSize = static_cast<VkDeviceSize>(miTemplateCount) * sizeof(VkDrawIndexedIndirectCommand);

	for (int64_t iFramebuffer = 0; iFramebuffer < kiMaxFramebuffers; ++iFramebuffer)
	{
		// SSBO: N_templates × kiMaxPlacementsPerTemplate slots. Zero-initialized — every slot is a
		// zero-width quad until UpdateActiveIslands writes a real placement, which produces a
		// degenerate triangle the vertex shader culls.
		mIslandsStorageBuffers.at(iFramebuffer).Create(
		{
			.name = "Islands",
			.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
			.iCount = iSsboEntryCount,
			.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
			.dataVkDeviceSize = static_cast<VkDeviceSize>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout),
		});
		std::memset(mIslandsStorageBuffers.at(iFramebuffer).mpMappedMemory, 0, static_cast<size_t>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout));

		// Baseline record: whole SSBO just zeroed, so no slots are stale — all last-written counts start at 0.
		mLastWrittenCounts.at(iFramebuffer).resize(static_cast<size_t>(miTemplateCount), 0u);

		// Per-template VkDrawIndexedIndirectCommand buffer. indexCount / firstIndex / vertexOffset /
		// firstInstance baked here; instanceCount rewritten per frame from UpdateActiveIslands.
		VmaAllocationInfo vmaAllocationInfo {};
		VkDeviceMemory vkDeviceMemoryUnused = VK_NULL_HANDLE;
		Buffer::CreateBuffer("IslandsIndirect", vkIndirectSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mIslandsIndirectVkBuffers.at(iFramebuffer), vkDeviceMemoryUnused, mIslandsIndirectVmaAllocations.at(iFramebuffer), &vmaAllocationInfo);
		mppIslandsIndirectMapped.at(iFramebuffer) = static_cast<VkDrawIndexedIndirectCommand*>(vmaAllocationInfo.pMappedData);

		for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
		{
			const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(gpIslandTerrain->mIslandCrcsSorted[static_cast<size_t>(iTemplate)]);
			mppIslandsIndirectMapped.at(iFramebuffer)[iTemplate] = VkDrawIndexedIndirectCommand
			{
				.indexCount = static_cast<uint32_t>(rTemplate.miMeshIndexCount),
				.instanceCount = 0,
				.firstIndex = 0,
				.vertexOffset = 0,
				.firstInstance = static_cast<uint32_t>(iTemplate * kiMaxPlacementsPerTemplate),
			};
		}
	}
}

Islands::~Islands()
{
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

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, std::span<const GridCoord> rActiveCoords)
{
	// Write only the framebuffer instance the current frame will consume. miFramebufferIndex was set by the
	// trailing AcquireNextImage of the prior render (Graphics.cpp); it is the index RenderGlobal reads
	// (Graphics.cpp:166) and the record-once CB for that framebuffer binds, and is stable across this
	// ClientUpdate. Re-acquiring this image index implies the prior frame that used it has presented, so its
	// GPU read of this instance has finished; this frame's render is not yet recorded — hence no host/GPU race.
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

	// Zero every template's instanceCount so the indirect-gated terrain MESH pass skips inactive
	// templates. That gate alone is NOT enough for the elevation and shadow-elevation prepasses,
	// which draw the full fixed slot count (miTemplateCount * kiMaxPlacementsPerTemplate) and rely
	// purely on zero-width-quad culling — so stale SSBO quad geometry left from a prior frame (e.g. a
	// no-longer-active larger island) would keep rendering into the elevation RTT the water early-out
	// samples.
	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		pIndirect[iTemplate].instanceCount = 0;
	}

	auto pSsbo = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(rStorageBuffer.mpMappedMemory);

	// Clear only the slots this framebuffer instance wrote the last time it was populated. Those are the
	// only slots that can be stale: the ctor's full memset zeroed everything, and every frame writes each
	// template's slots densely from its base (iTemplate * kiMaxPlacementsPerTemplate) up to its recorded
	// count, leaving all other slots at their prior value. Re-clearing exactly the previously-written
	// ranges restores the all-zero baseline the active writes below overwrite — equivalent to the old
	// whole-SSBO memset, but touching only (last active) slots of the ~16 MiB reserved slab. Buffer is
	// host-coherent (matches the boot memset), so no barrier / CB re-record is needed.
	std::vector<uint32_t>& rLastWrittenCounts = mLastWrittenCounts.at(iFramebuffer);
	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		uint32_t uiLastCount = rLastWrittenCounts[static_cast<size_t>(iTemplate)];
		if (uiLastCount > 0)
		{
			std::memset(&pSsbo[iTemplate * kiMaxPlacementsPerTemplate], 0, static_cast<size_t>(uiLastCount) * sizeof(shaders::AxisAlignedQuadLayout));
		}
	}

	// Per-template running emit counter — sized to miTemplateCount, lives in the workbuffer (no heap).
	auto puiPerTemplateCount = common::gpThreadLocal->mWorkbuffer.PushBuffer<uint32_t*>(static_cast<size_t>(miTemplateCount) * sizeof(uint32_t));
	std::memset(puiPerTemplateCount, 0, static_cast<size_t>(miTemplateCount) * sizeof(uint32_t));

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
			++rTemplate.miRefCount;
			rTemplate.muiLastUsedRenderFrame = gpGraphics->muiFrameCounter;

			int64_t iTemplate = rTemplate.miTemplateArrayIndex;
			ASSERT(iTemplate >= 0 && iTemplate < miTemplateCount);
			uint32_t uiSlotInTemplate = puiPerTemplateCount[iTemplate];
			if (uiSlotInTemplate >= static_cast<uint32_t>(kiMaxPlacementsPerTemplate))
			{
				// Worst-case headroom blown. Skip extra placements rather than corrupt neighbouring
				// templates' SSBO ranges; raise kiMaxPlacementsPerTemplate if this fires.
				ASSERT(false);
				continue;
			}

			int64_t iSsboIndex = iTemplate * kiMaxPlacementsPerTemplate + static_cast<int64_t>(uiSlotInTemplate);
			shaders::AxisAlignedQuadLayout& rQuad = pSsbo[iSsboIndex];

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
			rQuad.uiTextureSlot = static_cast<uint32_t>(gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc));

			++puiPerTemplateCount[iTemplate];
			pIndirect[iTemplate].instanceCount = puiPerTemplateCount[iTemplate];
		}
	}

	// Record this frame's dense per-template counts so the next population of this framebuffer index clears
	// exactly these slots. puiPerTemplateCount == pIndirect[].instanceCount and is capped at
	// kiMaxPlacementsPerTemplate by the overflow guard above, so every recorded range stays in bounds.
	std::memcpy(rLastWrittenCounts.data(), puiPerTemplateCount, static_cast<size_t>(miTemplateCount) * sizeof(uint32_t));
}

} // namespace engine

#endif // BT_CLIENT
