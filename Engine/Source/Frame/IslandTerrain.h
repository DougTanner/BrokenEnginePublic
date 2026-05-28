#pragma once

#include "Frame/NavBuild.h"
#if defined(BT_CLIENT)
#include "Graphics/Objects/Buffer.h"
#include "Graphics/Objects/Texture.h"
#endif

namespace engine
{

struct FrameStaticData;

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
	// below water, positive == above water. DataPacker reads the archetype Sea node's normalized
	// Level (fallback 0.1) and shifts Gaea's [0,1] normalized output by `Level × elevationMeters`
	// at bake time, so no runtime conversion is required (sea floor sits at the per-island depth
	// -(Level × elevationMeters)). Heightmap is anisotropic: DataPacker auto-crops each island
	// to its land bbox > 1 m, expanded to a multiple of 4 × kiElevationDivisor so BC encoding
	// and elevation downsample alignment hold on both axes.
	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapWidth = 0;
	int32_t miHeightmapHeight = 0;

	float mfWorldFootprintXMeters = 0.0f;
	float mfWorldFootprintYMeters = 0.0f;
	float mfWorldElevationMeters = 0.0f;

	// Actual peak of the shipped heightmap in engine-meters above beach (vs mfWorldElevationMeters,
	// the configured elevation range). Manifest metadata, populated in IslandTerrain ctor.
	float mfMaxHeightMeters = 0.0f;

	// Anisotropic quad footprint in engine units; derived in ctor as
	// mfWorldFootprint{X,Y}Meters * kfMetersToUnits.
	float mfQuadFootprintX = 0.0f;
	float mfQuadFootprintY = 0.0f;

	NavContour mNavContour;

	int64_t miTextureSlot = -1;

	// Fixed index into IslandTerrain::mIslandCrcsSorted, assigned in ctor right after the sort.
	// Drives per-template SSBO range and indirect-cmd slot in Islands; never changes after boot.
	// Decoupled from miTextureSlot (which mints lazily on first visit).
	int64_t miTemplateArrayIndex = -1;

	// Mesh vertex/index counts come from IslandHeader synchronously (manifest metadata is loaded
	// before chunk data). Populated in IslandTerrain ctor so Islands ctor can bake indexCount
	// into the per-template indirect commands at boot. CPU mesh data pointers (mpfMeshPositions /
	// mpuiMeshIndices) are filled later in WaitForElevationMaps once the kIsland chunk's payload
	// is resident.
	int32_t miMeshVertexCount = 0;
	int32_t miMeshIndexCount = 0;

	// Per-island valid-area convex hull (CCW) in island-local meters, centered. Slices the kIsland
	// chunk payload after the mesh indices (set by WaitForElevationMaps). Shared: the server packs
	// island placements against the rotated hull (IslandChainPlacement) and the client also debug-
	// renders it (MainUniforms DebugRenderIslandValidArea). A count < 3 (or null pointer) means no
	// usable polygon.
	const XMFLOAT2* mpf2ValidAreaVertices = nullptr;
	int32_t miValidAreaVertexCount = 0;

#if defined(BT_CLIENT)
	// Phase 5 LRU eviction state. mbGpuResident means "slot points at this template's real
	// Texture*s AND those Textures have live GPU resources". False while in slot-0 fallback
	// (the neutral placeholder textures) — covers both first-mint-pre-adopt and
	// post-eviction-pre-restore. miRefCount is recomputed from scratch each frame in
	// Islands::UpdateActiveIslands.
	int64_t miRefCount = 0;
	uint64_t muiLastUsedRenderFrame = 0;
	bool mbGpuResident = false;

	// Gaea Mesher-baked terrain mesh in island-local meters (XY centered).
	// CPU pointers slice into the kIsland chunk's payload after the heightmap floats (set by
	// WaitForElevationMaps). The GPU buffer combines indices and vertices: [uint32 indices,
	// float2 positions (XY pairs)], uploaded once by CreateClientMeshBuffers at boot and kept
	// resident for the lifetime of the template (textures-only LRU eviction; mesh is small
	// relative to texture VRAM). Z is not stored — Terrain.vert re-derives it from the elevation
	// sampler.
	const float* mpfMeshPositions = nullptr;   // interleaved XY pairs (2 floats per vertex)
	const uint32_t* mpuiMeshIndices = nullptr;
	Buffer mMeshBuffer;

	// Elevation R32_SFLOAT image uploaded at first-mint from mpfHeightmapData. Participates in LRU
	// eviction alongside color/normals/AO/masks (freed in EvictionSweep, re-Created on the next
	// AcquireTextureSlot first-mint). Lives on the template (not in TextureManager::mTextureMap)
	// because no standalone elevation chunk ships in the pack — DataPacker moved the data path to the
	// kIsland chunk's heightmap payload.
	Texture mElevationTexture;
#endif
};

class IslandTerrain
{
public:

	IslandTerrain();
	~IslandTerrain();

	void WaitForElevationMaps(float fNavThreshold);

	// Sim path (Frame-tick callers). Cell-local O(1) nearest-texel lookup into the cell's
	// precomputed FrameStaticData::elevationGrid. Out-of-cell positions return mfSeaFloorElevation.
	// Honors the Frame Purity Constraint: the caller hands its own static data in, so this never
	// touches gpGame->mCoordFrames and never reads a neighbor cell. Builds happen at the top of
	// RunFrameTick (see FrameTick.cpp), before any sim phase that would query.
	[[nodiscard]] float XM_CALLCONV FrameElevation(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const;
	[[nodiscard]] XMVECTOR XM_CALLCONV FrameNormal(const FrameStaticData& rStaticData, FXMVECTOR vecPosition) const;

	// Build the cell's elevation grid by splatting each placement's heightmap into the per-cell
	// float grid (max-blend across overlapping footprints, matching the per-point semantics of
	// GlobalElevation). Allocates rOutGrid.assign(kDim*kDim, mfSeaFloorElevation) and then
	// stamps each island. Called once per cell at the top of RunFrameTick when the grid is empty.
	void XM_CALLCONV BuildElevationGrid(GridCoord coord, const std::vector<IslandPlacement>& rPlacements, std::vector<float>& rOutGrid) const;

	// Render path (engine client — ProjectToBaseHeight). Position-based iteration over
	// mCoordFrames' immutable islands list. Never touches the per-cell grid, so it never races
	// the tick-time build. MUST NOT be called from Frame-tick code; use FrameElevation/FrameNormal
	// from a Frame-tick context.
	[[nodiscard]] float XM_CALLCONV GlobalElevation(FXMVECTOR vecPosition) const;
	[[nodiscard]] XMVECTOR XM_CALLCONV GlobalNormal(FXMVECTOR vecPosition) const;

#if defined(BT_CLIENT)
	// Create each template's GPU mesh buffer from the CPU pointers set by WaitForElevationMaps.
	// Called from Islands ctor (after VMA exists, before terrain CB record). Per the record-once
	// CB invariant, mesh buffers must exist at CB record time — they can't be created lazily on
	// first visit.
	void CreateClientMeshBuffers();

	// Client-only: assign or retrieve the bindless texture-array slot for an island template.
	// First call for a CRC binds its 4 textures into mRenderTargetTextures at the next free slot.
	// Newly-minted templates start in slot-0 fallback (slot points at the neutral placeholder
	// textures) until RestorationSweep detects per-texture adoption and patches the slot to real.
	int64_t AcquireTextureSlot(common::crc_t islandCrc);

	// Phase 5 LRU eviction sweeps. Both must run inside RenderGlobal post-fence-wait
	// (descriptor-patch safety window), bracketing TextureManager::ProcessPendingTextures.
	void EvictionSweep();
	void RestorationSweep();

	// Cheap pre-scans: true iff EvictionSweep / RestorationSweep would actually free or patch GPU
	// resources this frame. RenderGlobal uses these to drain all in-flight fences only on churn
	// frames (the guard conditions mirror the in-sweep skip logic exactly).
	bool AnyEvictionPending() const;
	bool AnyRestorationPending() const;

	// Destroy per-template GPU buffers (mMeshBuffer) before Graphics tears down the VMA allocator.
	// IslandTerrain is game-frame-owned and outlives Graphics, but mMeshBuffer was allocated
	// through gpDeviceManager's allocator — must be released before mpDeviceManager.reset().
	// Called from Graphics::Destroy() at the kSurface tier.
	void ReleaseGpuResources();

	// Reset per-template slot-assignment state so the next AcquireTextureSlot call runs the
	// first-mint path (re-pointing bindless array slots from the new TextureManager's placeholders
	// to real Textures, re-registering the elevation array on all three of its consumers:
	// kPipelineTerrainElevation, kPipelineShadowElevation, and kPipelineTerrain). Required after a
	// kSurface-tier Graphics teardown destroys TextureManager — the
	// stale miTextureSlot >= 0 would otherwise short-circuit AcquireTextureSlot's hot path and
	// strand every island on the new placeholder forever. Called from TextureManager ctor.
	void ResetTextureSlots();
#endif

	std::unordered_map<common::crc_t, IslandTemplate> mIslands;
	std::vector<common::crc_t> mIslandCrcsSorted;

	// Same CRCs as mIslandCrcsSorted, ordered by footprint area (mfWorldFootprintXMeters *
	// mfWorldFootprintYMeters) descending, CRC ascending as a stable tiebreak. Drives only the
	// debug main-menu island browser (Game::BuildMenuIslandPlacement) so it cycles largest-first.
	// Kept separate from mIslandCrcsSorted, whose CRC order is load-bearing (template slot
	// assignment + world-gen placement RNG) and must not change.
	std::vector<common::crc_t> mIslandCrcsByArea;

	// Templates bucketed into 4 size classes by footprint AREA (mfWorldFootprintX * mfWorldFootprintY
	// meters), classified in the ctor from manifest metadata. Area, not larger dimension: the multi-island
	// export tiles a master into 1x1 / 2x1 / 4x4 pieces, and a 2x1 strip shares its long edge with the 1x1
	// master, so only area separates them. Same CRCs as mIslandCrcsSorted, each bucket in sorted-CRC order
	// (deterministic, identical client + server). Drive IslandChainPlacement role selection; any bucket may
	// be empty for a small asset set (placement falls back through related buckets to mIslandCrcsSorted).
	std::vector<common::crc_t> mHugeCrcs;      // area >= kfHugeIslandAreaMeters   (1x1 full tiles, ~400x400)
	std::vector<common::crc_t> mLargeCrcs;     // area >= kfLargeIslandAreaMeters  (2x1 / 3x1 strips)
	std::vector<common::crc_t> mMediumCrcs;    // area >= kfMediumIslandAreaMeters (mid tiles)
	std::vector<common::crc_t> mSmallCrcs;     // smaller                          (4x4 tiles, ~100x100)

	float mfSeaFloorElevation = 0.0f;

#if defined(BT_CLIENT)
	// Starts at 1: slot 0 is reserved as a permanent neutral placeholder anchor, never adopted
	// by any real island. See TextureManager::mIslandPlaceholder* members.
	int64_t miNextTextureSlot = 1;

	// Slots reclaimed by EvictionSweep (full teardown sets the template's miTextureSlot = -1). Popped
	// first by AcquireTextureSlot before bumping miNextTextureSlot, so a long browse / churn session
	// reuses indices instead of marching toward the kiMaxIslands ceiling. Main-thread-only (RenderGlobal
	// eviction and UpdateActiveIslands mint run on the same thread). Cleared in ResetTextureSlots.
	std::vector<int64_t> mFreeTextureSlots;
#endif
};

inline IslandTerrain* gpIslandTerrain = nullptr;

} // namespace engine
