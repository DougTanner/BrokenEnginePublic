#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderMiscSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kMisc);

	WrapperSeparatorText("Moon");
	WrapperSlider("Moon Brightness", kiSection);
	WrapperSlider("Minimum Ambient", kiSection);

	WrapperSeparatorText("Misc");
	WrapperSlider("Misc Island Height", kiSection);
	WrapperSlider("Water Depth", kiSection);
	WrapperSlider("Water Terrain Height", kiSection);
	WrapperSlider("Water Terrain Fade", kiSection);
	WrapperSlider("Misc Depth Reflection Feather", kiSection);
	WrapperSlider("Misc0", kiSection);
}

} // namespace engine
