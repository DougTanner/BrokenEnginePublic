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
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWind);

	if (ImGui::BeginTabBar("WindTabs"))
	{
		if (BeginSubtab("Wind", kiSection, 0))
		{
			WrapperSeparatorText("Time & Global");
			WrapperSlider("Wind Time Scale", kiSection);
			WrapperSlider("Wind Threshold Low", kiSection);
			WrapperSlider("Wind Threshold High", kiSection);

			WrapperSeparatorText("Propagation");
			if (ImGui::BeginTable("WindPropagation", 2))
			{
				ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Advection Scale High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Scale High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Amount High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Swirl Speed High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Vorticity Confinement High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Decay Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Decay High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Momentum Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Momentum High", kiSection, 1.0f);

				ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion Low", kiSection, 1.0f);
				ImGui::TableNextColumn(); WrapperSlider("Wind Diffusion High", kiSection, 1.0f);

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Deposits", kiSection, 1))
		{
			RenderWindDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
