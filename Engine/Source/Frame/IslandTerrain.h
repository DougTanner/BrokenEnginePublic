#pragma once

#include "Frame/NavBuild.h"
#if defined(BT_CLIENT)
#include "Graphics/Objects/Buffer.h"
#endif

namespace engine
{

// Islands adopt 1 m = 1 engine unit. The rest of the engine (player radius, base
// flying height, cell-derived camera ranges, etc.) still runs in unit-less units;
// a future pass converts the whole engine to meters and drops this constant.
inline constexpr float kfMetersToUnits = 1.0f;

#if defined(BT_CLIENT)
// Phase 5 LRU grace: a template's GPU resources stay resident this many render frames after its
// last placement reference drops. ~5s @60Hz, ~2.5s @120Hz. Chosen to cover transient absences
// in moving-camera traversal without holding GPU memory indefinitely.
inline constexpr uint64_t kuiGraceRenderFrames = 300;
#endif

// One entry per kIsland chunk in the manifest. Heightmap pointer fills in
// WaitForElevationMaps once chunk data is resident; NavContour is built for
// every template server-side so multi-template cells get correct nav data.
struct IslandTemplate
{
	common::crc_t mIslandCrc = 0;

	// Heightmap pixel values are engine-meters relative to beach: 0 == sea level, negative ==
	// below water, positive == above water. DataPacker offsets Gaea's [0,1] normalized output
	// by its global beach-height constant (`kfBeachHeightMeters` in BakeIslandIntermediates.cpp)
	// at bake time, so no runtime conversion is required (sea floor sits at -kfBeachHeightMeters
	// for every island). Heightmap is anisotropic: DataPacker auto-crops each island to its land
	// bbox > 1 m, expanded to a multiple of 4 × kiElevationDivisor so BC encoding and elevation
	// downsample alignment hold on both axes.
	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapWidth = 0;
	int32_t miHeightmapHeight = 0;

	float mfWorldFootprintXMeters = 0.0f;
	float mfWorldFootprintYMeters = 0.0f;
	float mfWorldElevationMeters = 0.0f;

	// Anisotropic quad footprint in engine units; derived in ctor as
	// mfWorldFootprint{X,Y}Meters * kfMetersToUnits.
	float mfQuadFootprintX = 0.0f;
	float mfQuadFootprintY = 0.0f;

	NavContour mNavContour;

	int64_t miTextureSlot = -1;

#if defined(BT_CLIENT)
	// Phase 5 LRU eviction state. mbGpuResident means "slot points at this template's real
	// Texture*s AND those Textures have live GPU resources". False while in slot-0 fallback
	// (the neutral placeholder textures) — covers both first-mint-pre-adopt and
	// post-eviction-pre-restore. miRefCount is recomputed from scratch each frame in
	// Islands::UpdateActiveIslands.
	int64_t miRefCount = 0;
	uint64_t muiLastUsedRenderFrame = 0;
	bool mbGpuResident = false;

	// Gaea Mesher-baked terrain mesh in island-local meters (XY centered, Z=0 at sea level).
	// CPU pointers slice into the kIsland chunk's payload after the heightmap floats (set by
	// WaitForElevationMaps). The GPU buffer combines indices and vertices: [uint32 indices,
	// float3 positions], uploaded once on first AcquireTextureSlot and kept resident for the
	// lifetime of the template (textures-only LRU eviction; mesh is small relative to texture VRAM).
	const float* mpfMeshPositions = nullptr;
	const uint32_t* mpuiMeshIndices = nullptr;
	int32_t miMeshVertexCount = 0;
	int32_t miMeshIndexCount = 0;
	Buffer mMeshBuffer;
#endif
};

class IslandTerrain
{
public:

	IslandTerrain();
	~IslandTerrain();

	void WaitForElevationMaps(float fNavThreshold);

	float XM_CALLCONV GlobalElevation(FXMVECTOR vecPosition) const;
	XMVECTOR XM_CALLCONV GlobalNormal(FXMVECTOR vecPosition) const;

#if defined(BT_CLIENT)
	// Client-only: assign or retrieve the bindless texture-array slot for an island template.
	// First call for a CRC binds its 4 textures into mRenderTargetTextures at the next free slot.
	// Newly-minted templates start in slot-0 fallback (slot points at the neutral placeholder
	// textures) until RestorationSweep detects per-texture adoption and patches the slot to real.
	int64_t AcquireTextureSlot(common::crc_t islandCrc);

	// Phase 5 LRU eviction sweeps. Both must run inside RenderGlobal post-fence-wait
	// (descriptor-patch safety window), bracketing TextureManager::ProcessPendingTextures.
	void EvictionSweep();
	void RestorationSweep();

	// Destroy per-template GPU buffers (mMeshBuffer) before Graphics tears down the VMA allocator.
	// IslandTerrain is game-frame-owned and outlives Graphics, but mMeshBuffer was allocated
	// through gpDeviceManager's allocator — must be released before mpDeviceManager.reset().
	// Called from Graphics::Destroy() at the kSurface tier.
	void ReleaseGpuResources();
#endif

	std::unordered_map<common::crc_t, IslandTemplate> mIslands;
	std::vector<common::crc_t> mIslandCrcsSorted;

	float mfSeaFloorElevation = 0.0f;

#if defined(BT_CLIENT)
	// Starts at 1: slot 0 is reserved as a permanent neutral placeholder anchor, never adopted
	// by any real island. See TextureManager::mIslandPlaceholder* members.
	int64_t miNextTextureSlot = 1;
#endif
};

inline IslandTerrain* gpIslandTerrain = nullptr;

} // namespace engine
