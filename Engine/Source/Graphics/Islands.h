#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct CoordFrames;
struct GridCoord;

// Per-template SSBO range size. IslandChainPlacement draws each per-cell role (Huge anchor / Large+Medium
// chain / Small surround) from a different size bucket, so the same template repeating across the whole
// active ring is unlikely; but with only a handful of distinct small assets a single small template can
// recur heavily. The surround rings every big island (~6) with perimeter-many smalls, so a cell can place
// ~100 islands; if they all landed on one scarce small template that is ~100 x the 9-cell ring ~= 900
// before overlap rejection trims it. 1024 covers that worst case, and Islands.cpp's overflow guard skips
// (with an assert) any excess rather than corrupting neighbouring template ranges. Total SSBO memory =
// N_templates × 1024 × sizeof(AxisAlignedQuadLayout); negligible.
inline constexpr int64_t kiMaxPlacementsPerTemplate = 1024;

// The SSBO and indirect buffers are triple-buffered (one instance per framebuffer index, sized by
// kiMaxFramebuffers from Managers/BufferManager.h): UpdateActiveIslands (in ClientUpdate) writes only the
// instance for the framebuffer index just re-acquired. Re-acquiring that image index implies the prior
// frame that used it has presented — so its GPU read of that instance is complete — while this frame's
// render is not yet recorded, so the host write never overlaps an in-flight GPU read. (The earlier single
// shared buffer raced because every in-flight frame read the same backing memory the host was rewriting.)
// All instances are allocated once at boot and indexed by gpSwapchainManager->miFramebufferIndex, so they
// survive swapchain recreation unchanged.

class Islands
{
public:

	Islands();
	~Islands();

	void UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords);

	// One SSBO per framebuffer index. Each is indexed by [T_array_index * kiMaxPlacementsPerTemplate + n]
	// where T_array_index is the template's index in gpIslandTerrain->mIslandCrcsSorted (fixed at boot).
	// Inactive slots stay zero-width so the vertex shader emits degenerate triangles (GPU-culled).
	// Triple-buffered (see kiMaxFramebuffers) and bound via kPerCommandBufferStorageBuffers so an
	// in-flight frame's GPU read never races the host rewrite of the instance the current frame consumes.
	std::array<Buffer, kiMaxFramebuffers> mIslandsStorageBuffers;

	// One per-template VkDrawIndexedIndirectCommand buffer per framebuffer index. indexCount / firstIndex /
	// vertexOffset / firstInstance are baked once at boot into every instance; instanceCount is rewritten
	// per frame from UpdateActiveIslands into the acquired framebuffer's instance only. Allocated manually
	// (Buffer wrapper has no INDIRECT_BUFFER_BIT path); mirrors Pipeline's mIndirectVkBuffer pattern in
	// SetupIndirectBuffer (Engine/Source/Graphics/Objects/PipelineCreator.cpp).
	std::array<VkBuffer, kiMaxFramebuffers> mIslandsIndirectVkBuffers {};
	std::array<VmaAllocation, kiMaxFramebuffers> mIslandsIndirectVmaAllocations {};
	std::array<VkDrawIndexedIndirectCommand*, kiMaxFramebuffers> mppIslandsIndirectMapped {};

	int64_t miTemplateCount = 0;  // Cached gpIslandTerrain->mIslandCrcsSorted.size() (fixed at boot).
};

inline Islands* gpIslands = nullptr;

} // namespace engine

#endif // BT_CLIENT
