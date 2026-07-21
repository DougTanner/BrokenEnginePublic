#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/WindWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gWindRegistrar
{
	// Time & Global
	{"Wind Time Scale", &gWindTimeScale},
	{"Wind Threshold Low", &gWindThresholdLow},
	{"Wind Threshold High", &gWindThresholdHigh},
	// Propagation
	{"Wind Advection Scale High", &gWindAdvectionScaleHigh},
	{"Wind Advection Scale Low", &gWindAdvectionScaleLow},
	{"Wind Swirl Scale High", &gWindSwirlScaleHigh},
	{"Wind Swirl Scale Low", &gWindSwirlScaleLow},
	{"Wind Swirl Amount High", &gWindSwirlAmountHigh},
	{"Wind Swirl Amount Low", &gWindSwirlAmountLow},
	{"Wind Swirl Speed High", &gWindSwirlSpeedHigh},
	{"Wind Swirl Speed Low", &gWindSwirlSpeedLow},
	{"Wind Vorticity Confinement High", &gWindVorticityConfinementHigh},
	{"Wind Vorticity Confinement Low", &gWindVorticityConfinementLow},
	{"Wind Decay High", &gWindDecayHigh},
	{"Wind Decay Low", &gWindDecayLow},
	{"Wind Momentum High", &gWindMomentumHigh},
	{"Wind Momentum Low", &gWindMomentumLow},
	{"Wind Diffusion High", &gWindDiffusionHigh},
	{"Wind Diffusion Low", &gWindDiffusionLow},
};
}

void TweaksScreenBase::RenderWindSection()
{
	const int64_t iSection = giTweakSectionWind;

	if (ImGui::BeginTabBar("WindTabs"))
	{
		if (BeginSubtab("Wind", iSection, 0))
		{
			WrapperSeparatorText("Time & Global");
			WrapperSlider("Wind Time Scale", iSection);
			WrapperSlider("Wind Threshold Low", iSection);
			WrapperSlider("Wind Threshold High", iSection);

			WrapperSeparatorText("Propagation");
			if (ImGui::BeginTable("WindPropagation", 2))
			{
				ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Decay Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Decay High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Momentum Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Momentum High", iSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion Low", iSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion High", iSection, 1.0f);

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Deposits", iSection, 1))
		{
			RenderWindDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
