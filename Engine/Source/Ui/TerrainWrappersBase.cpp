#include "TerrainWrappersBase.h"

namespace engine
{

// Beach
Wrapper gTerrainSnowMultiplier(2.4f, 1.0f, 5.0f);
Wrapper gTerrainBeachHeight(0.05f, 0.0f, 0.2f);
Wrapper gTerrainBeachSandSize(0.17f, 0.01f, 0.4f);
Wrapper gTerrainBeachSandBlend(0.6f, 0.0f, 1.0f);
Wrapper gTerrainBeachNormalsSizeOne(0.09f, 0.001f, 0.2f);
Wrapper gTerrainBeachNormalsSizeTwo(0.02f, 0.005f, 0.04f);
Wrapper gTerrainBeachNormalsSizeThree(0.05f, 0.01f, 0.2f);
Wrapper gTerrainBeachNormalsBlend(0.2f, 0.0f, 0.5f);

// Rock
Wrapper gTerrainIslandHeight(30.0f, 10.0f, 50.0f);
Wrapper gTerrainRockMultiplier(3.0f, 1.0f, 20.0f);
Wrapper gTerrainRockSize(0.15f, 0.01f, 0.4f);
Wrapper gTerrainRockBlend(0.6f, 0.0f, 1.0f);
Wrapper gTerrainRockNormalsSizeOne(0.04f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsSizeTwo(0.03f, 0.005f, 0.5f);
Wrapper gTerrainRockNormalsSizeThree(0.25f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsBlend(0.65f, 0.0f, 2.0f);

} // namespace engine
