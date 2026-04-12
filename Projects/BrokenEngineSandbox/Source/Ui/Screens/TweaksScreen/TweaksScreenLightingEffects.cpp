#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

void TweaksScreen::RenderLightingEffectsTab()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(engine::TweakSection::kLighting);

	WrapperSeparatorText("Explosions - Primary");
	WrapperSlider("Visible Area", kiSection, 1.0f, "Exp Primary Visible Area");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Exp Primary Visible Intensity");
	WrapperSlider("Lighting Area", kiSection, 1.0f, "Exp Primary Lighting Area");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Exp Primary Lighting Intensity");

	WrapperSeparatorText("Explosions - Secondary");
	WrapperSlider("Visible Area", kiSection, 1.0f, "Exp Secondary Visible Area");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Exp Secondary Visible Intensity");
	WrapperSlider("Lighting Area", kiSection, 1.0f, "Exp Secondary Lighting Area");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Exp Secondary Lighting Intensity");

	WrapperSeparatorText("Explosions - Primary Puff");
	WrapperSlider("Area Start", kiSection, 1.0f, "Exp Primary Puff Area Start");
	WrapperSlider("Area End", kiSection, 1.0f, "Exp Primary Puff Area End");
	WrapperSlider("Intensity", kiSection, 1.0f, "Exp Primary Puff Intensity");

	WrapperSeparatorText("Explosions - Secondary Puff");
	WrapperSlider("Area Start", kiSection, 1.0f, "Exp Secondary Puff Area Start");
	WrapperSlider("Area End", kiSection, 1.0f, "Exp Secondary Puff Area End");
	WrapperSlider("Intensity", kiSection, 1.0f, "Exp Secondary Puff Intensity");

	WrapperSeparatorText("Blasters - Terrain Crater");
	WrapperSlider("Visible Area Start", kiSection, 1.0f, "Crater Visible Area Start");
	WrapperSlider("Visible Area End", kiSection, 1.0f, "Crater Visible Area End");
	WrapperSlider("Visible Int Start", kiSection, 1.0f, "Crater Visible Intensity Start");
	WrapperSlider("Visible Int End", kiSection, 1.0f, "Crater Visible Intensity End");
	WrapperSlider("Lighting Area", kiSection, 1.0f, "Crater Lighting Area");
	WrapperSlider("Lighting Int Start", kiSection, 1.0f, "Crater Lighting Intensity Start");
	WrapperSlider("Lighting Int Mid", kiSection, 1.0f, "Crater Lighting Intensity Mid");
	WrapperSlider("Lighting Int End", kiSection, 1.0f, "Crater Lighting Intensity End");

	WrapperSeparatorText("Blasters - Terrain Puff");
	WrapperSlider("Area Start", kiSection, 1.0f, "Blaster Puff Area Start");
	WrapperSlider("Area End", kiSection, 1.0f, "Blaster Puff Area End");
	WrapperSlider("Intensity Start", kiSection, 1.0f, "Blaster Puff Intensity Start");
	WrapperSlider("Intensity End", kiSection, 1.0f, "Blaster Puff Intensity End");

	WrapperSeparatorText("Players - Area Light");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Player Area Light Visible Intensity");
	WrapperSlider("Lighting Size", kiSection, 1.0f, "Player Area Light Lighting Size");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Player Area Light Lighting Intensity");

	WrapperSeparatorText("Players - Impact Light");
	WrapperSlider("Start Visible Area", kiSection, 1.0f, "Player Impact Start Visible Area");
	WrapperSlider("Start Visible Int", kiSection, 1.0f, "Player Impact Start Visible Intensity");
	WrapperSlider("Start Lighting Area", kiSection, 1.0f, "Player Impact Start Lighting Area");
	WrapperSlider("Start Lighting Int", kiSection, 1.0f, "Player Impact Start Lighting Intensity");
	WrapperSlider("End Visible Int", kiSection, 1.0f, "Player Impact End Visible Intensity");
	WrapperSlider("End Lighting Int", kiSection, 1.0f, "Player Impact End Lighting Intensity");

	WrapperSeparatorText("Players - Impact Puff");
	WrapperSlider("Area Start", kiSection, 1.0f, "Player Impact Puff Area Start");
	WrapperSlider("Area End", kiSection, 1.0f, "Player Impact Puff Area End");
	WrapperSlider("Intensity Start", kiSection, 1.0f, "Player Impact Puff Intensity Start");

	WrapperSeparatorText("Players - Hex Shield");
	WrapperSlider("Intensity Decay", kiSection, 1.0f, "Hex Shield Intensity Decay");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Hex Shield Lighting Intensity");

	WrapperSeparatorText("Missiles - Exhaust");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Missile Exhaust Visible Intensity");
	WrapperSlider("Lighting Area", kiSection, 1.0f, "Missile Exhaust Lighting Area");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Missile Exhaust Lighting Intensity");

	WrapperSeparatorText("Missiles - Trail");
	WrapperSlider("Intensity", kiSection, 1.0f, "Missile Trail Intensity");

	WrapperSeparatorText("Spaceships - Explosion");
	WrapperSlider("Intensity", kiSection, 1.0f, "Spaceship Explosion Intensity");
	WrapperSlider("Particle Lighting Size", kiSection, 1.0f, "Spaceship Explosion Particle Lighting Size");
	WrapperSlider("Particle Lighting Int", kiSection, 1.0f, "Spaceship Explosion Particle Lighting Intensity");

	WrapperSeparatorText("Spaceships - Enemy Blaster");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Enemy Blaster Visible Intensity");
	WrapperSlider("Lighting Size", kiSection, 1.0f, "Enemy Blaster Lighting Size");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Enemy Blaster Lighting Intensity");

	WrapperSeparatorText("Spaceships - Hit Flash");
	WrapperSlider("Visible Area", kiSection, 1.0f, "Hit Flash Visible Area");
	WrapperSlider("Visible Intensity", kiSection, 1.0f, "Hit Flash Visible Intensity");
	WrapperSlider("Lighting Area", kiSection, 1.0f, "Hit Flash Lighting Area");
	WrapperSlider("Lighting Intensity", kiSection, 1.0f, "Hit Flash Lighting Intensity");
}

} // namespace game

#endif // BT_CLIENT
