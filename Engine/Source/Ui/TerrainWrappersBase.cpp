#include "TerrainWrappersBase.h"

namespace engine
{

// Ambient Occlusion
Wrapper gIslandAmbientOcclusion(0.7f, 0.0f, 1.0f);

// Beach
Wrapper gTerrainSnowBlend(1.0f, 0.0f, 2.0f);
Wrapper gTerrainSnowAmbientOcclusionExclusion(1.0f, 0.0f, 1.0f);
Wrapper gTerrainBeachHeight(0.05f, 0.0f, 0.2f);
Wrapper gTerrainBeachSandSize(0.15f, 0.01f, 0.4f);
Wrapper gTerrainBeachSandBlend(1.0f, 0.0f, 1.0f);
Wrapper gTerrainBeachNormalsSizeOne(0.09f, 0.001f, 0.2f);
Wrapper gTerrainBeachNormalsSizeTwo(0.015f, 0.005f, 0.04f);
Wrapper gTerrainBeachNormalsSizeThree(0.1f, 0.01f, 0.2f);
Wrapper gTerrainBeachNormalsBlend(0.5f, 0.0f, 1.0f);

// Rock
Wrapper gTerrainRockSize(0.1f, 0.01f, 0.4f);
Wrapper gTerrainRockBlend(0.15f, 0.0f, 1.0f);
Wrapper gTerrainRockNormalsSizeOne(0.04f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsSizeTwo(0.03f, 0.005f, 0.5f);
Wrapper gTerrainRockNormalsSizeThree(0.25f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsBlend(0.5f, 0.0f, 2.0f);

} // namespace engine
