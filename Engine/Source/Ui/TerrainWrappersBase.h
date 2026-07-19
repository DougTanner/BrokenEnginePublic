#pragma once

#include "HeightLerpWrapperQuartet.h"
#include "WrapperBase.h"

namespace engine
{

// Ambient Occlusion
extern Wrapper gIslandAmbientOcclusion;

// Terrain Detail
extern HeightLerpWrapperQuartet gTerrainDetailNormalsMultiplier;

// Beach
extern Wrapper gTerrainSnowBlend;
extern Wrapper gTerrainSnowAmbientOcclusionExclusion;
extern Wrapper gTerrainBeachSandSize;
extern Wrapper gTerrainBeachSandBlend;
extern Wrapper gTerrainBeachNormalsSizeOne;
extern Wrapper gTerrainBeachNormalsSizeTwo;
extern Wrapper gTerrainBeachNormalsSizeThree;
extern Wrapper gTerrainBeachNormalsBlend;

// Rock
extern Wrapper gTerrainRockSize;
extern Wrapper gTerrainRockBlend;
extern Wrapper gTerrainRockNormalsSizeOne;
extern Wrapper gTerrainRockNormalsSizeTwo;
extern Wrapper gTerrainRockNormalsSizeThree;
extern Wrapper gTerrainRockNormalsBlend;

} // namespace engine
