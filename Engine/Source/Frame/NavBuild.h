#pragma once

namespace engine
{

struct NavData
{
	float fIslandWidth = 0.0f;
	float fIslandHeight = 0.0f;

	std::vector<XMFLOAT2> vertices;
	std::vector<int32_t> polygonOffsets;

	std::vector<int32_t> visEdgeA;
	std::vector<int32_t> visEdgeB;
};

void BuildNavData(NavData& rNavData, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fBeachElevation, float fIslandWidth, float fIslandHeight);

} // namespace engine
