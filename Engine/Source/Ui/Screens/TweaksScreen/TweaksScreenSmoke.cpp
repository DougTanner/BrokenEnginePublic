#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderSmokeSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSmoke);

	if (ImGui::BeginTabBar("SmokeTabs"))
	{
		if (ImGui::BeginTabItem("Smoke", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
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

			RenderSmokeDepositsTab();

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
