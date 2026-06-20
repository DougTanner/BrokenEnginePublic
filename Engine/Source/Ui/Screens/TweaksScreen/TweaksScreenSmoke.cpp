#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/SmokeWrappersBase.h"

#if defined(BT_CLIENT)

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
	{"Smoke Trails Follow", &gSmokeTrailsFollow},
};
}

void TweaksScreenBase::RenderSmokeSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSmoke);

	if (ImGui::BeginTabBar("SmokeTabs"))
	{
		if (ImGui::BeginTabItem("Smoke", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 0;
			}

			if (ImGui::BeginTable("SmokeColumns", 2))
			{
				// Left column
				ImGui::TableNextColumn();

				WrapperSeparatorText("Decay");
				WrapperSlider("Smoke Max", kiSection, 1.0f);
				WrapperSlider("Smoke Power", kiSection, 1.0f);
				WrapperSlider("Smoke Decay", kiSection, 1.0f);
				WrapperSlider("Smoke Edge Decay Distance", kiSection, 1.0f);

				WrapperSeparatorText("Color");
				WrapperSlider("Smoke Color Min", kiSection, 1.0f);
				WrapperSlider("Smoke Color Multiplier", kiSection, 1.0f);
				WrapperSlider("Smoke Lighting Multiplier", kiSection, 1.0f);

				WrapperSeparatorText("Noise");
				WrapperSlider("Smoke Noise Scale One", kiSection, 1.0f);
				WrapperSlider("Smoke Noise Scale Two", kiSection, 1.0f);
				WrapperSlider("Smoke Wind Noise Scale", kiSection, 1.0f);
				WrapperSlider("Smoke Noise Quantity", kiSection, 1.0f);
				WrapperSlider("Smoke Wind Noise Quantity", kiSection, 1.0f);

				// Right column
				ImGui::TableNextColumn();

				WrapperSeparatorText("Wind Displacement");
				WrapperSlider("Wind To Smoke Strength", kiSection, 1.0f);
				WrapperSlider("Wind To Smoke Power", kiSection, 1.0f);
				WrapperSlider("Wind Displacement Noise Scale", kiSection, 1.0f);
				WrapperSlider("Wind Smoke Retention", kiSection, 1.0f);
				WrapperSlider("Wind Smoke Advection", kiSection, 1.0f);

				WrapperSeparatorText("Object");
				WrapperSlider("Smoke Object Height", kiSection, 1.0f);

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Deposits", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 1;
			}

			RenderSmokeDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
