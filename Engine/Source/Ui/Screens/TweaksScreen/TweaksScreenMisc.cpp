#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/TerrainWrappersBase.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gMiscRegistrar
{
	// Test
	{"Test One", &gMiscTestOne},
	{"Test Two", &gMiscTestTwo},
	{"Debug Texture Range", &gMiscDebugTextureLinearRange},
	// Misc - cross-tab: gTerrainIslandHeight lives in TerrainWrappersBase.cpp
	{"Misc Island Height", &gTerrainIslandHeight},
};
}

void TweaksScreenBase::RenderMiscSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kMisc);

	WrapperSeparatorText("Test");
	WrapperSlider("Test One", kiSection);
	WrapperSlider("Test Two", kiSection);
	WrapperSlider("Debug Texture Range", kiSection);

	WrapperSeparatorText("Misc");
	WrapperSlider("Misc Island Height", kiSection);
	WrapperSlider("Misc0", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
