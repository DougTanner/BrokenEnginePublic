#pragma once

#include "Graphics/Managers/BufferManager.h"

namespace engine
{

class Texture;

inline constexpr int64_t kiGlobalHeightmapSize = 1024;
inline constexpr float kfGlobalHeightmapSize = static_cast<float>(kiGlobalHeightmapSize);

DirectX::XMVECTOR XM_CALLCONV TerrainCollision(DirectX::FXMVECTOR vecStart, DirectX::FXMVECTOR vecEnd, float fStepInterval);

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
	void BuildGlobalHeightmap();
	void FillQuads();

	const shaders::AxisAlignedQuadLayout& XM_CALLCONV GetIsland(DirectX::FXMVECTOR vecPosition);
	float XM_CALLCONV GlobalElevation(DirectX::FXMVECTOR vecPosition);
	DirectX::XMVECTOR XM_CALLCONV GlobalNormal(DirectX::FXMVECTOR vecPosition);

	int64_t miCount = 0;
	float mfBeachElevation = 0.0f;
	float mfSeaFloorElevation = 0.0f;

	IslandsFlip meCurrentIslandsFlip = kFlipNone;
	bool mbFlipX = false;
	bool mbFlipY = false;
	bool mbBuildGlobalHeightmap = true;

	DirectX::XMFLOAT4 mf4GlobalArea {};
	float mppfElevations[kiGlobalHeightmapSize][kiGlobalHeightmapSize] {};

	std::vector<shaders::AxisAlignedQuadLayout> mQuads;
	Buffer mIslandsStorageBuffer;
};

inline Islands* gpIslands = nullptr;

} // namespace engine
