#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderShadowSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kShadow);

	WrapperSeparatorText("Feather");
	WrapperSlider("Feather Noon", kiSection);
	WrapperSlider("Feather Noon Offset", kiSection);
	WrapperSlider("Feather Sunset", kiSection);
	WrapperSlider("Feather Sunset Offset", kiSection);
	WrapperSlider("Feather Power", kiSection);
	WrapperSlider("Distance Falloff", kiSection);
	WrapperSlider("Blur Sigma", kiSection);
	WrapperSlider("Affect Ambient", kiSection);
	WrapperSlider("Height Fade Top", kiSection);
	WrapperSlider("Height Fade Bottom", kiSection);

	WrapperSeparatorText("Object Shadows");
	WrapperSlider("Render Multiplier", kiSection);
	WrapperSlider("Blur Multiplier", kiSection);
	WrapperSlider("Shadow Noon", kiSection);
	WrapperSlider("Shadow Sunset", kiSection);
	WrapperSlider("Sunset Stretch", kiSection);
	WrapperSlider("Blur Distance Noon", kiSection);
	WrapperSlider("Blur Distance Sunset", kiSection);
	WrapperSlider("Object Blur Sigma", kiSection);
	WrapperSlider("Smoke Shadow Intensity", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
