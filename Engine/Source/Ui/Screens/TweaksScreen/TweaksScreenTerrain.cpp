#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderTerrainSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kTerrain);

	WrapperSeparatorText("Beach");
	WrapperSlider("Snow Multiplier", kiSection);
	WrapperSlider("Beach Height", kiSection);
	WrapperSlider("Beach Sand Size", kiSection);
	WrapperSlider("Beach Sand Blend", kiSection);
	WrapperSlider("Beach Normals Size 1", kiSection);
	WrapperSlider("Beach Normals Size 2", kiSection);
	WrapperSlider("Beach Normals Size 3", kiSection);
	WrapperSlider("Beach Normals Blend", kiSection);

	WrapperSeparatorText("Rock");
	WrapperSlider("Island Height", kiSection);
	WrapperSlider("Rock Multiplier", kiSection);
	WrapperSlider("Rock Size", kiSection);
	WrapperSlider("Rock Blend", kiSection);
	WrapperSlider("Rock Normals Size 1", kiSection);
	WrapperSlider("Rock Normals Size 2", kiSection);
	WrapperSlider("Rock Normals Size 3", kiSection);
	WrapperSlider("Rock Normals Blend", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
