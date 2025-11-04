#pragma once

#include "Graphics/Managers/BufferManager.h"

namespace engine
{

class Texture;

XMVECTOR XM_CALLCONV TerrainCollision(FXMVECTOR vecStart, FXMVECTOR vecEnd, float fStepInterval);

enum IslandsFlip
{
	kFlipNone = 0,
	kFlipX = 1,
	kFlipY = 2,
	kFlipXY = 3,

	kFlipCount = 4,
};

class Islands
{
public:

	Islands();
	~Islands();

	void SetIslandsFlip(IslandsFlip eIslandsFlip);
	void FillQuads();
	void WaitForElevationMaps();

	const shaders::AxisAlignedQuadLayout& XM_CALLCONV GetIsland(FXMVECTOR vecPosition);
	float XM_CALLCONV GlobalElevation(FXMVECTOR vecPosition);
	XMVECTOR XM_CALLCONV GlobalNormal(FXMVECTOR vecPosition);

	struct Island
	{
		shaders::AxisAlignedQuadLayout quad;
		const float* pfHeightmapData = nullptr;
		int32_t iHeightmapWidth = 0;
		int32_t iHeightmapHeight = 0;
	};

	float mfBeachElevation = 0.0f;
	float mfSeaFloorElevation = 0.0f;

	IslandsFlip meCurrentIslandsFlip = kFlipNone;
	bool mbFlipX = false;
	bool mbFlipY = false;

	XMFLOAT4 mf4GlobalArea {};
	std::vector<Island> mIslands;
	Buffer mIslandsStorageBuffer;

	static inline std::vector<common::crc_t> smPriorityIslands;
};

inline Islands* gpIslands = nullptr;

} // namespace engine
