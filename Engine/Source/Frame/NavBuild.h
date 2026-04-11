#pragma once

#include "Graphics/IslandsFlip.h"

namespace engine
{

// Canonical island contour in UV space [0,1]x[0,1], built once from heightmap
struct NavContour
{
	std::vector<XMFLOAT2> vertices;
	std::vector<int32_t> polygonOffsets;
	std::vector<int32_t> visEdgeA;
	std::vector<int32_t> visEdgeB;
};

inline constexpr int64_t kiNavDataVersion = 5;

// Per-cell navigation data in world space, stored in FrameStaticData
struct NavData
{
	std::vector<XMFLOAT2> vertices;
	std::vector<int32_t> polygonOffsets;
	std::vector<int32_t> visEdgeA;
	std::vector<int32_t> visEdgeB;

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

void BuildNavContour(NavContour& rContour, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fBeachElevation, float fWorldThreshold);
void XM_CALLCONV BuildCellNavData(NavData& rNavData, const NavContour& rContour, FXMVECTOR vecArea, IslandsFlip eFlip, XMFLOAT2 f2IslandOffset, float fIslandWidth, float fIslandHeight);

} // namespace engine
