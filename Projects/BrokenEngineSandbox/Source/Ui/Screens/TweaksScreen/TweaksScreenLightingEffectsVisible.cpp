#include "TweaksScreen.h"

#include "Ui/LightingWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gLightingEffectsVisibleRegistrar
{
	// Explosion Primary Visible
	{"Explosion Primary Visible Area One", &gExplosionPrimaryVisibleAreaOne},
	{"Explosion Primary Visible Area Two", &gExplosionPrimaryVisibleAreaTwo},
	{"Explosion Primary Visible Area Three", &gExplosionPrimaryVisibleAreaThree},
	{"Explosion Primary Visible Intensity One", &gExplosionPrimaryVisibleIntensityOne},
	{"Explosion Primary Visible Intensity Two", &gExplosionPrimaryVisibleIntensityTwo},
	{"Explosion Primary Visible Intensity Three", &gExplosionPrimaryVisibleIntensityThree},
	// Explosion Secondary Visible
	{"Explosion Secondary Visible Area One", &gExplosionSecondaryVisibleAreaOne},
	{"Explosion Secondary Visible Area Two", &gExplosionSecondaryVisibleAreaTwo},
	{"Explosion Secondary Visible Area Three", &gExplosionSecondaryVisibleAreaThree},
	{"Explosion Secondary Visible Intensity One", &gExplosionSecondaryVisibleIntensityOne},
	{"Explosion Secondary Visible Intensity Two", &gExplosionSecondaryVisibleIntensityTwo},
	{"Explosion Secondary Visible Intensity Three", &gExplosionSecondaryVisibleIntensityThree},
	// Crater Visible
	{"Crater Visible Area One", &gCraterVisibleAreaOne},
	{"Crater Visible Area Two", &gCraterVisibleAreaTwo},
	{"Crater Visible Area Three", &gCraterVisibleAreaThree},
	{"Crater Visible Area Four", &gCraterVisibleAreaFour},
	{"Crater Visible Intensity One", &gCraterVisibleIntensityOne},
	{"Crater Visible Intensity Two", &gCraterVisibleIntensityTwo},
	{"Crater Visible Intensity Three", &gCraterVisibleIntensityThree},
	{"Crater Visible Intensity Four", &gCraterVisibleIntensityFour},
	// Players
	{"Player Area Light Visible Intensity", &gPlayerAreaLightVisibleIntensity},
	{"Player Impact Visible Area One", &gPlayerImpactVisibleAreaOne},
	{"Player Impact Visible Area Two", &gPlayerImpactVisibleAreaTwo},
	{"Player Impact Visible Intensity One", &gPlayerImpactVisibleIntensityOne},
	{"Player Impact Visible Intensity Two", &gPlayerImpactVisibleIntensityTwo},
	// Hex Shield
	{"Hex Shield Intensity Decay", &gHexShieldIntensityDecay},
	// Missiles
	{"Missile Exhaust Visible Intensity", &gMissileExhaustVisibleIntensity},
	// Spaceships
	{"Enemy Blaster Visible Intensity", &gEnemyBlasterVisibleIntensity},
	{"Hit Flash Visible Area One", &gHitFlashVisibleAreaOne},
	{"Hit Flash Visible Area Two", &gHitFlashVisibleAreaTwo},
	{"Hit Flash Visible Intensity One", &gHitFlashVisibleIntensityOne},
	{"Hit Flash Visible Intensity Two", &gHitFlashVisibleIntensityTwo},
};
}

void TweaksScreen::RenderLightingEffectsVisibleTab()
{
	const int64_t iSection = engine::giTweakSectionLighting;

	if (ImGui::BeginTable("LightingEffectsVisibleColumns", 2))
	{
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary");
		WrapperSlider("Visible Area One", iSection, 1.0f, "Explosion Primary Visible Area One");
		WrapperSlider("Visible Area Two", iSection, 1.0f, "Explosion Primary Visible Area Two");
		WrapperSlider("Visible Area Three", iSection, 1.0f, "Explosion Primary Visible Area Three");
		WrapperSlider("Visible Int One", iSection, 1.0f, "Explosion Primary Visible Intensity One");
		WrapperSlider("Visible Int Two", iSection, 1.0f, "Explosion Primary Visible Intensity Two");
		WrapperSlider("Visible Int Three", iSection, 1.0f, "Explosion Primary Visible Intensity Three");

		WrapperSeparatorText("Explosions - Secondary");
		WrapperSlider("Visible Area One", iSection, 1.0f, "Explosion Secondary Visible Area One");
		WrapperSlider("Visible Area Two", iSection, 1.0f, "Explosion Secondary Visible Area Two");
		WrapperSlider("Visible Area Three", iSection, 1.0f, "Explosion Secondary Visible Area Three");
		WrapperSlider("Visible Int One", iSection, 1.0f, "Explosion Secondary Visible Intensity One");
		WrapperSlider("Visible Int Two", iSection, 1.0f, "Explosion Secondary Visible Intensity Two");
		WrapperSlider("Visible Int Three", iSection, 1.0f, "Explosion Secondary Visible Intensity Three");

		WrapperSeparatorText("Blasters - Terrain Crater");
		WrapperSlider("Visible Area One", iSection, 1.0f, "Crater Visible Area One");
		WrapperSlider("Visible Area Two", iSection, 1.0f, "Crater Visible Area Two");
		WrapperSlider("Visible Area Three", iSection, 1.0f, "Crater Visible Area Three");
		WrapperSlider("Visible Area Four", iSection, 1.0f, "Crater Visible Area Four");
		WrapperSlider("Visible Int One", iSection, 1.0f, "Crater Visible Intensity One");
		WrapperSlider("Visible Int Two", iSection, 1.0f, "Crater Visible Intensity Two");
		WrapperSlider("Visible Int Three", iSection, 1.0f, "Crater Visible Intensity Three");
		WrapperSlider("Visible Int Four", iSection, 1.0f, "Crater Visible Intensity Four");

		ImGui::TableNextColumn();

		WrapperSeparatorText("Players - Area Light");
		WrapperSlider("Visible Intensity", iSection, 1.0f, "Player Area Light Visible Intensity");

		WrapperSeparatorText("Players - Impact Light");
		WrapperSlider("Visible Area One", iSection, 1.0f, "Player Impact Visible Area One");
		WrapperSlider("Visible Area Two", iSection, 1.0f, "Player Impact Visible Area Two");
		WrapperSlider("Visible Int One", iSection, 1.0f, "Player Impact Visible Intensity One");
		WrapperSlider("Visible Int Two", iSection, 1.0f, "Player Impact Visible Intensity Two");

		WrapperSeparatorText("Players - Hex Shield");
		WrapperSlider("Intensity Decay", iSection, 1.0f, "Hex Shield Intensity Decay");

		WrapperSeparatorText("Missiles - Exhaust");
		WrapperSlider("Visible Intensity", iSection, 1.0f, "Missile Exhaust Visible Intensity");

		WrapperSeparatorText("Spaceships - Enemy Blaster");
		WrapperSlider("Visible Intensity", iSection, 1.0f, "Enemy Blaster Visible Intensity");

		WrapperSeparatorText("Spaceships - Hit Flash");
		WrapperSlider("Visible Area One", iSection, 1.0f, "Hit Flash Visible Area One");
		WrapperSlider("Visible Area Two", iSection, 1.0f, "Hit Flash Visible Area Two");
		WrapperSlider("Visible Int One", iSection, 1.0f, "Hit Flash Visible Intensity One");
		WrapperSlider("Visible Int Two", iSection, 1.0f, "Hit Flash Visible Intensity Two");

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
