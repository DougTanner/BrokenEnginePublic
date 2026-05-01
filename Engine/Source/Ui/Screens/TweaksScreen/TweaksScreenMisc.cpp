#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderMiscSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kMisc);

	WrapperSeparatorText("Test");
	WrapperSlider("Test One", kiSection);
	WrapperSlider("Test Two", kiSection);
	WrapperSlider("Debug Texture Range", kiSection);

	WrapperSeparatorText("Moon");
	WrapperSlider("Moon Brightness", kiSection);
	WrapperSlider("Minimum Ambient", kiSection);

	WrapperSeparatorText("Misc");
	WrapperSlider("Misc Island Height", kiSection);
	WrapperSlider("Misc0", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
