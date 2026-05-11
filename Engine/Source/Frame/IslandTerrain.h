#pragma once

#include "Frame/NavBuild.h"

namespace engine
{

// Engine currently uses unit-less "units" rather than meters. Island assets store
// real-world meters; consumers multiply by this to translate. Future task will
// convert the engine to meters wholesale and remove this constant.
inline constexpr float kfMetersToUnits = 0.2f;

// One entry per kIsland chunk in the manifest. Heightmap pointer fills in
// WaitForElevationMaps once chunk data is resident; NavContour is only built
// for the canonical island (server-side).
struct IslandTemplate
{
	common::crc_t mIslandCrc = 0;

	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapWidth = 0;
	int32_t miHeightmapHeight = 0;

	float mfBeachElevation = 0.0f;
	float mfWorldWidthMeters = 0.0f;
	float mfWorldHeightMeters = 0.0f;

	// World-space quad footprint in engine units; derived in ctor from world meters (or a
	// legacy fallback when the asset predates world.json). Renderer + sim queries read these
	// instead of the global game::Frame::kfIslandWidth/kfIslandHeight constants.
	float mfQuadWidth = 0.0f;
	float mfQuadHeight = 0.0f;

	NavContour mNavContour;

	int64_t miTextureSlot = -1;
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
	int64_t AcquireTextureSlot(common::crc_t islandCrc);
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
