#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct Frame;

} // namespace game

namespace engine
{

class Texture;
struct CoordFrames;
struct GridCoord;

// Per-template SSBO range size. Worst-case per-template placement count = 9 active cells × up to 4
// placements per cell = 36, all hitting the same template — round to 64 for headroom. Total SSBO
// memory = N_templates × 64 × sizeof(AxisAlignedQuadLayout); negligible.
inline constexpr int64_t kiMaxPlacementsPerTemplate = 64;

class Islands
{
public:

	Islands();
	~Islands();

	void UpdateActiveIslands(const std::unordered_map<GridCoord, CoordFrames>& rFrames, const std::vector<GridCoord>& rActiveCoords);

	XMFLOAT4 mf4GlobalArea {};

	// SSBO indexed by [T_array_index * kiMaxPlacementsPerTemplate + n] where T_array_index is the
	// template's index in gpIslandTerrain->mIslandCrcsSorted (fixed at boot). Inactive slots stay
	// zero-width so the vertex shader emits degenerate triangles (GPU-culled).
	Buffer mIslandsStorageBuffer;

	// Per-template VkDrawIndexedIndirectCommand. indexCount / firstIndex / vertexOffset / firstInstance
	// are baked once at boot; instanceCount is rewritten per frame from UpdateActiveIslands.
	// Allocated manually (Buffer wrapper has no INDIRECT_BUFFER_BIT path); mirrors Pipeline's
	// mIndirectVkBuffer pattern at Engine/Source/Graphics/Objects/PipelineCreator.cpp:75-78.
	VkBuffer mIslandsIndirectVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mIslandsIndirectVmaAllocation = VK_NULL_HANDLE;
	VkDrawIndexedIndirectCommand* mpIslandsIndirectMappedMemory = nullptr;

	int64_t miTemplateCount = 0;  // Cached gpIslandTerrain->mIslandCrcsSorted.size() (fixed at boot).
};

inline Islands* gpIslands = nullptr;

} // namespace engine

#endif // BT_CLIENT
