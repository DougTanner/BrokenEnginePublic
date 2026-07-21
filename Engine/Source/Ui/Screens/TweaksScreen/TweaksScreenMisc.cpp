#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/MiscWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gMiscRegistrar
{
	{"Debug Texture Range", &gMiscDebugTextureLinearRange},
};
}

void TweaksScreenBase::RenderMiscSection()
{
	const int64_t iSection = giTweakSectionMisc;

	WrapperSeparatorText("Misc");
	WrapperSlider("Debug Texture Range", iSection);
}

} // namespace engine

#endif // BT_CLIENT
