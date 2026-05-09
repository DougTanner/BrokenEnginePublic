#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/TerrainWrappersBase.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gTerrainRegistrar
{
	// Beach
	{"Snow Multiplier", &gTerrainSnowMultiplier},
	{"Beach Height", &gTerrainBeachHeight},
	{"Beach Sand Size", &gTerrainBeachSandSize},
	{"Beach Sand Blend", &gTerrainBeachSandBlend},
	{"Beach Normals Size 1", &gTerrainBeachNormalsSizeOne},
	{"Beach Normals Size 2", &gTerrainBeachNormalsSizeTwo},
	{"Beach Normals Size 3", &gTerrainBeachNormalsSizeThree},
	{"Beach Normals Blend", &gTerrainBeachNormalsBlend},
	// Rock
	{"Island Height", &gTerrainIslandHeight},
	{"Rock Multiplier", &gTerrainRockMultiplier},
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
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kTerrain);

	WrapperSeparatorText("Beach");
	WrapperSlider("Snow Multiplier", kiSection);
	WrapperSlider("Beach Height", kiSection);
	WrapperSlider("Beach Sand Size", kiSection);
	WrapperSlider("Beach Sand Blend", kiSection);
	WrapperSlider("Beach Normals Size 1", kiSection);
	WrapperSlider("Beach Normals Size 2", kiSection);
	WrapperSlider("Beach Normals Size 3", kiSection);
	WrapperSlider("Beach Normals Blend", kiSection);

	WrapperSeparatorText("Rock");
	WrapperSlider("Island Height", kiSection);
	WrapperSlider("Rock Multiplier", kiSection);
	WrapperSlider("Rock Size", kiSection);
	WrapperSlider("Rock Blend", kiSection);
	WrapperSlider("Rock Normals Size 1", kiSection);
	WrapperSlider("Rock Normals Size 2", kiSection);
	WrapperSlider("Rock Normals Size 3", kiSection);
	WrapperSlider("Rock Normals Blend", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
