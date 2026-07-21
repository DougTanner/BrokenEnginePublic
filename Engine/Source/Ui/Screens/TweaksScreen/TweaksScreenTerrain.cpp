#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/TerrainWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gTerrainRegistrar
{
	// Ambient Occlusion
	{"Ambient Occlusion", &gIslandAmbientOcclusion},
	// Terrain Detail
	{"Detail Normals Multiplier Start Height", &gTerrainDetailNormalsMultiplier.StartHeight},
	{"Detail Normals Multiplier End Height", &gTerrainDetailNormalsMultiplier.EndHeight},
	{"Detail Normals Multiplier Low", &gTerrainDetailNormalsMultiplier.Low},
	{"Detail Normals Multiplier High", &gTerrainDetailNormalsMultiplier.High},
	// Beach
	{"Snow Blend", &gTerrainSnowBlend},
	{"Snow AO Exclusion", &gTerrainSnowAmbientOcclusionExclusion},
	{"Beach Sand Size", &gTerrainBeachSandSize},
	{"Beach Sand Blend", &gTerrainBeachSandBlend},
	{"Beach Normals Size 1", &gTerrainBeachNormalsSizeOne},
	{"Beach Normals Size 2", &gTerrainBeachNormalsSizeTwo},
	{"Beach Normals Size 3", &gTerrainBeachNormalsSizeThree},
	{"Beach Normals Blend", &gTerrainBeachNormalsBlend},
	// Rock
	{"Rock Size", &gTerrainRockSize},
	{"Rock Blend", &gTerrainRockBlend},
	{"Rock Normals Size 1", &gTerrainRockNormalsSizeOne},
	{"Rock Normals Size 2", &gTerrainRockNormalsSizeTwo},
	{"Rock Normals Size 3", &gTerrainRockNormalsSizeThree},
	{"Rock Normals Blend", &gTerrainRockNormalsBlend},
};
}

void TweaksScreenBase::RenderTerrainSection()
{
	const int64_t iSection = giTweakSectionTerrain;

	WrapperSeparatorText("Ambient Occlusion");
	WrapperSlider("Ambient Occlusion", iSection);

	WrapperSeparatorText("Terrain Detail");
	WrapperSlider("Detail Normals Multiplier Start Height", iSection);
	WrapperSlider("Detail Normals Multiplier End Height", iSection);
	WrapperSlider("Detail Normals Multiplier Low", iSection);
	WrapperSlider("Detail Normals Multiplier High", iSection);

	WrapperSeparatorText("Beach");
	WrapperSlider("Snow Blend", iSection);
	WrapperSlider("Snow AO Exclusion", iSection);
	WrapperSlider("Beach Sand Size", iSection);
	WrapperSlider("Beach Sand Blend", iSection);
	WrapperSlider("Beach Normals Size 1", iSection);
	WrapperSlider("Beach Normals Size 2", iSection);
	WrapperSlider("Beach Normals Size 3", iSection);
	WrapperSlider("Beach Normals Blend", iSection);

	WrapperSeparatorText("Rock");
	WrapperSlider("Rock Size", iSection);
	WrapperSlider("Rock Blend", iSection);
	WrapperSlider("Rock Normals Size 1", iSection);
	WrapperSlider("Rock Normals Size 2", iSection);
	WrapperSlider("Rock Normals Size 3", iSection);
	WrapperSlider("Rock Normals Blend", iSection);
}

} // namespace engine

#endif // BT_CLIENT
