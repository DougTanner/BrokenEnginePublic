#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderWindSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWind);

	if (ImGui::BeginTabBar("WindTabs"))
	{
		if (ImGui::BeginTabItem("Wind", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 0;
			}

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

			WrapperSeparatorText("Particles");
			WrapperSlider("Particles Wind Strength", kiSection);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Deposits", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 1;
			}

			RenderWindDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
