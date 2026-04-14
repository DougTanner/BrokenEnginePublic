#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderLightingSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kLighting);

	if (ImGui::BeginTabBar("LightingTabs"))
	{
		if (ImGui::BeginTabItem("Write", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 0;
			}
			if (ImGui::BeginTable("LightingWriteColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("1. Pre-Blur");
				WrapperSlider("Sigma", kiSection, 1.0f, "Lighting Blur Sigma");
				WrapperSlider("Sample Count", kiSection, 1.0f, "Lighting Blur Sample Count");
				WrapperSlider("Edge Falloff", kiSection, 1.0f, "Lighting Blur Edge Falloff");

				WrapperSeparatorText("2. Deposit");
				WrapperSlider("Texture Multiplier", kiSection, 1.0f, "Deposit Texture Multiplier");

				WrapperSeparatorText("3a. Spread");
				WrapperSlider("Pass Count", kiSection, 1.0f, "Spread Pass Count");
				WrapperSlider("Decay", kiSection, 1.0f, "Spread Decay");
				WrapperSlider("Accumulation Decay", kiSection, 1.0f, "Spread Accumulation Decay");

				float fSpreadStartY = ImGui::GetCursorPosY();

				WrapperSeparatorText("3b. Spread Start");
				WrapperSlider("Texture Multiplier Start", kiSection, 1.0f, "Spread Texture Multiplier Start");
				WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality");
				WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count");
				WrapperSlider("Distance", kiSection, 1.0f, "Spread Distance");
				WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count");
				WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter");
				WrapperSlider("Distance Falloff", kiSection, 1.0f, "Spread Distance Falloff");
				WrapperSlider("Height Distance", kiSection, 1.0f, "Spread Height Distance");
				WrapperSlider("Height Intensity", kiSection, 1.0f, "Spread Height Intensity");
				WrapperSlider("Height Intensity Target", kiSection, 1.0f, "Spread Height Intensity Target");

				ImGui::TableNextColumn();

				WrapperSeparatorText("4. Combine");
				WrapperSlider("Max Brightness", kiSection, 1.0f, "Combine Max Brightness");
				WrapperSlider("Contrast", kiSection, 1.0f, "Combine Contrast");
				WrapperSlider("Linear Start", kiSection, 1.0f, "Combine Linear Start");
				WrapperSlider("Linear Length", kiSection, 1.0f, "Combine Linear Length");
				WrapperSlider("Toe", kiSection, 1.0f, "Combine Toe");
				WrapperSlider("Black Tightness", kiSection, 1.0f, "Combine Black Tightness");
				WrapperSlider("Pass Normalize", kiSection, 1.0f, "Combine Pass Normalize");
				WrapperSlider("Exposure Pass Scale", kiSection, 1.0f, "Combine Exposure Pass Scale");
			WrapperSlider("Hue Preserve", kiSection, 1.0f, "Combine Hue Preserve");

				ImGui::SetCursorPosY(fSpreadStartY);

				WrapperSeparatorText("3c. Spread End");
				WrapperSlider("Texture Multiplier End", kiSection, 1.0f, "Spread Texture Multiplier End");
				WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality End");
				WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count End");
				WrapperSlider("Distance", kiSection, 1.0f, "Spread Distance End");
				WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count End");
				WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter End");
				WrapperSlider("Distance Falloff", kiSection, 1.0f, "Spread Distance Falloff End");
				WrapperSlider("Height Distance", kiSection, 1.0f, "Spread Height Distance End");
				WrapperSlider("Height Intensity", kiSection, 1.0f, "Spread Height Intensity End");
				WrapperSlider("Height Intensity Target", kiSection, 1.0f, "Spread Height Intensity Target End");

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Read", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 1;
			}
			WrapperSeparatorText("Lighting");
			WrapperSlider("New Directional", kiSection, 1.0f);
			WrapperSlider("New Directional Power", kiSection, 1.0f);
			WrapperSlider("Directional Power Mode", kiSection, 1.0f);
			WrapperSlider("New Ambient", kiSection, 1.0f);
			WrapperSlider("New Ambient Power", kiSection, 1.0f);
			WrapperSlider("Ambient Power Mode", kiSection, 1.0f);
			WrapperSlider("Water Ambient Power Mode", kiSection, 1.0f);
			WrapperSlider("Terrain", kiSection, 1.0f);
			WrapperSlider("Terrain Add", kiSection, 1.0f);
			WrapperSlider("Below Base Multiplier", kiSection, 1.0f, "Terrain Below Base Multiplier");
			WrapperSlider("Below Base Power", kiSection, 1.0f, "Terrain Below Base Power");
			WrapperSlider("Objects", kiSection, 1.0f);
			WrapperSlider("Objects Add", kiSection, 1.0f);
			WrapperSlider("Day Final Multiplier", kiSection, 1.0f);
			WrapperSlider("Night Final Multiplier", kiSection, 1.0f);

			WrapperSeparatorText("Water Lighting");
			WrapperSlider("Normal Soften", kiSection, 1.0f, "Water Normal Soften");
			WrapperSlider("Normal Blend Wave", kiSection, 1.0f, "Water Normal Blend Wave");
			WrapperSlider("Intensity", kiSection, 1.0f, "Water Intensity");
			WrapperSlider("Add", kiSection, 1.0f, "Water Add");
			WrapperSlider("One", kiSection, 1.0f, "Water One");
			WrapperSlider("One Power", kiSection, 1.0f, "Water One Power");
			WrapperSlider("Two", kiSection, 1.0f, "Water Two");
			WrapperSlider("Two Power", kiSection, 1.0f, "Water Two Power");
			WrapperSlider("Three", kiSection, 1.0f, "Water Three");
			WrapperSlider("Three Power", kiSection, 1.0f, "Water Three Power");
			WrapperSlider("Power Mode", kiSection, 1.0f, "Water Power Mode");

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Effects", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 2;
			}
			RenderLightingEffectsTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
