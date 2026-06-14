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
	{"Debug Texture Range", &gMiscDebugTextureLinearRange},
};
}

void TweaksScreenBase::RenderMiscSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kMisc);

	WrapperSeparatorText("Misc");
	WrapperSlider("Debug Texture Range", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
