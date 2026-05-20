#pragma once

#include "WrapperBase.h"

namespace engine
{

// Ambient Occlusion
extern Wrapper gIslandAmbientOcclusion;

// Beach
extern Wrapper gTerrainSnowBlend;
extern Wrapper gTerrainSnowAmbientOcclusionExclusion;
extern Wrapper gTerrainBeachHeight;
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
