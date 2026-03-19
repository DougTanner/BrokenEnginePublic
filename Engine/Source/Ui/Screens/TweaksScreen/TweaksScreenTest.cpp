#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderTestSection()
{
	WrapperSlider("Test One", static_cast<int>(TweakSection::kTest));
	WrapperSlider("Test Two", static_cast<int>(TweakSection::kTest));
}

} // namespace engine
