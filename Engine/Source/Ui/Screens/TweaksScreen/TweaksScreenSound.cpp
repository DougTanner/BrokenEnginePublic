#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/SoundSettingsWrappersBase.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gSoundRegistrar
{
	{"Master Volume", &gMasterVolume},
	{"Music Volume", &gMusicVolume},
	{"Sound Volume", &gSoundVolume},
};
}

void TweaksScreenBase::RenderSoundSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSound);

	// Engine settings rendered inline (no outer table) so the game-side hook can own its own 2-column table at full window width.
	WrapperSeparatorText("Settings");
	WrapperSlider("Master", kiSection, 1.0f, "Master Volume");
	WrapperSlider("Music", kiSection, 1.0f, "Music Volume");
	WrapperSlider("Sound", kiSection, 1.0f, "Sound Volume");

	RenderSoundEffects();
}

} // namespace engine

#endif // BT_CLIENT
