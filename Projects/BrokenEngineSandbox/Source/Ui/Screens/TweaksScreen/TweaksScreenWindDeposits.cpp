#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderWindDepositsSection()
{
	static constexpr int kiSection = static_cast<int>(engine::TweakSection::kWindDeposits);

	WrapperSeparatorText("Player");
	WrapperSlider("Player Deposit Width", kiSection);
	WrapperSlider("Player Deposit Intensity", kiSection);
	WrapperSlider("Player Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Spaceships");
	WrapperSlider("Spaceships Deposit Width", kiSection);
	WrapperSlider("Spaceships Deposit Intensity", kiSection);
	WrapperSlider("Spaceships Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Player Blasters");
	WrapperSlider("Player Blasters Deposit Width", kiSection);
	WrapperSlider("Player Blasters Deposit Intensity", kiSection);
	WrapperSlider("Blasters Deposit Length Multiplier", kiSection);

	WrapperSeparatorText("Spaceships Blasters");
	WrapperSlider("Spaceships Blasters Deposit Width", kiSection);
	WrapperSlider("Spaceships Blasters Deposit Intensity", kiSection);

	WrapperSeparatorText("Explosions");
	WrapperSlider("Explosions Deposit Width", kiSection);
	WrapperSlider("Explosions Deposit Intensity", kiSection);
}

} // namespace game

#endif // BT_CLIENT
