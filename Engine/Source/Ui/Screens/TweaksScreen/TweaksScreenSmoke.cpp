#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/SmokeWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gSmokeRegistrar
{
	// Decay
	{"Smoke Max", &gSmokeMax},
	{"Smoke Power", &gSmokePower},
	{"Smoke Decay", &gSmokeDecay},
	{"Smoke Edge Decay Distance", &gSmokeEdgeDecayDistance},
	// Color
	{"Smoke Color Min", &gSmokeColorMin},
	{"Smoke Color Multiplier", &gSmokeColorMultiplier},
	{"Smoke Lighting Multiplier", &gSmokeLightingMultiplier},
	// Noise
	{"Smoke Noise Scale One", &gSmokeNoiseScaleOne},
	{"Smoke Noise Scale Two", &gSmokeNoiseScaleTwo},
	{"Smoke Wind Noise Scale", &gSmokeWindNoiseScale},
	{"Smoke Noise Quantity", &gSmokeNoiseQuantity},
	{"Smoke Wind Noise Quantity", &gSmokeWindNoiseQuantity},
	// Wind Displacement
	{"Wind To Smoke Strength", &gWindToSmokeStrength},
	{"Wind To Smoke Power", &gWindToSmokePower},
	{"Wind Displacement Noise Scale", &gWindDisplacementNoiseScale},
	{"Wind Smoke Retention", &gWindSmokeRetention},
	{"Wind Smoke Advection", &gWindSmokeAdvection},
	// Object
	{"Smoke Object Height", &gSmokeObjectHeight},
	// Trails (also rendered in game-side TweaksScreenSmokeDeposits column 3)
	{"Smoke Trails Quantity", &gSmokeTrailsQuantity},
	{"Smoke Trails Width Current", &gSmokeTrailsWidthCurrent},
	{"Smoke Trails Width Previous", &gSmokeTrailsWidthPrevious},
	{"Smoke Trails Length", &gSmokeTrailsLength},
	{"Smoke Trails Length Jitter", &gSmokeTrailsLengthJitter},
	{"Smoke Trails Side Jitter", &gSmokeTrailsSideJitter},
	{"Smoke Intensity Falloff", &gSmokeIntensityFalloff},
};
}

void TweaksScreenBase::RenderSmokeSection()
{
	const int64_t iSection = giTweakSectionSmoke;

	if (ImGui::BeginTabBar("SmokeTabs"))
	{
		if (BeginSubtab("Smoke", iSection, 0))
		{
			if (ImGui::BeginTable("SmokeColumns", 2))
			{
				// Left column
				ImGui::TableNextColumn();

				WrapperSeparatorText("Decay");
				WrapperSlider("Smoke Max", iSection, 1.0f);
				WrapperSlider("Smoke Power", iSection, 1.0f);
				WrapperSlider("Smoke Decay", iSection, 1.0f);
				WrapperSlider("Smoke Edge Decay Distance", iSection, 1.0f);

				WrapperSeparatorText("Color");
				WrapperSlider("Smoke Color Min", iSection, 1.0f);
				WrapperSlider("Smoke Color Multiplier", iSection, 1.0f);
				WrapperSlider("Smoke Lighting Multiplier", iSection, 1.0f);

				WrapperSeparatorText("Noise");
				WrapperSlider("Smoke Noise Scale One", iSection, 1.0f);
				WrapperSlider("Smoke Noise Scale Two", iSection, 1.0f);
				WrapperSlider("Smoke Wind Noise Scale", iSection, 1.0f);
				WrapperSlider("Smoke Noise Quantity", iSection, 1.0f);
				WrapperSlider("Smoke Wind Noise Quantity", iSection, 1.0f);

				// Right column
				ImGui::TableNextColumn();

				WrapperSeparatorText("Wind Displacement");
				WrapperSlider("Wind To Smoke Strength", iSection, 1.0f);
				WrapperSlider("Wind To Smoke Power", iSection, 1.0f);
				WrapperSlider("Wind Displacement Noise Scale", iSection, 1.0f);
				WrapperSlider("Wind Smoke Retention", iSection, 1.0f);
				WrapperSlider("Wind Smoke Advection", iSection, 1.0f);

				WrapperSeparatorText("Object");
				WrapperSlider("Smoke Object Height", iSection, 1.0f);

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Deposits", iSection, 1))
		{
			RenderSmokeDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
