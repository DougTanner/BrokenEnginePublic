#pragma once

#include "Frame/NavBuild.h"

namespace engine
{

// Engine currently uses unit-less "units" rather than meters. Island assets store
// real-world meters; consumers multiply by this to translate. Future task will
// convert the engine to meters wholesale and remove this constant.
inline constexpr float kfMetersToUnits = 0.2f;

#if defined(BT_CLIENT)
// Phase 5 LRU grace: a template's GPU resources stay resident this many render frames after its
// last placement reference drops. ~5s @60Hz, ~2.5s @120Hz. Chosen to cover transient absences
// in moving-camera traversal without holding GPU memory indefinitely.
inline constexpr uint64_t kuiGraceRenderFrames = 300;
#endif

// One entry per kIsland chunk in the manifest. Heightmap pointer fills in
// WaitForElevationMaps once chunk data is resident; NavContour is only built
// for the canonical island (server-side).
struct IslandTemplate
{
	common::crc_t mIslandCrc = 0;

	// Heightmap pixel values are engine-meters relative to beach: 0 == sea level, negative ==
	// below water, positive == above water. DataPacker offsets Gaea's [0,Terrain.Height] range
	// by common::kfOceanDepthMeters at bake time so no runtime conversion is required.
	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapSize = 0;

	float mfWorldFootprintMeters = 0.0f;
	float mfWorldElevationMeters = 0.0f;

	// Isotropic square quad footprint in engine units; derived in ctor from world meters (or a
	// legacy fallback when the asset predates per-island dimensions). Renderer + sim queries
	// read this instead of the global game::Frame::kfIslandWidth constant.
	float mfQuadFootprint = 0.0f;

	NavContour mNavContour;

	int64_t miTextureSlot = -1;

#if defined(BT_CLIENT)
	// Phase 5 LRU eviction state. mbGpuResident means "slot points at this template's real
	// Texture*s AND those Textures have live GPU resources". False while in canonical-slot
	// fallback (either first-mint-pre-adopt or post-eviction-pre-restore). miRefCount is
	// recomputed from scratch each frame in Islands::UpdateActiveIslands. mbPinned templates
	// (kIslands01Crc + kIslands02Crc) skip eviction entirely so menu<->game stays instant.
	int64_t miRefCount = 0;
	uint64_t muiLastUsedRenderFrame = 0;
	bool mbGpuResident = false;
	bool mbPinned = false;
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
	// Non-pinned templates start in canonical-slot fallback (slot points at kIslands01Crc's
	// textures) until RestorationSweep detects per-texture adoption and patches the slot to real.
	int64_t AcquireTextureSlot(common::crc_t islandCrc);

	// Phase 5 LRU eviction sweeps. Both must run inside RenderGlobal post-fence-wait
	// (descriptor-patch safety window), bracketing TextureManager::ProcessPendingTextures.
	void EvictionSweep();
	void RestorationSweep();
#endif

	std::unordered_map<common::crc_t, IslandTemplate> mIslands;
	std::vector<common::crc_t> mIslandCrcsSorted;

	// Cached pointer into mIslands for hot-path elevation/normal queries; resolved once in ctor.
	const IslandTemplate* mpCanonical = nullptr;

	float mfSeaFloorElevation = 0.0f;

#if defined(BT_CLIENT)
	int64_t miNextTextureSlot = 0;
#endif
};

inline IslandTerrain* gpIslandTerrain = nullptr;

} // namespace engine
