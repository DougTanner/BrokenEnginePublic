#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderMiscSection()
{
	WrapperSlider("Misc Island Height", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Depth", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Terrain Height", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Water Terrain Fade", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Misc Depth Reflection Feather", static_cast<int>(TweakSection::kMisc));
	WrapperSlider("Misc0", static_cast<int>(TweakSection::kMisc));
}

} // namespace engine
