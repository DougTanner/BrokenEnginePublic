#include "TerrainWrappersBase.h"

namespace engine
{

// Ambient Occlusion
Wrapper gIslandAmbientOcclusion(0.65f, 0.0f, 1.0f);

// Terrain Detail
HeightLerpWrapperQuartet gTerrainDetailNormalsMultiplier
{
	.StartHeight = Wrapper(300.0f, 150.0f, 2000.0f),
	.EndHeight = Wrapper(600.0f, 150.0f, 2000.0f),
	.Low = Wrapper(1.0f, 0.0f, 1.0f),
	.High = Wrapper(0.0f, 0.0f, 1.0f),
};

// Beach
Wrapper gTerrainSnowBlend(2.0f, 0.0f, 2.0f);
Wrapper gTerrainSnowAmbientOcclusionExclusion(1.0f, 0.0f, 1.0f);
Wrapper gTerrainBeachSandSize(0.1f, 0.01f, 0.4f);
Wrapper gTerrainBeachSandBlend(0.8f, 0.0f, 1.0f);
Wrapper gTerrainBeachNormalsSizeOne(0.06f, 0.001f, 0.2f);
Wrapper gTerrainBeachNormalsSizeTwo(0.006f, 0.005f, 0.04f);
Wrapper gTerrainBeachNormalsSizeThree(0.06f, 0.01f, 0.2f);
Wrapper gTerrainBeachNormalsBlend(0.25f, 0.0f, 1.0f);

// Rock
Wrapper gTerrainRockSize(0.4f, 0.01f, 0.4f);
Wrapper gTerrainRockBlend(0.15f, 0.0f, 1.0f);
Wrapper gTerrainRockNormalsSizeOne(0.04f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsSizeTwo(0.03f, 0.005f, 0.5f);
Wrapper gTerrainRockNormalsSizeThree(0.25f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsBlend(0.7f, 0.0f, 2.0f);

} // namespace engine
