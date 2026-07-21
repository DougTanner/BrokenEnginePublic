#include "TweaksScreen.h"

#include "Ui/SmokeWrappers.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
// SmokeDeposits is the sole consumer of these labels, despite their gExplosion*/gBlasterPuff*/gPlayerImpactPuff*/gMissileTrail* globals living in SmokeWrappers.h. Owning them here keeps registration co-located with the WrapperSlider call sites below.
const engine::TweaksSliderMapRegistrar gSmokeDepositsRegistrar
{
	// Explosion Primary Puff
	{"Explosion Primary Puff Area One", &gExplosionPrimaryPuffAreaOne},
	{"Explosion Primary Puff Area Two", &gExplosionPrimaryPuffAreaTwo},
	{"Explosion Primary Puff Intensity One", &gExplosionPrimaryPuffIntensityOne},
	{"Explosion Primary Puff Intensity Two", &gExplosionPrimaryPuffIntensityTwo},
	// Explosion Secondary Puff
	{"Explosion Secondary Puff Area One", &gExplosionSecondaryPuffAreaOne},
	{"Explosion Secondary Puff Area Two", &gExplosionSecondaryPuffAreaTwo},
	{"Explosion Secondary Puff Intensity One", &gExplosionSecondaryPuffIntensityOne},
	{"Explosion Secondary Puff Intensity Two", &gExplosionSecondaryPuffIntensityTwo},
	// Explosion Primary Trail
	{"Explosion Primary Trail Intensity", &gExplosionPrimaryTrailIntensity},
	{"Explosion Primary Trail Length", &gExplosionPrimaryTrailLength},
	{"Explosion Primary Trail Duration", &gExplosionPrimaryTrailDuration},
	// Explosion Secondary Trail
	{"Explosion Secondary Trail Intensity", &gExplosionSecondaryTrailIntensity},
	{"Explosion Secondary Trail Length", &gExplosionSecondaryTrailLength},
	{"Explosion Secondary Trail Duration", &gExplosionSecondaryTrailDuration},
	// Blaster Puff
	{"Blaster Puff Area Start", &gBlasterPuffAreaStart},
	{"Blaster Puff Area End", &gBlasterPuffAreaEnd},
	{"Blaster Puff Intensity Start", &gBlasterPuffIntensityStart},
	{"Blaster Puff Intensity End", &gBlasterPuffIntensityEnd},
	// Player Impact Puff
	{"Player Impact Puff Area One", &gPlayerImpactPuffAreaOne},
	{"Player Impact Puff Area Two", &gPlayerImpactPuffAreaTwo},
	{"Player Impact Puff Intensity One", &gPlayerImpactPuffIntensityOne},
	{"Player Impact Puff Intensity Two", &gPlayerImpactPuffIntensityTwo},
	// Missile Trail
	{"Missile Trail Intensity", &gMissileTrailIntensity},
};
}

void TweaksScreen::RenderSmokeDepositsTab()
{
	const int64_t iSection = engine::giTweakSectionSmoke;

	if (ImGui::BeginTable("SmokeDepositsColumns", 3))
	{
		// Column 1: Explosions
		ImGui::TableNextColumn();

		WrapperSeparatorText("Explosions - Primary Puff");
		WrapperSlider("Area One", iSection, 1.0f, "Explosion Primary Puff Area One");
		WrapperSlider("Area Two", iSection, 1.0f, "Explosion Primary Puff Area Two");
		WrapperSlider("Intensity One", iSection, 1.0f, "Explosion Primary Puff Intensity One");
		WrapperSlider("Intensity Two", iSection, 1.0f, "Explosion Primary Puff Intensity Two");

		WrapperSeparatorText("Explosions - Secondary Puff");
		WrapperSlider("Area One", iSection, 1.0f, "Explosion Secondary Puff Area One");
		WrapperSlider("Area Two", iSection, 1.0f, "Explosion Secondary Puff Area Two");
		WrapperSlider("Intensity One", iSection, 1.0f, "Explosion Secondary Puff Intensity One");
		WrapperSlider("Intensity Two", iSection, 1.0f, "Explosion Secondary Puff Intensity Two");

		WrapperSeparatorText("Explosions - Primary Trail");
		WrapperSlider("Intensity", iSection, 1.0f, "Explosion Primary Trail Intensity");
		WrapperSlider("Length", iSection, 1.0f, "Explosion Primary Trail Length");
		WrapperSlider("Duration", iSection, 1.0f, "Explosion Primary Trail Duration");

		WrapperSeparatorText("Explosions - Secondary Trail");
		WrapperSlider("Intensity", iSection, 1.0f, "Explosion Secondary Trail Intensity");
		WrapperSlider("Length", iSection, 1.0f, "Explosion Secondary Trail Length");
		WrapperSlider("Duration", iSection, 1.0f, "Explosion Secondary Trail Duration");

		// Column 2: Blasters / Players / Missiles
		ImGui::TableNextColumn();

		WrapperSeparatorText("Blasters - Terrain Puff");
		WrapperSlider("Area Start", iSection, 1.0f, "Blaster Puff Area Start");
		WrapperSlider("Area End", iSection, 1.0f, "Blaster Puff Area End");
		WrapperSlider("Intensity Start", iSection, 1.0f, "Blaster Puff Intensity Start");
		WrapperSlider("Intensity End", iSection, 1.0f, "Blaster Puff Intensity End");

		WrapperSeparatorText("Players - Impact Puff");
		WrapperSlider("Area One", iSection, 1.0f, "Player Impact Puff Area One");
		WrapperSlider("Area Two", iSection, 1.0f, "Player Impact Puff Area Two");
		WrapperSlider("Intensity One", iSection, 1.0f, "Player Impact Puff Intensity One");
		WrapperSlider("Intensity Two", iSection, 1.0f, "Player Impact Puff Intensity Two");

		WrapperSeparatorText("Missiles - Trail");
		WrapperSlider("Intensity", iSection, 1.0f, "Missile Trail Intensity");

		// Column 3: Engine smoke trail rendering params
		ImGui::TableNextColumn();

		WrapperSeparatorText("Trails");
		WrapperSlider("Smoke Trails Quantity", iSection, 1.0f);
		WrapperSlider("Smoke Trails Width Current", iSection, 1.0f);
		WrapperSlider("Smoke Trails Width Previous", iSection, 1.0f);
		WrapperSlider("Smoke Trails Length", iSection, 1.0f);
		WrapperSlider("Smoke Trails Length Jitter", iSection, 1.0f);
		WrapperSlider("Smoke Trails Side Jitter", iSection, 1.0f);
		WrapperSlider("Smoke Intensity Falloff", iSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace game

#endif // BT_CLIENT
