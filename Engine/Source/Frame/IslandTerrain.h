#pragma once

#include "Frame/GridCoord.h"
#include "Frame/NavBuild.h"
#if defined(BT_CLIENT)
#include "Graphics/Objects/Buffer.h"
#include "Graphics/Objects/Texture.h"
#endif

namespace engine
{

struct FrameStaticData;

#if defined(BT_CLIENT)
// Phase 5 LRU grace: a template's GPU resources stay resident this many render frames after its
// last placement reference drops. ~5s @60Hz, ~2.5s @120Hz. Chosen to cover transient absences
// in moving-camera traversal without holding GPU memory indefinitely.
inline constexpr uint64_t kuiGraceRenderFrames = 300;

enum class IslandMeshResidency : uint8_t
{
	kNonresident,
	kAsyncPending,
	kCpuReady,
	kArenaBlocked,
	kFailed,
	kResident,
};
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
	// Packed IEEE half-float (R16); aliases the kIsland chunk payload at offset 0. DataPacker quantizes at
	// export (ExportIsland); dequantize via DirectX::PackedVector::XMConvertHalfToFloat at each read site.
	const uint16_t* mpHeightmapHalf = nullptr;
	int32_t miHeightmapWidth = 0;
	int32_t miHeightmapHeight = 0;

	float mfWorldFootprintXMeters = 0.0f;
	float mfWorldFootprintYMeters = 0.0f;
	float mfWorldElevationMeters = 0.0f;

	// Actual peak of the shipped heightmap in engine-meters above beach (vs mfWorldElevationMeters,
	// the configured elevation range). Manifest metadata, populated in IslandTerrain ctor.
	float mfMaxHeightMeters = 0.0f;

	// Anisotropic quad footprint in engine units (islands use 1 m = 1 engine unit), so a
	// direct copy of mfWorldFootprint{X,Y}Meters; set in ctor.
	float mfQuadFootprintX = 0.0f;
	float mfQuadFootprintY = 0.0f;

	NavContour mNavContour;

	int64_t miTextureSlot = -1;

	// Stable template-group index matching IslandTerrain::mIslandCrcsSorted, assigned in ctor right after
	// the sort. Identifies the template's grouping and indirect-command slot in Islands; never changes
	// after boot.
	// Decoupled from miTextureSlot (which mints lazily on first visit).
	int64_t miTemplateArrayIndex = -1;

	// Mesh vertex/index counts and CPU mesh data pointers (mpfMeshPositions / mpuiMeshIndices) are
	// populated by WaitForElevationMaps after it validates the resident kIsland chunk payload.
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

	// Gaea Mesher-baked terrain mesh in island-local meters (XY centered). CPU pointers slice into
	// the kIsland chunk payload after the heightmap halfs (set by WaitForElevationMaps). The
	// persistent arena is addressed through these virtual allocations; Z is not stored because
	// Terrain.vert re-derives it from the elevation sampler.
	const float* mpfMeshPositions = nullptr;   // interleaved XY pairs (2 floats per vertex)
	const uint32_t* mpuiMeshIndices = nullptr;
	VmaVirtualAllocation mMeshIndexAllocation = VK_NULL_HANDLE;
	VmaVirtualAllocation mMeshVertexAllocation = VK_NULL_HANDLE;
	VkDeviceSize mMeshIndexOffset = 0;
	VkDeviceSize mMeshVertexOffset = 0;
	IslandMeshResidency meMeshResidency = IslandMeshResidency::kNonresident;
	uint64_t muiMeshArenaBlockedGeneration = 0;
	// True once the [positions][indices] CPU slice has been decommitted from the lazy pool.
	bool mbMeshCpuDecommitted = false;

	// Elevation R16_SFLOAT image uploaded at first-mint from mpHeightmapHalf (raw byte-copy — the resident
	// heightmap is already R16). Participates in LRU
	// eviction alongside color/normals/AO/masks (freed in EvictionSweep, re-Created on the next
	// AcquireTextureSlot first-mint). Lives on the template (not in TextureManager::mTextureMap)
	// because no standalone elevation chunk ships in the pack — DataPacker moved the data path to the
	// kIsland chunk's heightmap payload.
	Texture mElevationTexture;
#endif
};

// Precomputed per-cell elevation sampler. Hoists FrameElevation's per-cell constants (grid pointer,
// cell origin, sea floor) so a batch of samples that all fall in one cell pays the origin compute and
// the empty check once, then samples by position. Sample() is bit-identical to FrameElevation per
// sample (same float ops in the same order) — this is the CRC/determinism sim path (/fp:strict), so a
// caller batching many samples (e.g. game::ComputeTerrainAvoidance) gets results identical to calling
// FrameElevation directly. FrameElevation is itself implemented on top of this, so the arithmetic has
// one home. pGrid is null when the cell has no elevation grid (Sample returns fSeaFloor).
struct FrameElevationSampler
{
	const std::vector<float>* pGrid = nullptr;
	float fCellOriginX = 0.0f;
	float fCellOriginY = 0.0f;
	float fSeaFloor = 0.0f;

	[[nodiscard]] float XM_CALLCONV Sample(FXMVECTOR vecPosition) const;
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

	// Build a FrameElevationSampler for one cell so a caller can batch many FrameElevation-equivalent
	// samples without redoing the per-cell origin compute / empty check each call. Same sim/CRC path as
	// FrameElevation (bit-identical per sample). See FrameElevationSampler above.
	[[nodiscard]] FrameElevationSampler XM_CALLCONV MakeFrameElevationSampler(const FrameStaticData& rStaticData) const;

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

	// Clear per-template GPU residency before Graphics tears down the VMA allocator. The arena
	// itself belongs to Islands, which is destroyed first.
	void ReleaseGpuResources();

	// Reset per-template slot-assignment state so the next AcquireTextureSlot call runs the
	// first-mint path (re-registering all five channel bindings; elevation remains at the new
	// TextureManager placeholder until the four chunk-backed channels are ready). Required after a
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
private:
	// First-mint half of AcquireTextureSlot (extracted for readability): pick or reuse a slot,
	// create the elevation texture from the in-memory heightmap, wire the 5 bindless array pointers,
	// and register each per-pipeline binding. Returns the assigned slot.
	int64_t FirstMintTextureSlot(common::crc_t islandCrc, IslandTemplate& rTemplate, const common::crc_t (&textureCrcs)[4], std::string_view name);

	enum class MeshEvictionReason : uint8_t
	{
		kGrace,
		kArenaExhaustion,
	};

	// Evict one template's complete texture+mesh residency. Arena exhaustion bypasses only the
	// grace period; it still requires a resident, unreferenced template.
	bool EvictTemplate(common::crc_t islandCrc, IslandTemplate& rTemplate, MeshEvictionReason eReason = MeshEvictionReason::kGrace);
	bool IsEvictionPending(const IslandTemplate& rTemplate) const;
	bool IsRestorationPending(common::crc_t islandCrc, const IslandTemplate& rTemplate) const;
	bool HasArenaEvictionCandidate(common::crc_t excludedCrc) const;

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
