#pragma once

#if defined(BT_CLIENT)

#include "Frame/IslandChainPlacement.h"

namespace engine
{

struct CoordFrames;
struct GridCoord;

// Global SSBO arena capacity: every subscribed coordinate slot plus the local unconfirmed cell can contribute
// at most kiMaxIslandsPerCell placements. Per-template runs are packed contiguously into this shared arena,
// with mesh-visible placements first. Resident allocation is kiMaxFramebuffers × kiMaxActivePlacements ×
// sizeof(AxisAlignedQuadLayout): with kiCoordSlots = 16 and kiMaxIslandsPerCell = 107 this is 436,560 bytes
// (426.33 KiB), independent of the number of island templates.
inline constexpr int64_t kiMaxActivePlacements = (game::NetworkSessionContract::kiCoordSlots + 1) * kiMaxIslandsPerCell;
inline constexpr VkDeviceSize kiIslandMeshArenaBytes = 64ull * 1024ull * 1024ull;

// The SSBO and indirect buffers are triple-buffered (one instance per framebuffer index, sized by
// kiMaxFramebuffers from Managers/BufferManager.h): UpdateActiveIslands (immediately before RenderGlobal) writes
// only the instance for the framebuffer index just re-acquired. Re-acquiring that image index implies the prior
// frame that used it has presented — so its GPU read of that instance is complete — while this frame's
// render is not yet submitted, so the host write never overlaps an in-flight GPU read. (The earlier single
// shared buffer raced because every in-flight frame read the same backing memory the host was rewriting.)
// All instances are allocated once at boot and indexed by gpSwapchainManager->miFramebufferIndex, so they
// survive swapchain recreation unchanged.

class Islands
{
public:

	Islands();
	~Islands();

	bool AllocateMeshRanges(VkDeviceSize vkIndexSize, VkDeviceSize vkVertexSize, VmaVirtualAllocation& rIndexAllocation, VkDeviceSize& rIndexOffset, VmaVirtualAllocation& rVertexAllocation, VkDeviceSize& rVertexOffset);
	void FreeMeshRanges(VmaVirtualAllocation vmaIndexAllocation, VmaVirtualAllocation vmaVertexAllocation);
	void UploadMesh(VkDeviceSize vkIndexOffset, const void* pIndexData, VkDeviceSize vkIndexSize, VkDeviceSize vkVertexOffset, const void* pVertexData, VkDeviceSize vkVertexSize);
	void WriteMeshIndirect(int64_t iTemplate, VkDeviceSize vkIndexOffset, VkDeviceSize vkVertexOffset, uint32_t uiIndexCount);

	void UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, std::span<const GridCoord> rActiveCoords);

	Buffer mIslandMeshArena;
	uint64_t muiMeshArenaCapacityGeneration = 0;

	// One SSBO per framebuffer index. Placements occupy contiguous per-template runs packed into the shared
	// arena by UpdateActiveIslands; each run starts at that template's per-frame firstInstance, with its
	// mesh-visible prefix before the offscreen remainder. Inactive slots stay zero-width so the vertex shader
	// emits degenerate triangles (GPU-culled).
	// Triple-buffered (see kiMaxFramebuffers) and bound via kPerCommandBufferStorageBuffers so an
	// in-flight frame's GPU read never races the host rewrite of the instance the current frame consumes.
	std::array<Buffer, kiMaxFramebuffers> mIslandsStorageBuffers;

	// One per-template VkDrawIndexedIndirectCommand buffer per framebuffer index. Mesh residency rewrites
	// indexCount / firstIndex / vertexOffset in every instance; firstInstance and instanceCount are rewritten
	// per frame by UpdateActiveIslands in the acquired framebuffer's instance only.
	// Allocated manually (Buffer wrapper has no INDIRECT_BUFFER_BIT path); mirrors Pipeline's
	// mIndirectVkBuffer pattern in SetupIndirectBuffer (Engine/Source/Graphics/Objects/PipelineCreator.cpp).
	std::array<VkBuffer, kiMaxFramebuffers> mIslandsIndirectVkBuffers {};
	std::array<VmaAllocation, kiMaxFramebuffers> mIslandsIndirectVmaAllocations {};
	std::array<VkDrawIndexedIndirectCommand*, kiMaxFramebuffers> mppIslandsIndirectMapped {};

	int64_t miTemplateCount = 0;  // Cached gpIslandTerrain->mIslandCrcsSorted.size() (fixed at boot).

	// Per-framebuffer total placement count written the last time that framebuffer index was populated. The
	// current run always starts at arena slot 0, so UpdateActiveIslands clears only the tail from the current
	// total through this previous total after rewriting, instead of clearing the whole reserved arena every
	// frame. Values are zero-initialized to match the ctor's baseline full memset.
	std::array<uint32_t, kiMaxFramebuffers> mLastWrittenCounts {};

private:

	VmaVirtualBlock mIslandMeshVirtualBlock = VK_NULL_HANDLE;
};

inline Islands* gpIslands = nullptr;

} // namespace engine

#endif // BT_CLIENT
