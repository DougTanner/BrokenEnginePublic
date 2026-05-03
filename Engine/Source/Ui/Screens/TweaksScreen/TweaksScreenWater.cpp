#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderWaterSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWater);

	if (ImGui::BeginTabBar("WaterTabs"))
	{
		if (ImGui::BeginTabItem("Specular", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 0;
			}
			if (ImGui::BeginTable("WaterSpecularColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Normals");
				WrapperSlider("Sampled Normals One Size", kiSection, 1.0f);
				WrapperSlider("Sampled Normals Two Size", kiSection, 1.0f);
				WrapperSlider("Sampled Normals Speed", kiSection, 1.0f);
				WrapperSlider("Sampled Normals Blend", kiSection, 1.0f);
				WrapperSlider("Depth Reflection Feather", kiSection, 1.0f);

				WrapperSeparatorText("Skybox");
				WrapperSlider("Sun Bias", kiSection, 1.0f);
				WrapperSlider("Normal Soften", kiSection, 1.0f);
				WrapperSlider("Normal Blend Wave", kiSection, 1.0f);
				WrapperSlider("Intensity", kiSection, 1.0f);
				WrapperSlider("Add", kiSection, 1.0f);

				ImGui::TableNextColumn();

				WrapperSeparatorText("Skybox");
				WrapperSlider("Skybox 1", kiSection, 1.0f);
				WrapperSlider("Skybox 1 Power", kiSection, 1.0f);
				WrapperSlider("Skybox 2", kiSection, 1.0f);
				WrapperSlider("Skybox 2 Power", kiSection, 1.0f);
				WrapperSlider("Skybox 3", kiSection, 1.0f);
				WrapperSlider("Skybox 3 Power", kiSection, 1.0f);
				WrapperSlider("Skybox Lod", kiSection, 1.0f);

				WrapperSeparatorText("Height Darken");
				WrapperSlider("Height Darken Top", kiSection, 1.0f);
				WrapperSlider("Height Darken Bottom", kiSection, 1.0f);
				WrapperSlider("Height Darken Clamp", kiSection, 1.0f);

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Low", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 1;
			}
			RenderWaveCountRadioButtons(gLowCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Low Max", kiSection);
			WrapperSlider("Angle", kiSection);
			WrapperSlider("Wavelength", kiSection);
			WrapperSlider("Amplitude", kiSection);
			WrapperSlider("Speed", kiSection);
			WrapperSlider("Steepness", kiSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Angle Adjust", kiSection);
			WrapperSlider("Wavelength Adjust", kiSection);
			WrapperSlider("Amplitude Adjust", kiSection);
			WrapperSlider("Speed Adjust", kiSection);

			WrapperSeparatorText("Beach Fade");
			WrapperSlider("Beach Fade Top", kiSection);
			WrapperSlider("Beach Fade Bottom", kiSection);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Medium", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 2;
			}
			RenderWaveCountRadioButtons(gMediumCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Medium Wavelength", kiSection);
			WrapperSlider("Medium Amplitude", kiSection);
			WrapperSlider("Medium Speed", kiSection);
			WrapperSlider("Medium Steepness", kiSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Medium Angle Adjust", kiSection);
			WrapperSlider("Medium Wavelength Adjust", kiSection);
			WrapperSlider("Medium Amplitude Adjust", kiSection);
			WrapperSlider("Medium Speed Adjust", kiSection);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Depth", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 3) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 3)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 3;
			}
			WrapperSlider("Water Depth", kiSection);
			WrapperSlider("Water Terrain Height", kiSection);
			WrapperSlider("Water Terrain Fade", kiSection);

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
