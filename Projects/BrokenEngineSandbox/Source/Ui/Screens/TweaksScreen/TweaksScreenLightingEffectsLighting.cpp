#include "TweaksScreen.h"

#include "Ui/LightingWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gLightingEffectsLightingRegistrar
{
	// Explosion Primary Lighting
	{"Explosion Primary Lighting Area One", &gExplosionPrimaryLightingAreaOne},
	{"Explosion Primary Lighting Area Two", &gExplosionPrimaryLightingAreaTwo},
	{"Explosion Primary Lighting Area Three", &gExplosionPrimaryLightingAreaThree},
	{"Explosion Primary Lighting Intensity One", &gExplosionPrimaryLightingIntensityOne},
	{"Explosion Primary Lighting Intensity Two", &gExplosionPrimaryLightingIntensityTwo},
	{"Explosion Primary Lighting Intensity Three", &gExplosionPrimaryLightingIntensityThree},
	// Explosion Secondary Lighting
	{"Explosion Secondary Lighting Area One", &gExplosionSecondaryLightingAreaOne},
	{"Explosion Secondary Lighting Area Two", &gExplosionSecondaryLightingAreaTwo},
	{"Explosion Secondary Lighting Area Three", &gExplosionSecondaryLightingAreaThree},
	{"Explosion Secondary Lighting Intensity One", &gExplosionSecondaryLightingIntensityOne},
	{"Explosion Secondary Lighting Intensity Two", &gExplosionSecondaryLightingIntensityTwo},
	{"Explosion Secondary Lighting Intensity Three", &gExplosionSecondaryLightingIntensityThree},
	// Crater Lighting
	{"Crater Lighting Area One", &gCraterLightingAreaOne},
	{"Crater Lighting Area Two", &gCraterLightingAreaTwo},
	{"Crater Lighting Area Three", &gCraterLightingAreaThree},
	{"Crater Lighting Area Four", &gCraterLightingAreaFour},
	{"Crater Lighting Intensity One", &gCraterLightingIntensityOne},
	{"Crater Lighting Intensity Two", &gCraterLightingIntensityTwo},
	{"Crater Lighting Intensity Three", &gCraterLightingIntensityThree},
	{"Crater Lighting Intensity Four", &gCraterLightingIntensityFour},
	// Players
	{"Player Area Light Lighting Size", &gPlayerBlasterLightingArea},
	{"Player Area Light Lighting Intensity", &gPlayerBlasterLightingIntensity},
	{"Player Impact Lighting Area One", &gPlayerImpactLightingAreaOne},
	{"Player Impact Lighting Area Two", &gPlayerImpactLightingAreaTwo},
	{"Player Impact Lighting Intensity One", &gPlayerImpactLightingIntensityOne},
	{"Player Impact Lighting Intensity Two", &gPlayerImpactLightingIntensityTwo},
	// Hex Shield
	{"Hex Shield Lighting Intensity", &gHexShieldLightingIntensity},
	// Missiles
	{"Missile Exhaust Lighting Area", &gMissileExhaustLightingArea},
	{"Missile Exhaust Lighting Intensity", &gMissileExhaustLightingIntensity},
	// Spaceships
	{"Enemy Blaster Lighting Area", &gEnemyBlasterLightingArea},
	{"Enemy Blaster Lighting Intensity", &gEnemyBlasterLightingIntensity},
	{"Hit Flash Lighting Area One", &gHitFlashLightingAreaOne},
	{"Hit Flash Lighting Area Two", &gHitFlashLightingAreaTwo},
	{"Hit Flash Lighting Intensity One", &gHitFlashLightingIntensityOne},
	{"Hit Flash Lighting Intensity Two", &gHitFlashLightingIntensityTwo},
};
}

void TweaksScreen::RenderLightingEffectsLightingTab()
{
	const int64_t iSection = engine::giTweakSectionLighting;

	if (ImGui::BeginTable("LightingEffectsLightingColumns", 2))
	{
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary");
		WrapperSlider("Lighting Area One", iSection, 1.0f, "Explosion Primary Lighting Area One");
		WrapperSlider("Lighting Area Two", iSection, 1.0f, "Explosion Primary Lighting Area Two");
		WrapperSlider("Lighting Area Three", iSection, 1.0f, "Explosion Primary Lighting Area Three");
		WrapperSlider("Lighting Int One", iSection, 1.0f, "Explosion Primary Lighting Intensity One");
		WrapperSlider("Lighting Int Two", iSection, 1.0f, "Explosion Primary Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", iSection, 1.0f, "Explosion Primary Lighting Intensity Three");

		WrapperSeparatorText("Explosions - Secondary");
		WrapperSlider("Lighting Area One", iSection, 1.0f, "Explosion Secondary Lighting Area One");
		WrapperSlider("Lighting Area Two", iSection, 1.0f, "Explosion Secondary Lighting Area Two");
		WrapperSlider("Lighting Area Three", iSection, 1.0f, "Explosion Secondary Lighting Area Three");
		WrapperSlider("Lighting Int One", iSection, 1.0f, "Explosion Secondary Lighting Intensity One");
		WrapperSlider("Lighting Int Two", iSection, 1.0f, "Explosion Secondary Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", iSection, 1.0f, "Explosion Secondary Lighting Intensity Three");

		WrapperSeparatorText("Blasters - Terrain Crater");
		WrapperSlider("Lighting Area One", iSection, 1.0f, "Crater Lighting Area One");
		WrapperSlider("Lighting Area Two", iSection, 1.0f, "Crater Lighting Area Two");
		WrapperSlider("Lighting Area Three", iSection, 1.0f, "Crater Lighting Area Three");
		WrapperSlider("Lighting Area Four", iSection, 1.0f, "Crater Lighting Area Four");
		WrapperSlider("Lighting Int One", iSection, 1.0f, "Crater Lighting Intensity One");
		WrapperSlider("Lighting Int Two", iSection, 1.0f, "Crater Lighting Intensity Two");
		WrapperSlider("Lighting Int Three", iSection, 1.0f, "Crater Lighting Intensity Three");
		WrapperSlider("Lighting Int Four", iSection, 1.0f, "Crater Lighting Intensity Four");

		ImGui::TableNextColumn();

		WrapperSeparatorText("Players - Area Light");
		WrapperSlider("Lighting Size", iSection, 1.0f, "Player Area Light Lighting Size");
		WrapperSlider("Lighting Intensity", iSection, 1.0f, "Player Area Light Lighting Intensity");

		WrapperSeparatorText("Players - Impact Light");
		WrapperSlider("Lighting Area One", iSection, 1.0f, "Player Impact Lighting Area One");
		WrapperSlider("Lighting Area Two", iSection, 1.0f, "Player Impact Lighting Area Two");
		WrapperSlider("Lighting Int One", iSection, 1.0f, "Player Impact Lighting Intensity One");
		WrapperSlider("Lighting Int Two", iSection, 1.0f, "Player Impact Lighting Intensity Two");

		WrapperSeparatorText("Players - Hex Shield");
		WrapperSlider("Lighting Intensity", iSection, 1.0f, "Hex Shield Lighting Intensity");

		WrapperSeparatorText("Missiles - Exhaust");
		WrapperSlider("Lighting Area", iSection, 1.0f, "Missile Exhaust Lighting Area");
		WrapperSlider("Lighting Intensity", iSection, 1.0f, "Missile Exhaust Lighting Intensity");

		WrapperSeparatorText("Spaceships - Enemy Blaster");
		WrapperSlider("Lighting Area", iSection, 1.0f, "Enemy Blaster Lighting Area");
		WrapperSlider("Lighting Intensity", iSection, 1.0f, "Enemy Blaster Lighting Intensity");

		WrapperSeparatorText("Spaceships - Hit Flash");
		WrapperSlider("Lighting Area One", iSection, 1.0f, "Hit Flash Lighting Area One");
		WrapperSlider("Lighting Area Two", iSection, 1.0f, "Hit Flash Lighting Area Two");
		WrapperSlider("Lighting Int One", iSection, 1.0f, "Hit Flash Lighting Intensity One");
		WrapperSlider("Lighting Int Two", iSection, 1.0f, "Hit Flash Lighting Intensity Two");

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
