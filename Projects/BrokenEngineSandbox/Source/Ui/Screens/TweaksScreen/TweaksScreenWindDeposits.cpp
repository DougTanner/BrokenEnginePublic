#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gWindDepositsRegistrar
{
	// Player
	{"Player Deposit Width", &gWindDepositPlayerWidth},
	{"Player Deposit Intensity", &gWindDepositPlayerIntensity},
	{"Player Deposit Length Multiplier", &gWindDepositPlayerLengthMultiplier},
	// Spaceships
	{"Spaceships Deposit Width", &gWindDepositSpaceshipsWidth},
	{"Spaceships Deposit Intensity", &gWindDepositSpaceshipsIntensity},
	{"Spaceships Deposit Length Multiplier", &gWindDepositSpaceshipsLengthMultiplier},
	// Player Blasters
	{"Player Blasters Deposit Width", &gWindDepositPlayerBlastersWidth},
	{"Player Blasters Deposit Intensity", &gWindDepositPlayerBlastersIntensity},
	{"Blasters Deposit Length Multiplier", &gWindDepositPlayerBlastersLengthMultiplier},
	// Spaceships Blasters
	{"Spaceships Blasters Deposit Width", &gWindDepositSpaceshipsBlastersWidth},
	{"Spaceships Blasters Deposit Intensity", &gWindDepositSpaceshipsBlastersIntensity},
	// Explosions
	{"Explosions Deposit Width", &gWindDepositExplosionsWidth},
	{"Explosions Deposit Intensity", &gWindDepositExplosionsIntensity},
};
}

void TweaksScreen::RenderWindDepositsTab()
{
	const int64_t iSection = engine::giTweakSectionWind;

	WrapperSeparatorText("Player");
	WrapperSlider("Player Deposit Width", iSection);
	WrapperSlider("Player Deposit Intensity", iSection);
	WrapperSlider("Player Deposit Length Multiplier", iSection);

	WrapperSeparatorText("Spaceships");
	WrapperSlider("Spaceships Deposit Width", iSection);
	WrapperSlider("Spaceships Deposit Intensity", iSection);
	WrapperSlider("Spaceships Deposit Length Multiplier", iSection);

	WrapperSeparatorText("Player Blasters");
	WrapperSlider("Player Blasters Deposit Width", iSection);
	WrapperSlider("Player Blasters Deposit Intensity", iSection);
	WrapperSlider("Blasters Deposit Length Multiplier", iSection);

	WrapperSeparatorText("Spaceships Blasters");
	WrapperSlider("Spaceships Blasters Deposit Width", iSection);
	WrapperSlider("Spaceships Blasters Deposit Intensity", iSection);

	WrapperSeparatorText("Explosions");
	WrapperSlider("Explosions Deposit Width", iSection);
	WrapperSlider("Explosions Deposit Intensity", iSection);
}

} // namespace game

#endif // BT_CLIENT
