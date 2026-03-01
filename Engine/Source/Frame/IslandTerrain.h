#pragma once

namespace engine
{

class IslandTerrain
{
public:

	IslandTerrain();
	~IslandTerrain();

	void WaitForElevationMaps();

	float XM_CALLCONV GlobalElevation(FXMVECTOR vecPosition) const;
	XMVECTOR XM_CALLCONV GlobalNormal(FXMVECTOR vecPosition) const;

	// Shared heightmap (all islands use the same heightmap, just flipped by parity)
	const float* mpfHeightmapData = nullptr;
	int32_t miHeightmapWidth = 0;
	int32_t miHeightmapHeight = 0;

	// Constants loaded from pack files
	float mfBeachElevation = 0.0f;
	float mfSeaFloorElevation = 0.0f;

	static inline std::vector<common::crc_t> smPriorityIslands;
};

inline IslandTerrain* gpIslandTerrain = nullptr;

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval);

} // namespace engine
