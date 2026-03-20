#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderTestSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kTest);

	WrapperSlider("Test One", kiSection);
	WrapperSlider("Test Two", kiSection);
}

} // namespace engine
