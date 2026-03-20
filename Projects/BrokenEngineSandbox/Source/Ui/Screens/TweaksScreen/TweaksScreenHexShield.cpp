#include "TweaksScreen.h"

namespace game
{

void TweaksScreen::RenderHexShieldSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kHexShield);

	WrapperSeparatorText("Edge");
	WrapperSlider("Grow", kiSection);
	WrapperSlider("Edge Distance", kiSection);
	WrapperSlider("Edge Power", kiSection);
	WrapperSlider("Edge Multiplier", kiSection);

	WrapperSeparatorText("Wave");
	WrapperSlider("Wave Multiplier", kiSection);
	WrapperSlider("Wave Dot", kiSection);
	WrapperSlider("Wave Intensity", kiSection);
	WrapperSlider("Wave Intensity Power", kiSection);
	WrapperSlider("Wave Falloff Power", kiSection);

	WrapperSeparatorText("Direction");
	WrapperSlider("Direction Falloff Power", kiSection);
	WrapperSlider("Direction Multiplier", kiSection);
}

} // namespace game
