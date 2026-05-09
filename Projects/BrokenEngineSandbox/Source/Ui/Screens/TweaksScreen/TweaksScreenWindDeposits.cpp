#include "TweaksScreen.h"

#include "Ui/WindDepositsWrappers.h"

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
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kWind);

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
