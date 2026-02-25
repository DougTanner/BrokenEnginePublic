#pragma once

#include "Graphics/IslandsFlip.h"
#ifdef BT_CLIENT
#include "Graphics/Managers/BufferManager.h"
#endif

namespace game
{

struct Frame;

} // namespace game

namespace engine
{

class Texture;
struct GridCoord;

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval);

inline constexpr int64_t kiDefaultIslandCapacity = 16;

class Islands
{
public:

	Islands();
	~Islands();

	void SetIslandsFlip(IslandsFlip eIslandsFlip);
	void SetIslandFlip(int64_t iIndex, IslandsFlip eIslandsFlip);
	void FillQuads();
	void WaitForElevationMaps();

	void UpdateActiveIslands(const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rFrames, const std::vector<GridCoord>& rActiveCoords);

	const shaders::AxisAlignedQuadLayout& XM_CALLCONV GetIsland(FXMVECTOR vecPosition);
	float XM_CALLCONV GlobalElevation(FXMVECTOR vecPosition);
	XMVECTOR XM_CALLCONV GlobalNormal(FXMVECTOR vecPosition);

	struct Island
	{
		shaders::AxisAlignedQuadLayout quad;
		const float* pfHeightmapData = nullptr;
		int32_t iHeightmapWidth = 0;
		int32_t iHeightmapHeight = 0;
		bool bFlipX = false;
		bool bFlipY = false;
	};

	float mfBeachElevation = 0.0f;
	float mfSeaFloorElevation = 0.0f;

	IslandsFlip meCurrentIslandsFlip = kFlipNone;
	bool mbFlipX = false;
	bool mbFlipY = false;

	XMFLOAT4 mf4GlobalArea {};
	std::vector<Island> mIslands;
#ifdef BT_CLIENT
	Buffer mIslandsStorageBuffer;
#endif

	static inline std::vector<common::crc_t> smPriorityIslands;
};

inline Islands* gpIslands = nullptr;

} // namespace engine
