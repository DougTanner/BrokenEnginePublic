#pragma once

#include "Frame/NavBuild.h"

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
	// per-island by the archetype Sea node's `ShoreHeight × elevationMeters` at bake time, so
	// no runtime conversion is required.
	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapSize = 0;

	float mfWorldFootprintMeters = 0.0f;
	float mfWorldElevationMeters = 0.0f;

	// Isotropic square quad footprint in engine units; derived in ctor as
	// mfWorldFootprintMeters * kfMetersToUnits.
	float mfQuadFootprint = 0.0f;

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
