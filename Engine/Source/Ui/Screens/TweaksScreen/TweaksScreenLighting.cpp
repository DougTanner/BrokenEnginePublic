#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderLightingSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kLighting);

	WrapperSeparatorText("Blur");
	WrapperSlider("Texture Multiplier", kiSection);
	WrapperSlider("Blur Distance", kiSection);
	WrapperSlider("Blur Directionality", kiSection);
	WrapperSlider("Blur Jitter", kiSection);
	WrapperSlider("Downscale", kiSection);

	WrapperSeparatorText("Combine");
	WrapperSlider("Combine Index", kiSection);
	WrapperSlider("Blur First Divisor", kiSection);
	WrapperSlider("Blur Divisor", kiSection);
	WrapperSlider("Combine Decay", kiSection);
	WrapperSlider("Combine Power", kiSection);

	WrapperSeparatorText("Directional");
	WrapperSlider("Directional", kiSection);
	WrapperSlider("Indirect", kiSection);
	WrapperSlider("Terrain", kiSection);
	WrapperSlider("Terrain Add", kiSection);
	WrapperSlider("Objects", kiSection);
	WrapperSlider("Objects Add", kiSection);
	WrapperSlider("Time of Day Multiplier", kiSection);
}

} // namespace engine
