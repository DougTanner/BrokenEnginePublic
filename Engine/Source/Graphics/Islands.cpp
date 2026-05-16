#include "Islands.h"

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Frame/IslandPlacement.h"

namespace engine
{

Islands::Islands()
{
	gpIslands = this;

	// Create per-template mesh buffers before any other render setup. CB record happens later in
	// Graphics::Create after this ctor returns; the recorded CB binds every template's mesh, so
	// they must exist now. WaitForElevationMaps (Main.cpp) populated mesh CPU pointers before
	// Graphics ctor runs.
	gpIslandTerrain->CreateClientMeshBuffers();

	miTemplateCount = static_cast<int64_t>(gpIslandTerrain->mIslandCrcsSorted.size());
	ASSERT(miTemplateCount > 0);
	ASSERT(miTemplateCount <= shaders::kiMaxIslands);

	// Calculate global area bounds from the base cell
	mf4GlobalArea.x = game::Frame::kfBaseAreaMinX;
	mf4GlobalArea.y = game::Frame::kfBaseAreaMaxY;
	mf4GlobalArea.z = game::Frame::kfBaseAreaMaxX;
	mf4GlobalArea.w = game::Frame::kfBaseAreaMinY;

	// SSBO: N_templates × kiMaxPlacementsPerTemplate slots. Zero-initialized — every slot is a
	// zero-width quad until UpdateActiveIslands writes a real placement, which produces a
	// degenerate triangle the vertex shader culls.
	int64_t iSsboEntryCount = miTemplateCount * kiMaxPlacementsPerTemplate;
	mIslandsStorageBuffer.Create(
	{
		.name = "Islands",
		.flags = {BufferFlags::kStorage, BufferFlags::kHostVisible},
		.iCount = iSsboEntryCount,
		.iVertexStride = sizeof(shaders::AxisAlignedQuadLayout),
		.dataVkDeviceSize = static_cast<VkDeviceSize>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout),
	});
	std::memset(mIslandsStorageBuffer.mpMappedMemory, 0, static_cast<size_t>(iSsboEntryCount) * sizeof(shaders::AxisAlignedQuadLayout));

	// Per-template VkDrawIndexedIndirectCommand buffer. indexCount / firstIndex / vertexOffset /
	// firstInstance baked here; instanceCount rewritten per frame from UpdateActiveIslands.
	VkDeviceSize vkIndirectSize = static_cast<VkDeviceSize>(miTemplateCount) * sizeof(VkDrawIndexedIndirectCommand);
	VmaAllocationInfo vmaAllocationInfo {};
	VkDeviceMemory vkDeviceMemoryUnused = VK_NULL_HANDLE;
	Buffer::CreateBuffer("IslandsIndirect", vkIndirectSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mIslandsIndirectVkBuffer, vkDeviceMemoryUnused, mIslandsIndirectVmaAllocation, &vmaAllocationInfo);
	mpIslandsIndirectMappedMemory = static_cast<VkDrawIndexedIndirectCommand*>(vmaAllocationInfo.pMappedData);

	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(gpIslandTerrain->mIslandCrcsSorted[static_cast<size_t>(iTemplate)]);
		mpIslandsIndirectMappedMemory[iTemplate] = VkDrawIndexedIndirectCommand
		{
			.indexCount = static_cast<uint32_t>(rTemplate.miMeshIndexCount),
			.instanceCount = 0,
			.firstIndex = 0,
			.vertexOffset = 0,
			.firstInstance = static_cast<uint32_t>(iTemplate * kiMaxPlacementsPerTemplate),
		};
	}
}

Islands::~Islands()
{
	if (mIslandsIndirectVkBuffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mIslandsIndirectVkBuffer, mIslandsIndirectVmaAllocation);
		mIslandsIndirectVkBuffer = VK_NULL_HANDLE;
		mIslandsIndirectVmaAllocation = VK_NULL_HANDLE;
		mpIslandsIndirectMappedMemory = nullptr;
	}
	gpIslands = nullptr;
}

void Islands::UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords)
{
	// Phase 5 LRU: recompute per-template ref counts from scratch each frame. Templates with ref
	// count 0 for kuiGraceRenderFrames become eviction candidates in the next RenderGlobal
	// pre-fence EvictionSweep.
	for (auto& [rCrc, rTemplate] : gpIslandTerrain->mIslands)
	{
		rTemplate.miRefCount = 0;
	}

	// Zero every template's instanceCount. Each per-template SSBO entry that was active last frame
	// stays in mIslandsStorageBuffer with stale data, but since the indirect cmd's instanceCount
	// is zero the shader never reads it. Active entries below are overwritten before instanceCount
	// is bumped — no stale reads possible.
	for (int64_t iTemplate = 0; iTemplate < miTemplateCount; ++iTemplate)
	{
		mpIslandsIndirectMappedMemory[iTemplate].instanceCount = 0;
	}

	// Per-template running emit counter — sized to miTemplateCount, lives in the workbuffer (no heap).
	auto puiPerTemplateCount = common::gpThreadLocal->mWorkbuffer.PushBuffer<uint32_t*>(static_cast<size_t>(miTemplateCount) * sizeof(uint32_t));
	std::memset(puiPerTemplateCount, 0, static_cast<size_t>(miTemplateCount) * sizeof(uint32_t));

	auto pSsbo = reinterpret_cast<shaders::AxisAlignedQuadLayout*>(mIslandsStorageBuffer.mpMappedMemory);

	float fMinX = std::numeric_limits<float>::max();
	float fMaxY = std::numeric_limits<float>::lowest();
	float fMaxX = std::numeric_limits<float>::lowest();
	float fMinY = std::numeric_limits<float>::max();
	bool bAnyActive = false;

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
			mpIslandsIndirectMappedMemory[iTemplate].instanceCount = puiPerTemplateCount[iTemplate];

			// Expand mf4GlobalArea by this rotated rect.
			float fHalfW = 0.5f * std::abs(rQuad.f4VertexRect.z);
			float fHalfH = 0.5f * std::abs(rQuad.f4VertexRect.w);
			float fAbsCos = std::abs(std::cos(rQuad.fRotation));
			float fAbsSin = std::abs(std::sin(rQuad.fRotation));
			float fRotHalfW = fHalfW * fAbsCos + fHalfH * fAbsSin;
			float fRotHalfH = fHalfW * fAbsSin + fHalfH * fAbsCos;
			float fCenterX = rQuad.f4VertexRect.x + 0.5f * rQuad.f4VertexRect.z;
			float fCenterY = rQuad.f4VertexRect.y + 0.5f * rQuad.f4VertexRect.w; // w negative
			fMinX = std::min(fMinX, fCenterX - fRotHalfW);
			fMaxY = std::max(fMaxY, fCenterY + fRotHalfH);
			fMaxX = std::max(fMaxX, fCenterX + fRotHalfW);
			fMinY = std::min(fMinY, fCenterY - fRotHalfH);
			bAnyActive = true;
		}
	}

	if (bAnyActive)
	{
		mf4GlobalArea.x = fMinX;
		mf4GlobalArea.y = fMaxY;
		mf4GlobalArea.z = fMaxX;
		mf4GlobalArea.w = fMinY;
	}
}

} // namespace engine

#endif // BT_CLIENT
